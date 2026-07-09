#pragma once
// KeyBindings.h — unified per-key binding table for Lockstep (DESIGN §13).
// Drives both key-cell rendering and dispatch (via ActionId).
//
// Resolution rule: among rows with matching (button, index, layer) where
// requiredMods ⊆ heldMods, the row with the highest popcount(requiredMods)
// wins. Ties are broken by kScopePriority order (Func = weakest). This
// "most-specific wins" rule replaces per-key forbidden-modifier checks.

#include <array>
#include <cstdint>
#include <span>
#include "../io/ControllerEvent.h"
#include "SurfaceLayer.h"
#include "Gesture.h"
#include "../ui/SurfaceModel.h"  // CellState enum

namespace lockstep
{
    struct UiState;

    // -------------------------------------------------------------------------
    // ActionId — one value per distinct action reachable from a key.
    // Named after the dispatch block it represents. A4 will wire these
    // to the actual dispatch handlers.
    // -------------------------------------------------------------------------
    enum class ActionId : uint16_t
    {
        None,
        // Scope-modifier holds (down = enter scope; up = exit scope)
        HoldFuncScope,
        HoldTrackScope,
        HoldPhraseScope,
        HoldSceneScope,
        HoldMorphScope,
        HoldSongScope,
        HoldMuteScope,
        HoldFillScope,
        HoldSceneMuteView,   // Scene+Mute combo: enters scene-mute grid view
        // Overlay pickers (step grid re-skin)
        OpenMachinePicker,
        OpenTrackFxPicker,
        OpenMasterFxPicker,
        FocusGlobal,         // Func+Song: focus global/master-bus parameters
        // Tap / metronome
        TapTempo,
        MetronomeToggle,
        // Navigation
        NavTrackUp,          // nav-up key bare: focus next track up
        NavTrackDown,        // nav-down key bare: navigate down
        NavPageLeft,
        NavPageRight,
        NavOctaveUp,
        NavOctaveDown,
        // Length / rotate operations (Func-qualified nav)
        LengthDouble,        // Func+NavUp
        LengthHalve,         // Func+NavDown
        RotateLeft,          // Func+NavLeft
        RotateRight,         // Func+NavRight
        // Track-held nav (cycles track input mode)
        CycleInputModeUp,
        CycleInputModeDown,
        CycleInputModeLeft,
        CycleInputModeRight,
        // Morph pole picks (NavUp=A, NavDown=B while Morph held)
        MorphPickPoleA,
        MorphPickPoleB,
        // Section / meta-section selects (index carries section index)
        SelectSection,
        SelectMetaSection,
        // Primary verbs
        VerbSnapshot,
        VerbRestore,         // Func+Snapshot
        VerbRecord,
        VerbPlay,
        VerbClear,
        VerbDelete,          // Func+Clear
        VerbConfirm,         // P / YES
        VerbCancel,          // Func+P / NO
        // Scope-qualified verb relabels (same key, different action meaning)
        VerbCopy,            // VerbRecord while any section-suite scope held (not Morph)
        VerbPaste,           // VerbPlay while any section-suite scope held (not Morph)
        VerbScopedClear,     // VerbClear while any section-suite scope held (incl. Morph)
        VerbBakeScene,       // Scene+VerbRecord (bare): bake live deviations → confirm
        VerbMorphBake,       // Morph+VerbClear: bake morph state
        VerbMorphErase,      // Func+Morph+VerbClear: erase morph state
        // Mute/solo cluster (index = track)
        GlobalMuteToggle,    // Mute+step: immediate global mute
        SoloToggle,          // Func+Mute+step: solo
        SceneMuteToggle,     // Scene+Mute+step: toggle per-scene active-mask
        FluidMuteToggle,     // Morph+Mute+step: toggle fluid-mute morph
        // Misc
        QuantizeHeld,
        // Capture
        ToggleCapture,       // Func+Song+Record: arm/disarm WAV capture

