#include "TestHarness.h"
#include "GestureHarness.h"
#include "../src/command/ButtonLayers.h"

namespace lockstep
{
    using namespace test;
    using CB = ControllerButton;
    using PS = EditMode::PrimaryScope;
    using T  = ControllerEvent::Type;

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

    static void scenario_trigClearPLocks()
    {
        GestureFixture f;
        auto& s7 = f.track(0).steps[7];
        s7.trig = true;
        s7.overrides.set(0, 0.5f);

        f.editMode.onScopeEvent({ T::ButtonDown, CB::Func, -1, 0 });
        f.holdStep(0, 7);

        const bool handled = f.verb(PS::Trig, CB::VerbClear);
        CHECK(handled, "Trig+Func+Clear should be handled");
        CHECK(f.track(0).steps[7].trig, "trig intact after P-Lock clear");
        CHECK(!f.track(0).steps[7].overrides.has(0), "P-Lock cleared");
    }

    // Scenario 5: PS::Trig / VerbNo with Func held → clear note/vel/gate overrides

    static void scenario_trigClearNotes()
    {
        GestureFixture f;
        auto& s1 = f.track(0).steps[1];
        s1.trig = true;
        s1.trigOverride.noteCount  = 2;
        s1.trigOverride.hasVelocity = true;
        s1.trigOverride.velocity   = 80;

        f.editMode.onScopeEvent({ T::ButtonDown, CB::Func, -1, 0 });
        f.holdStep(0, 1);

        const bool handled = f.verb(PS::Trig, CB::VerbNo);
        CHECK(handled, "Trig+Func+No should be handled");
        CHECK(f.track(0).steps[1].trigOverride.noteCount == 0, "noteCount cleared");
        CHECK(!f.track(0).steps[1].trigOverride.hasVelocity, "hasVelocity cleared");
        CHECK(f.track(0).steps[1].trig, "trig still set");
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

    // Scenario 8: PS::Track / VerbClear clears steps

    static void scenario_trackClear()
    {
        GestureFixture f;
        f.uiState.activeTrack = 2;
        f.track(2).steps[0].trig = true;
        f.track(2).steps[1].trig = true;

        const bool handled = f.verb(PS::Track, CB::VerbClear);
        CHECK(handled, "Track+Clear handled");
        CHECK(!f.track(2).steps[0].trig, "step 0 cleared");
        CHECK(!f.track(2).steps[1].trig, "step 1 cleared");
    }

    // Scenario 9: PS::Phrase / VerbRecord and VerbPlay round-trip

    static void scenario_phraseCopyPaste()
    {
        GestureFixture f;
        f.track(3).steps[8].trig = true;

        bool handled = f.verb(PS::Phrase, CB::VerbRecord);
        CHECK(handled, "Phrase+Record handled");
        CHECK(f.clipboard.type == ClipboardType::Pattern, "clipboard type Pattern");

        // Mutate.
        f.track(3).steps[8].trig = false;
        handled = f.verb(PS::Phrase, CB::VerbPlay);
        CHECK(handled, "Phrase+Play handled");
        CHECK(f.track(3).steps[8].trig, "step restored after paste");
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

    // Scenario 11: PS::None / VerbYes → Song-scope snapshot

    static void scenario_noneSnapshot()
    {
        GestureFixture f;
        // No scope held, so primaryScope() == None.
        const bool handled = f.verb(PS::None, CB::VerbYes);
        CHECK(handled, "None+VerbYes handled (snapshot)");
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
        f.uiState.activeTrack     = 0;
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

        ControllerEvent ev { ControllerEvent::Type::ButtonUp, CB::TrackScope };
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

        ControllerEvent ev { ControllerEvent::Type::ButtonUp, CB::TrackScope };
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

        ControllerEvent ev { ControllerEvent::Type::ButtonUp, CB::MuteScope };
        auto c = f.ctx();
        [[maybe_unused]] const bool h = f.core.handleUp(ev, c, f.effects);

        CHECK(!f.uiState.muteHeld, "muteHeld cleared");
        CHECK(f.effects.repaints == 1, "repaint requested");
    }

    // ── KeyBindings mute resolution golden tests ─────────────────────────────

    static void scenario_muteBindingResolution()
    {
        using AId = ActionId;
        using SL  = SurfaceLayer;

        // Bare Mute+step → GlobalMuteToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModMute, SL::MuteView).action
              == AId::GlobalMuteToggle, "bare Mute → GlobalMuteToggle");

        // Func+Mute+step → SoloToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModFunc | kModMute, SL::MuteView).action
              == AId::SoloToggle, "Func+Mute → SoloToggle");

        // Scene+Mute+step → SceneMuteToggle in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModScene | kModMute, SL::MuteView).action
              == AId::SceneMuteToggle, "Scene+Mute → SceneMuteToggle");

        // Morph+Mute+step → FluidMuteToggle in MorphMuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModMorph | kModMute, SL::MorphMuteView).action
              == AId::FluidMuteToggle, "Morph+Mute → FluidMuteToggle");

        // Func+Mute wins over bare Mute (popcount 2 > 1) in MuteView
        CHECK(resolveBinding(CB::ToggleMute, 0, kModFunc | kModMute, SL::MuteView).action
              != AId::GlobalMuteToggle, "Func+Mute does NOT resolve to GlobalMuteToggle");
    }

    // -------------------------------------------------------------------------
    // Test runner

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
        CHECK(handled,                           "foreign press swallowed while confirm pending");
        CHECK(!f.uiState.confirm.pending(),      "confirm cleared after foreign press");
        CHECK(f.effects.confirmsExecuted.empty(),"no execution on cancel");
        CHECK(!f.effects.statuses.empty(),       "cancelled status emitted");
        CHECK(f.effects.statuses.back() == "Cancelled", "status text = Cancelled");
    }

    // Pending → Func press → NOT cancelled (Func never cancels).
    static void scenario_confirmFuncNeverCancels()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::Func, -1 });
        CHECK(!handled,                     "Func not swallowed — still pending");
        CHECK(f.uiState.confirm.pending(), "confirm survives Func press");
    }

    // Pending → Func held → VerbNo (P) → cancel (No).
    static void scenario_confirmFuncPCancels()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeletePhrase);
        f.uiState.funcHeld = true;  // simulate Func held

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::VerbNo, -1 });
        CHECK(handled,                           "Func+P swallowed");
        CHECK(!f.uiState.confirm.pending(),      "confirm cleared");
        CHECK(f.effects.confirmsExecuted.empty(),"executeConfirm NOT called on No");
        CHECK(!f.effects.statuses.empty(),       "cancelled status emitted");
    }

    // Pending → P (no Func) → executed with correct kind+target.
    static void scenario_confirmYesExecutes()
    {
        GestureFixture f;
        armConfirm(f, ConfirmKind::DeleteTrack, 5);
        f.uiState.funcHeld = false;

        const bool handled = f.down({ ControllerEvent::Type::ButtonDown, CB::VerbNo, -1 });
        CHECK(handled,                            "P swallowed");
        CHECK(!f.uiState.confirm.pending(),       "confirm cleared");
        CHECK(f.effects.confirmsExecuted.size() == 1, "executeConfirm called once");
        CHECK(f.effects.confirmsExecuted[0].kind   == ConfirmKind::DeleteTrack, "correct kind");
        CHECK(f.effects.confirmsExecuted[0].target == 5,                        "correct target");
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
        scenario_confirmStickyOnRelease();
        scenario_confirmCancelledByForeignKey();
        scenario_confirmFuncNeverCancels();
        scenario_confirmFuncPCancels();
        scenario_confirmYesExecutes();
    }
}
