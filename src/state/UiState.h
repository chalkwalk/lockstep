#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include "../core/Sequence.h"        // kNumTracks
#include "../core/TrackInputMode.h"
#include "../core/HarmonyGen.h"      // HarmonyProgression (harmony overlay buffer)
#include "../io/TrigGridMode.h"
#include "../machine/IMachine.h"    // kMaxSections

namespace lockstep
{
    // ── Overlay ──────────────────────────────────────────────────────────────
    // The mutually-exclusive sticky/modal overlay family.
    // Exactly one value is active at a time: illegal co-existence is
    // unrepresentable.  UiState::overlay is the SSOT; activeOverlay() in
    // ModeReducer reads euclidHeld first (transient chord), then this field.
    enum class Overlay : uint8_t
    {
        None,     // no sticky overlay active
        Euclid,   // Euclidean generator (Phrase+Fill chord — stored in euclidHeld, not here)
        Melodic,  // melodic generator (generator hub cell 3 — stored in melodicHeld, not here)
        Harmony,  // harmonic voice-mover (generator hub cell 4 — stored in harmonyHeld, not here)
        Time,     // tempo + time-sig (Song/Scene+TRIG entry chord)
        Density,  // density editor (Func+MOD entry chord)
        Vel,      // velocity overlay (Func+AMP entry chord)
        SampleProps,  // pool sample-properties editor (Props… button on a pool row, 9.23)
    };

    // ── Pending-confirm state ─────────────────────────────────────────────────
    // Captured at arm time so Yes-resolution is correct even if scope is released.
    enum class ConfirmKind : uint8_t
    {
        None,
        DeleteTrack,          // target = track index
        DeletePhrase,         // target = -1 (Stage 7 adds slot-specific deletion)
        DeleteScene,          // target = scene index at arm time
        BakeScene,            // target = unused
        CreateScene,          // target = dest phrase slot
        CreateBaselineScene,  // target = dest phrase slot
        PasteScene,           // target = dest phrase slot
        ClearTrack,           // target = track index — blanks steps in current phrase only
        ClearTrackAll,        // target = track index — blanks steps in every phrase
        ClearPhrase,          // target = -1 — blanks all tracks in current phrase
    };

    struct ConfirmState
    {
        ConfirmKind kind = ConfirmKind::None;
        int target = -1;
        [[nodiscard]] bool pending() const noexcept { return kind != ConfirmKind::None; }
        void reset() noexcept
        {
            kind = ConfirmKind::None;
            target = -1;
        }
    };

    // ── Delete picker scope ───────────────────────────────────────────────────
    // Which entity class the deletion picker is currently browsing.
    enum class DeleteScope : uint8_t
    {
        None,
        Track,
        Phrase,
        Scene
    };

    struct DeletePickerState
    {
        DeleteScope scope = DeleteScope::None;
        [[nodiscard]] bool active() const noexcept { return scope != DeleteScope::None; }
        void reset() noexcept { scope = DeleteScope::None; }
    };

    // Virtual-hold (latch) state — one bool per latchable modifier.
    // Func never latches. Each bool, when true, means that modifier is held
    // hands-free; its corresponding xxxHeld flag in UiState stays true even
    // while the physical key is up. Column exclusivity is enforced at set time:
    // at most one latch per column ({phrase,morph,mute} vs {track,scene,song,fill}).
    struct LatchState
    {
        // Col 1 (Func is not latchable):
        bool phrase = false;
        bool morph = false;
        bool mute = false;
        // Col 2:
        bool track = false;
        bool scene = false;
        bool song = false;
        bool fill = false;

        [[nodiscard]] bool any() const noexcept
        {
            return phrase || morph || mute || track || scene || song || fill;
        }

        // (Step latches live in EditContext; query hasAnyLatchedStep() directly —
        // there is no mirror here. A former `anySteps` bool was write-only dead state.)
    };

    // UI-local selection state. Not persisted. Not accessed from the audio thread.
    struct UiState
    {
        // Focused track (0 .. kNumTracks-1). Authoritative source of truth;
        // KeyboardArea delegates getActiveTrack()/setActiveTrack() to this field.
        int activeTrack = 0;

        // Per-track active section (0 .. kMaxSections-1).
        std::array<int, kNumTracks> trackSection{};

        // Per-track, per-section active page index (0 .. pageCount-1).
        std::array<std::array<int, IMachine::kMaxSections>, kNumTracks> trackPage{};

        // P6: which scope resolved the page currently stored in trackPage — true if
        // the last press of this section key was Track-scoped. A scope change on the
        // same key resets the page (the two views have different page lists).
        std::array<std::array<bool, IMachine::kMaxSections>, kNumTracks> trackPageTrackScope{};

