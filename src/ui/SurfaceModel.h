#pragma once

#include <array>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../io/ControllerEvent.h"
#include "../core/Sequence.h"   // kNumTracks
#include "GridDisplayMode.h"

namespace lockstep
{
    class  LockstepProcessor;
    struct UiState;
    class  EditContext;
    class  PressTracker;

    // =========================================================================
    // CellState — add-only semantic token (§35.8.6, CLAUDE.md)
    //
    // Never renumber or remove a token. Deprecate and rely on compatColour()
    // fallback. External controller profiles bind by token value.
    // =========================================================================
    enum class CellState : uint16_t
    {
        // Key family — folds in KeyButtonState.
        Resting    = 0,
        Pressed    = 1,
        ModeActive = 2,
        FuncHeld   = 3,   // retained for controller compat; screen uses Resting + label swap
        Disabled   = 4,

        // Step-grid family
        StepEmpty          = 10,
        StepTrigCertain    = 11,
        StepTrigProbable   = 12,
        StepTrigSuppressed = 13,
        StepFillAdd        = 14,
        StepFillSuppress   = 15,
        StepOutOfRange     = 16,
        StepPlayhead       = 17,
        StepHeld           = 18,

        // Selector / re-skin family (scope picker, mute viewer, machine picker)
        SelectorCurrent    = 30,
        SelectorOccupied   = 31,
        SelectorEmpty      = 32,
        SelectorOutRange   = 33,
        SelectorNext       = 34,
        SelectorChain      = 35,
        MuteMuted          = 40,
        MuteAudible        = 41,
        MachineCurrent     = 50,
        MachineAvailable   = 51,
        MachineUnavailable = 52,

        // NoteEdit overlay family
        NoteEditActive   = 60,
        NoteEditStaged   = 61,  // note active and staged for removal
        NoteEditOther    = 62,  // present in other octave(s) only
        NoteEditResting  = 63,  // no note on this semitone

        // Chromatic keyboard family
        ChromaticWhite   = 70,
        ChromaticBlack   = 71,

        // Levels velocity picker
        LevelsCell       = 80,

        // Phrase-length authoring re-skin (Phase 7 / DESIGN §34.4).
        // Applied while Pattern+Func or Scene+Func is held (momentary).
        LengthInRun    = 90,   // step falls within the active phrase length
        LengthBoundary = 91,   // the exact last step (length boundary marker)
        LengthOutRun   = 92,   // step falls outside the active phrase length

        // Phrase/Section selector badges (Phase 7 / DESIGN §4.7).
        SelectorDeviated = 95, // phrase currently playing due to a live deviation
        SelectorHome     = 96, // the scene's global/home phrase (dual-marker border)
    };

    // =========================================================================
    // CellDecoration — named overlay channel (§35.8.3)
    // =========================================================================
    struct CellDecoration
    {
        CellState token   = CellState::Resting;
        uint32_t  colour  = 0;
        bool      present = false;
    };

    // =========================================================================
    // SurfaceCell — complete appearance description for one grid cell.
    //
    // Frozen §35.8.3 prefix: controllers bind by field offset — do NOT reorder.
    // Screen-text extension is appended after the frozen block; controllers ignore it.
    // =========================================================================
    struct SurfaceCell
    {
        // --- Frozen §35.8.3 contract (do not reorder) ---
        ControllerButton button = ControllerButton::None;
        int              index  = -1;           // step/section/track index, else -1
        CellState        base       = CellState::Resting;  // semantic token
        uint32_t         baseColour = 0;         // resolved ARGB — fallback for dumb devices
        float            level      = 1.0f;      // 0..1 brightness (probability dim etc.)
        CellDecoration   border{};               // playhead / held / mode-active outline
        CellDecoration   dot{};                  // P-Lock presence
        CellDecoration   strip{};                // compound-chord / fill marker
        CellDecoration   pip{};                  // latch / virtual-hold (MHZ.9.6)

        // --- Screen-text extension (Slice 1+; controllers ignore) ---
        juce::String primary;    // ALWAYS the live function (decision 1)
        juce::String funcHint;   // dim secondary; "" = none (Func-variant or always-on hint)
        juce::String keyHint;    // physical QWERTY legend ("D", "5", "Q" etc.)
        bool pressed  = false;   // physical OR mouse press, every modality
        bool disabled = false;   // dead key — base label visibly dimmed

        // Scope-glow tint (MHZ.1.x, DESIGN §6.6): non-zero ARGB when this cell is
        // *in scope* under a held modifier — i.e. the held scope rebinds it. The
        // screen renders fill+border in this colour, brighter, so the surface shows
        // exactly which keys the scope rewrites. 0 = not in scope (normal tint).
        uint32_t scopeTint = 0;
    };

