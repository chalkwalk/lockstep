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
    // Colour helpers
    // =========================================================================

    // Full 128-entry Push 1 RGB palette (DEFAULT_PALETTE from DrivenByMoss).
    // Used by nearestPaletteIndex() to map arbitrary ARGB to the closest index.
    struct PaletteEntry { uint8_t r, g, b; };
    static constexpr std::array<PaletteEntry, 128> kPalette = {{
        {0x00,0x00,0x00}, {0x1E,0x1E,0x1E}, {0x7F,0x7F,0x7F}, {0xFF,0xFF,0xFF},  //   0-3
        {0xFF,0x4C,0x4C}, {0xFF,0x00,0x00}, {0x59,0x00,0x00}, {0x19,0x00,0x00},  //   4-7
        {0xFF,0xBD,0x6C}, {0xFF,0x54,0x00}, {0x59,0x1D,0x00}, {0x27,0x1B,0x00},  //   8-11
        {0xFF,0xFF,0x4C}, {0xFF,0xFF,0x00}, {0x59,0x59,0x00}, {0x19,0x19,0x00},  //  12-15
        {0x88,0xFF,0x4C}, {0x54,0xFF,0x00}, {0x1D,0x59,0x00}, {0x14,0x2B,0x00},  //  16-19
        {0x4C,0xFF,0x4C}, {0x00,0xFF,0x00}, {0x00,0x59,0x00}, {0x00,0x19,0x00},  //  20-23
        {0x4C,0xFF,0x5E}, {0x00,0xFF,0x19}, {0x00,0x59,0x0D}, {0x00,0x19,0x02},  //  24-27
        {0x4C,0xFF,0x88}, {0x00,0xFF,0x55}, {0x00,0x59,0x1D}, {0x00,0x1F,0x12},  //  28-31
        {0x4C,0xFF,0xB7}, {0x00,0xFF,0x99}, {0x00,0x59,0x35}, {0x00,0x19,0x12},  //  32-35
        {0x4C,0xC3,0xFF}, {0x00,0xA9,0xFF}, {0x00,0x41,0x52}, {0x00,0x10,0x19},  //  36-39
        {0x4C,0x88,0xFF}, {0x00,0x55,0xFF}, {0x00,0x1D,0x59}, {0x00,0x08,0x19},  //  40-43
        {0x4C,0x4C,0xFF}, {0x00,0x00,0xFF}, {0x00,0x00,0x59}, {0x00,0x00,0x19},  //  44-47
        {0x87,0x4C,0xFF}, {0x54,0x00,0xFF}, {0x19,0x00,0x64}, {0x0F,0x00,0x30},  //  48-51
        {0xFF,0x4C,0xFF}, {0xFF,0x00,0xFF}, {0x59,0x00,0x59}, {0x19,0x00,0x19},  //  52-55
        {0xFF,0x4C,0x87}, {0xFF,0x00,0x54}, {0x59,0x00,0x1D}, {0x22,0x00,0x13},  //  56-59
        {0xFF,0x15,0x00}, {0x99,0x35,0x00}, {0x79,0x51,0x00}, {0x43,0x64,0x00},  //  60-63
        {0x03,0x39,0x00}, {0x00,0x57,0x35}, {0x00,0x54,0x7F}, {0x00,0x00,0xFF},  //  64-67
        {0x00,0x45,0x4F}, {0x25,0x00,0xCC}, {0x7F,0x7F,0x7F}, {0x20,0x20,0x20},  //  68-71
        {0xFF,0x00,0x00}, {0xBD,0xFF,0x2D}, {0xAF,0xED,0x06}, {0x64,0xFF,0x09},  //  72-75
        {0x10,0x8B,0x00}, {0x00,0xFF,0x87}, {0x00,0xA9,0xFF}, {0x00,0x2A,0xFF},  //  76-79
        {0x3F,0x00,0xFF}, {0x7A,0x00,0xFF}, {0xB2,0x1A,0x7D}, {0x40,0x21,0x00},  //  80-83
        {0xFF,0x4A,0x00}, {0x88,0xE1,0x06}, {0x72,0xFF,0x15}, {0x00,0xFF,0x00},  //  84-87
        {0x3B,0xFF,0x26}, {0x59,0xFF,0x71}, {0x38,0xFF,0xCC}, {0x5B,0x8A,0xFF},  //  88-91
        {0x31,0x51,0xC6}, {0x87,0x7F,0xE9}, {0xD3,0x1D,0xFF}, {0xFF,0x00,0x5D},  //  92-95
        {0xFF,0x7F,0x00}, {0xB9,0xB0,0x00}, {0x90,0xFF,0x00}, {0x83,0x5D,0x07},  //  96-99
        {0x39,0x2B,0x00}, {0x14,0x4C,0x10}, {0x0D,0x50,0x38}, {0x15,0x15,0x2A},  // 100-103
        {0x16,0x20,0x5A}, {0x69,0x3C,0x1C}, {0xA8,0x00,0x0A}, {0xDE,0x51,0x3D},  // 104-107
        {0xD8,0x6A,0x1C}, {0xFF,0xE1,0x26}, {0x9E,0xE1,0x2F}, {0x67,0xB5,0x0F},  // 108-111
        {0x1E,0x1E,0x30}, {0xDC,0xFF,0x6B}, {0x80,0xFF,0xBD}, {0x9A,0x99,0xFF},  // 112-115
        {0x8E,0x66,0xFF}, {0x40,0x40,0x40}, {0x75,0x75,0x75}, {0xE0,0xFF,0xFF},  // 116-119
        {0xA0,0x00,0x00}, {0x35,0x00,0x00}, {0x1A,0xD0,0x00}, {0x07,0x42,0x00},  // 120-123
        {0xB9,0xB0,0x00}, {0x3F,0x31,0x00}, {0xB3,0x5F,0x00}, {0x4B,0x15,0x02},  // 124-127
    }};

    // Finds the palette index whose RGB is nearest to the given ARGB colour.
    // Skips index 0 (black) so dim colours don't snap to off.
    static uint8_t nearestPaletteIndex(uint32_t argb) noexcept
    {
        const int tr = static_cast<int>((argb >> 16) & 0xFF);
        const int tg = static_cast<int>((argb >>  8) & 0xFF);
        const int tb = static_cast<int>( argb        & 0xFF);

        // If the colour is very close to black, show a minimum dim (index 1).
        if (tr < 8 && tg < 8 && tb < 8)
            return 1;

        uint32_t bestDist = UINT32_MAX;
        uint8_t  bestIdx  = 1;

        for (int i = 1; i < 128; ++i)
        {
            const auto& p   = kPalette[static_cast<std::size_t>(i)];
            const int   dr  = tr - static_cast<int>(p.r);
            const int   dg  = tg - static_cast<int>(p.g);
            const int   db  = tb - static_cast<int>(p.b);
            const auto  d   = static_cast<uint32_t>(dr*dr + dg*dg + db*db);
            if (d < bestDist)
            {
                bestDist = d;
                bestIdx  = static_cast<uint8_t>(i);
            }
        }
        return bestIdx;
    }

    // Maps a SurfaceCell to a pad/lower-button palette index (0-127).
    // Decorations collapse into the index (playhead → amber, held → white).
    // For Resting and Disabled states the baseColour drives the palette lookup
    // so the on-screen scope/section colours are faithfully reflected.
    uint8_t Push1Surface::rgbPaletteFor(CellState state,
                                          const CellDecoration& border,
                                          const CellDecoration& pip,
                                          float level,
                                          uint32_t baseColour) noexcept
    {
        // Border decoration overrides: playhead = amber, held step = white.
        if (border.present)
        {
            if (border.token == CellState::StepPlayhead) return 9;   // amber hi
            if (border.token == CellState::StepHeld)     return 3;   // white
        }
        // Latch pip = scope tint dim
        if (pip.present) return 2;  // grey light

        switch (state)
        {
            case CellState::Resting:            return nearestPaletteIndex(baseColour);
            case CellState::Pressed:            return 3;   // white
            case CellState::ModeActive:         return nearestPaletteIndex(baseColour ? baseColour : 0xFF00FF00u);
            case CellState::FuncHeld:           return 9;   // amber hi
            case CellState::Disabled:           return 1;   // grey lo — visible but clearly inactive

            // Step family
            case CellState::StepEmpty:          return 0;
            case CellState::StepTrigCertain:
            {
                // Level dims the colour (probability).
                if (level >= 0.75f) return 22;  // green hi
                if (level >= 0.5f)  return 19;  // green lo
                return 1;                        // grey lo
            }
            case CellState::StepTrigProbable:   return 19;  // green lo
            case CellState::StepTrigSuppressed: return 1;   // grey lo
            case CellState::StepFillAdd:        return 10;  // amber hi (orange)
            case CellState::StepFillSuppress:   return 46;  // blue
            case CellState::StepOutOfRange:     return 0;
            case CellState::StepPlayhead:       return 9;   // amber hi
            case CellState::StepHeld:           return 3;   // white

            // Selector family
            case CellState::SelectorCurrent:    return 3;   // white
            case CellState::SelectorOccupied:   return 22;  // green hi
            case CellState::SelectorEmpty:      return 1;   // grey lo
            case CellState::SelectorOutRange:   return 0;
            case CellState::SelectorNext:       return 50;  // orchid
            case CellState::SelectorChain:      return 50;  // orchid
            case CellState::SelectorDeviated:   return 50;  // orchid
            case CellState::SelectorHome:       return 9;   // amber

            // Mute
            case CellState::MuteMuted:          return 6;   // red
            case CellState::MuteAudible:        return 22;  // green hi

            // Machine picker
            case CellState::MachineCurrent:     return 3;   // white
            case CellState::MachineAvailable:   return 22;  // green hi
            case CellState::MachineUnavailable: return 1;   // grey lo

            // NoteEdit overlay
            case CellState::NoteEditActive:     return 22;  // green hi
            case CellState::NoteEditStaged:     return 8;   // red-amber
            case CellState::NoteEditOther:      return 19;  // green lo
            case CellState::NoteEditResting:    return 0;

            // Chromatic keyboard
            case CellState::ChromaticWhite:     return 22;  // green hi
            case CellState::ChromaticBlack:     return 19;  // green lo

            // Levels velocity picker
            case CellState::LevelsCell:         return 46;  // blue

            // Phrase-length authoring
            case CellState::LengthInRun:        return 50;  // orchid
            case CellState::LengthBoundary:     return 3;   // white (edge marker)
            case CellState::LengthOutRun:       return 0;

            default: return 1;  // grey lo for unknown tokens
        }
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
            const uint8_t colour = rgbPaletteFor(cell.base, cell.border, cell.pip, cell.level, cell.baseColour);
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
            const uint8_t tapC = rgbPaletteFor(tap.base, tap.border, tap.pip, tap.level, tap.baseColour);
            const auto si60 = static_cast<std::size_t>(60 - 36);
            if (tapC != padShadow_[si60])
            {
                padShadow_[si60] = tapC;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(tapC)));
            }
        }
        {
            const auto& nav  = model.navUp;
            const uint8_t navC = rgbPaletteFor(nav.base, nav.border, nav.pip, nav.level, nav.baseColour);
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
            const uint8_t colour = rgbPaletteFor(cell.base, cell.border, cell.pip, cell.level, cell.baseColour);
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
            const uint8_t colour = cell
                ? rgbPaletteFor(cell->base, cell->border, cell->pip, cell->level, cell->baseColour)
                : uint8_t(0);
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
            const uint8_t colour = rgbPaletteFor(cell.base, cell.border, cell.pip, cell.level, cell.baseColour);
            const int note = kStepNotes[static_cast<std::size_t>(s)];
            const auto si = static_cast<std::size_t>(note - 36);
            if (colour != padShadow_[si])
            {
                padShadow_[si] = colour;
                out.sendMessageNow(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
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
            const uint8_t val = cell ? monoFor(cell->base) : uint8_t(0);
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
        // Top 2 lines = value text; bottom 2 lines = param name.
        auto buildDisplayLine = [&](bool isValueLine) -> juce::String
        {
            juce::String line;
            line.preallocateBytes(68);
            for (int enc = 0; enc < 8; ++enc)
            {
                const auto& slot = model.slots[static_cast<std::size_t>(enc)];
                juce::String col = slot.inRange
                    ? (isValueLine ? slot.valueText : slot.label)
                    : juce::String();
                // Truncate or pad to exactly 8 chars.
                if (col.length() > 8) col = col.substring(0, 8);
                while (col.length() < 8) col += ' ';
                line += col;
                // Insert the within-display separator after the left encoder of each pair.
                if (enc % 2 == 0)
                    line += ' ';
            }
            // Should be exactly 8*8 + 4 = 68 chars.
            return line;
        };

        const juce::String valueLine = buildDisplayLine(true);
        const juce::String nameLine  = buildDisplayLine(false);

        // Lines 0-1 = value; lines 2-3 = name.
        if (valueLine != displayShadow_[0]) { displayShadow_[0] = valueLine; writeDisplayLine(out, 0, valueLine); }
        if (valueLine != displayShadow_[1]) { displayShadow_[1] = valueLine; writeDisplayLine(out, 1, valueLine); }
        if (nameLine  != displayShadow_[2]) { displayShadow_[2] = nameLine;  writeDisplayLine(out, 2, nameLine);  }
        if (nameLine  != displayShadow_[3]) { displayShadow_[3] = nameLine;  writeDisplayLine(out, 3, nameLine);  }
    }

}  // namespace lockstep
