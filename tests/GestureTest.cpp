#include "TestHarness.h"
#include "GestureHarness.h"
#include "../src/command/ButtonLayers.h"

namespace lockstep
{
    using namespace test;
    using CB = ControllerButton;
    using PS = EditMode::PrimaryScope;
    using T = ControllerEvent::Type;

    // -------------------------------------------------------------------------
    // Scenario 1: PS::Trig / VerbRecord copies held steps to clipboard

    static void scenario_trigCopy()
    {
        GestureFixture f;
        auto& s3 = f.track(0).steps[3];
        s3.trig = true;
        s3.condition.probabilityPercent = 75;

        f.holdStep(0, 3);

        const bool handled = f.verb(PS::Trig, CB::VerbRecord);
        CHECK(handled, "Trig+Record should be handled by CommandCore");
        CHECK(f.clipboard.type == ClipboardType::Step, "clipboard type should be Step");
        CHECK(f.clipboard.stepEntries.size() == 1, "one step entry copied");
        CHECK(f.clipboard.stepEntries[0].relOffset == 0, "anchor offset is 0");
        CHECK(f.clipboard.stepEntries[0].data.trig, "copied step has trig=true");
        CHECK(f.clipboard.stepEntries[0].data.condition.probabilityPercent == 75, "condition preserved");
    }

    // Scenario 2: PS::Trig / VerbPlay pastes with wrap-around

    static void scenario_trigPaste()
    {
        GestureFixture f;
        auto& s5 = f.track(0).steps[5];
        s5.trig = true;
        f.holdStep(0, 5);
        f.verb(PS::Trig, CB::VerbRecord);
        f.releaseAllSteps();
        f.effects.reset();

        f.holdStep(0, 15);
        const bool handled = f.verb(PS::Trig, CB::VerbPlay);
        CHECK(handled, "Trig+Play should be handled");
        CHECK(f.track(0).steps[15].trig, "pasted trig at step 15");
    }

    // Scenario 3: PS::Trig / VerbClear with no Func and no active slot
    // → full clear (trig=false, condition reset, P-Locks wiped)

    static void scenario_trigClearFull()
    {
        GestureFixture f;
        auto& s2 = f.track(0).steps[2];
        s2.trig = true;
        s2.condition.probabilityPercent = 50;

        f.holdStep(0, 2);
        const bool handled = f.verb(PS::Trig, CB::VerbClear);
        CHECK(handled, "Trig+Clear should be handled");
        CHECK(!f.track(0).steps[2].trig, "trig cleared");
        CHECK(f.track(0).steps[2].condition.probabilityPercent == 100, "condition reset to default");
        CHECK(f.editContext.wasParamWritten(), "markParamWritten called");
    }

    // Scenario 4: PS::Trig / VerbClear with Func held → clear P-Locks only

    // 9.37 item C: this was Trig+Func+Clear until 9.4 gave Func+O to UNDO. The
    // behaviour did not go away -- it moved to the Clear key's HOLD rail, where it
    // needs no chord at all -- so the scenario follows it and keeps guarding the same
    // promise: the locks go, the trig stays.
    static void scenario_trigClearPLocks()
    {
        GestureFixture f;
        auto& s7 = f.track(0).steps[7];
        s7.trig = true;
        s7.overrides.set(0, 0.5f);

        f.holdStep(0, 7);

        CHECK(f.action(ActionId::ClearStepLocks, CB::VerbClear),
              "Trig + hold(Clear) should be handled");
        CHECK(f.track(0).steps[7].trig, "trig intact after P-Lock clear");
        CHECK(!f.track(0).steps[7].overrides.has(0), "P-Lock cleared");
    }

    // Scenario 5: PS::Trig / VerbClear with SRC section held → clear note/vel/gate
    // overrides on held step(s), leaving trig and P-Locks intact. (DESIGN §13.2 —
    // replaces the old Func+P note-clear; all clearing now lives on the Clear verb.)

    static void scenario_trigClearNotes()
    {
        GestureFixture f;
        auto& s1 = f.track(0).steps[1];
        s1.trig = true;
        s1.trigOverride.noteCount = 2;
        s1.trigOverride.hasVelocity = true;
        s1.trigOverride.velocity = 80;
        s1.overrides.set(0, 0.5f);  // a P-Lock that must survive the note-clear

        // Hold the SRC section (index 1) while holding the step. Trig outranks
        // Section, so primaryScope stays Trig; sectionHeld() routes the domain.
        f.uiState.trackSection[0] = IMachine::kSrcSecIdx;
        f.editMode.setSectionHeld(true);
        f.holdStep(0, 1);

        const bool handled = f.verb(PS::Trig, CB::VerbClear);
        CHECK(handled, "Trig+SRC+Clear should be handled");
        CHECK(f.track(0).steps[1].trigOverride.noteCount == 0, "noteCount cleared");
        CHECK(!f.track(0).steps[1].trigOverride.hasVelocity, "hasVelocity cleared");
        CHECK(f.track(0).steps[1].trig, "trig still set");
        CHECK(f.track(0).steps[1].overrides.has(0), "P-Lock preserved (notes-only clear)");
    }

