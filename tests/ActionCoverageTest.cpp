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
        ActionId::HoldFuncScope,     ActionId::HoldTrackScope,
        ActionId::HoldPhraseScope,   ActionId::HoldSceneScope,
        ActionId::HoldMorphScope,    ActionId::HoldSongScope,
        ActionId::HoldMuteScope,     ActionId::HoldFillScope,
        ActionId::HoldSceneMuteView,
        ActionId::OpenMachinePicker, ActionId::OpenTrackFxPicker,
        ActionId::OpenMasterFxPicker, ActionId::FocusGlobal,
        ActionId::TapTempo,          ActionId::MetronomeToggle,
        ActionId::NavTrackUp,        ActionId::NavTrackDown,
        ActionId::NavPageLeft,       ActionId::NavPageRight,
        ActionId::NavOctaveUp,       ActionId::NavOctaveDown,
        ActionId::LengthDouble,      ActionId::LengthHalve,
        ActionId::RotateLeft,        ActionId::RotateRight,
        ActionId::CycleInputModeUp,  ActionId::CycleInputModeDown,
        ActionId::CycleInputModeLeft, ActionId::CycleInputModeRight,
        ActionId::MorphPickPoleA,    ActionId::MorphPickPoleB,
        ActionId::SelectSection,     ActionId::SelectMetaSection,
        ActionId::VerbSnapshot,      ActionId::VerbRecord,
        ActionId::VerbPlay,          ActionId::VerbClear,
        ActionId::VerbDelete,        ActionId::VerbConfirm,
        ActionId::VerbCancel,        ActionId::VerbCopy,
        ActionId::VerbPaste,         ActionId::VerbScopedClear,
        ActionId::VerbBakeScene,     ActionId::VerbMorphBake,
        ActionId::VerbMorphErase,    ActionId::QuantizeHeld,
        // Display-only rows: they exist so the key's frame can advertise the
        // gesture; the transport owns the behaviour (see KeyBindings.h).
        ActionId::TransportTrackCut, ActionId::TransportMasterCut,
    };
}   // namespace

void runActionCoverageTests()
{
    test::GestureFixture fixture;   // heap-allocates Arrangement (~47 MB); see the gotcha
    auto ctx = fixture.ctx();

    int handled = 0;
    int pending = 0;
    for (int i = 1; i < static_cast<int>(ActionId::Count); ++i)   // skip None
    {
        const auto action = static_cast<ActionId>(i);
        const ControllerEvent ev{ ControllerEvent::Type::ButtonDown, ControllerButton::Step,
                                  0, 0 };

        const bool wired = fixture.core.handleAction(action, ev, ctx, fixture.effects);
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