        // Modifier key states (updated by PluginEditor key events).
        // Cluster:
        //   Col 1 (1/Q/A/Z): Func / Phrase / Morph / Mute.
        //   Col 2 (2/W/S/X): Track / Scene / Song / Fill.
        // Col 1:
        bool funcHeld = false;  // key 1
        bool phraseScopeHeld = false;  // key Q (MHY: moved from A)
        bool phraseScopeUsed = false;  // true if a step was pressed while PhraseScope held
        bool morphHeld = false;  // key A (MHY: moved from S)
        int morphNavQualifier = 0;    // 0=none 1=A-pole(^) 2=B-pole(v); held while Morph active
        bool muteHeld = false;  // key Z
        // 9.17: transient Mute+Play chord — arms the per-track relaunch/retrigger
        // view (Mute+Play+step). Set while Play is held under Mute; suppresses the
        // normal Play transport toggle. Precedent: euclidHeld.
        bool relaunchHeld = false;
        // Col 2:
        bool trackHeld = false;  // key 2 (MHY: moved from Q)
        bool sceneHeld = false;  // key W (MHY new — §4.7)
        bool songHeld = false;  // key S (MHY: moved from X)
        bool fillHeld = false;  // key X (MHY: moved from 2)
        // Cue is reserved (MU); no key bound post-MHY.
        bool cueHeld = false;

        // MHZ.9.1: virtual-hold state. Kept in sync with xxxHeld (effective = physical OR latched).
        LatchState latch;

        // Active master section (-1 = none).
        int masterSection = -1;

        // Set true by any non-swing interaction while a swing scope is held (C3).
        // Cleared on scope down/up so the swing default returns with the next hold.
        bool swingDismissed = false;

        // True whenever at least one step key is held (heldStepKeys_ non-empty).
        // Set by PluginEditor so KeyboardArea can show COP/PST/CLR on verb keys.
        bool stepHeld = false;

        // MHZ.3.4: step-driven P-Lock clear mode. Entered when Func+step is pressed
        // (no other step held). Step cells re-skin to show only the target step's
        // P-locked slots, packed into the first N cells; pressing a cell stages that
        // slot for removal. Pressing a staged cell cancels the removal. Clears are
        // committed permanently when Func is released.
        bool pLockClearMode = false;
        int pLockClearTrack = -1;
        int pLockClearStep = -1;
        std::set<int> pLockClearStaged;  // slot indices pending permanent removal

        // 9.14: true while a held step is being moved/micro-nudged (hold step + ←/→
        // or Func+←/→). Drops the grid out of the StepInspector re-skin back to the
        // sequencer view (so the moved step is visible) and flips the MZ to the
        // Step-Position panel. The moved step's current slot is pLockClearStep.
        // Cleared on step release via resetPLockClear().
        bool stepMoveActive = false;
        // Home slot of the moved step for the current hold session. Moves are
        // swap-with-destination relative to this anchor (restore-then-swap), so
        // cells the step passes stay put and nothing is lost. Set on step-hold,
        // cleared by resetPLockClear.
        int stepMoveAnchor = -1;

        // MHZ.3.5: true while Func+Part are both held (machine picker mode).
        // Step cells re-skin to show available machine names; pressing a cell assigns
        // the machine for the active track.
        bool funcTrackHeld = false;

        // Note-edit mode: Func+Src(NOTE)+step gesture. Step cells become a 1-octave
        // chromatic keyboard; pressing a cell toggles a pitch on the target steps.
        // Entered when a step is released while Func+Section(1/SRC) are still held.
        bool funcSrcHeld = false;  // true while Func + Section(1/SRC) are both held
        bool noteEditMode = false;
        int noteEditOctave = 3;  // current view octave (C3 = MIDI 48, C4 = MIDI 60)
        std::set<int> noteEditSteps;  // step indices currently being edited
        // Pitches (absolute MIDI note) staged for removal; committed on Func release.
        std::map<int, std::set<int>> noteEditStaged;  // stepIndex → set of MIDI notes

        // Per-track pending pattern mute toggle (Func+Mute deferred multi-select).
        // True = this track has an odd number of pending presses and will flip its
        // committed pattern mute state when Func releases.
        std::array<bool, kNumTracks> pendingPatternMuteToggle{};

        // MHZ.7.1: per-track input mode. RAM-only; set by Track+verb gesture (MHZ.7.2).
        // Mode applies on the focused track; TrackInputMode::Play is the default.
        std::array<TrackInputMode, kNumTracks> trackInputMode{};

