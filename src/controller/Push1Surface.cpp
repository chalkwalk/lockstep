#include "Push1Surface.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace lockstep
{
    // =========================================================================
    // Push 1 SysEx header: F0 47 7F 15 <cmd> ...
    // =========================================================================
    static constexpr uint8_t kPush1Header[] = { 0xF0, 0x47, 0x7F, 0x15 };

    Push1Surface::Push1Surface()
    {
        padShadow_.fill(255);
        upperShadow_.fill(255);
        sceneShadow_.fill(255);
        monoShadow_.fill(255);
    }

    // =========================================================================
    // SysEx helpers
    // =========================================================================

    void Push1Surface::sendSysEx(juce::MidiOutput& out,
                                  std::initializer_list<uint8_t> payload)
    {
        juce::MemoryBlock block;
        for (auto b : kPush1Header) block.append(&b, 1);
        for (auto b : payload)      block.append(&b, 1);
        const uint8_t eox = 0xF7;
        block.append(&eox, 1);
        out.sendMessageNow(juce::MidiMessage(block.getData(),
                                              static_cast<int>(block.getSize())));
    }

    void Push1Surface::sendModeChange(juce::MidiOutput& out, uint8_t mode)
    {
        // F0 47 7F 15 62 00 01 <mode> F7   (0=Live, 1=User)
        sendSysEx(out, { 0x62, 0x00, 0x01, mode });
    }

    void Push1Surface::sendStripMode(juce::MidiOutput& out, uint8_t mode)
    {
        // F0 47 7F 15 63 00 01 <mode> F7
        sendSysEx(out, { 0x63, 0x00, 0x01, mode });
    }

    void Push1Surface::writeDisplayLine(juce::MidiOutput& out, int line,
                                         const juce::String& text)
    {
        // F0 47 7F 15 <0x18+line> 00 45 00 <68 ASCII bytes> F7
        jassert(line >= 0 && line <= 3);

        juce::MemoryBlock block;
        for (auto b : kPush1Header) block.append(&b, 1);
        const uint8_t cmd = static_cast<uint8_t>(0x18 + line);
        block.append(&cmd, 1);
        const uint8_t hdr[] = { 0x00, 0x45, 0x00 };
        block.append(hdr, 3);

        // Pad or truncate to exactly 68 ASCII characters.
        for (int i = 0; i < 68; ++i)
        {
            const uint8_t ch = (i < text.length())
                ? static_cast<uint8_t>(text[i] & 0x7F)
                : static_cast<uint8_t>(' ');
            block.append(&ch, 1);
        }
        const uint8_t eox = 0xF7;
        block.append(&eox, 1);
        out.sendMessageNow(juce::MidiMessage(block.getData(),
                                              static_cast<int>(block.getSize())));
    }

    void Push1Surface::clearDisplay(juce::MidiOutput& out)
    {
        for (int line = 0; line < 4; ++line)
        {
            // F0 47 7F 15 <0x1C+line> 00 00 F7
            const uint8_t cmd = static_cast<uint8_t>(0x1C + line);
            sendSysEx(out, { cmd, 0x00, 0x00 });
        }
    }

    // =========================================================================
    // onConnect — sent once on (re)open
    // =========================================================================

    void Push1Surface::onConnect(juce::MidiOutput& out)
    {
        sendModeChange(out, 1);   // User mode for User Port
        sendStripMode(out, 3);    // CUSTOM_DISCRETE: host-painted, stepped feel

        clearDisplay(out);

        // Reset all shadow caches so the next render() repaints every cell.
        padShadow_.fill(255);
        upperShadow_.fill(255);
        sceneShadow_.fill(255);
        monoShadow_.fill(255);
        stripShadow_ = -1;
        displayShadow_.fill({});
    }

    // =========================================================================
    // decodeDelta — two's-complement relative encoder
    // =========================================================================

    int Push1Surface::decodeDelta(int ccValue) noexcept
    {
        if (ccValue >= 1 && ccValue <= 63)  return  ccValue;
        if (ccValue >= 64)                  return -(128 - ccValue);
        return 0;
    }

    // =========================================================================
    // onInput — MIDI input decode
    // =========================================================================

    void Push1Surface::onInput(const juce::MidiMessage& msg, ControllerEventSink& sink)
    {
        // Always branch on message type first — CC 36-43 and Note 36-43 collide.

        // -----------------------------------------------------------------
        // Note messages: pads + encoder touch
        // -----------------------------------------------------------------
        if (msg.isNoteOnOrOff())
        {
            const int note = msg.getNoteNumber();
            const bool isDown = msg.isNoteOn();

            // Encoder touch (Notes 0-7)
            if (note >= 0 && note <= 7)
            {
                const auto ei = static_cast<std::size_t>(note);
                auto& ts = encoderTouch_[ei];
                if (isDown)
                {
                    const juce::int64 now = juce::Time::currentTimeMillis();
                    const bool isDoubleTap = (!ts.touching)
                        && ((now - ts.touchMs) < kDoubleTapMs)
                        && !ts.turned;
                    ts.touching = true;
                    ts.turned   = false;
                    ts.touchMs  = now;
                    if (isDoubleTap && sink.resetSlot)
                        sink.resetSlot(note);
                }
                else
                {
                    ts.touching = false;
                }
                return;
            }

            // Modifier pads (top-left 2×4)
            for (int i = 0; i < 8; ++i)
            {
                if (note == kModifierNotes[static_cast<std::size_t>(i)])
                {
                    if (sink.emitEvent)
                        sink.emitEvent({
                            isDown ? ControllerEvent::Type::ButtonDown
                                   : ControllerEvent::Type::ButtonUp,
                            kModifierButtons[static_cast<std::size_t>(i)],
                            -1, 0 });
                    return;
                }
            }

            // Section row (row 4: notes 60-67)
            if (note >= 60 && note <= 67)
            {
                const int col = note - 60;
                if (col == 0)
                {
                    // TAP (note 60)
                    if (isDown && sink.emitEvent)
                        sink.emitEvent({ ControllerEvent::Type::ButtonDown,
                                         ControllerButton::TapTempo, -1, 0 });
                    return;
                }
                if (col == 1)
                {
                    // NavUp (note 61)
                    if (sink.emitEvent)
                        sink.emitEvent({
                            isDown ? ControllerEvent::Type::ButtonDown
                                   : ControllerEvent::Type::ButtonUp,
                            ControllerButton::NavUp, -1, 0 });
                    return;
                }
                // Sections 0-5 on notes 62-67
                const int sectionIdx = col - 2;
                if (sectionIdx >= 0 && sectionIdx <= 5 && sink.emitEvent)
                    sink.emitEvent({
                        isDown ? ControllerEvent::Type::ButtonDown
                               : ControllerEvent::Type::ButtonUp,
                        ControllerButton::Section, sectionIdx, 0 });
                return;
            }

            // Verb/nav row (row 3: notes 52-59)
            if (note >= 52 && note <= 59)
            {
                const int col = note - 52;
                if (sink.emitEvent)
                    sink.emitEvent({
                        isDown ? ControllerEvent::Type::ButtonDown
                               : ControllerEvent::Type::ButtonUp,
                        kVerbRowButtons[static_cast<std::size_t>(col)], -1, 0 });
                return;
            }

            // Step rows (row 2: 44-51 = steps 0-7; row 1: 36-43 = steps 8-15)
            for (int s = 0; s < 16; ++s)
            {
                if (note == kStepNotes[static_cast<std::size_t>(s)])
                {
                    if (sink.emitEvent)
                        sink.emitEvent({
                            isDown ? ControllerEvent::Type::ButtonDown
                                   : ControllerEvent::Type::ButtonUp,
                            ControllerButton::Step, s, 0 });
                    return;
                }
            }
        }

        // -----------------------------------------------------------------
        // Pitch bend: touch strip input → crossfader
        // -----------------------------------------------------------------
        else if (msg.isPitchWheel())
        {
            // 14-bit 0-16383 → 0..1
            const float norm = static_cast<float>(msg.getPitchWheelValue()) / 16383.0f;
            if (sink.setCrossfader)
                sink.setCrossfader(norm);
        }

        // -----------------------------------------------------------------
        // CC messages
        // -----------------------------------------------------------------
        else if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();

            // Param encoders CC 71-78 (turns)
            if (cc >= 71 && cc <= 78)
            {
                const int slot = cc - 71;
                const int delta = decodeDelta(val);
                if (delta != 0)
                {
                    // Mark any active touch as "turned" so single-release doesn't reset.
                    const auto si = static_cast<std::size_t>(slot);
                    encoderTouch_[si].turned = true;
                    if (sink.applyParamDelta)
                        sink.applyParamDelta(slot, delta);
                }
                return;
            }

            // Extra encoders: Tempo CC14, Swing CC15, Master CC79
            if (cc == 14)
            {
                if (sink.applyGlobalDelta)
                    sink.applyGlobalDelta(GlobalTarget::Tempo, decodeDelta(val));
                return;
            }
            if (cc == 15)
            {
                if (sink.applyGlobalDelta)
                    sink.applyGlobalDelta(GlobalTarget::Swing, decodeDelta(val));
                return;
            }
            if (cc == 79)
            {
                if (sink.applyGlobalDelta)
                    sink.applyGlobalDelta(GlobalTarget::Master, decodeDelta(val));
                return;
            }

            // Mono function buttons — ignore CCs 20-27/36-43/102-109 (LED output only).
            // CCs 20-27 and 36-43 send 127/0 on press/release.
            // We only process the function-button CCs listed in kMonoButtons.
            for (const auto& entry : kMonoButtons)
            {
                if (cc == entry.cc)
                {
                    if (sink.emitEvent)
                        sink.emitEvent({
                            (val == 127) ? ControllerEvent::Type::ButtonDown
                                         : ControllerEvent::Type::ButtonUp,
                            entry.button, -1, 0 });
                    return;
                }
            }
        }
    }

    // =========================================================================
    // render — placeholder (Stage 5)
    // =========================================================================

    void Push1Surface::render(const SurfaceModel& /*model*/, juce::MidiOutput& /*out*/)
    {
        // Implemented in Stage 5.
    }

    // =========================================================================
    // Colour helpers — placeholder implementations for Stage 5
    // =========================================================================

    uint8_t Push1Surface::rgbPaletteFor(CellState, const CellDecoration&,
                                          const CellDecoration&, float) noexcept
    {
        return 0;
    }

    uint8_t Push1Surface::biColourFor(CellState) noexcept { return 0; }

    uint8_t Push1Surface::monoFor(CellState) noexcept { return 0; }

    int Push1Surface::monoCC(ControllerButton btn) noexcept
    {
        for (const auto& entry : kMonoButtons)
        {
            if (entry.button == btn)
                return entry.cc;
        }
        return 0;
    }

}  // namespace lockstep
