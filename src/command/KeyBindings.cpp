#include "KeyBindings.h"
#include "../state/UiState.h"
#include "../io/EditMode.h"
#include <bit>      // std::popcount (C++20)

namespace lockstep
{
    using CB = ControllerButton;
    using AId = ActionId;
    using SL = SurfaceLayer;
    using CS = CellState;

    // =========================================================================
    // kKeyBindingsData — the canonical per-key binding table.
    //
    // Row ordering within a group: most-specific (highest requiredMods popcount)
    // first for readability. resolveBinding() uses popcount, not order.
    //
    // Sections (5-0): carry ActionId only; labels deferred to ScopedSectionMatrix.
    //
    // Nav combos with Morph+Func: explicit combined rows (popcount 2) override
    // the ambiguous single-mod tie and preserve exact current behaviour.
    // =========================================================================
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
    static const KeyBinding kKeyBindingsData[] = {

        // ── Func modifier (key 1) ───────────────────────────────────────────
        { CB::Func, -1, kModNone, SL::Base, AId::HoldFuncScope, u8"FUNC", u8"", CS::Resting },

        // ── Track modifier (key 2) ──────────────────────────────────────────
        { CB::TrackScope, -1, kModFunc, SL::Base, AId::OpenMachinePicker, u8"KIT", u8"", CS::FuncHeld },
        { CB::TrackScope, -1, kModNone, SL::Base, AId::HoldTrackScope, u8"TRACK", u8"KIT", CS::Resting },

        // ── Phrase scope (key Q) ─────────────────────────────────────────────
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::HoldPhraseScope, u8"PHRASE", u8"", CS::Resting },

        // ── Scene scope (key W) ──────────────────────────────────────────────
        { CB::SceneScope, -1, kModNone, SL::Base, AId::HoldSceneScope, u8"SCENE", u8"", CS::Resting },

        // ── Morph scope (key A) ──────────────────────────────────────────────
        { CB::MorphScope, -1, kModNone, SL::Base, AId::HoldMorphScope, u8"MORPH", u8"", CS::Resting },

        // ── Song scope (key S) ───────────────────────────────────────────────
        { CB::SongScope, -1, kModFunc, SL::Base, AId::FocusGlobal, u8"GLOBAL", u8"", CS::FuncHeld },
        { CB::SongScope, -1, kModNone, SL::Base, AId::HoldSongScope, u8"SONG", u8"GLOBAL", CS::Resting },

        // ── Mute scope (key Z) ───────────────────────────────────────────────
        // Scene+Mute = scene-mute grid view (S-MUTE). Bare Mute = track/fill mute.
        { CB::MuteScope, -1, kModScene, SL::Base, AId::HoldSceneMuteView, u8"S-MUTE", u8"", CS::Resting },
        { CB::MuteScope, -1, kModNone, SL::Base, AId::HoldMuteScope, u8"MUTE", u8"", CS::Resting },
        // ── Mute+step actions (ToggleMute synthetic button, index=trackIdx) ──────
        // Layer reflects the active step-grid layer when each compound fires:
        //   Morph+Mute held → MorphMuteView; any other Mute hold → MuteView.
        { CB::ToggleMute, -1, kModMorph | kModMute, SL::MorphMuteView, AId::FluidMuteToggle, u8"F-MUTE", u8"", CS::MuteMuted },
        { CB::ToggleMute, -1, kModFunc | kModMute, SL::MuteView, AId::SoloToggle, u8"SOLO", u8"", CS::MuteAudible },
        { CB::ToggleMute, -1, kModScene | kModMute, SL::MuteView, AId::SceneMuteToggle, u8"S-MUTE", u8"", CS::MuteMuted },
        { CB::ToggleMute, -1, kModMute, SL::MuteView, AId::GlobalMuteToggle, u8"MUTE", u8"SOLO", CS::MuteMuted },

        // ── Fill scope (key X) ───────────────────────────────────────────────
        { CB::FillScope, -1, kModNone, SL::Base, AId::HoldFillScope, u8"FILL", u8"", CS::Resting },

