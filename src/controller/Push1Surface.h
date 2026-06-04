#pragma once

#include <array>
#include "IControllerSurface.h"

namespace lockstep
{
    // Bespoke controller surface for the Ableton Push 1.
    //
    // Pad layout (bottom-left origin = note 36, note = 36 + row*8 + col):
    //   Rows 5-8 top-left 2 cols : 8 modifier cluster (mirrors on-screen col 1-2)
    //   Row 4 (notes 60-67)      : TAP, NavUp, TRIG, SRC, FILTER, AMP, MOD, FX
    //   Row 3 (notes 52-59)      : NavLeft, NavDown, NavRight, Snapshot, Rec, Play, Clear, Yes
    //   Row 2 (notes 44-51)      : steps 0-7
    //   Row 1 (notes 36-43)      : steps 8-15
    //   Top-right pads (rows 5-8, cols 2-7) : dark / unused
    //
    // Input:
    //   Pads (Note On/Off)   → Step/Section/Modifier/Verb ButtonDown/Up events
    //   CC 71-78 (turn)      → applyParamDelta MZ slots 0-7
    //   Notes 0-7 (touch)    → double-tap = resetSlot; single touch = no-op
    //   CC 14 / 15 / 79      → applyGlobalDelta Tempo / Swing / Master
    //   Pitch bend           → setCrossfader (0..1)
    //   Mono CC buttons      → their ControllerButton events (Play 85, Rec 86, etc.)
    //
    // Feedback (render, diffed against shadow caches):
    //   Pads + lower buttons (CC 102-109) → RGB palette index (0-127)
    //   Upper buttons (CC 20-27)          → bi-colour 0-24
    //   Scene buttons (CC 36-43)          → bi-colour 0-24
    //   Function buttons (mono CCs)       → 0/1/4 (off/dim/bright)
    //   Touch strip                       → pitch-bend out from model.crossfader
    //   Display (4 × 68 chars)            → name (lower 2 lines) / value (upper 2 lines)
    //
    // Hardware reference: PUSH1.md (probe confirmed 2026-06-03).
    class Push1Surface : public IControllerSurface
    {
    public:
        Push1Surface();

        void onConnect(juce::MidiOutput& out) override;
        void onInput(const juce::MidiMessage& msg, ControllerEventSink& sink) override;
        void render(const SurfaceModel& model, juce::MidiOutput& out) override;

    private:
        // Two's-complement relative encoder decode (same as X-Touch).
        static int decodeDelta(int ccValue) noexcept;

        // SysEx helpers
        static void sendSysEx(juce::MidiOutput& out,
                              std::initializer_list<uint8_t> payload);
        void sendModeChange(juce::MidiOutput& out, uint8_t mode);
        void sendStripMode(juce::MidiOutput& out, uint8_t mode);
        void writeDisplayLine(juce::MidiOutput& out, int line, const juce::String& text);
        void clearDisplay(juce::MidiOutput& out);

        // Colour-space helpers.
        // rgbPaletteFor is cell-driven: it resolves from cell.baseColour (the same
        // scope-tinted ARGB the on-screen renderer draws) so the Push mirrors the
        // screen, with semantic overrides for press / held / playhead / off.
        static uint8_t rgbPaletteFor(const SurfaceCell& cell) noexcept;
        static uint8_t biColourFor(CellState state) noexcept;
        static uint8_t monoFor(CellState state) noexcept;

        // ControllerButton → function-button CC (0 = not a mono-CC button).
        static int monoCC(ControllerButton btn) noexcept;

        // ----------------------------------------------------------------
        // Encoder touch / double-tap reset tracking
        // ----------------------------------------------------------------
        struct TouchState
        {
            bool      touching  = false;
            bool      turned    = false;   // any turn since touch-down?
            juce::int64 touchMs = 0;       // time of most recent touch-down
        };
        std::array<TouchState, 8> encoderTouch_{};
        static constexpr juce::int64 kDoubleTapMs = 400;

        // ----------------------------------------------------------------
        // Shadow caches — 255 = uninitialised (forces first-frame output)
        // ----------------------------------------------------------------

        // Pad grid + lower buttons: 64 pads (notes 36-99) + 8 lower (CC 102-109).
        // Indexed pad as (note - 36), lower as (64 + cc - 102).
        std::array<uint8_t, 72> padShadow_{};

        // Upper display buttons (CC 20-27) and scene buttons (CC 36-43).
        std::array<uint8_t, 8>  upperShadow_{};
        std::array<uint8_t, 8>  sceneShadow_{};

