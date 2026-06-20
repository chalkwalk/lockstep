#pragma once

#include <array>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../io/ControllerEvent.h"
#include "../core/Sequence.h"   // kNumTracks
#include "../machine/IMachine.h"  // kMaxSections
#include "GridDisplayMode.h"
#include "../command/Gesture.h"

namespace lockstep
{
    class LockstepProcessor;
    struct UiState;
    class EditContext;
    class PressTracker;

    // =========================================================================
    // CellState — add-only semantic token (§35.8.6, CLAUDE.md)
    //
    // Never renumber or remove a token. Deprecate and rely on compatColour()
    // fallback. External controller profiles bind by token value.
    // =========================================================================
    enum class CellState : uint16_t
    {
        // Key family — folds in KeyButtonState.
        Resting = 0,
        Pressed = 1,
        ModeActive = 2,
        FuncHeld = 3,   // retained for controller compat; screen uses Resting + label swap
        Disabled = 4,
        ModalEntryInert = 5,  // modal-entry key: reachable but band has no enabled content

        // Step-grid family
        StepEmpty = 10,
        StepTrigCertain = 11,
        StepTrigProbable = 12,
        StepTrigSuppressed = 13,
        StepFillAdd = 14,
        StepFillSuppress = 15,
        StepOutOfRange = 16,
        StepPlayhead = 17,
        StepHeld = 18,

        // Selector / re-skin family (scope picker, mute viewer, machine picker)
        SelectorCurrent = 30,
        SelectorOccupied = 31,
        SelectorEmpty = 32,
        SelectorOutRange = 33,
        SelectorNext = 34,
        SelectorChain = 35,
        MuteMuted = 40,
        MuteAudible = 41,
        MachineCurrent = 50,
        MachineAvailable = 51,
        MachineUnavailable = 52,

        // NoteEdit overlay family
        NoteEditActive = 60,
        NoteEditStaged = 61,  // note active and staged for removal
        NoteEditOther = 62,  // present in other octave(s) only
        NoteEditResting = 63,  // no note on this semitone

        // Chromatic keyboard family
        ChromaticWhite = 70,
        ChromaticBlack = 71,

        // Levels velocity picker
        LevelsCell = 80,

        // Phrase-length authoring re-skin (Phase 7 / DESIGN §34.4).
        // Applied while Phrase+Func (focused) or Morph+Func (broadcast) is held.
        LengthInRun = 90,   // step falls within the active phrase length
        LengthBoundary = 91,   // the exact last step (length boundary marker)
        LengthOutRun = 92,   // step falls outside the active phrase length

        // Phrase/Section selector badges (Phase 7 / DESIGN §4.7).
        SelectorDeviated = 95, // phrase currently playing due to a live deviation
        SelectorHome = 96, // the scene's global/home phrase (dual-marker border)

        // Morph step view (5.2): step grid in morph mode shows A/B pole states.
        // Row 0 (D-;) = A poles, Row 1 (C-/) = B poles.
        MorphPoleActive = 100,  // pole has a live value in the morph overlay
        MorphPoleDormant = 101,  // pole value saved in UI memory, suppressed from blend
        MorphPoleDark = 102,  // no value has been captured for this pole

        // Sound Pool overlay (5.7): Fill+SRC re-skins the grid to saved sounds.
        SoundPoolOccupied = 110,  // slot has a saved sound
        SoundPoolEmpty = 111,  // slot is empty
        SoundPoolCurrent = 112,  // the currently active sound on this track

        // Retrig/ratchet overlay (5.7): Fill+TRIG re-skins the grid to rate choices.
        // Cells show available retrig rates; selected = the rate set on the held step.
        RetrigRate = 120,  // an available ratchet rate
        RetrigSelected = 121,  // the rate currently selected / authored on this step

