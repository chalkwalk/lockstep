#pragma once

#include <array>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../io/ControllerEvent.h"
#include "../core/Sequence.h"   // kNumTracks
#include "../machine/IMachine.h"  // kMaxSections
#include "GridDisplayMode.h"
#include "../command/Gesture.h"
#include "../command/SurfaceLayer.h"

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
        // 5.6 trigless / lock-only step (rides P-Locks onto a sustaining voice).
        // Add-only value past the existing block (never renumber tokens).
        StepLockOnly = 165,

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
        NoteEditResting = 63,  // no note on this semitone, out of the active key
        NoteEditScaleNote = 64,  // no note, but the semitone is in the active key
        NoteEditScaleRoot = 65,  // no note; the tonic of the active key (emphasis)
        NoteEditActiveOff = 66,  // a chord note that is OUT of the active key (flag)

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
        EffectLoadedBypassed = 143,  // loaded in the focused slot but bypassed (re-picked to toggle off)

        // Confirm overlay (8.24): P key in PendingConfirm layer shows CONFIRM/CANCEL.
        ConfirmYes = 150,  // P without Func — green affirm state
        ConfirmNo = 151,  // P with Func held — red cancel state

        // Generator hub (9.10): momentary picker for the deterministic generator family.
        // Each token is a distinct hue on hardware (PRINCIPLES §19 dual-target).
        GeneratorEuclid = 160,
        GeneratorDensity = 161,
        GeneratorVel = 162,
        GeneratorMelodic = 163,  // melodic generator (10.7)
        GeneratorHarmonic = 164,  // harmonic voice-mover (10.8)

        // KEY panel (DESIGN §4.10): the step grid hosts the modifier checkboxes
        // (4 states) and the two symmetric-scale radio cells. Brightness = whether
        // the modifier applies in the current tonality; mark = whether it is set.
        KeyModActive = 170,   // set AND applies (bright, marked)
        KeyModAvailable = 171,  // not set but would apply (bright, unmarked)
        KeyModDormant = 172,  // set but does not apply here (grey, marked)
        KeyModUnavail = 173,  // not set and would not apply (grey, unmarked)
        KeySymOn = 174,  // a symmetric scale, selected
        KeySymOff = 175,  // a symmetric scale, available

        // Loop verb cells (Track+Record/Play/Clear on a focused looper, #3). Each
        // is a distinct hue so the looper controls read as their own family on
        // screen and on a controller (PRINCIPLES §19 dual-target), instead of riding
        // the generic scope glow. State-keyed by LockstepProcessor::looperState().
        LooperRecReady = 180,   // REC — idle/stopped, ready to record (dim red)
        LooperRecArmed = 181,   // ARM — quantized record waiting for the bar (bright amber)
        LooperRecActive = 182,  // END — recording now (bright red)
        LooperOverdub = 183,    // DUB — overdubbing (amber)
        LooperPlayReady = 184,  // PLAY — stopped/idle, ready to play (dim green)
        LooperPlaying = 185,    // STOP — playing/overdubbing (bright green)
        LooperErase = 186,      // ERASE — clear the loop (muted red)

        // Loop-phase view on the step grid (#26): a focused looper repurposes the
        // step grid as a quantize-aware position bar (cell count = beats/steps).
        LooperPhaseSeg = 187,   // a loop segment (not the playhead) — dim teal
        LooperPhaseHead = 188,  // the segment the playhead is in — bright cyan
        LooperPhaseStart = 189, // the loop-start / downbeat anchor segment

        // Loop console (S3): the always-on transport + performance grid for a
        // focused looper (the step grid is free real estate — a looper does not
        // sequence). Add-only; each is a distinct hue (PRINCIPLES §19 dual-target).
        // The *Active variants light when that function is engaged this block.
        LooperConRec = 190,        // REC — start / punch-out record
        LooperConRecActive = 191,  // recording now (bright red)
        LooperConArm = 192,        // armed — quantized record waiting for the bar
        LooperConPlay = 193,       // PLAY
        LooperConPlayActive = 194, // playing (bright green)
        LooperConStop = 195,       // STOP
        LooperConErase = 196,      // ERASE — clear the loop
        LooperConUndo = 197,       // UNDO — undo the last overdub
        LooperConDub = 198,        // DUB — explicit overdub toggle
        LooperConDubActive = 199,  // overdubbing (amber)
        LooperConHalf = 200,       // HALF — halve the loop window (S4)
        LooperConDouble = 201,     // DBL — double the loop window (S4)
        LooperConRpt = 202,        // beat-repeat rate cell, idle (S5)
        LooperConRptActive = 203,  // beat-repeat held (S5)
        LooperConTape = 204,       // tape-FX cell, idle (S6)
        LooperConTapeActive = 205, // tape-FX held (S6)
        LooperConIdle = 206,       // a console cell with no live function right now

        // ---- Route routing-matrix console (7c) — one cell per track ----------
        RouteConOff = 210,         // track routed to Off (silent)
        RouteConMaster = 211,      // track routed to the Master sum
        RouteConBus = 212,         // track routed into another track's bus input
        RouteConChanged = 213,     // staged edit differs from the committed dest

        // ---- quantized mute pending (9.17) — armed, not yet fired ------------
        MutePendingMute = 214,     // a track armed to mute at the launch boundary
        MutePendingUnmute = 215,   // a track armed to unmute at the launch boundary

        // ---- deck TRACKS console page (11.8, §40.3/§40.5) — 4 sub-tracks × 4 ----
        DeckTrkArm = 216,          // ARM cell, sub-track not armed
        DeckTrkArmOn = 217,        // armed (punch/overdub target)
        DeckTrkMute = 218,         // MUTE cell, sub-track audible
        DeckTrkMuteOn = 219,       // muted
        DeckTrkSolo = 220,         // SOLO cell, not soloed
        DeckTrkSoloOn = 221,       // soloed
        DeckTrkSrc = 222,          // SRC cell (opens the source picker), shows label
        DeckTrkEmpty = 223,        // a row past the sub-track count (dim, inert)

        // ---- Tape console (11.4, §40.5) — transport + markers -----------------
        TapeConRec = 224,          // REC / punch, ready
        TapeConRecActive = 225,    // punched in (recording)
        TapeConPlay = 226,         // PLAY (lit while playing)
        TapeConStop = 227,         // STOP (lit while stopped)
        TapeConClear = 228,        // CLEAR the reel
        TapeConUndo = 229,         // UNDO the last punch
        TapeConDrop = 230,         // drop a marker here
        TapeConCue = 231,          // cue (locate) to a marker
        TapeConIdle = 232,         // an inert console cell
        TapeConRew = 233,          // << hold-to-wind rewind (standalone only, §40.2)
        TapeConFwd = 234,          // >> hold-to-wind fast-forward (standalone only)

        // ---- Identity naming/colour overlay (5.3 / §23.4) --------------------
        // The two step rows show full composed name candidates (live preview);
        // the colour page shows palette swatches. *Sel = the currently chosen cell.
        NameCandidate = 235,       // a composable name candidate (top or bottom row)
        NameCandidateSel = 236,    // the selected half in its row (bright)
        PaletteSwatch = 237,       // a colour-page swatch (its own hue via cell colour)
        PaletteSwatchSel = 238,    // the selected swatch (ringed)
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

        // 9.4 item G: checkpoint MARK count for a scope key (DESIGN §13.6). > 0 only on
        // the four scope-cluster keys when that scope's mark stack is non-empty; the pip
        // channel above carries the presence, this carries the number the painter draws.
        // Undo entries are not marks and are not counted here. Non-frozen extension —
        // appended after the §35.8.3 controller-bound prefix, so controllers ignore it.
        int markDepth = 0;

        // 6.4 cue indicator (DESIGN §31). Non-frozen extension (controllers ignore
        // it). `cued` = this cell's track has cue balance > 0 (routed to the
        // headphones); `cuePending` = a quantized cue flip is armed but not yet
        // fired. Set by buildSurfaceModel on the cue-console flip cells and any
        // per-track cell that wants to show cue state; painted by KeyButton.
        bool cued = false;
        bool cuePending = false;

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
        // Triple-tap secondary (layered stop: Play triple = MASTER CUT). When present,
        // the key surrenders its unused single-tap slot to show both cut depths without
        // shifting the primary row (KeyButton.cpp preserves the §19 primary-lock).
        juce::String tripleTapLabel;
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

        // Dynamic banner for Labeled overlays whose banner carries live content
        // (e.g. the Identity overlay's composed name + current mode). When non-empty
        // it overrides gridBanner in the generic labeled-grid renderer, so the banner
        // text is model-owned (controller-visible) rather than painter-derived.
        juce::String stepBanner;

        // The resolved active layer (SSOT: resolveActiveLayer). Hoisted onto the model so
        // step-grid renderers can key off it without re-deriving precedence — e.g. the
        // LooperConsole layer needs a dedicated label-drawing branch in paintStepRows.
        SurfaceLayer activeLayer = SurfaceLayer::Base;

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
