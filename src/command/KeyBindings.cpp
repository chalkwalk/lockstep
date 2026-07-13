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
        { CB::Func, -1, kModNone, SL::Base, AId::HoldFuncScope, u8"FUNC", CS::Resting },

        // ── Track modifier (key 2) ──────────────────────────────────────────
        // Func+Track = the MACHINE scope (9.29 / §13.9): the unkeyed rung inside
        // Track. Track owns identity, Machine owns the sound. It no longer opens the
        // picker — choosing which machine a track runs is a Track-scope act and lives
        // on Track+hold(SRC), with the FX pickers.
        { CB::TrackScope, -1, kModFunc, SL::Base, AId::HoldMachineScope, u8"MACHINE", CS::FuncHeld },
        { CB::TrackScope, -1, kModNone, SL::Base, AId::HoldTrackScope, u8"TRACK", CS::Resting },

        // ── Phrase scope (key Q) ─────────────────────────────────────────────
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::HoldPhraseScope, u8"PHRASE", CS::Resting },

        // ── Scene scope (key W) ──────────────────────────────────────────────
        { CB::SceneScope, -1, kModNone, SL::Base, AId::HoldSceneScope, u8"SCENE", CS::Resting },

        // ── Morph scope (key A) ──────────────────────────────────────────────
        { CB::MorphScope, -1, kModNone, SL::Base, AId::HoldMorphScope, u8"MORPH", CS::Resting },

        // ── Song scope (key S) ───────────────────────────────────────────────
        // Func+Song = the SET scope (9.29 / §13.9): the unkeyed rung above Song.
        // Same behaviour as before (global / master-bus / transport params) — the
        // name is the honest one now that both unkeyed rungs are named.
        { CB::SongScope, -1, kModFunc, SL::Base, AId::FocusGlobal, u8"SET", CS::FuncHeld },
        { CB::SongScope, -1, kModNone, SL::Base, AId::HoldSongScope, u8"SONG", CS::Resting },

        // ── Mute scope (key Z) ───────────────────────────────────────────────
        // Scene+Mute = scene-mute grid view (S-MUTE). Bare Mute = track/fill mute.
        { CB::MuteScope, -1, kModScene, SL::Base, AId::HoldSceneMuteView, u8"S-MUTE", CS::Resting },
        { CB::MuteScope, -1, kModNone, SL::Base, AId::HoldMuteScope, u8"MUTE", CS::Resting },
        // ── Mute+step actions (ToggleMute synthetic button, index=trackIdx) ──────
        // Layer reflects the active step-grid layer when each compound fires:
        //   Morph+Mute held → MorphMuteView; any other Mute hold → MuteView.
        { CB::ToggleMute, -1, kModMorph | kModMute, SL::MorphMuteView, AId::FluidMuteToggle, u8"F-MUTE", CS::MuteMuted },
        { CB::ToggleMute, -1, kModFunc | kModMute, SL::MuteView, AId::SoloToggle, u8"SOLO", CS::MuteAudible },
        { CB::ToggleMute, -1, kModScene | kModMute, SL::MuteView, AId::SceneMuteToggle, u8"S-MUTE", CS::MuteMuted },
        { CB::ToggleMute, -1, kModMute, SL::MuteView, AId::GlobalMuteToggle, u8"MUTE", CS::MuteMuted },

        // ── Fill scope (key X) ───────────────────────────────────────────────
        { CB::FillScope, -1, kModNone, SL::Base, AId::HoldFillScope, u8"FILL", CS::Resting },

        // ── TAP (key 3) ──────────────────────────────────────────────────────
        // Func+3 reserved (was MetronomeToggle pre-9.10 — metronome now in TIME band).
        { CB::TapTempo, -1, kModNone, SL::Base, AId::TapTempo, u8"TAP TEMPO", CS::Resting },

        // ── The nav cluster (4 / E / R / T) ───────────────────────────────────
        // 9.12 st.7c: dispatch now RESOLVES here instead of re-deriving the same
        // priorities in an if-cascade, which is what let three rows drift into
        // lying about what the key does. All three are corrected below:
        //
        //   * Func+Morph said ×2 / ÷2. The cascade checks morphHeld FIRST, so the key
        //     actually picks the A/B pole. Morph wins; the rows now say so.
        //   * bare NavDown said NavOctaveDown. It has always moved the FOCUS TRACK
        //     down. (Octave lives on left/right, and only inside NoteEdit / CHROMATIC
        //     — which is a LAYER, not a modifier, so it is declared as layer rows.)
        //   * Phrase+Nav (transpose, 10.9) had NO row at all: dispatch did it, the
        //     surface never advertised it, and resolving Phrase+↑ through the table
        //     would have landed on the bare row and changed track instead. Declared.
        //
        // Priority within the cluster, top to bottom: Morph > Phrase > Track > Func.
        // Explicit two-mod rows encode it, because popcount alone cannot.

        // ── NavUp / ↑ (key 4) ─────────────────────────────────────────────────
        { CB::NavUp, -1, kModPhrase | kModFunc, SL::Base, AId::TransposeUp, u8"+1", CS::Resting },
        { CB::NavUp, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeUp, u8"↑", CS::Resting },
        { CB::NavUp, -1, kModFunc | kModMorph, SL::Base, AId::MorphPickPoleA, u8"A", CS::Resting },
        { CB::NavUp, -1, kModPhrase, SL::Base, AId::TransposeUp, u8"+OCT", CS::Resting },
        { CB::NavUp, -1, kModFunc, SL::Base, AId::LengthDouble, u8"×2", CS::Resting },
        { CB::NavUp, -1, kModTrack, SL::Base, AId::CycleInputModeUp, u8"↑", CS::Resting },
        { CB::NavUp, -1, kModMorph, SL::Base, AId::MorphPickPoleA, u8"A", CS::Resting },
        { CB::NavUp, -1, kModNone, SL::Base, AId::NavTrackUp, u8"↑", CS::Resting },

        // ── NavLeft / ← (key E) ───────────────────────────────────────────────
        // Track+←/→ is NOT an input-mode cycle: it pages the grid, exactly as bare
        // ←/→ does. The mode cycle is up/down only (README §5.17, and the cascade never
        // had a Track branch here) -- the CycleInputModeLeft/Right rows were the third
        // table lie this migration turned up. Rows removed; the two ActionIds are now
        // vestigial and get swept in Stage 8, when the enum is made exhaustive.
        { CB::NavLeft, -1, kModFunc, SL::Base, AId::RotateLeft, u8"←ROT", CS::Resting },
        { CB::NavLeft, -1, kModNone, SL::Base, AId::NavPageLeft, u8"←", CS::Resting },
        // Octave shift is a LAYER behaviour: inside NoteEdit or CHROMATIC input the
        // nav pair shifts the octave instead of paging/rotating (Func included — the
        // rotate role steps aside). Declaring it as layer rows is what lets the key
        // frame say OCT- while the mode is up, instead of lying about paging.
        { CB::NavLeft, -1, kModFunc, SL::NoteEdit, AId::NavOctaveDown, u8"OCT-", CS::Resting },
        { CB::NavLeft, -1, kModNone, SL::NoteEdit, AId::NavOctaveDown, u8"OCT-", CS::Resting },
        { CB::NavLeft, -1, kModFunc, SL::ChromaticInput, AId::NavOctaveDown, u8"OCT-", CS::Resting },
        { CB::NavLeft, -1, kModNone, SL::ChromaticInput, AId::NavOctaveDown, u8"OCT-", CS::Resting },

        // ── NavDown / ↓ (key R) ───────────────────────────────────────────────
        { CB::NavDown, -1, kModPhrase | kModFunc, SL::Base, AId::TransposeDown, u8"-1", CS::Resting },
        { CB::NavDown, -1, kModFunc | kModTrack, SL::Base, AId::CycleInputModeDown, u8"↓", CS::Resting },
        { CB::NavDown, -1, kModFunc | kModMorph, SL::Base, AId::MorphPickPoleB, u8"B", CS::Resting },
        { CB::NavDown, -1, kModPhrase, SL::Base, AId::TransposeDown, u8"-OCT", CS::Resting },
        { CB::NavDown, -1, kModFunc, SL::Base, AId::LengthHalve, u8"÷2", CS::Resting },
        { CB::NavDown, -1, kModTrack, SL::Base, AId::CycleInputModeDown, u8"↓", CS::Resting },
        { CB::NavDown, -1, kModMorph, SL::Base, AId::MorphPickPoleB, u8"B", CS::Resting },
        { CB::NavDown, -1, kModNone, SL::Base, AId::NavTrackDown, u8"↓", CS::Resting },

        // ── NavRight / → (key T) ─────────────────────────────────────────────
        { CB::NavRight, -1, kModFunc, SL::Base, AId::RotateRight, u8"ROT→", CS::Resting },
        { CB::NavRight, -1, kModNone, SL::Base, AId::NavPageRight, u8"→", CS::Resting },
        { CB::NavRight, -1, kModFunc, SL::NoteEdit, AId::NavOctaveUp, u8"OCT+", CS::Resting },
        { CB::NavRight, -1, kModNone, SL::NoteEdit, AId::NavOctaveUp, u8"OCT+", CS::Resting },
        { CB::NavRight, -1, kModFunc, SL::ChromaticInput, AId::NavOctaveUp, u8"OCT+", CS::Resting },
        { CB::NavRight, -1, kModNone, SL::ChromaticInput, AId::NavOctaveUp, u8"OCT+", CS::Resting },

        // ── VerbSnapshot / SNAP (key Y) ────────────────────────────────────────────
        // Scene+Y = SYNC (re-sync all tracks to scene floor).
        { CB::VerbSnapshot, -1, kModScene, SL::Base, AId::VerbConfirm, u8"SYNC", CS::Resting },
        { CB::VerbSnapshot, -1, kModFunc, SL::Base, AId::VerbRestore, u8"RESTORE", CS::Resting },
        { CB::VerbSnapshot, -1, kModNone, SL::Base, AId::VerbSnapshot, u8"SNAP", CS::Resting },

        // ── VerbRecord / REC (key U) ──────────────────────────────────────────
        // Scope+VerbRecord = COPY for Track/Phrase/Song. Scene bare = BAKE (arms confirm);
        // Func+Scene+Record = COPY (scene copy, handled in verbs::scene).
        // Func+Song+Record = CAPTURE (arm/disarm WAV capture of master output).
        { CB::VerbRecord, -1, kModSong | kModFunc, SL::Base, AId::ToggleCapture, u8"CAPTURE", CS::FuncHeld },
        { CB::VerbRecord, -1, kModScene | kModFunc, SL::Base, AId::VerbCopy, u8"COPY", CS::FuncHeld },
        { CB::VerbRecord, -1, kModTrack, SL::Base, AId::VerbCopy, u8"COPY", CS::Resting },
        { CB::VerbRecord, -1, kModPhrase, SL::Base, AId::VerbCopy, u8"COPY", CS::Resting },
        { CB::VerbRecord, -1, kModScene, SL::Base, AId::VerbBakeScene, u8"BAKE", CS::Resting },
        { CB::VerbRecord, -1, kModSong, SL::Base, AId::VerbCopy, u8"COPY", CS::Resting },
        // Promoted so the tap REC keeps the primary slot over the RESET hold secondary
        // (below): at equal specificity deriveSlots' fallback would otherwise let the
        // hold win, flipping the key's big label to RESET.
        { CB::VerbRecord, -1, kModNone, SL::Base, AId::VerbRecord, u8"REC", CS::Resting,
          Gesture::Tap, true },

        // ── VerbPlay / PLAY (key I) ───────────────────────────────────────────
        // Scope+VerbPlay = PASTE for Track/Phrase/Song. Scene bare is inert (verbs::scene
        // requires Func); Func+Scene+Play = PASTE.
        { CB::VerbPlay, -1, kModScene | kModFunc, SL::Base, AId::VerbPaste, u8"PASTE", CS::FuncHeld },
        { CB::VerbPlay, -1, kModTrack, SL::Base, AId::VerbPaste, u8"PASTE", CS::Resting },
        { CB::VerbPlay, -1, kModPhrase, SL::Base, AId::VerbPaste, u8"PASTE", CS::Resting },
        { CB::VerbPlay, -1, kModSong, SL::Base, AId::VerbPaste, u8"PASTE", CS::Resting },
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::VerbPlay, u8"PLAY", CS::Resting },

        // ── VerbClear / CLEAR (key O) ─────────────────────────────────────────
        // 9.29: DELETE moved off the Func qualifier onto the GESTURE axis. Func+Track
        // is the Machine scope now, so Func+Track+Clear cannot also mean "delete the
        // track" -- and the tap/hold split is the better home anyway: tap = clear the
        // scope's contents (recoverable), HOLD = delete the entity (picker + confirm).
        // The destructive verb costs the deliberate gesture.
        //   Machine (Func+Track) + Clear = INIT the sound (the compound wins on popcount).
        //   Morph+Func+Clear = ERASE (morph erase, not a generic delete).
        //   Song hosts no deletable entity: no hold row, so Song+hold(Clear) is inert.
        { CB::VerbClear, -1, kModTrack | kModFunc, SL::Base, AId::MachineInit, u8"INIT", CS::FuncHeld },
        { CB::VerbClear, -1, kModMorph | kModFunc, SL::Base, AId::VerbMorphErase, u8"ERASE", CS::FuncHeld },
        // Hints are the Func-variant preview (the universal hint rule): Track's Func
        // variant is the Machine scope's INIT; Phrase/Scene have no Func variant any
        // more (their delete moved to the hold rail, which deriveSlots reads from the
        // Gesture::Hold rows below), so their hint is empty.
        // Promoted (like the bare REC tap over its RESET hold): at equal specificity
        // deriveSlots' fallback would let the HOLD row take the big primary slot, so
        // the key would shout DEL TRACK while the finger is resting on a scope. The
        // common act keeps the primary; the destructive one sits on the secondary rail.
        { CB::VerbClear, -1, kModTrack, SL::Base, AId::VerbScopedClear, u8"CLEAR", CS::Resting,
          Gesture::Tap, true },
        { CB::VerbClear, -1, kModPhrase, SL::Base, AId::VerbScopedClear, u8"CLEAR", CS::Resting,
          Gesture::Tap, true },
        { CB::VerbClear, -1, kModScene, SL::Base, AId::VerbScopedClear, u8"CLEAR", CS::Resting,
          Gesture::Tap, true },
        { CB::VerbClear, -1, kModMorph, SL::Base, AId::VerbMorphBake, u8"BAKE", CS::Resting },
        { CB::VerbClear, -1, kModSong, SL::Base, AId::VerbScopedClear, u8"PANIC", CS::Resting },
        { CB::VerbClear, -1, kModNone, SL::Base, AId::VerbClear, u8"CLEAR", CS::Resting },

        // The delete family, on the hold rail (9.29). One row per deletable scope --
        // the entity a scope owns is the entity its hold deletes.
        { CB::VerbClear, -1, kModTrack, SL::Base, AId::VerbDelete, u8"DEL TRACK", CS::Resting,
          Gesture::Hold, false },
        { CB::VerbClear, -1, kModPhrase, SL::Base, AId::VerbDelete, u8"DEL PHRASE", CS::Resting,
          Gesture::Hold, false },
        { CB::VerbClear, -1, kModScene, SL::Base, AId::VerbDelete, u8"DEL SCENE", CS::Resting,
          Gesture::Hold, false },

        // ── VerbConfirm / CONFIRM (key P) ─────────────────────────────────────────
        // Scope+P = QUANT (zero microOffset on the scope's steps). 7e: the rows said
        // AId::VerbConfirm while LABELLED "QUANT" -- the label was the honest half. The
        // action is QuantizeHeld, and the dispatch now agrees with the frame.
        // No hint: Func over Track/Phrase+P still resolves to QUANT (the scope wins the
        // popcount tiebreak), so there is no distinct Func-overlay to preview.
        { CB::VerbConfirm, -1, kModTrack,  SL::Base, AId::QuantizeHeld, u8"QUANT", CS::Resting },
        { CB::VerbConfirm, -1, kModPhrase, SL::Base, AId::QuantizeHeld, u8"QUANT", CS::Resting },
        { CB::VerbConfirm, -1, kModFunc,   SL::Base, AId::VerbCancel,  u8"CANCEL", CS::Resting },
        { CB::VerbConfirm, -1, kModNone,   SL::Base, AId::VerbConfirm, u8"CONFIRM", CS::Resting },
        // A held step is not a modifier, so the step-scoped quantize (bare P while
        // holding step(s) = quantize THOSE steps) could never be a Base row -- which is
        // why it lived as the first branch of an if-cascade and the key frame never
        // advertised it. It is a LAYER: while a step is held the grid is the inspector,
        // and P reads QUANT.
        { CB::VerbConfirm, -1, kModNone, SL::StepInspector, AId::QuantizeHeld, u8"QUANT", CS::Resting },

        // ── PendingConfirm layer — P key shows live CONFIRM (green) / CANCEL (red) ──
        { CB::VerbConfirm, -1, kModFunc, SL::PendingConfirm, AId::VerbCancel, u8"CANCEL", CS::ConfirmNo },
        { CB::VerbConfirm, -1, kModNone, SL::PendingConfirm, AId::VerbConfirm, u8"CONFIRM", CS::ConfirmYes },

        // ── Section keys (5-0): ActionId only; labels from ScopedSectionMatrix ──
        { CB::Section, 0, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        { CB::Section, 1, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        { CB::Section, 2, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        { CB::Section, 3, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        { CB::Section, 4, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        // FX (section 5): Func+FX retired as picker entry (9.14 Stage 2 — freed).
        { CB::Section, 5, kModFunc, SL::Base, AId::SelectMetaSection, u8"", CS::Resting },
        { CB::Section, 0, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },
        { CB::Section, 1, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },
        { CB::Section, 2, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },
        { CB::Section, 3, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },
        { CB::Section, 4, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },
        { CB::Section, 5, kModNone, SL::Base, AId::SelectSection, u8"", CS::Resting },

        // Picker hold-gesture rows (9.14, extended by 9.29): tap = navigate, hold =
        // choose what fills that section, at the held scope. Scope-gated (Track = this
        // track's machine / its inserts, Song = master) so the hold-rail label only
        // appears under the scope where the picker actually fires. Bare hold(SRC) is
        // NOT this: it opens an OnDemand machine console (consoleSectionIndex defaults
        // to SRC) — the scope gate is what keeps the two apart.
        { CB::Section, 1, kModTrack, SL::Base, AId::OpenMachinePicker, u8"PICK MACHINE", CS::Resting,
          Gesture::Hold, false },
        { CB::Section, 5, kModTrack, SL::Base, AId::OpenTrackFxPicker, u8"PICK FX", CS::Resting,
          Gesture::Hold, false },
        { CB::Section, 5, kModSong, SL::Base, AId::OpenMasterFxPicker, u8"PICK MASTER FX", CS::Resting,
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
        { CB::Func, -1, kModNone, SL::Base, AId::HoldFuncScope, u8"FUNC", CS::Resting,
          Gesture::Hold, true },
        { CB::Func, -1, kModNone, SL::Base, AId::FuncEscape, u8"ESCAPE", CS::Resting,
          Gesture::DoubleTap, false },

        // TrackScope (key 2): hold = TRACK (primary scope); dbl-tap = LATCH.
        { CB::TrackScope, -1, kModNone, SL::Base, AId::HoldTrackScope, u8"TRACK", CS::Resting,
          Gesture::Hold, true },
        { CB::TrackScope, -1, kModNone, SL::Base, AId::LatchTrackScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // PhraseScope (key Q): hold = PHRASE (primary scope); dbl-tap = LATCH.
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::HoldPhraseScope, u8"PHRASE", CS::Resting,
          Gesture::Hold, true },
        { CB::PhraseScope, -1, kModNone, SL::Base, AId::LatchPhraseScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // SceneScope (key W): hold = SCENE (primary scope); dbl-tap = LATCH.
        { CB::SceneScope, -1, kModNone, SL::Base, AId::HoldSceneScope, u8"SCENE", CS::Resting,
          Gesture::Hold, true },
        { CB::SceneScope, -1, kModNone, SL::Base, AId::LatchSceneScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // MorphScope (key A): hold = MORPH (primary scope); dbl-tap = LATCH.
        { CB::MorphScope, -1, kModNone, SL::Base, AId::HoldMorphScope, u8"MORPH", CS::Resting,
          Gesture::Hold, true },
        { CB::MorphScope, -1, kModNone, SL::Base, AId::LatchMorphScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // SongScope (key S): hold = SONG (primary scope); dbl-tap = LATCH.
        { CB::SongScope, -1, kModNone, SL::Base, AId::HoldSongScope, u8"SONG", CS::Resting,
          Gesture::Hold, true },
        { CB::SongScope, -1, kModNone, SL::Base, AId::LatchSongScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // MuteScope (key Z): hold = MUTE (primary scope); dbl-tap = LATCH.
        { CB::MuteScope, -1, kModNone, SL::Base, AId::HoldMuteScope, u8"MUTE", CS::Resting,
          Gesture::Hold, true },
        { CB::MuteScope, -1, kModNone, SL::Base, AId::LatchMuteScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // FillScope (key X): hold = FILL (primary scope); dbl-tap = LATCH.
        { CB::FillScope, -1, kModNone, SL::Base, AId::HoldFillScope, u8"FILL", CS::Resting,
          Gesture::Hold, true },
        { CB::FillScope, -1, kModNone, SL::Base, AId::LatchFillScope, u8"LATCH", CS::Resting,
          Gesture::DoubleTap, false },

        // TapTempo (key 3): hold = GEN HUB (primary); tap = TAP TEMPO (existing row).
        { CB::TapTempo, -1, kModNone, SL::Base, AId::OpenGeneratorHub, u8"GEN HUB", CS::Resting,
          Gesture::Hold, true },

        // VerbPlay (key I): layered stop by tap-count (dispatch is editor-owned via
        // GestureRecognizer::playTapCount — these rows are display-only affordances).
        // dbl-tap = track CUT (sends + master ring); triple-tap = MASTER CUT (dead).
        // Rewind is decoupled onto hold-Record (see VerbRecord RESET below).
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::TransportTrackCut, u8"CUT", CS::Resting,
          Gesture::DoubleTap, false },
        { CB::VerbPlay, -1, kModNone, SL::Base, AId::TransportMasterCut, u8"MASTER CUT", CS::Resting,
          Gesture::TripleTap, false },

        // VerbRecord (key U): hold = RESET (stop + rewind, re-arm one-shots). Dispatch
        // is editor-owned (long-press on kTransportResetToken); this row is display-only.
        { CB::VerbRecord, -1, kModNone, SL::Base, AId::PlayStopReset, u8"RESET", CS::Resting,
          Gesture::Hold, false },

        // VerbSnapshot Func+Y (RESTORE): hold = RESTORE → FLOOR.
        { CB::VerbSnapshot, -1, kModFunc, SL::Base, AId::RestoreFloor, u8"→ FLOOR", CS::Resting,
          Gesture::Hold, false },

        // NavRight (key T): dbl-tap = UNLOCK page navigation.
        { CB::NavRight, -1, kModNone, SL::Base, AId::NavPageUnlock, u8"UNLOCK", CS::Resting,
          Gesture::DoubleTap, false },

        // RecordArm (key 9): tap = REC ARM; dbl-tap = OVERDUB.
        { CB::RecordArm, -1, kModNone, SL::Base, AId::RecordArmToggle, u8"REC ARM", CS::Resting,
          Gesture::Tap, false },
        { CB::RecordArm, -1, kModNone, SL::Base, AId::RecordArmOverdub, u8"OVERDUB", CS::Resting,
          Gesture::DoubleTap, false },

        // PlayStop (key 0): tap = PLAY/STOP toggle.
        { CB::PlayStop, -1, kModNone, SL::Base, AId::PlayStopToggle, u8"PLY/STOP", CS::Resting,
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

    // 9.12 Stage 8 — the hint is DERIVED, not stored.
    //
    // Every key's secondary label was, by rule, "what this key becomes with Func also
    // held" (the universal hint rule, pinned by KeyBindingTest since 9.11). Storing it
    // in a column meant the same fact was written twice: once as the Func row's primary,
    // once as the bare row's hint — and a row could contradict its own Func variant with
    // nothing to catch it. Now there is one fact, and the hint reads it.
    const char8_t* hintFor(ControllerButton b, int idx, uint16_t heldMods,
                           SurfaceLayer layer, Gesture g) noexcept
    {
        const auto& bare = resolveBinding(b, idx, heldMods, layer, g);
        const auto& func = resolveBinding(b, idx, heldMods | kModFunc, layer, g);
        // No Func variant, or Func changes nothing here: there is no secondary to show.
        if (func.action == ActionId::None || func.action == bare.action)
            return u8"";
        return func.primary;
    }

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