    // Scenario 6: no held steps → trig copy returns false (no-op)

    static void scenario_trigCopyNoSteps()
    {
        GestureFixture f;
        const bool handled = f.verb(PS::Trig, CB::VerbRecord);
        CHECK(!handled, "Trig+Record with no held steps should not be handled");
    }

    // -------------------------------------------------------------------------
    // Scenario 7: PS::Track / VerbRecord copies track, VerbPlay pastes it

    static void scenario_trackCopyPaste()
    {
        GestureFixture f;
        f.uiState.activeTrack = 0;
        f.track(0).steps[4].trig = true;
        f.track(0).steps[4].condition.probabilityPercent = 60;

        bool handled = f.verb(PS::Track, CB::VerbRecord);
        CHECK(handled, "Track+Record should be handled");
        CHECK(f.clipboard.type == ClipboardType::Track, "clipboard type Track");
        CHECK(f.effects.statuses.size() == 1, "status emitted");

        // Paste to track 1.
        f.uiState.activeTrack = 1;
        handled = f.verb(PS::Track, CB::VerbPlay);
        CHECK(handled, "Track+Play should be handled");
        CHECK(f.track(1).steps[4].trig, "pasted trig to track 1 step 4");
        CHECK(f.track(1).steps[4].condition.probabilityPercent == 60, "condition preserved");
    }

    // Scenario 8: PS::Track / VerbClear arms confirm (does NOT clear immediately).
    // The actual clear executes in PluginEditor::executeConfirm after the user
    // presses CONFIRM (P).  Track+Song+Clear arms the all-phrases variant.

    static void scenario_trackClear()
    {
        // Bare Track+Clear → confirm-tier arm (this phrase only).
        GestureFixture f;
        f.uiState.activeTrack = 2;
        f.track(2).steps[0].trig = true;
        f.track(2).steps[1].trig = true;

        const bool handled = f.verb(PS::Track, CB::VerbClear);
        CHECK(handled, "Track+Clear handled");
        CHECK(f.uiState.confirm.pending(), "confirm armed");
        CHECK(f.uiState.confirm.kind == ConfirmKind::ClearTrack, "kind = ClearTrack");
        CHECK(f.uiState.confirm.target == 2, "target = active track");
        CHECK(f.track(2).steps[0].trig, "steps NOT yet cleared (wait for confirm)");

        // Track+Song+Clear → confirm-tier arm (all phrases).
        GestureFixture f2;
        f2.uiState.activeTrack = 3;
        f2.uiState.songHeld = true;
        f2.track(3).steps[0].trig = true;
        const bool handled2 = f2.verb(PS::Track, CB::VerbClear);
        CHECK(handled2, "Track+Song+Clear handled");
        CHECK(f2.uiState.confirm.kind == ConfirmKind::ClearTrackAll, "kind = ClearTrackAll");
        CHECK(f2.uiState.confirm.target == 3, "target = active track");
        CHECK(f2.track(3).steps[0].trig, "steps NOT yet cleared (wait for confirm)");
    }

    // Scenario 9: PS::Phrase / VerbRecord and VerbPlay round-trip

    static void scenario_phraseCopyPaste()
    {
        // 5.3 (DESIGN §23.3): Phrase+Record/Play now act on the focused track's
        // single active phrase (fork-on-shared), so the grab + paste are delegated
        // to the editor (which must flush live edits and can raise an async fork
        // confirm). The dispatch layer's job is to fire the right effect.
        GestureFixture f;

        bool handled = f.verb(PS::Phrase, CB::VerbRecord);
        CHECK(handled, "Phrase+Record handled");
        CHECK(f.effects.copyPhraseActiveSlotCount == 1, "copy delegated to editor once");

        // A loaded phrase clip (type Pattern) routes Play to the fork-aware paste.
        f.clipboard.type = ClipboardType::Pattern;
        handled = f.verb(PS::Phrase, CB::VerbPlay);
        CHECK(handled, "Phrase+Play handled");
        CHECK(f.effects.pastePhraseActiveSlotCount == 1, "paste delegated to editor once");
    }

    // Scenario 10: PS::Song / VerbClear → Panic transport effect

    static void scenario_songPanic()
    {
        GestureFixture f;
        const bool handled = f.verb(PS::Song, CB::VerbClear);
        CHECK(handled, "Song+Clear handled");
        CHECK(f.effects.transports == 1, "transport called once");
        CHECK(f.effects.transportActions[0] == CommandEffects::TransportAction::Panic,
              "transport action is Panic");
    }

    // Scenario 11: PS::None / VerbSnapshot → Song-scope snapshot

    static void scenario_noneSnapshot()
    {
        GestureFixture f;
        // No scope held, so primaryScope() == None.
        const bool handled = f.verb(PS::None, CB::VerbSnapshot);
        CHECK(handled, "None+VerbSnapshot handled (snapshot)");
        // No crash; arrangement state unchanged (just verifying it doesn't assert).
    }

    // Scenario 12: resolveLayer preserves velocity through a Track-layer remap

