// ActionCoverageTest -- ROADMAP 9.12 Stage 6.
//
// Every ActionId must be either (a) handled by CommandCore::handleAction, or
// (b) named in kNotYetMigrated below. There is no third option, and a NEW
// ActionId that is neither is a build-visible failure rather than a key that
// silently does nothing when pressed.
//
// Why a list instead of an exhaustive switch: during the migration (stages 7-8)
// most actions are still served by the imperative branches in dispatchDown, so
// handleAction genuinely does not handle them yet. The list is the honest record
// of that debt -- it shrinks as families migrate, and Stage 8 deletes it entirely
// when handleAction becomes exhaustive over ActionId with no `default:`.

#include "TestHarness.h"
#include "GestureHarness.h"

#include "../src/command/CommandCore.h"
#include "../src/command/KeyBindings.h"

#include <set>

namespace lockstep
{
namespace
{
    // Actions still owned by an imperative branch in PluginEditor::dispatchDown /
    // dispatchUp. Stages 7-8 move these across; every line removed here is a family
    // migrated. Do NOT add to this list to silence a failure on a NEW action --
    // wire the action instead; the list is debt, not an escape hatch.
    const std::set<ActionId> kNotYetMigrated = {
        // (the eight Hold*Scope actions migrated in Stage 7a)
        // (7a modifiers, 7b verbs, 7c nav, 7d sections, 7e confirm/quantize,
        //  7f transport -- all migrated. What is left is NOT debt: every entry below is
        //  an action handleAction correctly DECLINES, and Stage 8 turns this list into
        //  an exhaustive switch that says so in code rather than in a set.)

        // Confirm / Cancel: routed through handleAction (7e), but they DELEGATE to the
        // scope x verb matrix, which declines them in every scope -- a pending prompt is
        // intercepted by handleDown long before an action is ever resolved. "Unhandled"
        // is the correct answer here, not a hole.
        ActionId::VerbConfirm,       ActionId::VerbCancel,

        // Display-only rows. They exist so a key's FRAME can advertise a gesture whose
        // behaviour is owned elsewhere:
        //   TransportTrackCut/MasterCut -- the Play key's multi-tap, owned by the transport.
        //   HoldSceneMuteView           -- Scene+Mute is a VIEW, derived from held state.
        //   HoldMachineScope / FocusGlobal -- the compound scopes (9.29). A modifier press
        //     always resolves on its BARE row (7a: "enter this scope" whatever else is
        //     held), so the compound row advertises, and enterScopeHold sets the state.
        ActionId::TransportTrackCut, ActionId::TransportMasterCut,
        ActionId::HoldSceneMuteView,
        ActionId::HoldMachineScope,  ActionId::FocusGlobal,
    };
}   // namespace

void runActionCoverageTests()
{
    test::GestureFixture fixture;   // heap-allocates Arrangement (~47 MB); see the gotcha

    // Hold a scope. The verb actions (Stage 7b) delegate to the scope x verb matrix,
    // so with NO scope held they correctly decline to act and report as unhandled --
    // a wired verb would look unwired. A scoped verb only means something inside a
    // scope, so that is the state to probe it in.
    fixture.uiState.trackHeld = true;
    fixture.editMode.onScopeEvent(
        ControllerEvent{ ControllerEvent::Type::ButtonDown, ControllerButton::TrackScope, 0, 0 });

    auto ctx = fixture.ctx();

    test::GestureFixture bare;   // no scope held
    auto bareCtx = bare.ctx();

    int handled = 0;
    int pending = 0;
    for (int i = 1; i < static_cast<int>(ActionId::Count); ++i)   // skip None
    {
        const auto action = static_cast<ActionId>(i);

        // Fire each action with the button that actually CARRIES it. A verb action
        // delegates to the scope x verb matrix keyed on ev.button, so a synthetic
        // "Step" event would resolve to no verb and a wired action would report as
        // unwired. The binding table already knows which key carries which action --
        // ask it, rather than inventing an event the surface never produces.
        ControllerEvent ev{ ControllerEvent::Type::ButtonDown, ControllerButton::Step, 0, 0 };
        for (const auto& row : kKeyBindings)
        {
            if (row.action == action)
            {
                ev.button = row.button;
                ev.index = row.index >= 0 ? row.index : 0;
                break;
            }
        }

        // Probe scoped AND unscoped. A verb is only defined in the scopes it means
        // something in -- `VerbSnapshot` lives at no-scope (the Song snapshot), and
        // under Track it is reserved and inert, so it rightly declines. The question
        // this guard asks is "does handleAction KNOW this action", not "did something
        // happen": a reserved combination returning false is the grammar working
        // (PRINCIPLES §2, orthogonality), not a hole.
        const bool wired = fixture.core.handleAction(action, ev, ctx, fixture.effects)
                           || bare.core.handleAction(action, ev, bareCtx, bare.effects);
        const bool listed = kNotYetMigrated.count(action) > 0;

        // The two states are exclusive: an action that is wired AND listed means the
        // list is stale (the debt was paid but not written off), which would quietly
        // hide the next unwired action behind an out-of-date list.
        CHECK(wired != listed,
              juce::String("ActionId ") + juce::String(i)
                  + (wired ? " is handled but still listed as not-yet-migrated "
                             "(remove it from kNotYetMigrated)"
                           : " is neither handled by handleAction nor listed in "
                             "kNotYetMigrated -- wire it, or record the debt"));
        wired ? ++handled : ++pending;
    }

    CHECK(handled > 0, "some actions are wired");
    juce::Logger::writeToLog("ActionCoverage: " + juce::String(handled) + " wired, "
                             + juce::String(pending) + " awaiting migration");
}
}   // namespace lockstep
