// Headless unit-test entry point. Aggregates every runXxxTests() translation
// unit and reports a single pass/fail. Run via: lockstep_tests (0 = pass).

#include "TestHarness.h"
#include <juce_events/juce_events.h>   // initialiseJuce_GUI / shutdownJuce_GUI

int main()
{
    // JUCE needs a minimal init for juce::String in some configurations.
    juce::initialiseJuce_GUI();

    lockstep::runSurfaceModelTests();
    lockstep::runStateResolverTests();
    lockstep::runHierarchyNavTests();
    lockstep::runArrangementTests();
    lockstep::runCheckpointTests();
    lockstep::runSerializerTests();
    lockstep::runAmpDspTests();
    lockstep::runEuclideanTests();
    // Phase 8 characterisation tests
    lockstep::runEditModeTests();
    lockstep::runLayerResolveTests();
    lockstep::runSurfaceLayerTests();
    lockstep::runKeyBindingTests();
    lockstep::runSerializerRoundTripTests();
    // Phase 8 gesture tests
    lockstep::runGestureTests();
    lockstep::runStatusTextTests();
    lockstep::runParamSpecTests();
    // 8.13 Machine DSP smoke + envelope characterization
    lockstep::runMachineDspTests();
    // 8.14 Headless processBlock harness
    lockstep::runEngineTests();
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