        // 5.7: momentary trig-grid overlay. Default while no activating chord is held.
        // Fill+TRIG sets Retrig; Fill+SRC sets SoundPool. Cleared on modifier release.
        TrigGridMode trigGridMode = TrigGridMode::Default;

        // 6.5: Func+FX picker — re-skins the step grid to the effect catalogue.
        // funcFxInsertSlot: which insert slot (0 or 1) the picker targets.
        bool funcFxHeld = false;
        int funcFxInsertSlot = 0;

        // 6.5 / 8.26: Master FX (2 global inserts + 2 send returns).
        // masterFxPickerOpen — step grid shows effect catalogue overlay (Func+Song+FX chord).
        // masterFxInsertSlot — which of the 4 master units the picker / MZ targets:
        //   0 = Insert 1, 1 = Insert 2, 2 = Send A, 3 = Send B. Cycles on re-press.
        // Params are visible whenever masterSection==5 (Song+FX navigates there).
        bool masterFxPickerOpen = false;
        int masterFxInsertSlot = 0;

        // 7b: an OnDemand machine console is open (opened/closed by long-pressing
        // the focused machine's consoleSectionIndex() key). Drives
        // SurfaceLayer::MachineConsole via resolveActiveLayer. AlwaysOn consoles
        // ignore this flag. Ephemeral — never serialised.
        bool machineConsoleOpen = false;

        // 7c: Route routing-matrix scratch session. Live while the Route console is
        // open. routeScratch[t] holds the staged output destination for track t
        // (encoded, matching decodeOutputDest). SurfaceModel renders from it; Confirm
        // commits each cell to the track's channelState.out, Cancel/close discards.
        // Ephemeral — never serialised.
        bool routeConsoleActive = false;
        std::array<float, static_cast<std::size_t>(kNumTracks)> routeScratch{};

        // Active sticky overlay (Overlay::None when no overlay is active).
        // Replaces the former timeStickyMode / densityStickyMode / velStickyMode booleans.
        // ModeReducer::activeOverlay() checks euclidHeld first, then this field.
        Overlay overlay = Overlay::None;

        // Per-overlay parameters (remain even when their overlay is not active;
        // cleared by escapeOverlay when that overlay exits).
        // ── Density ──
        int  densityBank = 0;  // 0 = tracks 0-7, 1 = tracks 8-15

        enum class DensitySubPage { Amount, Musicality, Selection };
        DensitySubPage densitySubPage = DensitySubPage::Amount;

        // ── Vel ──
        int  velBank = 0;   // 0 = tracks 0-7, 1 = tracks 8-15

        enum class VelSubPage { Depth, Center, Mode, Blend };
        VelSubPage velSubPage = VelSubPage::Depth;

        // ── SampleProps (9.23) ──
        // Absolute pool index being edited by the sample-properties band.
        // -1 = none (the band renders inert). Set on entry from the pool row
        // Props… button; reset to -1 by escapeOverlay(SampleProps).
        int samplePropsPoolIndex = -1;

        // ── Time / Key (the signatures band; DESIGN §4.8 / §4.10) ──
        // timeEntryScope: set to the resolved scope at toggle-on time so that
        // bare (no modifier held) writes land on the intended level, not scope 0.
        int  timeEntryScope = 2;  // default: Song

        // The TIME band and KEY band are a family on the TRIG section key:
        // re-pressing TRIG while the band is open cycles TIME <-> KEY.
        enum class SigPage { Time, Key };
        SigPage sigPage = SigPage::Time;

        // Generator hub (9.10): true while the 3-key has been held ≥350 ms,
        // showing the Euclid / Density / Vel picker. Closes on key-up.
        bool generatorHubHeld = false;

        // 5.5 Euclidean generator: entered via generator hub (9.10) or legacy Phrase+Fill chord.
        // Parameters: Pulses/Offset/Accent shown in MZ via MetaBand::Euclidean.
        bool euclidHeld = false;
        int euclidPulses = 4;    // number of onsets
        int euclidOffset = 0;    // rotation (signed)
        int euclidAccents = 0;    // accented onsets (velocity 100 vs 64)

        // 10.7 Melodic generator: entered via generator hub (cell 3). A *print*
        // tool like Euclid; parameters shown in MZ via MetaBand::Melodic and fed
        // to generateMelody() against the effective KeySig. Held flag is the SSOT
        // (activeOverlay maps it to Overlay::Melodic); the live preview + stash
        // mirror the Euclid editor-owned pattern (see PluginEditor).
        bool melodicHeld = false;
        int melodyDensity = 8;     // onsets (placed strongest-beat first)
        int melodyCore    = 1;     // 0 = triad, 1 = penta, 2 = full
        int melodyContour = 0;     // MelodyContour
        int melodyOctaves = 2;     // pitch span (1..4)
        int melodyStepLeap = 30;   // 0..100 deviation from the contour
        int melodySeed    = 1;     // deterministic seed (re-roll bumps it)
        int melodySource  = 0;     // MelodySource: 0 = Generate, 1 = Keep rhythm