        // Mono function-button LEDs: indexed by shadow table position.
        // We track 40 potential CCs (see kMonoButtons).
        std::array<uint8_t, 40> monoShadow_{};

        // Strip (pitch-bend out) last value sent, 0-16383; -1 = uninitialised.
        int stripShadow_ = -1;

        // Display line text — compared to avoid resending unchanged lines.
        std::array<juce::String, 4> displayShadow_{};

        // ----------------------------------------------------------------
        // Pad layout tables (compile-time)
        // ----------------------------------------------------------------

        // Modifier pads: top-left 2×4 (rows 5-8, cols 0-1).
        // Index 0-7 matches modifiers[0-7] in SurfaceModel.
        // Layout: col0=Func/Phrase/Morph/Mute (top→bottom), col1=Track/Scene/Song/Fill.
        static constexpr std::array<int, 8> kModifierNotes = {
            92, 93,   // row 8: Func (col0), Track (col1)
            84, 85,   // row 7: Phrase, Scene
            76, 77,   // row 6: Morph, Song
            68, 69,   // row 5: Mute, Fill
        };

        // Section row: row 4 (notes 60-67).
        // TAP=60, NavUp=61, TRIG=62, SRC=63, FILTER=64, AMP=65, MOD=66, FX=67.
        static constexpr std::array<int, 8> kSectionRowNotes = {
            60, 61, 62, 63, 64, 65, 66, 67
        };

        // Verb/nav row: row 3 (notes 52-59).
        // NavLeft=52, NavDown=53, NavRight=54, Snapshot=55,
        // Rec=56, Play=57, Clear=58, Yes=59.
        static constexpr std::array<int, 8> kVerbRowNotes = {
            52, 53, 54, 55, 56, 57, 58, 59
        };

        // Step rows: row 2 = steps 0-7 (notes 44-51), row 1 = steps 8-15 (notes 36-43).
        static constexpr std::array<int, 16> kStepNotes = {
            44, 45, 46, 47, 48, 49, 50, 51,  // steps 0-7
            36, 37, 38, 39, 40, 41, 42, 43,  // steps 8-15
        };

        // ControllerButton values for verb/nav row (matches kVerbRowNotes order).
        static constexpr std::array<ControllerButton, 8> kVerbRowButtons = {
            ControllerButton::NavLeft,
            ControllerButton::NavDown,
            ControllerButton::NavRight,
            ControllerButton::VerbYes,    // Snapshot
            ControllerButton::VerbRecord,
            ControllerButton::VerbPlay,
            ControllerButton::VerbClear,
            ControllerButton::VerbNo,
        };

        // ControllerButton values for modifier pads (matches kModifierNotes order).
        static constexpr std::array<ControllerButton, 8> kModifierButtons = {
            ControllerButton::Func,
            ControllerButton::TrackScope,
            ControllerButton::PhraseScope,
            ControllerButton::SceneScope,
            ControllerButton::MorphScope,
            ControllerButton::SongScope,
            ControllerButton::MuteScope,
            ControllerButton::FillScope,
        };

        // Mono CC button table: {cc, ControllerButton}.
        // Only the subset that maps to a Lockstep action is wired; the rest stay dark.
        struct MonoEntry { int cc; ControllerButton button; };
        static constexpr std::array<MonoEntry, 16> kMonoButtons = {{
            {  85, ControllerButton::VerbPlay        },
            {  86, ControllerButton::VerbRecord      },
            {  44, ControllerButton::NavLeft         },
            {  45, ControllerButton::NavRight        },
            {  46, ControllerButton::NavUp           },
            {  47, ControllerButton::NavDown         },
            {   3, ControllerButton::TapTempo        },
            {   9, ControllerButton::MetronomeToggle },
            {  87, ControllerButton::VerbClear       },  // New → Clear alias
            { 118, ControllerButton::VerbDelete      },  // Delete
            { 119, ControllerButton::VerbNo          },  // Undo → No/cancel alias
            {  49, ControllerButton::Func            },  // Shift → Func alias
            {  60, ControllerButton::MuteScope       },  // Mute → hold-to-mute (= Z)
            {  29, ControllerButton::StopReset       },  // Stop Clip → stop + reset to top
            {  54, ControllerButton::NavLeft         },  // Octave Down → NavLeft (octave in note/chrom)
            {  55, ControllerButton::NavRight        },  // Octave Up → NavRight (octave in note/chrom)
        }};
    };
}
