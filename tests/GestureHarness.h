#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include <string>

#include "../src/command/CommandContext.h"
#include "../src/command/CommandCore.h"
#include "../src/command/CommandEffects.h"
#include "../src/command/ButtonLayers.h"
#include "../src/core/Arrangement.h"
#include "../src/io/Clipboard.h"
#include "../src/io/EditContext.h"
#include "../src/io/EditMode.h"
#include "../src/state/UiState.h"

namespace lockstep::test
{
    // RecordingEffects: captures everything CommandCore emits for assertion.
    struct RecordingEffects final : CommandEffects
    {
        std::vector<juce::String> statuses;
        int repaints = 0;
        int transports = 0;   // count of transport calls
        std::vector<CommandEffects::TransportAction> transportActions;
        std::vector<std::string> machineAssigns;  // "track:id"
        std::vector<std::pair<CommandEffects::OverlayId, int>> overlays;
        std::vector<float> crossfaders;

        void status(const juce::String& msg) override { statuses.push_back(msg); }
        void requestRepaint() override { ++repaints; }
        void transport(TransportAction a) override
        {
            ++transports;
            transportActions.push_back(a);
        }
        void machineAssign(int t, const char* id) override { machineAssigns.push_back(std::to_string(t) + ":" + id); }
        void openOverlay(OverlayId id, int p) override { overlays.push_back({ id, p }); }
        void crossfader(float v) override { crossfaders.push_back(v); }
        void releaseLatch(ControllerButton) override {}
        void sceneFloorPaste() override {}
        void sceneFullPaste(int) override {}
        void morphBake(int) override {}
        void morphErase(int) override {}

        struct ConfirmRecord
        {
            ConfirmKind kind;
            int target;
        };
        std::vector<ConfirmRecord> confirmsExecuted;

        std::vector<int> globalMuteTracks;
        std::vector<int> soloTracks;
        std::vector<int> sceneMuteTracks;
        std::vector<int> fluidMuteTracks;

        void executeConfirm(ConfirmKind k, int t) override { confirmsExecuted.push_back({ k, t }); }
        void globalMuteToggle(int t) override { globalMuteTracks.push_back(t); }
        void soloToggle(int t) override { soloTracks.push_back(t); }
        void sceneMuteToggle(int t) override { sceneMuteTracks.push_back(t); }
        void fluidMuteToggle(int t) override { fluidMuteTracks.push_back(t); }
        int captureToggles = 0;
        void toggleCapture() override { ++captureToggles; }

        // 9.12 Stage 6 — the gesture-axis effects.
        std::vector<ControllerButton> latchedModifiers;
        int escapes = 0;
        int restorePops = 0;
        int restoreFloors = 0;
        int overdubArms = 0;
        int stepLatches = 0;
        int navPageUnlocks = 0;
        int generatorHubOpens = 0;
        std::vector<TrigGridMode> trigGridModes;

        std::vector<ControllerButton> scopesEntered;
        void enterScope(ControllerButton cb) override { scopesEntered.push_back(cb); }
        void latchModifier(ControllerButton cb) override { latchedModifiers.push_back(cb); }
        void escapeOverlay() override { ++escapes; }
        void restorePop() override { ++restorePops; }
        void restoreFloor() override { ++restoreFloors; }
        void recordArmOverdub() override { ++overdubArms; }
        void stepLatch(int) override { ++stepLatches; }
        void navPageUnlock() override { ++navPageUnlocks; }
        void openGeneratorHub() override { ++generatorHubOpens; }
        void setTrigGridMode(TrigGridMode m) override { trigGridModes.push_back(m); }

        void reset()
        {
            statuses.clear();
            repaints = 0;
            transports = 0;
            transportActions.clear();
            machineAssigns.clear();
            overlays.clear();
            crossfaders.clear();
            confirmsExecuted.clear();
            globalMuteTracks.clear();
            soloTracks.clear();
            sceneMuteTracks.clear();
            fluidMuteTracks.clear();
        }
    };

    // Minimal IMachineCatalog implementation for gesture tests.
    struct FakeMachineCatalog final : IMachineCatalog
    {
        [[nodiscard]] int numParams(int) const override { return 0; }
        [[nodiscard]] ParamSpec paramSpec(int, int) const override { return {}; }
        [[nodiscard]] SectionInfo section(int, int) const override { return {}; }
        [[nodiscard]] const char* machineId(int) const override { return "lockstep.sample.v1"; }
    };

    // GestureFixture: wires real core model objects + FakeMachineCatalog into
    // CommandContext; drives CommandCore exactly as the live editor does.
    // Heap-allocate big state to avoid stack blowout.
    struct GestureFixture
    {
        std::unique_ptr<Arrangement> arrangement;
        EditContext editContext;
        EditMode editMode;
        UiState uiState;
        Clipboard clipboard;
        SoundPool soundPool;
        FakeMachineCatalog catalog;
        RecordingEffects effects;
        CommandCore core;

        GestureFixture() : arrangement(std::make_unique<Arrangement>()) {}

        CommandContext ctx()
        {
            return {
                *arrangement,
                arrangement->working,
                editContext,
                editMode,
                uiState,
                clipboard,
                soundPool,
                catalog
            };
        }

        bool down(ControllerEvent ev)
        {
            auto c = ctx();
            return core.handleDown(ev, c, effects);
        }

        bool up(ControllerEvent ev)
        {
            auto c = ctx();
            return core.handleUp(ev, c, effects);
        }

        bool verb(EditMode::PrimaryScope scope, ControllerButton v)
        {
            auto c = ctx();
            return core.handleVerb(scope, v, c, effects);
        }

        bool action(ActionId id, ControllerButton btn, int idx = -1)
        {
            ControllerEvent ev{ ControllerEvent::Type::ButtonDown, btn, idx };
            auto c = ctx();
            return core.handleAction(id, ev, c, effects);
        }

        // Convenience: hold a step on a track via EditContext directly.
        void holdStep(int track, int step)
        {
            editContext.hold(track, step);
            editMode.setTrigHeld(true);
        }

        // Convenience: release all held steps.
        void releaseAllSteps()
        {
            editContext.release();
            editMode.setTrigHeld(false);
        }

        // Access the working sequence track for assertions.
        Track& track(int t)
        {
            return arrangement->working.tracks[static_cast<std::size_t>(t)];
        }
    };
}
