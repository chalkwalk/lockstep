// Headless unit-test entry point. Aggregates every runXxxTests() translation
// unit and reports a single pass/fail. Run via: lockstep_tests (0 = pass).

#include "TestHarness.h"
#include <juce_events/juce_events.h>   // initialiseJuce_GUI / shutdownJuce_GUI

int main()
{
    // JUCE needs a minimal init for juce::String in some configurations.
    juce::initialiseJuce_GUI();

    lockstep::runSurfaceModelTests();
    lockstep::runSurfaceInvalidationGuardTests();
    lockstep::runChromeStyleGuardTests();
    lockstep::runSamplePoolTests();
    lockstep::runTempoEstimateTests();
    lockstep::runKeyEstimateTests();
    lockstep::runSampleHintsTests();
    lockstep::runSyncSliceTests();
    lockstep::runStretchEngineTests();
    lockstep::runStretchMachineTests();
    lockstep::runClockLocateTests();
    lockstep::runMediumTests();
    lockstep::runHeadsTests();
    lockstep::runLayerStackTests();
    lockstep::runSeamTests();
    lockstep::runMarkerLaneTests();
    lockstep::runTakeSheetTests();
    lockstep::runTakePickerTests();
    lockstep::runDeckTests();
    lockstep::runDeckAdapterTests();
    lockstep::runResamplerTests();
    lockstep::runSamplePlayerTests();
    lockstep::runRecordMachineTests();
    lockstep::runLoopMachineTests();
    lockstep::runTapeMachineTests();
    lockstep::runStreamMachineTests();
    lockstep::runStateResolverTests();
    lockstep::runHierarchyNavTests();
    lockstep::runArrangementTests();
    lockstep::runCheckpointTests();
    lockstep::runSerializerTests();
    lockstep::runAmpDspTests();
    lockstep::runEuclideanTests();
    lockstep::runNameGenTests();
    lockstep::runScaleTests();
    lockstep::runMelodyGenTests();
    lockstep::runHarmonyGenTests();
    lockstep::runCaptureControllerTests();
    lockstep::runMotionRecorderTests();
    // Phase 8 characterisation tests
    lockstep::runEditModeTests();
    lockstep::runLayerResolveTests();
    lockstep::runSurfaceLayerTests();
    lockstep::runKeyBindingTests();
    lockstep::runSerializerRoundTripTests();
    lockstep::runSampleIdRoundTripTests();
    // Part 2 multi-step holds
    lockstep::runMultiStepHoldTests();
    // Phase 8 gesture tests
    lockstep::runGestureTests();
    lockstep::runStatusTextTests();
    lockstep::runParamSpecTests();
    // 8.13 Machine DSP smoke + envelope characterization
    lockstep::runMachineDspTests();
    // 8.14 Headless processBlock harness
    lockstep::runEngineTests();
    // v27 transport AND-gate + deterministic restart re-floor
    lockstep::runTransportGateTests();
    // 9.31 iteration rule (m:n) properties
    lockstep::runTrigConditionTests();
    // 9.31 meter tick + master drag mapping
    lockstep::runMeterMathTests();
    // Serializer upgrade-chain guard (ported off the plugin load path)
    lockstep::runPluginStateUpgradeTests();
    // 8.21 Controller-surface shared helpers
    lockstep::runControllerTests();
    // 8.26 CaptureRecorder lifecycle
    lockstep::runCaptureRecorderTests();
    // MetricGrid primitive (§39 metric-weight foundation)
    lockstep::runMetricGridTests();
    // Density overlay (§39)
    lockstep::runDensityTests();
    // MetaBand routing SSOT + MetaRotary::applyView totality
    lockstep::runMetaBandTests();
    // §39 print-auto-velocity accent generator
    lockstep::runAccentTests();
    // §39 MetricSelect tier+Euclid deterministic Scrub
    lockstep::runMetricSelectTests();
    // Stage 1: overlay reducer CUJs + presentation label goldens
    lockstep::runModeReducerTests();
    // Stage 2: GestureRecognizer timing invariants
    lockstep::runGestureRecognizerTests();
    // Stage 3: layerBanner() presentation goldens
    lockstep::runLayerBannerTests();
    // Stage 4: FuncReskin enter/exit/cancel CUJs
    lockstep::runFuncReskinTests();
    // Stage 5: LatchOps column membership + clearLatchColumnExcept invariants
    lockstep::runLatchOpsTests();
    // 9.12: InspectorModel context-region builder (grammar-derived)
    lockstep::runInspectorModelTests();
    lockstep::runTimelineModelTests();
    // 9.13: unified Modal accessor (reducer foundation)
    lockstep::runModalStateTests();
    // 9.15: SurfaceDispatcher coalescing (one surface-invalidation channel)
    lockstep::runSurfaceDispatcherTests();
    lockstep::runInputSourceTests();
    lockstep::runRoutingGraphTests();
    // P6: scope-aware section page-list selection (grab-bag items 6+7)
    lockstep::runScopeSectionSelectTests();
    lockstep::runSectionStackTests();
    lockstep::runSectionResolveTests();
    // 9.17: LaunchQuant authority (grid periods, boundary maths, legacy map)
    lockstep::runLaunchQuantTests();
    // S7: FreeLen fit target (round up to launch-quant multiple + jitter tolerance)
    lockstep::runLoopFitTests();
    // 6.4: per-track cue balance overlay (DESIGN §31)
    lockstep::runCueBalanceTests();

    // Also drive the JUCE UnitTests registered in lockstep_core (the serializer
    // upgrade chain, "PluginState" category) so they run headlessly in CI rather
    // than only at standalone construction.
    {
        juce::UnitTestRunner runner;
        runner.setAssertOnFailure(false);
        runner.runTestsInCategory("PluginState");
        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            const auto* r = runner.getResult(i);
            if (r != nullptr && r->failures > 0)
            {
                juce::Logger::writeToLog("FAIL [PluginState] " + r->unitTestName + " / " + r->subcategoryName + ": " + juce::String(r->failures) + " failure(s)");
                lockstep::gFailed += r->failures;
            }
        }
    }

    const int failed = lockstep::gFailed;
    if (failed == 0)
        juce::Logger::writeToLog("All tests passed.");
    else
        juce::Logger::writeToLog(juce::String(failed) + " test(s) FAILED.");

    juce::shutdownJuce_GUI();
    return failed > 0 ? 1 : 0;
}
