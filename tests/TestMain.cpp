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
    lockstep::runSerializerTests();

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
                juce::Logger::writeToLog("FAIL [PluginState] " + r->unitTestName
                    + " / " + r->subcategoryName + ": "
                    + juce::String(r->failures) + " failure(s)");
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
