#pragma once

// Shared harness for the headless lockstep_tests binary.
// Each test translation unit exposes a `void runXxxTests()` entry point and
// uses CHECK(); failures bump the shared counter that main() inspects.

#include <juce_core/juce_core.h>
#include <cmath>

namespace lockstep
{
    // C++17 inline variable — one shared instance across all test TUs.
    inline int gFailed = 0;

    // Exact-ish float compare for test assertions (values are stored literals,
    // but this keeps -Wfloat-equal quiet so real warnings stay visible).
    [[nodiscard]] inline bool feq(float a, float b, float eps = 1e-6f) noexcept
    {
        return std::abs(a - b) < eps;
    }

    #define CHECK(cond, msg) \
        do { \
            if (!(cond)) { \
                juce::Logger::writeToLog(juce::String("FAIL [") + __FILE__ ":" \
                    + juce::String(__LINE__) + "] " + (msg)); \
                ++lockstep::gFailed; \
            } \
        } while (false)

    void runSurfaceModelTests();
    void runStateResolverTests();
    void runHierarchyNavTests();
    void runArrangementTests();
    void runCheckpointTests();
    void runSerializerTests();
    void runAmpDspTests();
    void runEuclideanTests();
    // Phase 8 characterisation tests
    void runEditModeTests();
    void runLayerResolveTests();
    void runSerializerRoundTripTests();
    // Phase 8 gesture tests (grows with 8.4b–h)
    void runGestureTests();
}
