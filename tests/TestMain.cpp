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

    const int failed = lockstep::gFailed;
    if (failed == 0)
        juce::Logger::writeToLog("All tests passed.");
    else
        juce::Logger::writeToLog(juce::String(failed) + " test(s) FAILED.");

    juce::shutdownJuce_GUI();
    return failed > 0 ? 1 : 0;
}