        // Slice point picker (5.7): when the active track is ISliceable, the Retrig
        // overlay cells address slice points instead of rates.
        SlicePoint = 130,  // an addressable slice point
        SliceSelected = 131,  // slice point currently set on the held step
        SliceEmpty = 132,  // no slice at this index

        // FX insert picker (6.5): Func+FX re-skins the step grid to the effect catalogue.
        EffectAvailable = 140,  // an available effect type (not loaded)
        EffectLoaded = 141,  // this effect is currently loaded in the focused insert slot
        EffectLoadedOther = 142,  // this effect is loaded in a different slot (dim cross-slot hint)

        // Confirm overlay (8.24): P key in PendingConfirm layer shows CONFIRM/CANCEL.
        ConfirmYes = 150,  // P without Func — green affirm state
        ConfirmNo = 151,  // P with Func held — red cancel state

        // Generator hub (9.10): momentary picker for the deterministic generator family.
        // Each token is a distinct hue on hardware (PRINCIPLES §19 dual-target).
        GeneratorEuclid = 160,
        GeneratorDensity = 161,
        GeneratorVel = 162,
    };

    // Pure mapping for the §34.4 length-edit re-skin: classify an absolute step
    // index against the track length. Shared by buildSurfaceModel() and tests so
    // the boundary rule cannot diverge.
    inline CellState lengthEditCellState(int absStepIdx, int trackLength)
    {
        const int oneBased = absStepIdx + 1;
        if (oneBased < trackLength) return CellState::LengthInRun;
        if (oneBased == trackLength) return CellState::LengthBoundary;
        return CellState::LengthOutRun;
    }

    // =========================================================================
    // CellDecoration — named overlay channel (§35.8.3)
    // =========================================================================
    struct CellDecoration
    {
        CellState token = CellState::Resting;
        uint32_t colour = 0;
        bool present = false;
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
        int index = -1;           // step/section/track index, else -1
        CellState base = CellState::Resting;  // semantic token
        uint32_t baseColour = 0;         // resolved ARGB — fallback for dumb devices
        float level = 1.0f;      // 0..1 brightness (probability dim etc.)
        CellDecoration border{};               // playhead / held / mode-active outline
        CellDecoration dot{};                  // P-Lock presence
        CellDecoration strip{};                // compound-chord / fill marker
        CellDecoration pip{};                  // latch / virtual-hold (MHZ.9.6)

        // --- Screen-text extension (Slice 1+; controllers ignore) ---
        juce::String primary;    // ALWAYS the live function (decision 1)
        juce::String funcHint;   // dim secondary; "" = none (Func-variant or always-on hint)
        juce::String keyHint;    // physical QWERTY legend ("D", "5", "Q" etc.)
        bool pressed = false;   // physical OR mouse press, every modality
        bool disabled = false;   // dead key — base label visibly dimmed
        // 9.10 §19: F/J home-row index-finger anchor (step indices 1 and 4).
        // Present in every layer — pure orientation cue, never encodes state.
        bool homeKey = false;

        // Scope-glow tint (MHZ.1.x, DESIGN §6.6): non-zero ARGB when this cell is
        // *in scope* under a held modifier — i.e. the held scope rebinds it. The
        // screen renders fill+border in this colour, brighter, so the surface shows
        // exactly which keys the scope rewrites. 0 = not in scope (normal tint).
        uint32_t scopeTint = 0;

        // --- Gesture-affordance slots (9.12 / DESIGN §19) ---
        // Populated from the grammar (resolveBinding per gesture); "" = gesture absent.
        // `primaryGesture` indicates which gesture owns the large primary slot.
        // Controllers may display these in a secondary zone; screen shows the 5-slot layout.
        juce::String tapLabel;
        juce::String holdLabel;
        juce::String doubleTapLabel;
        Gesture primaryGesture = Gesture::Tap;
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