    static void scenario_resolveLayerPreservesVelocity()
    {
        // A Step event with velocity=127 and Track layer held should become
        // SelectTrack, but the velocity field must survive unchanged.
        ControllerEvent ev{ ControllerEvent::Type::ButtonDown, CB::Step, 5, 0, 127 };
        const LayerContext ctx{ false, true, false };  // trackHeld
        const ControllerEvent resolved = resolveLayer(ev, ctx);
        CHECK(resolved.button == CB::SelectTrack, "Step+Track remaps to SelectTrack");
        CHECK(resolved.index == 5, "index preserved");
        CHECK(resolved.velocity == 127, "velocity preserved through remap");
    }

    // Scenario 13: resolveLayer Func layer: Section → MetaSection

    static void scenario_resolveLayerSection()
    {
        ControllerEvent ev{ ControllerEvent::Type::ButtonDown, CB::Section, 2, 100 };
        const LayerContext ctx{ true, false, false };  // funcHeld
        const ControllerEvent resolved = resolveLayer(ev, ctx);
        CHECK(resolved.button == CB::MetaSection, "Section+Func remaps to MetaSection");
        CHECK(resolved.index == 2, "section index preserved");
    }

    // Scenario 14: PS::Section / VerbRecord copy P-Locks, VerbClear zeroes them

    static void scenario_sectionCopyClear()
    {
        GestureFixture f;
        f.uiState.activeTrack = 0;
        f.uiState.trackSection[0] = 0;  // section index 0

        // FakeMachineCatalog.paramSpec returns a default ParamSpec with sectionIndex==0,
        // so all 0 slots land in section 0. numParams returns 0 → nothing to copy.
        // We verify the handler fires without crash (FakeMachineCatalog has no params).
        const bool handled = f.verb(PS::Section, CB::VerbRecord);
        CHECK(handled, "Section+Record handled even with 0 params");
        CHECK(f.clipboard.type == ClipboardType::Section, "clipboard type Section");
        CHECK(f.clipboard.sectionSlots.empty(), "no slots (FakeMachineCatalog returns 0 params)");
    }

    // ── A4.1 mute/solo cluster (handleAction dispatch) ────────────────────────

    static void scenario_muteGlobalToggle()
    {
        GestureFixture f;
        const bool handled = f.action(ActionId::GlobalMuteToggle, CB::ToggleMute, 3);
        CHECK(handled, "GlobalMuteToggle handled");
        CHECK(f.effects.globalMuteTracks.size() == 1, "globalMuteToggle called once");
        CHECK(f.effects.globalMuteTracks[0] == 3, "track index forwarded");
        CHECK(f.effects.soloTracks.empty(), "soloToggle not called");
    }

    static void scenario_muteFunc()
    {
        GestureFixture f;
        const bool handled = f.action(ActionId::SoloToggle, CB::ToggleMute, 5);
        CHECK(handled, "SoloToggle handled");
        CHECK(f.effects.soloTracks.size() == 1, "soloToggle called once");
        CHECK(f.effects.soloTracks[0] == 5, "track index forwarded");
        CHECK(f.effects.globalMuteTracks.empty(), "globalMuteToggle not called");
    }

    static void scenario_muteScene()
    {
        GestureFixture f;
        const bool handled = f.action(ActionId::SceneMuteToggle, CB::ToggleMute, 2);
        CHECK(handled, "SceneMuteToggle handled");
        CHECK(f.effects.sceneMuteTracks.size() == 1, "sceneMuteToggle called once");
        CHECK(f.effects.sceneMuteTracks[0] == 2, "track index forwarded");
    }

    static void scenario_muteMorph()
    {
        GestureFixture f;
        const bool handled = f.action(ActionId::FluidMuteToggle, CB::ToggleMute, 7);
        CHECK(handled, "FluidMuteToggle handled");
        CHECK(f.effects.fluidMuteTracks.size() == 1, "fluidMuteToggle called once");
        CHECK(f.effects.fluidMuteTracks[0] == 7, "track index forwarded");
    }

    // ── A4.2 scope-up latch-release dedup ────────────────────────────────────

    static void scenario_scopeUpUnlatched()
    {
        // When not latched, handleUp clears xxxHeld and fires editMode scope event.
        GestureFixture f;
        f.uiState.trackHeld = true;
        f.uiState.latch.track = false;

        ControllerEvent ev{ ControllerEvent::Type::ButtonUp, CB::TrackScope };
        auto c = f.ctx();
        const bool handled = f.core.handleUp(ev, c, f.effects);

        CHECK(!handled, "handleUp returns false (caller does unique effects)");
        CHECK(!f.uiState.trackHeld, "trackHeld cleared when not latched");
        CHECK(f.effects.repaints == 1, "repaint requested");
    }

    static void scenario_scopeUpLatched()
    {
        // When latched, handleUp leaves xxxHeld unchanged.
        GestureFixture f;
        f.uiState.trackHeld = true;
        f.uiState.latch.track = true;

        ControllerEvent ev{ ControllerEvent::Type::ButtonUp, CB::TrackScope };
        auto c = f.ctx();
        const bool handled = f.core.handleUp(ev, c, f.effects);

        CHECK(!handled, "handleUp returns false");
        CHECK(f.uiState.trackHeld, "trackHeld stays true when latched");
        CHECK(f.effects.repaints == 0, "no repaint when latched");
    }