        // ── TAP (key 3) ──────────────────────────────────────────────────────
        // Func+3 reserved (was MetronomeToggle pre-9.10 — metronome now in TIME band).
        { CB::TapTempo, -1, kModNone, SL::Base, AId::TapTempo, u8"TAP TEMPO", u8"", CS::Resting },

        // ── NavUp / ↑ (key 4) ─────────────────────────────────────────────────
        // Func+Track (popcount 2) = cycle input mode. Explicit row beats any tie.
        // Func+Morph (popcount 2) = LengthDouble — preserves current dispatch order
        // where funcHeld&&!trackHeld shows ×2 even when morphHeld is also true.
        { CB::NavUp, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeUp, u8"↑", u8"", CS::Resting },
        { CB::NavUp, -1, kModFunc | kModMorph, SL::Base, AId::LengthDouble, u8"×2", u8"", CS::Resting },
        { CB::NavUp, -1, kModFunc, SL::Base, AId::LengthDouble, u8"×2", u8"", CS::Resting },
        { CB::NavUp, -1, kModTrack, SL::Base, AId::CycleInputModeUp, u8"↑", u8"", CS::Resting },
        { CB::NavUp, -1, kModMorph, SL::Base, AId::MorphPickPoleA, u8"A", u8"×2", CS::Resting },
        { CB::NavUp, -1, kModNone, SL::Base, AId::NavTrackUp, u8"↑", u8"×2", CS::Resting },

        // ── NavLeft / ← (key E) ───────────────────────────────────────────────
        { CB::NavLeft, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeLeft, u8"←", u8"", CS::Resting },
        { CB::NavLeft, -1, kModFunc, SL::Base, AId::RotateLeft, u8"←ROT", u8"", CS::Resting },
        { CB::NavLeft, -1, kModTrack, SL::Base, AId::CycleInputModeLeft, u8"←", u8"", CS::Resting },
        { CB::NavLeft, -1, kModNone, SL::Base, AId::NavPageLeft, u8"←", u8"←ROT", CS::Resting },

        // ── NavDown / ↓ (key R) ───────────────────────────────────────────────
        // Func+Morph = LengthHalve — same pattern as NavUp: funcHeld wins over morphHeld.
        { CB::NavDown, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeDown, u8"↓", u8"", CS::Resting },
        { CB::NavDown, -1, kModFunc | kModMorph, SL::Base, AId::LengthHalve, u8"÷2", u8"", CS::Resting },
        { CB::NavDown, -1, kModFunc, SL::Base, AId::LengthHalve, u8"÷2", u8"", CS::Resting },
        { CB::NavDown, -1, kModTrack, SL::Base, AId::CycleInputModeDown, u8"↓", u8"", CS::Resting },
        { CB::NavDown, -1, kModMorph, SL::Base, AId::MorphPickPoleB, u8"B", u8"÷2", CS::Resting },
        { CB::NavDown, -1, kModNone, SL::Base, AId::NavOctaveDown, u8"↓", u8"÷2", CS::Resting },

        // ── NavRight / → (key T) ─────────────────────────────────────────────
        { CB::NavRight, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeRight, u8"→", u8"", CS::Resting },
        { CB::NavRight, -1, kModFunc, SL::Base, AId::RotateRight, u8"ROT→", u8"", CS::Resting },
        { CB::NavRight, -1, kModTrack, SL::Base, AId::CycleInputModeRight, u8"→", u8"", CS::Resting },
        { CB::NavRight, -1, kModNone, SL::Base, AId::NavPageRight, u8"→", u8"ROT→", CS::Resting },

        // ── VerbSnapshot / SNAP (key Y) ────────────────────────────────────────────
        // Scene+Y = SYNC (re-sync all tracks to scene floor).
        { CB::VerbSnapshot, -1, kModScene, SL::Base, AId::VerbConfirm, u8"SYNC", u8"", CS::Resting },
        { CB::VerbSnapshot, -1, kModFunc, SL::Base, AId::VerbRestore, u8"RESTORE", u8"", CS::Resting },
        { CB::VerbSnapshot, -1, kModNone, SL::Base, AId::VerbSnapshot, u8"SNAP", u8"RESTORE", CS::Resting },