        // 9.12: Gesture-axis actions (appended; do not reorder above values)
        FuncEscape,          // Func double-tap: escape active overlay
        LatchTrackScope,     // TrackScope double-tap: latch scope on
        LatchPhraseScope,    // PhraseScope double-tap
        LatchSceneScope,     // SceneScope double-tap
        LatchMorphScope,     // MorphScope double-tap
        LatchSongScope,      // SongScope double-tap
        LatchMuteScope,      // MuteScope double-tap
        LatchFillScope,      // FillScope double-tap
        PlayStopReset,       // VerbRecord hold: reset (stop + rewind) — display row
        TransportTrackCut,   // VerbPlay double-tap: track cut (sends+master ring) — display row
        TransportMasterCut,  // VerbPlay triple-tap: master cut (dead) — display row
        RecordArmToggle,     // RecordArm tap: arm / disarm record
        RecordArmOverdub,    // RecordArm double-tap: enable overdub
        RestoreFloor,        // Func+VerbSnapshot hold: restore to floor
        NavPageUnlock,       // NavRight double-tap: unlock page navigation
        StepLatch,           // Step double-tap: latch step hold
        OpenGeneratorHub,    // TapTempo hold: open generator hub picker
        OpenRetrigPicker,    // step-grid: open retrig/ratchet picker
        OpenSoundPool,       // step-grid: open sound-pool picker
        PlayStopToggle,      // PlayStop (key 0) tap: play/stop toggle
    };

    // -------------------------------------------------------------------------
    // Modifier bitmask constants. Combine with | for requiredMods.
    // Bit assignment follows kScopePriority order (high-priority = higher bits)
    // EXCEPT Func which is bit 0 (weakest, for tiebreaking).
    // -------------------------------------------------------------------------
    enum ModBit : uint16_t
    {
        kModNone = 0,
        kModFunc = 1 << 0,   // Func — always the weakest tiebreaker
        kModTrack = 1 << 1,
        kModPhrase = 1 << 2,
        kModScene = 1 << 3,
        kModMorph = 1 << 4,
        kModSong = 1 << 5,
        kModMute = 1 << 6,
        kModFill = 1 << 7,
    };

    // -------------------------------------------------------------------------
    // KeyBinding — one row in kKeyBindings.
    // -------------------------------------------------------------------------
    struct KeyBinding
    {
        ControllerButton button;
        int index = -1;      // step/section index; -1 = any
        uint16_t requiredMods = kModNone;
        SurfaceLayer layer = SurfaceLayer::Base;
        ActionId action = ActionId::None;
        const char8_t* primary = u8"";    // main label (UTF-8, ≤8 visible chars)
        const char8_t* hint = u8"";    // Func-hint / bottom strip ("" = none)
        CellState state = CellState::Resting;
        // 9.12: gesture axis — appended at end so positional-init rows are unchanged.
        Gesture gesture = Gesture::Tap;
        bool promoted = false;   // explicit primary-promotion override
    };

    // The canonical table. Most-specific rows (higher requiredMods popcount)
    // should be listed first for readability; resolveBinding() uses popcount,
    // not table order.
    extern const std::span<const KeyBinding> kKeyBindings;

    // Derive the held-modifier bitmask from UiState.
    uint16_t heldModsFromUiState(const UiState& ui) noexcept;

    // Resolve the best-matching row for (button, index, layer, heldMods, gesture).
    // Returns a row with ActionId::None if nothing matches.
    // The gesture parameter defaults to Tap so all existing callers are unchanged.
    const KeyBinding& resolveBinding(ControllerButton b, int idx,
                                     uint16_t heldMods,
                                     SurfaceLayer layer,
                                     Gesture g = Gesture::Tap) noexcept;

    // Return which gesture is promoted to the primary display slot for a key.
    // Priority: explicit promoted row > any Hold row > Tap.
    Gesture promotedGesture(ControllerButton b, int idx,
                            uint16_t heldMods,
                            SurfaceLayer layer) noexcept;
}
