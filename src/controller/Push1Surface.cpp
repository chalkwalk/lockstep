#include "Push1Surface.h"
#include "Oklab.h"
#include "Push1Palette.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>

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
    // Colour helpers
    // =========================================================================

    // Gamut-stretch strengths for matching against the as-displayed palette
    // (`kCapturedPalette`, from Push1Palette.h). The device gamut is smaller than
    // sRGB — elevated black floor, weaker saturation — so before matching we
    // stretch the captured entries in Oklab: lightness fully toward [0,1], chroma
    // mildly toward a vivid target, hue fixed. This uses the whole palette and
    // preserves the screen's dim/bright/scope contrast. 0 = absolute fidelity,
    // 1 = full stretch; tune on hardware.
    static constexpr float kStretchL = 1.0f;
    static constexpr float kStretchC = 0.5f;

    // Flattens an ARGB colour over black, premultiplying by alpha. The model
    // encodes "dim" two ways: as genuinely dark RGB (opaque) and as a bright RGB
    // at low alpha (the on-screen renderer composites it over the near-black
    // grid). Push pads have no alpha, so we bake the alpha into the RGB here —
    // otherwise a 9%-alpha scope colour would map to a *bright* palette entry
    // instead of the faint one the screen shows. Opaque colours pass through.
    static uint32_t flattenOverBlack(uint32_t argb) noexcept
    {
        const float a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
        const int   r = static_cast<int>(static_cast<float>((argb >> 16) & 0xFF) * a);
        const int   g = static_cast<int>(static_cast<float>((argb >>  8) & 0xFF) * a);
        const int   b = static_cast<int>(static_cast<float>( argb        & 0xFF) * a);
        return (static_cast<uint32_t>(r) << 16)
             | (static_cast<uint32_t>(g) <<  8)
             |  static_cast<uint32_t>(b);
    }

    // The 127 lit palette entries in stretched Oklab, built once from
    // kCapturedPalette. index 0 (off) is excluded — in-use cells never map to it.
    static const std::array<oklab::Lab, 128>& matchTable() noexcept
    {
        static const std::array<oklab::Lab, 128> table = []() noexcept
        {
            std::array<oklab::LCh, 128> lch{};
            float lmin = 1.0e9f, lmax = -1.0e9f, cmax = 1.0e-6f;
            for (int i = 1; i < 128; ++i)
            {
                const auto e = oklab::labToLCh(
                    oklab::packedRgbToOklab(kCapturedPalette[static_cast<std::size_t>(i)]));
                lch[static_cast<std::size_t>(i)] = e;
                lmin = std::min(lmin, e.L);
                lmax = std::max(lmax, e.L);
                cmax = std::max(cmax, e.C);
            }
            const float lspan   = std::max(1.0e-6f, lmax - lmin);
            const float cTarget = 0.32f;            // ~ vivid sRGB chroma in Oklab
            const float cScale  = cTarget / cmax;

            std::array<oklab::Lab, 128> t{};
            for (int i = 1; i < 128; ++i)
            {
                oklab::LCh s = lch[static_cast<std::size_t>(i)];
                const float lNorm = (s.L - lmin) / lspan;          // device range -> [0,1]
                s.L = s.L + kStretchL * (lNorm        - s.L);
                s.C = s.C + kStretchC * (s.C * cScale - s.C);
                t[static_cast<std::size_t>(i)] = oklab::lChToLab(s);
            }
            return t;
        }();
        return table;
    }

    // Nearest lit palette index (1-127) to an alpha-flattened RGB, matched in
    // stretched Oklab (perceptually uniform). Never returns 0 (off): dim in-use
    // cells floor at the darkest lit entry, staying distinct from off/unused pads.
    static uint8_t nearestPaletteIndex(uint32_t rgb) noexcept
    {
        const auto  target = oklab::packedRgbToOklab(rgb);
        const auto& tbl     = matchTable();
        float   best    = 1.0e30f;
        uint8_t bestIdx = 1;
        for (int i = 1; i < 128; ++i)
        {
            const float d = oklab::distanceSq(target, tbl[static_cast<std::size_t>(i)]);
            if (d < best) { best = d; bestIdx = static_cast<uint8_t>(i); }
        }
        return bestIdx;
    }

    // Maps a SurfaceCell to a pad/lower-button palette index (0-127).
    //
    // Cell-driven (mirrors the on-screen paint path): the body colour comes from
    // cell.baseColour, which buildSurfaceModel already tints with the held scope
    // — so the Push grid recolours with the scope exactly like the screen, rather
    // than re-deriving a fixed colour per CellState token. Only a few semantics
    // override that body colour: a press, a held/selected step, the playhead, and
    // the genuinely-unused (out-of-range) cells.
    uint8_t Push1Surface::rgbPaletteFor(const SurfaceCell& cell) noexcept
    {
        // White / amber resolved against the loaded palette once (the old fixed
        // indices 3/9 assumed the nominal palette; a captured palette may differ).
        static const uint8_t kWhiteIdx = nearestPaletteIndex(0xFFFFFFu);
        static const uint8_t kAmberIdx = nearestPaletteIndex(0xFFCC44u);

        // 1. Press feedback wins on every pad, including steps.
        if (cell.pressed) return kWhiteIdx;

        // 2. Held / selected step (the on-screen white-border selection).
        if (cell.base == CellState::StepHeld
            || (cell.border.present && cell.border.token == CellState::StepHeld))
            return kWhiteIdx;

        // 3. Playhead.
        if (cell.border.present && cell.border.token == CellState::StepPlayhead)
            return kAmberIdx;

        // 4. Genuinely unused cells → off (kept distinct from in-range dim cells,
        //    which floor to a visible dark grey in nearestPaletteIndex).
        switch (cell.base)
        {
            case CellState::StepOutOfRange:
            case CellState::SelectorOutRange:
            case CellState::MachineUnavailable:
                return 0;
            default:
                break;
        }

        // 5. Body colour = the scope-tinted ARGB the screen draws.
        return nearestPaletteIndex(flattenOverBlack(cell.baseColour));
    }

    // Maps a CellState to a bi-colour value 0-24 for upper/scene buttons.
    // Green = active/occupied, Orange = attention, Red = muted/off.
    uint8_t Push1Surface::biColourFor(CellState state) noexcept
    {
        switch (state)
        {
            case CellState::ModeActive:         return 22;  // green hi
            case CellState::Pressed:            return 22;  // green hi
            case CellState::SelectorCurrent:    return 22;  // green hi
            case CellState::SelectorOccupied:   return 19;  // green lo
            case CellState::MuteAudible:        return 22;  // green hi
            case CellState::MuteMuted:          return 4;   // red hi
            case CellState::StepTrigCertain:    return 22;  // green hi
            case CellState::StepFillAdd:        return 10;  // orange hi
            default:                            return 0;   // off
        }
    }

    // Maps a CellState to a mono button LED value: 0=off, 1=dim, 4=bright.
    uint8_t Push1Surface::monoFor(CellState state) noexcept
    {
        switch (state)
        {
            case CellState::ModeActive:  return 4;
            case CellState::Pressed:     return 4;
            case CellState::Disabled:    return 0;
            default:                     return 1;   // Resting and anything else: dim
        }
    }

    int Push1Surface::monoCC(ControllerButton btn) noexcept
    {
        for (const auto& entry : kMonoButtons)
        {
            if (entry.button == btn)
                return entry.cc;
        }
        return 0;
    }

    // Build a `width`-char value bar for one encoder's display cell. Uses Push 1
    // block-bar glyphs (pushbase GRAPH_* set: \x03 = 1/4 … \x06 = full) when
    // kUseBlockGlyphs, else a coarse ASCII bar. The glyph rendering is confirmed
    // via push_probe 'Glyph/Bars'; flip kUseBlockGlyphs if a unit lacks them.
    static constexpr bool kUseBlockGlyphs = true;

    static juce::String buildBar(float position, RingMode mode, int width) noexcept
    {
        position = juce::jlimit(0.0f, 1.0f, position);
        const juce::juce_wchar full  =
            kUseBlockGlyphs ? static_cast<juce::juce_wchar>(0x06)
                            : static_cast<juce::juce_wchar>('#');
        const juce::juce_wchar blank = ' ';

        // Sub-char leading-edge glyph for the fractional fill (block mode only).
        auto partial = [&](float frac) -> juce::juce_wchar
        {
            if (!kUseBlockGlyphs) return blank;
            const int q = juce::jlimit(0, 3, static_cast<int>(frac * 4.0f));  // 0..3
            if (q <= 0) return blank;
            return static_cast<juce::juce_wchar>(0x03 + (q - 1));  // 0x03/04/05
        };

        juce::String bar;
        if (mode == RingMode::Dot)
        {
            const int pos = juce::jlimit(0, width - 1,
                static_cast<int>(position * static_cast<float>(width - 1) + 0.5f));
            for (int i = 0; i < width; ++i)
                bar += (i == pos) ? full : blank;
            return bar;
        }
        if (mode == RingMode::BipolarFromCentre)
        {
            const int mid = width / 2;
            const int ext = juce::roundToInt((position - 0.5f) * 2.0f * static_cast<float>(mid));
            for (int i = 0; i < width; ++i)
            {
                const bool on = (ext >= 0) ? (i >= mid && i < mid + ext)
                                           : (i >= mid + ext && i < mid);
                bar += on ? full : blank;
            }
            return bar;
        }
        // UnipolarFill — fill from the left, with a sub-char leading edge.
        const float filled = position * static_cast<float>(width);
        const int   fullCount = static_cast<int>(filled);
        const float frac = filled - static_cast<float>(fullCount);
        for (int i = 0; i < width; ++i)
        {
            if (i < fullCount)        bar += full;
            else if (i == fullCount)  bar += partial(frac);
            else                      bar += blank;
        }
        return bar;
    }

    // =========================================================================
    // render — LED and display feedback
    // =========================================================================

    void Push1Surface::render(const SurfaceModel& model, juce::MidiOutput& out)
    {
        // --- Pad grid (notes 36-99) ---
        // Modifier pads (top-left 2×4)
        for (int i = 0; i < 8; ++i)
        {
            const auto& cell = model.modifiers[static_cast<std::size_t>(i)];
            const uint8_t colour = rgbPaletteFor(cell);
            const int note = kModifierNotes[static_cast<std::size_t>(i)];
            const auto si = static_cast<std::size_t>(note - 36);
            if (colour != padShadow_[si])
            {
                padShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
            }
        }

        // Section row (row 4: notes 60-67)
        // note 60 = TAP, note 61 = NavUp, notes 62-67 = sections 0-5
        {
            const auto& tap  = model.tap;
            const uint8_t tapC = rgbPaletteFor(tap);
            const auto si60 = static_cast<std::size_t>(60 - 36);
            if (tapC != padShadow_[si60])
            {
                padShadow_[si60] = tapC;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(tapC)));
            }
        }
        {
            const auto& nav  = model.navUp;
            const uint8_t navC = rgbPaletteFor(nav);
            const auto si61 = static_cast<std::size_t>(61 - 36);
            if (navC != padShadow_[si61])
            {
                padShadow_[si61] = navC;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(navC)));
            }
        }
        for (int s = 0; s < 6; ++s)
        {
            const auto& cell = model.section[static_cast<std::size_t>(s)];
            const uint8_t colour = rgbPaletteFor(cell);
            const int note = 62 + s;
            const auto si = static_cast<std::size_t>(note - 36);
            if (colour != padShadow_[si])
            {
                padShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
            }
        }

        // Verb/nav row (row 3: notes 52-59)
        for (int i = 0; i < 8; ++i)
        {
            const SurfaceCell* cell = model.byButton(kVerbRowButtons[static_cast<std::size_t>(i)]);
            const uint8_t colour = cell ? rgbPaletteFor(*cell) : uint8_t(0);
            const int note = 52 + i;
            const auto si = static_cast<std::size_t>(note - 36);
            if (colour != padShadow_[si])
            {
                padShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
            }
        }

        // Step rows (row 2: 44-51 = steps 0-7; row 1: 36-43 = steps 8-15)
        for (int s = 0; s < 16; ++s)
        {
            const auto& cell = model.step[static_cast<std::size_t>(s)];
            const uint8_t colour = rgbPaletteFor(cell);
            const int note = kStepNotes[static_cast<std::size_t>(s)];
            const auto si = static_cast<std::size_t>(note - 36);
            if (colour != padShadow_[si])
            {
                padShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
            }
        }

        // Unused top-right pad block (rows 5-8, cols 2-7) — explicitly off, so a
        // colour left from a prior frame/session can't linger. Shadow-tracked.
        // notes = 70-75, 78-83, 86-91, 94-99 (the 6 columns right of the 2×4
        // modifier block in the top four rows).
        for (int rowFromBottom = 4; rowFromBottom <= 7; ++rowFromBottom)
        {
            for (int col = 2; col < 8; ++col)
            {
                const int note = 36 + rowFromBottom * 8 + col;
                const auto si = static_cast<std::size_t>(note - 36);
                if (padShadow_[si] != 0)
                {
                    padShadow_[si] = 0;
                    out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(0)));
                }
            }
        }

        // --- Upper display buttons (CC 20-27) — bi-colour ---
        // Map to function row (Q-row items).
        static constexpr std::array<int, 8> kUpperRowFn = { 0,1,2,3,4,5,6,7 };
        for (int i = 0; i < 8; ++i)
        {
            const auto& cell = model.functionRow[static_cast<std::size_t>(kUpperRowFn[static_cast<std::size_t>(i)])];
            const uint8_t colour = biColourFor(cell.base);
            const auto si = static_cast<std::size_t>(i);
            if (colour != upperShadow_[si])
            {
                upperShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::controllerEvent(1, 20 + i, colour));
            }
        }

        // --- Scene buttons (CC 36-43) — bi-colour ---
        // Dark for now (no direct mapping to scene-launch in this pass).
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            if (sceneShadow_[si] != 0)
            {
                sceneShadow_[si] = 0;
                out.sendMessageNow(juce::MidiMessage::controllerEvent(1, 36 + i, 0));
            }
        }

        // --- Mono function buttons ---
        for (int i = 0; i < static_cast<int>(kMonoButtons.size()); ++i)
        {
            const auto& entry = kMonoButtons[static_cast<std::size_t>(i)];
            const SurfaceCell* cell = model.byButton(entry.button);
            // Every entry is a live binding, so light it at least dim even when it
            // has no model cell (e.g. StopReset/Metronome) — an unlit labelled
            // button is unreadable on the unit.
            const uint8_t val = cell ? monoFor(cell->base) : uint8_t(1);
            const auto si = static_cast<std::size_t>(i);
            if (val != monoShadow_[si])
            {
                monoShadow_[si] = val;
                out.sendMessageNow(juce::MidiMessage::controllerEvent(1, entry.cc, val));
            }
        }

        // --- Touch strip LED (pitch-bend out from model.crossfader) ---
        {
            const int stripVal = juce::roundToInt(
                juce::jlimit(0.0f, 1.0f, model.crossfader) * 16383.0f);
            if (stripVal != stripShadow_)
            {
                stripShadow_ = stripVal;
                out.sendMessageNow(juce::MidiMessage::pitchWheel(1, stripVal));
            }
        }

        // --- Display (4 × 68 characters) ---
        // Layout: 4 physical displays × 17 chars each = 68 total.
        // Each display holds 2 encoders: [enc_left:8][gap:1][enc_right:8] = 17.
        // The gap is within each display; adjacent displays have no extra space
        // between them. Pattern across the full line:
        //   [enc0:8][ ][enc1:8] [enc2:8][ ][enc3:8] [enc4:8][ ][enc5:8] [enc6:8][ ][enc7:8]
        //    ←── display 0 ──→  ←── display 1 ──→   ←── display 2 ──→   ←── display 3 ──→
        // Per encoder (8×4 chars): row0 = value bar, row1 = value text,
        // row2 = section name, row3 = param name.
        auto buildLine = [&](auto&& cellFn) -> juce::String
        {
            juce::String line;
            line.preallocateBytes(72);
            for (int enc = 0; enc < 8; ++enc)
            {
                const auto& slot = model.slots[static_cast<std::size_t>(enc)];
                juce::String col = slot.inRange ? cellFn(slot) : juce::String();
                // Truncate or pad to exactly 8 chars (bar glyphs count as 1 char each).
                if (col.length() > 8) col = col.substring(0, 8);
                while (col.length() < 8) col += ' ';
                line += col;
                // Within-display separator after the left encoder of each pair.
                if (enc % 2 == 0)
                    line += ' ';
            }
            return line;  // exactly 8*8 + 4 = 68 chars
        };

        const std::array<juce::String, 4> lines = {
            buildLine([](const SurfaceSlot& s) { return buildBar(s.position, s.ringMode, 8); }),
            buildLine([](const SurfaceSlot& s) { return s.valueText; }),
            buildLine([](const SurfaceSlot& s) { return s.sectionLabel; }),
            buildLine([](const SurfaceSlot& s) { return s.label; }),
        };

        for (int l = 0; l < 4; ++l)
        {
            const auto li = static_cast<std::size_t>(l);
            if (lines[li] != displayShadow_[li])
            {
                displayShadow_[li] = lines[li];
                writeDisplayLine(out, l, lines[li]);
            }
        }
    }

}  // namespace lockstep