    static void scenario_muteUpUnlatched()
    {
        GestureFixture f;
        f.uiState.muteHeld = true;
        f.uiState.latch.mute = false;

        ControllerEvent ev{ ControllerEvent::Type::ButtonUp, CB::MuteScope };
        auto c = f.ctx();
        [[maybe_unused]] const bool h = f.core.handleUp(ev, c, f.effects);

        CHECK(!f.uiState.muteHeld, "muteHeld cleared");
        CHECK(f.effects.repaints == 1, "repaint requested");
    }

    // ── KeyBindings mute resolution golden tests ─────────────────────────────

    static void scenario_muteBindingResolution()
    {
        using AId = ActionId;
        using SL = SurfaceLayer;

        // Bare Mute+step → GlobalMuteToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModMute, SL::MuteView).action == AId::GlobalMuteToggle, "bare Mute → GlobalMuteToggle");

        // Func+Mute+step → SoloToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModFunc | kModMute, SL::MuteView).action == AId::SoloToggle, "Func+Mute → SoloToggle");

        // Scene+Mute+step → SceneMuteToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModScene | kModMute, SL::MuteView).action == AId::SceneMuteToggle, "Scene+Mute → SceneMuteToggle");

        // Morph+Mute+step → FluidMuteToggle in MorphMuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModMorph | kModMute, SL::MorphMuteView).action == AId::FluidMuteToggle, "Morph+Mute → FluidMuteToggle");

        // Func+Mute wins over bare Mute (popcount 2 > 1) in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModFunc | kModMute, SL::MuteView).action != AId::GlobalMuteToggle, "Func+Mute does NOT resolve to GlobalMuteToggle");
    }

    // -------------------------------------------------------------------------
    // Test runner

    // ── Delete picker scenarios ───────────────────────────────────────────────

    // Arm picker → release chord → picker still active (sticky).
    static void scenario_pickerStickyOnRelease()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Phrase;