    // Returns a fallback ARGB colour for any CellState token.
    // default: branch ensures future add-only tokens always resolve.
    uint32_t compatColour(CellState state, uint32_t fallback = 0xFF303030u) noexcept;

    // =========================================================================
    // SurfaceSlot — one manipulation-zone encoder slot (§35.8.5)
    //
    // Populated by buildSurfaceModel and consumed by controller render() paths.
    // Allows render() to be fully model-driven without poking LockstepProcessor.
    // =========================================================================
    enum class RingMode : uint8_t
    {
        Dot,              // single dot — stepped or enum params
        UnipolarFill,     // fill from bottom — standard continuous (min >= 0)
        BipolarFromCentre // fill from centre — bipolar (min < 0)
    };

    struct SurfaceSlot
    {
        juce::String label;       // parameter label (empty when slot is out of range)
        juce::String sectionLabel; // owning section name (e.g. "FILTER"); for displays
        juce::String valueText;   // formatted value string (empty when out of range)
        float        position  = 0.0f; // normalised 0..1 for ring/display
        RingMode     ringMode  = RingMode::UnipolarFill;
        bool         hasOverride = false; // true when a P-Lock is active for this slot
        bool         inRange     = false; // false when slot index exceeds machine's schema
    };

    // =========================================================================
    // SurfaceModel — complete per-frame surface description (§35.8.2)
    //
    // Zone arrays indexed by §35.8.2 zones; byButton() provides reverse lookup.
    // ManipulationZone (SurfaceSlot, §35.8.5) is out of scope for MW.5(a).
    // =========================================================================
    struct SurfaceModel
    {
        static constexpr uint32_t kCurrentSchema = 2;
        uint32_t schemaVersion = kCurrentSchema;

        // Modifier cluster: Func/Track/Pattern/Part/Scene/Master/Mute/Fill (indices 0-7).
        std::array<SurfaceCell, 8>  modifiers{};

        // Section row: TRIG/SRC/FILTER/AMP/MOD/FX (canonical section index 0-5).
        std::array<SurfaceCell, 6>  section{};

        // Function row: all 10 Q-row keys Q/W/E/R/T/Y/U/I/O/P (indices 0-9).
        std::array<SurfaceCell, 10> functionRow{};

        // Standalone number-row utility keys.
        SurfaceCell tap{};     // key 3 — TapTempo / Func+3=MetronomeToggle
        SurfaceCell navUp{};   // key 4 — NavUp / Func+4=SoundPool

        // Step grid: page-relative steps 0-15 (Slice 2+; initialised to defaults).
        std::array<SurfaceCell, 16> step{};

        // Per-track: does this track carry a real (non-stub) machine? Empty tracks
        // are greyed on screen (track strip + the edit area when focused) and can
        // be mirrored on a controller's track-select LEDs. Sourced from
        // LockstepProcessor::isTrackEmpty so screen and controller never diverge.
        std::array<bool, kNumTracks> trackHasMachine{};

        // Per-track: is this track playing a phrase OTHER than its scene's home
        // (global) phrase — i.e. deviated off the row (DESIGN §4.7)? Drives a
        // persistent deviation badge on screen and a controller's track LEDs.
        std::array<bool, kNumTracks> trackDeviated{};

        // Manipulation-zone encoder band (§35.8.5).
        // 8 slots starting at slotOffset (passed to buildSurfaceModel).
        // Controllers read these for ring LED positions and display text.
        std::array<SurfaceSlot, 8> slots{};

        // Scene A/B crossfader value normalised 0..1 (0 = full A, 1 = full B).
        // Passed explicitly to buildSurfaceModel since it lives in the editor.
        float crossfader = 0.0f;

        // Lookup by (ControllerButton, index). Returns nullptr if not found.
        [[nodiscard]] const SurfaceCell* byButton(ControllerButton btn, int idx = -1) const noexcept;
    };

    // =========================================================================
    // buildSurfaceModel — pure builder: the single computation (§35.8.1)
    //
    // Produces all cell appearances for one frame from the given state.
    // Both the screen renderer and controller feedback call this; they cannot
    // diverge because they call the same function with the same state.
    // =========================================================================
    SurfaceModel buildSurfaceModel(const UiState&      ui,
                                   const EditContext&  ec,
                                   const PressTracker* press,
                                   LockstepProcessor&  proc,
                                   int                 activeTrack,
                                   int                 stepPage,
                                   GridDisplayMode     displayMode,
                                   int                 slotOffset      = 0,
                                   float               crossfaderValue = 0.5f);
}