        // ── VerbRecord / REC (key U) ──────────────────────────────────────────
        // Scope+VerbRecord = COPY for Track/Phrase/Song. Scene bare = BAKE (arms confirm);
        // Func+Scene+Record = COPY (scene copy, handled in verbs::scene).
        // Func+Song+Record = CAPTURE (arm/disarm WAV capture of master output).
        { CB::VerbRecord, -1, kModSong | kModFunc, SL::Base, AId::ToggleCapture, u8"CAPTURE", u8"", CS::FuncHeld },
        { CB::VerbRecord, -1, kModScene | kModFunc, SL::Base, AId::VerbCopy, u8"COPY", u8"", CS::FuncHeld },
        { CB::VerbRecord, -1, kModTrack, SL::Base, AId::VerbCopy, u8"COPY", u8"", CS::Resting },
        { CB::VerbRecord, -1, kModPhrase, SL::Base, AId::VerbCopy, u8"COPY", u8"", CS::Resting },
        { CB::VerbRecord, -1, kModScene, SL::Base, AId::VerbBakeScene, u8"BAKE", u8"COPY", CS::Resting },
        { CB::VerbRecord, -1, kModSong, SL::Base, AId::VerbCopy, u8"COPY", u8"CAPTURE", CS::Resting },
        // Promoted so the tap REC keeps the primary slot over the RESET hold secondary
        // (below): at equal specificity deriveSlots' fallback would otherwise let the
        // hold win, flipping the key's big label to RESET.
        { CB::VerbRecord, -1, kModNone, SL::Base, AId::VerbRecord, u8"REC", u8"", CS::Resting,
          Gesture::Tap, true },