        auto ctx = f.ctx();
        (void)f.core.handleUp({ ControllerEvent::Type::ButtonUp, CB::PhraseScope, -1 }, ctx, f.effects);
        CHECK(f.uiState.deletePicker.active(), "picker survives scope-key release");
    }

    // Arm picker → step tap → transitions to named confirm.
    static void scenario_pickerStepToConfirm()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Phrase;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Step, 5 });
        CHECK(handled, "step swallowed by picker");
        CHECK(!f.uiState.deletePicker.active(), "picker cleared after step tap");
        CHECK(f.uiState.confirm.pending(), "confirm armed");
        CHECK(f.uiState.confirm.kind == ConfirmKind::DeletePhrase, "correct kind");
        CHECK(f.uiState.confirm.target == 5, "correct target slot");
        CHECK(!f.effects.statuses.empty(), "named confirm status emitted");
    }

    // Arm a TRACK picker → release Track → tap a slot, which now arrives as a plain
    // Step because kLayerRemaps only rewrote it to SelectTrack while Track was held.
    // This is the leg that was broken: the Track branch matched SelectTrack alone, so
    // the tap fell through to the cancel tail and the "sticky" picker was sticky for
    // Phrase and Scene only (9.38). PRINCIPLES §16 requires the release not to matter.
    static void scenario_pickerTrackStickyAfterRelease()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Track;
        f.uiState.trackHeld = false;   // the arming chord is already up

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Step, 3 });
        CHECK(handled, "slot tap swallowed by the track picker");
        CHECK(!f.uiState.deletePicker.active(), "picker cleared after the pick");
        CHECK(f.uiState.confirm.pending(), "confirm armed rather than cancelled");
        CHECK(f.uiState.confirm.kind == ConfirmKind::DeleteTrack, "correct kind");
        CHECK(f.uiState.confirm.target == 3, "correct target slot");
    }

    // The same pick with Track STILL held, where the key arrives as SelectTrack.
    // Both encodings must land the same confirm — that is the whole point.
    static void scenario_pickerTrackStickyWhileHeld()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Track;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::SelectTrack, 3 });
        CHECK(handled, "slot tap swallowed while Track is still held");
        CHECK(f.uiState.confirm.pending(), "confirm armed");
        CHECK(f.uiState.confirm.kind == ConfirmKind::DeleteTrack, "correct kind");
        CHECK(f.uiState.confirm.target == 3, "correct target slot");
    }

    // Arm picker → foreign key → cancelled.
    static void scenario_pickerCancelledByForeignKey()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Scene;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::VerbClear, -1 });
        CHECK(handled, "foreign key swallowed");
        CHECK(!f.uiState.deletePicker.active(), "picker cleared");
        CHECK(!f.uiState.confirm.pending(), "no confirm armed on cancel");
        CHECK(!f.effects.statuses.empty(), "cancelled status emitted");
    }

    // Arm picker → Func down → NOT cancelled.
    static void scenario_pickerFuncExempt()
    {
        GestureFixture f;
        f.uiState.deletePicker.scope = DeleteScope::Track;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Func, -1 });
        CHECK(!handled, "Func not swallowed");
        CHECK(f.uiState.deletePicker.active(), "picker survives Func");
    }

    // ── Confirm lifecycle scenarios ──────────────────────────────────────────

    // Helper: arm a DeletePhrase confirm directly (as the editor arm code would).
    static void armConfirm(GestureFixture& f, ConfirmKind kind = ConfirmKind::DeletePhrase, int target = -1)
    {
        f.uiState.confirm = { kind, target };
    }

    // Arm → release arming chord → confirm still pending (sticky).
    static void scenario_confirmStickyOnRelease()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);

        // Release an arming key (simulate Phrase scope up) — not routed through handleDown
        // so it never fires the cancel path; confirm must survive.
        auto ctx = f.ctx();
        (void)f.core.handleUp({ ControllerEvent::Type::ButtonUp, CB::PhraseScope, -1 }, ctx, f.effects);
        CHECK(f.uiState.confirm.pending(), "confirm survives scope-key release");
        CHECK(f.effects.confirmsExecuted.empty(), "no execution on release");
    }

    // Pending → foreign press (e.g. a step) → cancelled, event swallowed.
    static void scenario_confirmCancelledByForeignKey()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Step, 3 });
        CHECK(handled, "foreign press swallowed while confirm pending");
        CHECK(!f.uiState.confirm.pending(), "confirm cleared after foreign press");
        CHECK(f.effects.confirmsExecuted.empty(), "no execution on cancel");
        CHECK(!f.effects.statuses.empty(), "cancelled status emitted");
        CHECK(f.effects.statuses.back() == "Cancelled", "status text = Cancelled");
    }

    // Pending → Func press → NOT cancelled (Func never cancels).
    static void scenario_confirmFuncNeverCancels()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Func, -1 });
        CHECK(!handled, "Func not swallowed — still pending");
        CHECK(f.uiState.confirm.pending(), "confirm survives Func press");
    }

    // Pending → Func held → VerbConfirm (P) → cancel (No).
    static void scenario_confirmFuncPCancels()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);
        f.uiState.funcHeld = true;  // simulate Func held

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::VerbConfirm, -1 });
        CHECK(handled, "Func+P swallowed");
        CHECK(!f.uiState.confirm.pending(), "confirm cleared");
        CHECK(f.effects.confirmsExecuted.empty(), "executeConfirm NOT called on No");
        CHECK(!f.effects.statuses.empty(), "cancelled status emitted");
    }

    // Pending → P (no Func) → executed with correct kind+target.
    static void scenario_confirmYesExecutes()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeleteTrack, 5);
        f.uiState.funcHeld = false;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::VerbConfirm, -1 });
        CHECK(handled, "P swallowed");
        CHECK(!f.uiState.confirm.pending(), "confirm cleared");
        CHECK(f.effects.confirmsExecuted.size() == 1, "executeConfirm called once");
        CHECK(f.effects.confirmsExecuted[0].kind == ConfirmKind::DeleteTrack, "correct kind");
        CHECK(f.effects.confirmsExecuted[0].target == 5, "correct target");
    }


    // -------------------------------------------------------------------------
    // 9.29 — the Machine scope. Track owns identity, Machine owns the sound: these
    // verbs must move the machine + its params and nothing else.

    static void scenario_machineCopyPaste()
    {
        GestureFixture f;
        f.uiState.activeTrack = 2;
        f.catalog.params = 3;
        f.catalog.base = { 0.25f, 0.5f, 0.75f };
        f.catalog.id = "lockstep.fm.v1";

        CHECK(f.action(ActionId::MachineCopy, CB::VerbRecord), "Machine+Rec handled");
        CHECK(f.clipboard.type == ClipboardType::Machine, "clipboard types the SOUND");
        CHECK(f.clipboard.clipMachineId == "lockstep.fm.v1", "machine id captured");
        CHECK(f.clipboard.clipMachineParams.size() == 3, "all three slots captured");
        CHECK(f.clipboard.clipMachineParams[2] == 0.75f, "param values captured from the catalog");

        // Paste onto a track running a DIFFERENT machine: the engine must be loaded
        // first, or the assign would reset the params we are about to write.
        f.uiState.activeTrack = 5;
        f.catalog.id = "lockstep.sample.v1";
        CHECK(f.action(ActionId::MachinePaste, CB::VerbPlay), "Machine+Play handled");
        CHECK(f.effects.machineAssigns.size() == 1, "target machine loaded");
        CHECK(f.effects.machineAssigns[0] == "5:lockstep.fm.v1", "loaded onto the focused track");
        CHECK(f.effects.machineParamWrites.size() == 1, "params written once");
        CHECK(f.effects.machineParamWrites[0].first == 5, "written to the focused track");
        CHECK(f.effects.machineParamWrites[0].second.size() == 3, "whole param set written");
    }

    static void scenario_machinePasteSameMachineDoesNotReload()
    {
        GestureFixture f;
        f.catalog.params = 2;
        f.catalog.base = { 0.1f, 0.2f };
        CHECK(f.action(ActionId::MachineCopy, CB::VerbRecord), "copy handled");
        CHECK(f.action(ActionId::MachinePaste, CB::VerbPlay), "paste handled");
        CHECK(f.effects.machineAssigns.empty(),
              "same machine → no reload (a needless rebuild would drop voices)");
        CHECK(f.effects.machineParamWrites.size() == 1, "params still written");
    }

    static void scenario_machinePasteRejectsForeignClipboard()
    {
        GestureFixture f;
        f.clipboard.type = ClipboardType::Track;   // a track copy is not a sound
        CHECK(f.action(ActionId::MachinePaste, CB::VerbPlay), "handled (reports, not silent)");
        CHECK(f.effects.machineAssigns.empty(), "no machine loaded from a track clipboard");
        CHECK(f.effects.machineParamWrites.empty(), "no params written from a track clipboard");
    }

    static void scenario_machineInit()
    {
        GestureFixture f;
        f.uiState.activeTrack = 1;
        f.catalog.id = "lockstep.drum.v1";
        CHECK(f.action(ActionId::MachineInit, CB::VerbClear), "Machine+Clear handled");
        CHECK(f.effects.machineAssigns.size() == 1, "init re-assigns the machine");
        CHECK(f.effects.machineAssigns[0] == "1:lockstep.drum.v1",
              "init reloads the SAME machine (setTrackMachine resets params to defaults)");
    }

    // -------------------------------------------------------------------------
    // 9.29 — DELETE moved to the hold rail. The golden net drives taps only, so the
    // hold is proven here: the action the hold row resolves to must arm the picker
    // for the held scope, and must decline where no entity exists to delete.

    static void scenario_deleteHoldArmsPickerPerScope()
    {
        struct Case { const char* name; ControllerButton mod; DeleteScope expect; };
        const Case cases[] = {
            { "Track", CB::TrackScope, DeleteScope::Track },
            { "Phrase", CB::PhraseScope, DeleteScope::Phrase },
            { "Scene", CB::SceneScope, DeleteScope::Scene },
        };
        for (const auto& c : cases)
        {
            GestureFixture f;
            f.editMode.onScopeEvent({ T::ButtonDown, c.mod, 0, 0 });
            CHECK(f.action(ActionId::VerbDelete, CB::VerbClear),
                  juce::String(c.name) + " + hold(Clear) is handled");
            CHECK(f.uiState.deletePicker.active(),
                  juce::String(c.name) + " + hold(Clear) arms the deletion picker");
            CHECK(f.uiState.deletePicker.scope == c.expect,
                  juce::String(c.name) + " picker targets its own entity");
        }
    }

    static void scenario_deleteHoldInertWithoutDeletableScope()
    {
        {
            GestureFixture f;   // Song owns no deletable entity
            f.editMode.onScopeEvent({ T::ButtonDown, CB::SongScope, 0, 0 });
            CHECK(!f.action(ActionId::VerbDelete, CB::VerbClear), "Song+hold(Clear) declines");
            CHECK(!f.uiState.deletePicker.active(), "no picker armed under Song");
        }
        {
            GestureFixture f;   // no scope at all
            CHECK(!f.action(ActionId::VerbDelete, CB::VerbClear), "bare hold(Clear) declines");
            CHECK(!f.uiState.deletePicker.active(), "no picker armed with no scope");
        }
    }

    // Trig + Func + Clear = clear the P-Locks, keep the trig. Documented in README and
    // DESIGN, but DEAD until 9.29: the Func layer rewrote Clear to VerbDelete before
    // verbs::trig could read the Func flag, and nothing handled VerbDelete under a step
    // hold. Retiring that remap revives it -- this test is the proof, and the guard.
    static void scenario_trigFuncClearKeepsTrig()
    {
        GestureFixture f;
        auto& s4 = f.track(0).steps[4];
        s4.trig = true;
        s4.condition.probabilityPercent = 60;
        s4.overrides.set(2, 0.9f);
        f.holdStep(0, 4);

        // The Func layer must NOT rewrite the button (9.29 retired the VerbDelete
        // remap). It still must not -- but Func+O is UNDO now (9.4/9.37), so the
        // lock-clear reads its own action off the hold rail rather than a Func chord.
        const ControllerEvent raw{ T::ButtonDown, CB::VerbClear, -1, 0 };
        const auto routed = resolveLayer(raw, LayerContext{ /*func*/ true, false, false });
        CHECK(routed.button == CB::VerbClear, "Func+Clear stays Clear (no VerbDelete remap)");

        CHECK(f.action(ActionId::ClearStepLocks, CB::VerbClear), "Trig + hold(Clear) handled");
        CHECK(f.track(0).steps[4].trig, "the trig SURVIVES (this is the whole point)");
        CHECK(!f.track(0).steps[4].overrides.has(2), "the P-Lock is cleared");
    }

    // -------------------------------------------------------------------------
    // 9.12 st.7c — the nav family. The golden net drives dispatch, but it cannot put
    // the editor INTO the NoteEdit / CHROMATIC layers, so the octave rows are invisible
    // to it. And the +1/-1 sign convention is the single most likely thing to invert in
    // a migration like this. Both are pinned here, at the action->effect seam.

    static void scenario_navActionsMapToEffects()
    {
        struct Case { ActionId id; CB btn; const char* fx; int delta; };
        const Case cases[] = {
            { ActionId::NavTrackUp,       CB::NavUp,    "focusTrack", +1 },
            { ActionId::NavTrackDown,     CB::NavDown,  "focusTrack", -1 },
            { ActionId::NavPageRight,     CB::NavRight, "page",       +1 },
            { ActionId::NavPageLeft,      CB::NavLeft,  "page",       -1 },
            { ActionId::NavOctaveUp,      CB::NavRight, "octave",     +1 },
            { ActionId::NavOctaveDown,    CB::NavLeft,  "octave",     -1 },
            { ActionId::LengthDouble,     CB::NavUp,    "length",     +1 },
            { ActionId::LengthHalve,      CB::NavDown,  "length",     -1 },
            { ActionId::RotateRight,      CB::NavRight, "rotate",     +1 },
            { ActionId::RotateLeft,       CB::NavLeft,  "rotate",     -1 },
            { ActionId::CycleInputModeUp, CB::NavUp,    "inputMode",  +1 },
            { ActionId::CycleInputModeDown, CB::NavDown, "inputMode", -1 },
            { ActionId::MorphPickPoleA,   CB::NavUp,    "morphPole",   1 },
            { ActionId::MorphPickPoleB,   CB::NavDown,  "morphPole",   2 },
        };
        for (const auto& c : cases)
        {
            GestureFixture f;
            CHECK(f.action(c.id, c.btn), juce::String(c.fx) + " action is wired");
            CHECK(f.effects.navCalls.size() == 1, juce::String(c.fx) + " fires exactly one effect");
            CHECK(f.effects.navCalls[0].first == juce::String(c.fx),
                  juce::String(c.fx) + ": the right effect fired");
            CHECK(f.effects.navCalls[0].second == c.delta,
                  juce::String(c.fx) + ": the right direction (sign conventions must not flip)");
        }
    }

    // Transpose reads the Func qualifier rather than splitting into four actions: bare
    // is an octave, Func narrows it to a semitone. That IS what Func does everywhere --
    // narrow the same verb, not name a different one.
    static void scenario_transposeQualifiedByFunc()
    {
        {
            GestureFixture f;
            CHECK(f.action(ActionId::TransposeUp, CB::NavUp), "Phrase+Up handled");
            CHECK(f.effects.navCalls.at(0) == std::make_pair(juce::String("transpose"), 12),
                  "bare Phrase+Up transposes by an OCTAVE");
        }
        {
            GestureFixture f;
            f.uiState.funcHeld = true;
            CHECK(f.action(ActionId::TransposeDown, CB::NavDown), "Func+Phrase+Down handled");
            CHECK(f.effects.navCalls.at(0) == std::make_pair(juce::String("transpose"), -1),
                  "Func+Phrase+Down transposes by ONE SEMITONE");
        }
    }

    // -------------------------------------------------------------------------
    // 9.12 st.7d — the section family. Tap navigates; HOLD chooses what fills the
    // section, at the held scope (9.14 / §13.9). The hold is timing-based, so the
    // golden cannot see it; the table's answer and the effect it fires are pinned here.

    static void scenario_sectionHoldRowsAreScopeGated()
    {
        // The picker a section hold opens is the TABLE's call, not an index check.
        CHECK(resolveBinding(CB::Section, 5, kModTrack, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::OpenTrackFxPicker, "Track + hold(FX) = track FX picker");
        CHECK(resolveBinding(CB::Section, 5, kModSong, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::OpenMasterFxPicker, "Song + hold(FX) = master FX picker");
        CHECK(resolveBinding(CB::Section, 1, kModTrack, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::OpenMachinePicker, "Track + hold(SRC) = machine picker");
        // Bare holds carry NO picker: an unscoped hold of SRC is the OnDemand machine
        // console, and the scope gate is the only thing keeping the two apart.
        CHECK(resolveBinding(CB::Section, 1, kModNone, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::None, "bare hold(SRC) opens no picker (that hold is the console)");
        CHECK(resolveBinding(CB::Section, 5, kModNone, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::None, "bare hold(FX) opens no picker");
    }

    static void scenario_sectionActionsMapToEffects()
    {
        {
            GestureFixture f;
            CHECK(f.action(ActionId::SelectSection, CB::Section, 3), "tap is wired");
            CHECK(f.effects.sectionSelects == std::vector<int>{ 3 }, "tap navigates that section");
        }
        {
            GestureFixture f;
            CHECK(f.action(ActionId::SelectMetaSection, CB::Section, 2), "Func-layer tap is wired");
            CHECK(f.effects.metaSectionSelects == std::vector<int>{ 2 }, "meta tap navigates");
        }
        {
            GestureFixture f;
            CHECK(f.action(ActionId::OpenTrackFxPicker, CB::Section, 5), "track picker wired");
            CHECK(f.effects.fxPickerOpens == std::vector<bool>{ false }, "opens the TRACK picker");
        }
        {
            GestureFixture f;
            CHECK(f.action(ActionId::OpenMasterFxPicker, CB::Section, 5), "master picker wired");
            CHECK(f.effects.fxPickerOpens == std::vector<bool>{ true }, "opens the MASTER picker");
        }
    }

    // -------------------------------------------------------------------------
    // 9.12 st.7e/7f — confirm/quantize and transport.

    static void scenario_quantizeRowsAndEffect()
    {
        // The P key's rows SAID VerbConfirm while LABELLED "QUANT". The label was the
        // honest half; the action is QuantizeHeld and dispatch now agrees with the frame.
        CHECK(resolveBinding(CB::VerbConfirm, -1, kModTrack, SurfaceLayer::Base).action
                  == ActionId::QuantizeHeld, "Track+P = QUANT");
        CHECK(resolveBinding(CB::VerbConfirm, -1, kModPhrase, SurfaceLayer::Base).action
                  == ActionId::QuantizeHeld, "Phrase+P = QUANT");
        // A held step is not a modifier, so the step-scoped quantize is a LAYER row --
        // which is what finally lets the key frame advertise it during a step hold.
        CHECK(resolveBinding(CB::VerbConfirm, -1, kModNone, SurfaceLayer::StepInspector).action
                  == ActionId::QuantizeHeld, "P while holding a step = QUANT (layer row)");
        CHECK(resolveBinding(CB::VerbConfirm, -1, kModNone, SurfaceLayer::Base).action
                  == ActionId::VerbConfirm, "bare P is still the confirm verb");
        CHECK(resolveBinding(CB::VerbConfirm, -1, kModFunc, SurfaceLayer::Base).action
                  == ActionId::VerbCancel, "Func+P = CANCEL");

        GestureFixture f;
        CHECK(f.action(ActionId::QuantizeHeld, CB::VerbConfirm), "QUANT is wired");
        CHECK(f.effects.quantizes == 1, "QUANT fires exactly once");
    }

    static void scenario_transportActionsMapToEffects()
    {
        using TA = CommandEffects::TransportAction;
        {
            GestureFixture f;
            CHECK(f.action(ActionId::TapTempo, CB::TapTempo), "TAP TEMPO is wired");
            CHECK(f.effects.transportActions == std::vector<TA>{ TA::TapTempo },
                  "the tap routes to the ONE transport path (not a second copy)");
        }
        {
            GestureFixture f;
            CHECK(f.action(ActionId::MetronomeToggle, CB::MetronomeToggle), "metronome is wired");
            CHECK(f.effects.transportActions == std::vector<TA>{ TA::Metronome },
                  "metronome routes to the transport");
        }
        // The TAP key carries both: tap = tempo, hold = the generator hub. Firing the
        // tempo on PRESS would make every hub entry also nudge the tempo, which is why
        // the tap resolves on release.
        CHECK(resolveBinding(CB::TapTempo, -1, kModNone, SurfaceLayer::Base, Gesture::Hold).action
                  == ActionId::OpenGeneratorHub, "TAP + hold = GEN HUB");
        CHECK(resolveBinding(CB::TapTempo, -1, kModNone, SurfaceLayer::Base, Gesture::Tap).action
                  == ActionId::TapTempo, "TAP + tap = TAP TEMPO");
    }

    void runGestureTests()
    {
        scenario_trigCopy();
        scenario_trigPaste();
        scenario_trigClearFull();
        scenario_trigClearPLocks();
        scenario_trigClearNotes();
        scenario_trigCopyNoSteps();
        scenario_trackCopyPaste();
        scenario_trackClear();
        scenario_phraseCopyPaste();
        scenario_songPanic();
        scenario_noneSnapshot();
        scenario_resolveLayerPreservesVelocity();
        scenario_resolveLayerSection();
        scenario_sectionCopyClear();
        scenario_scopeUpUnlatched();
        scenario_scopeUpLatched();
        scenario_muteUpUnlatched();
        scenario_muteGlobalToggle();
        scenario_muteFunc();
        scenario_muteScene();
        scenario_muteMorph();
        scenario_muteBindingResolution();
        scenario_pickerStickyOnRelease();
        scenario_pickerTrackStickyAfterRelease();
        scenario_pickerTrackStickyWhileHeld();
        scenario_pickerStepToConfirm();
        scenario_pickerCancelledByForeignKey();
        scenario_pickerFuncExempt();
        scenario_confirmStickyOnRelease();
        scenario_confirmCancelledByForeignKey();
        scenario_confirmFuncNeverCancels();
        scenario_confirmFuncPCancels();
        scenario_confirmYesExecutes();
        scenario_machineCopyPaste();
        scenario_machinePasteSameMachineDoesNotReload();
        scenario_machinePasteRejectsForeignClipboard();
        scenario_machineInit();
        scenario_deleteHoldArmsPickerPerScope();
        scenario_deleteHoldInertWithoutDeletableScope();
        scenario_trigFuncClearKeepsTrig();
        scenario_navActionsMapToEffects();
        scenario_sectionHoldRowsAreScopeGated();
        scenario_sectionActionsMapToEffects();
        scenario_quantizeRowsAndEffect();
        scenario_transportActionsMapToEffects();
        scenario_transposeQualifiedByFunc();
    }
}