    // ReferenceMark — a scope-coloured reference tick on a rotary ring.
    // Normalised position 0..1 along the ring arc (0 = min, 1 = max).
    // Reusable for any layered/delta parameter (swing, morph, etc.).
    struct ReferenceMark
    {
        bool present = false;
        float position = 0.0f;   // normalised 0..1 along the ring
        uint32_t colour = 0;      // ARGB scope colour
        float alpha = 1.0f;   // opacity multiplier
    };

    struct SurfaceSlot
    {
        juce::String label;       // parameter label (empty when slot is out of range)
        juce::String sectionLabel; // owning section name (e.g. "FILTER"); for displays
        juce::String valueText;   // formatted value string (empty when out of range)
        float position = 0.0f; // normalised 0..1 for ring/display
        RingMode ringMode = RingMode::UnipolarFill;
        bool hasOverride = false; // true when a P-Lock is active for this slot
        bool inRange = false; // false when slot index exceeds machine's schema
        std::array<ReferenceMark, 2> marks{};  // scope-coloured reference ticks (0=inner)
    };

    // =========================================================================
    // SurfaceModel — complete per-frame surface description (§35.8.2)
    //
    // Zone arrays indexed by §35.8.2 zones; byButton() provides reverse lookup.
    // ManipulationZone (SurfaceSlot, §35.8.5) is out of scope for MW.5(a).
    // =========================================================================
    struct SurfaceModel
    {
        static constexpr uint32_t kCurrentSchema = 3;
        uint32_t schemaVersion = kCurrentSchema;

        // Modifier cluster: Func/Track/Pattern/Part/Scene/Master/Mute/Fill (indices 0-7).
        std::array<SurfaceCell, 8> modifiers{};

        // Section row: TRIG/SRC/FILTER/AMP/MOD/FX (canonical section index 0-5).
        std::array<SurfaceCell, 6> section{};

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

        // Sub-step phase for the active playhead step: 0.0 (step start) → 1.0 (step end).
        // -1.0 when the clock is stopped or no valid active track. Used by controllers to
        // render a phase-accurate playhead pulse instead of the slow firmware flash.
        float playheadPhase = -1.0f;

        // Contextual banner shown above the step grid when a picker/selector is active.
        // nullptr = no banner. Static string lifetimes (literals or kCanonicalSectionNames).
        const char* gridBanner = nullptr;

        // Per-section page dots for the section bar (screen + controller visible).
        struct PageDots
        {
            uint8_t count = 0;
            uint8_t active = 0;
        };
        std::array<PageDots, IMachine::kMaxSections> pageDots{};

        // Lookup by (ControllerButton, index). Returns nullptr if not found.
        [[nodiscard]] const SurfaceCell* byButton(ControllerButton btn, int idx = -1) const noexcept;
    };

    // =========================================================================
    // MorphViewState — per-slot A/B pole state for the morph step view (5.2).
    // Built by the editor (which holds dormant maps) and passed into
    // buildSurfaceModel so the model faithfully represents dormant values.
    // =========================================================================
    struct MorphViewState
    {
        enum class PoleState : uint8_t
        {
            Dark,
            Dormant,
            Active
        };
        struct SlotState
        {
            PoleState a = PoleState::Dark;
            PoleState b = PoleState::Dark;
        };
        std::array<SlotState, 8> slots{};
    };

    // =========================================================================
    // buildSurfaceModel — pure builder: the single computation (§35.8.1)
    //
    // Produces all cell appearances for one frame from the given state.
    // Both the screen renderer and controller feedback call this; they cannot
    // diverge because they call the same function with the same state.
    // =========================================================================
    SurfaceModel buildSurfaceModel(const UiState& ui,
                                   const EditContext& ec,
                                   const PressTracker* press,
                                   LockstepProcessor& proc,
                                   int activeTrack,
                                   int stepPage,
                                   GridDisplayMode displayMode,
                                   int slotOffset = 0,
                                   float crossfaderValue = 0.5f,
                                   const MorphViewState& morphView = {});
}