        // ── VerbPlay / PLAY (key I) ───────────────────────────────────────────
        // Scope+VerbPlay = PASTE for Track/Phrase/Song. Scene bare is inert (verbs::scene
        // requires Func); Func+Scene+Play = PASTE.
        { CB::VerbPlay, -1, kModScene | kModFunc, SL::Base, AId::VerbPaste, u8"PASTE", u8"", CS::FuncHeld },
        { CB::VerbPlay, -1, kModTrack, SL::Base, AId::VerbPaste, u8"PASTE", u8"", CS::Resting },
        { CB::VerbPlay, -1, kModPhrase, SL::Base, AId::VerbPaste, u8"PASTE", u8"", CS::Resting },
        { CB::VerbPlay, -1, kModSong, SL::Base, AId::VerbPaste, u8"PASTE", u8"", CS::Resting },
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::VerbPlay, u8"PLAY", u8"", CS::Resting },

        // ── VerbClear / CLEAR (key O) ─────────────────────────────────────────
        // Func+Scope+Clear = DEL for Track/Phrase/Scene (not Song/Morph).
        // Morph+Func+Clear = ERASE (morph erase, not generic delete).
        // Song+Func+Clear is inert — no Song-delete exists; Song row wins the tiebreak.
        { CB::VerbClear, -1, kModPhrase | kModFunc, SL::Base, AId::VerbDelete, u8"DEL", u8"", CS::FuncHeld },
        { CB::VerbClear, -1, kModTrack | kModFunc, SL::Base, AId::VerbDelete, u8"DEL", u8"", CS::FuncHeld },
        { CB::VerbClear, -1, kModScene | kModFunc, SL::Base, AId::VerbDelete, u8"DEL", u8"", CS::FuncHeld },
        { CB::VerbClear, -1, kModMorph | kModFunc, SL::Base, AId::VerbMorphErase, u8"ERASE", u8"", CS::FuncHeld },
        { CB::VerbClear, -1, kModFunc, SL::Base, AId::VerbDelete, u8"DEL", u8"", CS::Resting },
        { CB::VerbClear, -1, kModTrack, SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"DEL", CS::Resting },
        { CB::VerbClear, -1, kModPhrase, SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"DEL", CS::Resting },
        { CB::VerbClear, -1, kModScene, SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"DEL", CS::Resting },
        { CB::VerbClear, -1, kModMorph, SL::Base, AId::VerbMorphBake, u8"BAKE", u8"ERASE", CS::Resting },
        { CB::VerbClear, -1, kModSong, SL::Base, AId::VerbScopedClear, u8"PANIC", u8"", CS::Resting },
        { CB::VerbClear, -1, kModNone, SL::Base, AId::VerbClear, u8"CLEAR", u8"DEL", CS::Resting },

        // ── VerbConfirm / CONFIRM (key P) ─────────────────────────────────────────
        // Scope+P = QUANT (zero microOffset on scope). No hint: holding Func over
        // Track/Phrase+P still resolves to QUANT (scope wins the popcount tiebreak),
        // so there is no distinct Func-overlay to preview.
        { CB::VerbConfirm, -1, kModTrack,  SL::Base, AId::VerbConfirm, u8"QUANT", u8"", CS::Resting },
        { CB::VerbConfirm, -1, kModPhrase, SL::Base, AId::VerbConfirm, u8"QUANT", u8"", CS::Resting },
        { CB::VerbConfirm, -1, kModFunc,   SL::Base, AId::VerbCancel,  u8"CANCEL", u8"", CS::Resting },
        { CB::VerbConfirm, -1, kModNone,   SL::Base, AId::VerbConfirm, u8"CONFIRM", u8"CANCEL", CS::Resting },

        // ── PendingConfirm layer — P key shows live CONFIRM (green) / CANCEL (red) ──
        { CB::VerbConfirm, -1, kModFunc, SL::PendingConfirm, AId::VerbCancel, u8"CANCEL", u8"", CS::ConfirmNo },
        { CB::VerbConfirm, -1, kModNone, SL::PendingConfirm, AId::VerbConfirm, u8"CONFIRM", u8"CANCEL", CS::ConfirmYes },

        // ── Section keys (5-0): ActionId only; labels from ScopedSectionMatrix ──
        { CB::Section, 0, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section, 1, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section, 2, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section, 3, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section, 4, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        // FX (section 5): Func+FX retired as picker entry (9.14 Stage 2 — freed).
        { CB::Section, 5, kModFunc, SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section, 0, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },
        { CB::Section, 1, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },
        { CB::Section, 2, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },
        { CB::Section, 3, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },
        { CB::Section, 4, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },
        { CB::Section, 5, kModNone, SL::Base, AId::SelectSection, u8"", u8"", CS::Resting },

        // FX hold-gesture rows (9.14): tap = navigate, hold = picker. The picker is
        // scope-gated (Track = track inserts, Song = master) so the hold-rail label
        // only appears under the scope where the picker actually fires.
        { CB::Section, 5, kModTrack, SL::Base, AId::OpenTrackFxPicker, u8"PICK FX", u8"", CS::Resting,
          Gesture::Hold, false },
        { CB::Section, 5, kModSong, SL::Base, AId::OpenMasterFxPicker, u8"PICK MASTER FX", u8"", CS::Resting,
          Gesture::Hold, false },

        // ── 9.12: Gesture-axis rows ─────────────────────────────────────────────
        // These rows carry explicit gesture + promoted fields (last two columns).
        // Tap rows already exist above (defaulting to Gesture::Tap, promoted=false).

        // Modifier scope keys are HOLD-to-scope: pressing/holding engages the scope,
        // double-tap latches it. There is NO distinct tap action, so the primary
        // label is the bare scope name (the verbose "X SCOPE"/"X VIEW" wording was
        // redundant with the key's identity and only shrank the font). The access
        // glyph (hold ring) on the primary signals "hold to engage"; deriveSlots
        // suppresses the duplicate tap rail since tap and hold share one action.

        // Func (key 1): hold = FUNC (qualifier layer); dbl-tap = ESCAPE.
        { CB::Func, -1, kModNone, SL::Base, AId::HoldFuncScope, u8"FUNC", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::Func, -1, kModNone, SL::Base, AId::FuncEscape, u8"ESCAPE", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // TrackScope (key 2): hold = TRACK (primary scope); dbl-tap = LATCH.
        { CB::TrackScope, -1, kModNone, SL::Base, AId::HoldTrackScope, u8"TRACK", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::TrackScope, -1, kModNone, SL::Base, AId::LatchTrackScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // PhraseScope (key Q): hold = PHRASE (primary scope); dbl-tap = LATCH.
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::HoldPhraseScope, u8"PHRASE", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::LatchPhraseScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // SceneScope (key W): hold = SCENE (primary scope); dbl-tap = LATCH.
        { CB::SceneScope, -1, kModNone, SL::Base, AId::HoldSceneScope, u8"SCENE", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::SceneScope, -1, kModNone, SL::Base, AId::LatchSceneScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // MorphScope (key A): hold = MORPH (primary scope); dbl-tap = LATCH.
        { CB::MorphScope, -1, kModNone, SL::Base, AId::HoldMorphScope, u8"MORPH", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::MorphScope, -1, kModNone, SL::Base, AId::LatchMorphScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // SongScope (key S): hold = SONG (primary scope); dbl-tap = LATCH.
        { CB::SongScope, -1, kModNone, SL::Base, AId::HoldSongScope, u8"SONG", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::SongScope, -1, kModNone, SL::Base, AId::LatchSongScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // MuteScope (key Z): hold = MUTE (primary scope); dbl-tap = LATCH.
        { CB::MuteScope, -1, kModNone, SL::Base, AId::HoldMuteScope, u8"MUTE", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::MuteScope, -1, kModNone, SL::Base, AId::LatchMuteScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // FillScope (key X): hold = FILL (primary scope); dbl-tap = LATCH.
        { CB::FillScope, -1, kModNone, SL::Base, AId::HoldFillScope, u8"FILL", u8"", CS::Resting,
          Gesture::Hold, true },
        { CB::FillScope, -1, kModNone, SL::Base, AId::LatchFillScope, u8"LATCH", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // TapTempo (key 3): hold = GEN HUB (primary); tap = TAP TEMPO (existing row).
        { CB::TapTempo, -1, kModNone, SL::Base, AId::OpenGeneratorHub, u8"GEN HUB", u8"", CS::Resting,
          Gesture::Hold, true },

        // VerbPlay (key I): layered stop by tap-count (dispatch is editor-owned via
        // GestureRecognizer::playTapCount — these rows are display-only affordances).
        // dbl-tap = track CUT (sends + master ring); triple-tap = MASTER CUT (dead).
        // Rewind is decoupled onto hold-Record (see VerbRecord RESET below).
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::TransportTrackCut, u8"CUT", u8"", CS::Resting,
          Gesture::DoubleTap, false },
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::TransportMasterCut, u8"MASTER CUT", u8"", CS::Resting,
          Gesture::TripleTap, false },

        // VerbRecord (key U): hold = RESET (stop + rewind, re-arm one-shots). Dispatch
        // is editor-owned (long-press on kTransportResetToken); this row is display-only.
        { CB::VerbRecord, -1, kModNone, SL::Base, AId::PlayStopReset, u8"RESET", u8"", CS::Resting,
          Gesture::Hold, false },

        // VerbSnapshot Func+Y (RESTORE): hold = RESTORE → FLOOR.
        { CB::VerbSnapshot, -1, kModFunc, SL::Base, AId::RestoreFloor, u8"→ FLOOR", u8"", CS::Resting,
          Gesture::Hold, false },

        // NavRight (key T): dbl-tap = UNLOCK page navigation.
        { CB::NavRight, -1, kModNone, SL::Base, AId::NavPageUnlock, u8"UNLOCK", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // RecordArm (key 9): tap = REC ARM; dbl-tap = OVERDUB.
        { CB::RecordArm, -1, kModNone, SL::Base, AId::RecordArmToggle, u8"REC ARM", u8"", CS::Resting,
          Gesture::Tap, false },
        { CB::RecordArm, -1, kModNone, SL::Base, AId::RecordArmOverdub, u8"OVERDUB", u8"", CS::Resting,
          Gesture::DoubleTap, false },

        // PlayStop (key 0): tap = PLAY/STOP toggle.
        { CB::PlayStop, -1, kModNone, SL::Base, AId::PlayStopToggle, u8"PLY/STOP", u8"", CS::Resting,
          Gesture::Tap, false },
    };
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays)

    const std::span<const KeyBinding> kKeyBindings{ kKeyBindingsData };

    // =========================================================================
    // heldModsFromUiState
    // =========================================================================

    uint16_t heldModsFromUiState(const UiState& ui) noexcept
    {
        uint16_t m = kModNone;
        if (ui.funcHeld) m |= kModFunc;
        if (ui.trackHeld) m |= kModTrack;
        if (ui.phraseScopeHeld) m |= kModPhrase;
        if (ui.sceneHeld) m |= kModScene;
        if (ui.morphHeld) m |= kModMorph;
        if (ui.songHeld) m |= kModSong;
        if (ui.muteHeld) m |= kModMute;
        if (ui.fillHeld) m |= kModFill;
        return m;
    }

    // =========================================================================
    // resolveBinding
    // =========================================================================

    static const KeyBinding kNoBinding{ CB::None };

    const KeyBinding& resolveBinding(ControllerButton b, int idx,
                                     uint16_t heldMods,
                                     SurfaceLayer layer,
                                     Gesture g) noexcept
    {
        const KeyBinding* best = nullptr;
        int score = -1;

        for (const auto& row : kKeyBindings)
        {
            if (row.button != b) continue;
            if (row.layer != layer) continue;
            if (row.gesture != g) continue;
            // index -1 in the table matches any idx; a specific index must match exactly.
            if (row.index != -1 && row.index != idx) continue;
            // All required modifiers must be held.
            if ((heldMods & row.requiredMods) != row.requiredMods) continue;

            const int s = std::popcount(row.requiredMods);
            if (s > score)
            {
                best = &row;
                score = s;
            }
            else if (s == score && best != nullptr)
            {
                // Tiebreak: first differing bit in kScopePriority order wins.
                // Order (high→low priority): Track, Phrase, Scene, Mute, Morph,
                //   Song, Fill, Func (weakest).
                // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
                static constexpr uint16_t kOrder[] = {
                    kModTrack,
                    kModPhrase,
                    kModScene,
                    kModMute,
                    kModMorph,
                    kModSong,
                    kModFill,
                    kModFunc,
                };
                // NOLINTEND(cppcoreguidelines-avoid-c-arrays)
                const uint16_t diffMask = row.requiredMods ^ best->requiredMods;
                for (const auto bit : kOrder)
                {
                    if ((diffMask & bit) == 0) continue;
                    if ((row.requiredMods & bit) != 0)
                        best = &row;
                    break;
                }
            }
        }

        return best ? *best : kNoBinding;
    }

    // =========================================================================
    // promotedGesture
    // =========================================================================

    Gesture promotedGesture(ControllerButton b, int idx,
                            uint16_t heldMods,
                            SurfaceLayer layer) noexcept
    {
        // Pass 1: explicit promoted override wins.
        for (const auto& row : kKeyBindings)
        {
            if (row.button != b || row.layer != layer) continue;
            if (row.index != -1 && row.index != idx) continue;
            if ((heldMods & row.requiredMods) != row.requiredMods) continue;
            if (row.promoted) return row.gesture;
        }
        // Pass 2: Hold present → promoted to primary.
        for (const auto& row : kKeyBindings)
        {
            if (row.button != b || row.layer != layer) continue;
            if (row.index != -1 && row.index != idx) continue;
            if ((heldMods & row.requiredMods) != row.requiredMods) continue;
            if (row.gesture == Gesture::Hold) return Gesture::Hold;
        }
        return Gesture::Tap;
    }

} // namespace lockstep