        // 10.8 Harmonic voice-mover: entered via generator hub (cell 4). A sticky
        // *print* sculptor (Overlay::Harmony) — not a chord picker. The MZ shows the
        // cursor chord's four voices + structure (length/cursor/transpose/octave)
        // via MetaBand::Harmony; voices are ladder indices into the effective
        // KeySig, so they stay in-key. harmonyHeld is the SSOT (activeOverlay maps
        // it to Overlay::Harmony); the progression buffer lives here so the band can
        // read it. The live preview + stash mirror the Euclid/Melodic editor pattern.
        bool harmonyHeld = false;
        HarmonyProgression harmonyProg{};

        // MHZ.7.4: last note played per-track, used as LEVELS record-arm pitch.
        // Updated whenever a note is triggered (keyboard overlay or CHROMATIC mode).
        std::array<int, kNumTracks> lastPlayedNote{};  // default 60 (C4)

        // Pending-confirm state. Captured at arm time; cleared on Yes/No/cancel.
        ConfirmState confirm;

        // Delete picker: scope+Func+Clear enters this modality; step tap → named confirm.
        DeletePickerState deletePicker;

        // ── Bundled gesture-group resets ─────────────────────────────────────────
        // Call these instead of scattering individual field assignments — each
        // bundles all fields that belong to one gesture so no field is forgotten.

        // Clears the Func+Src/Note-edit gesture (funcSrcHeld, mode flag, steps,
        // staged pitches). Call on Func release or any escape that exits note-edit.
        void resetNoteEdit() noexcept
        {
            funcSrcHeld = false;
            noteEditMode = false;
            noteEditOctave = 3;
            noteEditSteps.clear();
            noteEditStaged.clear();
        }

        // Clears the P-Lock clear mode (mode flag, target track/step, staged slots).
        // Call AFTER committing or discarding the staged removals.
        void resetPLockClear() noexcept
        {
            pLockClearMode = false;
            pLockClearTrack = -1;
            pLockClearStep = -1;
            pLockClearStaged.clear();
            stepMoveActive = false;
            stepMoveAnchor = -1;
        }

        // Clears the Func-layer overlay pickers (machine picker, FX picker, master
        // FX picker). Call on Func release or picker close.
        void resetFxPickers() noexcept
        {
            funcTrackHeld = false;
            funcFxHeld = false;
            funcFxInsertSlot = 0;
            masterFxPickerOpen = false;
            masterFxInsertSlot = 0;
        }

        // Clears the generator hub picker (closes the 3-key hold picker).
        void resetGeneratorHub() noexcept { generatorHubHeld = false; }

        // Clears the Euclidean generator state (held flag and working params).
        // Call on Fill/Phrase release when euclid was active.
        void resetEuclid() noexcept
        {
            euclidHeld = false;
            euclidPulses = 4;
            euclidOffset = 0;
            euclidAccents = 0;
        }

        // Clears the melodic generator state (held flag and working params).
        // Seed is preserved deliberately so a re-entry reproduces the last melody;
        // only an explicit re-roll bumps it.
        void resetMelodic() noexcept
        {
            melodicHeld = false;
            melodyDensity = 8;
            melodyCore = 1;
            melodyContour = 0;
            melodyOctaves = 2;
            melodyStepLeap = 30;
            melodySource = 0;
        }

        // Clears the harmonic voice-mover state (held flag + progression buffer).
        // The buffer is re-seeded on every entry, so a full reset here is safe.
        void resetHarmony() noexcept
        {
            harmonyHeld = false;
            harmonyProg = HarmonyProgression{};
        }

        // Returns the first slot index for the currently active page on the given track.
        // Returns 0 if track is out of range or info.firstSlot is -1 (empty section).
        [[nodiscard]] int activeFirstSlot(int track, const SectionInfo& info) const
        {
            if (track < 0 || track >= static_cast<int>(kNumTracks) || info.firstSlot < 0)
                return 0;
            const int section = trackSection[static_cast<std::size_t>(track)];
            const int page = trackPage[static_cast<std::size_t>(track)]
                                      [static_cast<std::size_t>(section)];
            return info.firstSlot + (kParamsPerPage * page);
        }
    };
}
