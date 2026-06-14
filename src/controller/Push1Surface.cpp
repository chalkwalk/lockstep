#include "Push1Surface.h"
#include "SurfaceShared.h"
#include "../ui/UITheme.h"      // theme::kScope* / kVerb* sentinels for state→index mapping
#include "../ui/CellAppearance.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>

namespace lockstep
{
    // =========================================================================
    // Push 1 SysEx header: F0 47 7F 15 <cmd> ...
    // =========================================================================
    static constexpr uint8_t kPush1Header[] = { 0xF0, 0x47, 0x7F, 0x15 };

    // Push 1 drops MIDI when it arrives too fast — most visibly the connect burst
    // (mode change + clear + a full grid of LED writes), which left some pads
    // unlit until the next frame. Space each message a little. This is a brief
    // busy-wait: render runs on the message thread and the bursts are bounded
    // (~70 messages on connect), so the worst-case stall is a few ms.
    static void paceMidi() noexcept
    {
        const auto wait = juce::Time::getHighResolutionTicksPerSecond() / 5000;  // ~200 us
        const auto end = juce::Time::getHighResolutionTicks() + wait;
        while (juce::Time::getHighResolutionTicks() < end) { /* spin */ }
    }

    static void sendPaced(juce::MidiOutput& out, const juce::MidiMessage& m) noexcept
    {
        out.sendMessageNow(m);
        paceMidi();
    }

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
        for (auto b : payload) block.append(&b, 1);
        const uint8_t eox = 0xF7;
        block.append(&eox, 1);
        sendPaced(out, juce::MidiMessage(block.getData(),
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
        sendPaced(out, juce::MidiMessage(block.getData(),
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
    // decodeDelta — two's-complement relative encoder (Push 1 specific)
    // =========================================================================

    int Push1Surface::decodeDelta(int ccValue) noexcept
    {
        return ctrl::decodeTwosComplementDelta(ccValue);
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
                    const bool isDoubleTap = (!ts.touching) && ((now - ts.touchMs) < kDoubleTapMs) && !ts.turned;
                    ts.touching = true;
                    ts.turned = false;
                    ts.touchMs = now;
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
                        sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
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
                    // TAP (note 60) — momentary. Emit ButtonUp on release too, or
                    // the press is never cleared and the pad stays lit after a tap.
                    if (sink.emitEvent)
                        sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
                                                : ControllerEvent::Type::ButtonUp,
                                         ControllerButton::TapTempo, -1, 0 });
                    return;
                }
                if (col == 1)
                {
                    // NavUp (note 61)
                    if (sink.emitEvent)
                        sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
                                                : ControllerEvent::Type::ButtonUp,
                                         ControllerButton::NavUp, -1, 0 });
                    return;
                }
                // Sections 0-5 on notes 62-67
                const int sectionIdx = col - 2;
                if (sectionIdx >= 0 && sectionIdx <= 5 && sink.emitEvent)
                    sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
                                            : ControllerEvent::Type::ButtonUp,
                                     ControllerButton::Section, sectionIdx, 0 });
                return;
            }

            // Verb/nav row (row 3: notes 52-59)
            if (note >= 52 && note <= 59)
            {
                const int col = note - 52;
                if (sink.emitEvent)
                    sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
                                            : ControllerEvent::Type::ButtonUp,
                                     kVerbRowButtons[static_cast<std::size_t>(col)], -1, 0 });
                return;
            }

            // Step rows (row 2: 44-51 = steps 0-7; row 1: 36-43 = steps 8-15)
            // Push pads are velocity-sensitive: forward the note-on velocity so
            // chromatic/levels play-in can use the real dynamics (where available).
            for (int s = 0; s < 16; ++s)
            {
                if (note == kStepNotes[static_cast<std::size_t>(s)])
                {
                    if (sink.emitEvent)
                        sink.emitEvent({ isDown ? ControllerEvent::Type::ButtonDown
                                                : ControllerEvent::Type::ButtonUp,
                                         ControllerButton::Step, s, 0,
                                         isDown ? msg.getVelocity() : 0 });
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
            // Pre-set the strip shadow so render() (called immediately after in
            // the same drain) does not echo this value back to the hardware —
            // i.e. soft-takeover: only echo when the model differs from what
            // the user last told us they sent.
            stripShadow_ = juce::roundToInt(juce::jlimit(0.0f, 1.0f, norm) * 16383.0f);
            if (sink.setCrossfader)
                sink.setCrossfader(norm);
        }

        // -----------------------------------------------------------------
        // CC messages
        // -----------------------------------------------------------------
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
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
                        sink.emitEvent({ (val == 127) ? ControllerEvent::Type::ButtonDown
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

    // -------------------------------------------------------------------------
    // Static semantic → palette-index table (DESIGN §35.8; replaces the dynamic
    // Oklab matcher). Pad indices are fixed in the Push 1 firmware, so the table
    // is literal indices — no runtime palette/match needed. Each index was
    // hand-picked from the *as-displayed* factory palette (tools/color_audit)
    // top-down: the boldest/most-important states take the boldest (highest
    // chroma) device entries; secondary states take dim/dark variants; states
    // that never co-occur may share an entry. Tweak any single role here without
    // touching the others — that is the whole point of going static (the dynamic
    // match clumped distinct-but-similar UI colours onto identical pads).
    //
    // Roles named by the device colour they land on (verified mutually distinct
    // within each surface/view). The on-screen renderer keeps its own RGB
    // vocabulary (UITheme.h); screen and Push agree on the CellState *meaning*,
    // each styled for its medium.
    namespace pidx
    {
        // Neutrals.
        constexpr uint8_t kOff = 0, kWhite = 119, kGreyMid = 2, kGreyDim = 71;
        // Bold hues — active / high-importance states. (Reference palette: some
        // entries are unused today but kept for completeness.)
        [[maybe_unused]] constexpr uint8_t kAmber = 9, kRed = 5, kGreen = 21, kOrange = 61,
                                           kBlue = 46, kCyan = 37, kAzure = 41, kIndigo = 50,
                                           kMagenta = 53, kGold = 97, kChartreuse = 17,
                                           kSpring = 25, kViolet = 80, kTeal = 34, kLime = 18,
                                           kRose = 57;
        // Step-grid green variants.
        [[maybe_unused]] constexpr uint8_t kGreenDim = 23, kTealDk = 31;
        // Dark hues — resting modifiers / dim variants.
        [[maybe_unused]] constexpr uint8_t kAmberDk = 11, kCyanDk = 39, kSpringDk = 27,
                                           kIndigoDk = 51, kGoldDk = 15, kMagentaDk = 55,
                                           kRedDk = 7, kChartreuseDk = 19, kAzureDk = 43,
                                           kOrangeDk = 10;
        // Navigation slate.
        constexpr uint8_t kSlate = 103;
    }

    // The 8 scope hues (buildSurfaceModel stores the exact theme constant in
    // scopeTint) → their bold pad index, so an in-scope cell lights the scope's
    // colour exactly like the on-screen glow.
    static uint8_t scopeTintIndex(uint32_t argb) noexcept
    {
        using namespace theme;
        switch (argb)
        {
            case kScopeTrack:   return pidx::kCyan;
            case kScopePhrase:  return pidx::kIndigo;
            case kScopeScene:   return pidx::kSpring;
            case kScopeMorph:   return pidx::kMagenta;
            case kScopeSong:    return pidx::kGold;
            case kScopeMute:    return pidx::kRed;
            case kScopePMute:   return pidx::kRose;
            case kScopeFill:    return pidx::kChartreuse;
            case kScopeMachine: return pidx::kLime;
            default:            return pidx::kGreyMid;   // kScopeStep / none
        }
    }

    // Grid-area cells (cell.button == Step): all the CellState families share the
    // step grid. Overlays (held/playhead/home border, press) win over the body
    // token — see PUSH1.md "Fill + border collapse".
    static uint8_t stepGridIndex(const SurfaceCell& c) noexcept
    {
        using S = CellState;
        if (c.pressed) return pidx::kWhite;
        if (c.base == S::StepHeld || (c.border.present && c.border.token == S::StepHeld))
            return pidx::kWhite;
        if (c.border.present && c.border.token == S::StepPlayhead)
            return pidx::kAmber;
        if (c.border.present && c.border.token == S::SelectorHome)
            return pidx::kAmber;
        if (c.scopeTint != 0)
            return scopeTintIndex(c.scopeTint);

        // CellStates.def encodes the pushPad column for every token —
        // one source of truth shared with X-Touch and screen.
        return appearanceOf(c.base, { 0u, 0u, pidx::kGreyDim, 0u }).pushPad;
    }

    // Key cells (modifiers / verbs / sections / nav / tap). Active = pressed or
    // ModeActive → the bold hue; resting → the dark/dim variant. A held scope
    // glow (scopeTint set) wins, exactly as on screen.
    static uint8_t keyIndex(const SurfaceCell& c) noexcept
    {
        using namespace theme;
        if (c.disabled) return pidx::kOff;
        if (c.scopeTint != 0) return scopeTintIndex(c.scopeTint);
        const bool active = c.pressed || c.base == CellState::ModeActive;

        switch (c.button)
        {
            case ControllerButton::Func:
                return active ? pidx::kAmber : pidx::kAmberDk;
            case ControllerButton::TapTempo:
                return active ? pidx::kWhite : pidx::kGreyDim;

            case ControllerButton::TrackScope:
                return active ? pidx::kCyan : pidx::kCyanDk;
            case ControllerButton::PhraseScope:
                return active ? pidx::kIndigo : pidx::kIndigoDk;
            case ControllerButton::SceneScope:
                return active ? pidx::kSpring : pidx::kSpringDk;
            case ControllerButton::MorphScope:
                return active ? pidx::kMagenta : pidx::kMagentaDk;
            case ControllerButton::SongScope:
                return active ? pidx::kGold : pidx::kGoldDk;
            case ControllerButton::MuteScope:
                // PMute (Func+Mute) → rose; otherwise red.
                return active ? (c.baseColour == kScopePMute ? pidx::kRose
                                                             : pidx::kRed)
                              : pidx::kRedDk;
            case ControllerButton::FillScope:
                return active ? pidx::kChartreuse : pidx::kChartreuseDk;

            case ControllerButton::NavUp:
            case ControllerButton::NavLeft:
            case ControllerButton::NavDown:
            case ControllerButton::NavRight:
                return active ? pidx::kSlate : pidx::kGreyDim;

            case ControllerButton::VerbRecord:
                if (c.baseColour == kVerbODActive) return pidx::kAmber;  // overdub armed
                return active ? pidx::kRed : pidx::kGreyDim;
            case ControllerButton::VerbPlay:
                return active ? pidx::kGreen : pidx::kGreyDim;
            case ControllerButton::VerbClear:
                return active ? pidx::kOrangeDk : pidx::kGreyDim;
            case ControllerButton::VerbSnapshot:                 // Snapshot
                return active ? pidx::kViolet : pidx::kGreyDim;
            case ControllerButton::VerbConfirm:                  // confirm
                return active ? pidx::kGreen : pidx::kGreyDim;
            case ControllerButton::VerbDelete:
            case ControllerButton::VerbPanic:
                return pidx::kRed;
            case ControllerButton::VerbStopLegacy:                // legacy — neutral
                return pidx::kGreyDim;

            case ControllerButton::Section:
                if (c.baseColour == kScopeMachine) return pidx::kLime;
                if (c.baseColour == kScopeNoteEdit) return pidx::kAzure;
                if (c.baseColour == 0xFF404010u) return pidx::kGoldDk;  // master-active
                return active ? pidx::kTeal : pidx::kTealDk;

            default:
                return active ? pidx::kWhite : pidx::kGreyDim;
        }
    }

    // Maps a SurfaceCell to a pad/lower-button palette index (0-127) via the
    // static semantic table. Grid-area cells dispatch by CellState; key cells by
    // button + state.
    uint8_t Push1Surface::rgbPaletteFor(const SurfaceCell& cell) noexcept
    {
        return cell.button == ControllerButton::Step ? stepGridIndex(cell)
                                                     : keyIndex(cell);
    }

    // Maps a CellState to a bi-colour value 0-24 for upper/scene buttons.
    // Green = active/occupied, Orange = attention, Red = muted/off.
    uint8_t Push1Surface::biColourFor(CellState state) noexcept
    {
        switch (state)
        {
            case CellState::ModeActive:       return 22;  // green hi
            case CellState::Pressed:          return 22;  // green hi
            case CellState::SelectorCurrent:  return 22;  // green hi
            case CellState::SelectorOccupied: return 19;  // green lo
            case CellState::MuteAudible:      return 22;  // green hi
            case CellState::MuteMuted:        return 4;   // red hi
            case CellState::StepTrigCertain:  return 22;  // green hi
            case CellState::StepFillAdd:      return 10;  // orange hi
            default:                          return 0;   // off
        }
    }

    // Maps a CellState to a mono button LED value: 0=off, 1=dim, 4=bright.
    uint8_t Push1Surface::monoFor(CellState state) noexcept
    {
        switch (state)
        {
            case CellState::ModeActive: return 4;
            case CellState::Pressed:    return 4;
            case CellState::Disabled:   return 0;
            default:                    return 1;   // Resting and anything else: dim
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
        const juce::juce_wchar full =
            kUseBlockGlyphs ? static_cast<juce::juce_wchar>(0x06)
                            : static_cast<juce::juce_wchar>('#');
        const juce::juce_wchar blank = ' ';

        // Sub-char leading-edge glyph for the fractional fill (block mode only).
        auto partial = [&](float frac) -> juce::juce_wchar {
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
        const int fullCount = static_cast<int>(filled);
        const float frac = filled - static_cast<float>(fullCount);
        for (int i = 0; i < width; ++i)
        {
            if (i < fullCount) bar += full;
            else if (i == fullCount) bar += partial(frac);
            else bar += blank;
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
                sendPaced(out, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
            }
        }

        // Section row (row 4: notes 60-67)
        // note 60 = TAP, note 61 = NavUp, notes 62-67 = sections 0-5
        {
            const auto& tap = model.tap;
            const uint8_t tapC = rgbPaletteFor(tap);
            const auto si60 = static_cast<std::size_t>(60 - 36);
            if (tapC != padShadow_[si60])
            {
                padShadow_[si60] = tapC;
                sendPaced(out, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(tapC)));
            }
        }
        {
            const auto& nav = model.navUp;
            const uint8_t navC = rgbPaletteFor(nav);
            const auto si61 = static_cast<std::size_t>(61 - 36);
            if (navC != padShadow_[si61])
            {
                padShadow_[si61] = navC;
                sendPaced(out, juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(navC)));
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
                sendPaced(out, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
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
                sendPaced(out, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
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
                sendPaced(out, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(colour)));
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
                    sendPaced(out, juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(0)));
                }
            }
        }

        // --- Upper display buttons (CC 20-27) — bi-colour ---
        // Map to function row (Q-row items).
        static constexpr std::array<int, 8> kUpperRowFn = { 0, 1, 2, 3, 4, 5, 6, 7 };
        for (int i = 0; i < 8; ++i)
        {
            const auto& cell = model.functionRow[static_cast<std::size_t>(kUpperRowFn[static_cast<std::size_t>(i)])];
            const uint8_t colour = biColourFor(cell.base);
            const auto si = static_cast<std::size_t>(i);
            if (colour != upperShadow_[si])
            {
                upperShadow_[si] = colour;
                sendPaced(out, juce::MidiMessage::controllerEvent(1, 20 + i, colour));
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
                sendPaced(out, juce::MidiMessage::controllerEvent(1, 36 + i, 0));
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
                sendPaced(out, juce::MidiMessage::controllerEvent(1, entry.cc, val));
            }
        }

        // --- Touch strip LED (pitch-bend out from model.crossfader) ---
        {
            const int stripVal = juce::roundToInt(
                juce::jlimit(0.0f, 1.0f, model.crossfader) * 16383.0f);
            if (stripVal != stripShadow_)
            {
                stripShadow_ = stripVal;
                sendPaced(out, juce::MidiMessage::pitchWheel(1, stripVal));
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
        auto buildLine = [&](auto&& cellFn) -> juce::String {
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
