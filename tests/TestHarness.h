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

#define CHECK(cond, msg)                                                                                             \
    do                                                                                                               \
    {                                                                                                                \
        if (!(cond))                                                                                                 \
        {                                                                                                            \
            juce::Logger::writeToLog(juce::String("FAIL [") + __FILE__ ":" + juce::String(__LINE__) + "] " + (msg)); \
            ++lockstep::gFailed;                                                                                     \
        }                                                                                                            \
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
    void runSurfaceLayerTests();
    void runKeyBindingTests();
    void runSerializerRoundTripTests();
    // Phase 8 gesture tests (grows with 8.4b–h)
    void runGestureTests();
    // Phase 8 status text SSOT
    void runStatusTextTests();
    // Phase 8 ParamSpec golden ids + invariants
    void runParamSpecTests();
    // 8.13 Machine DSP smoke + envelope characterization
    void runMachineDspTests();
    // 8.14 Headless processBlock harness
    void runEngineTests();
    // 8.21 Controller-surface shared helpers
    void runControllerTests();
    // 8.26 CaptureRecorder lifecycle
    void runCaptureRecorderTests();
    // MetricGrid primitive (§39 metric-weight foundation)
    void runMetricGridTests();
    // Density overlay (§39)
    void runDensityTests();
    // MetaBand routing SSOT + MetaRotary::applyView totality
    void runMetaBandTests();
    // §39 print-auto-velocity accent generator formula
    void runAccentTests();
    // §39 MetricSelect tier+Euclid deterministic Scrub selector
    void runMetricSelectTests();
    // Stage 1: overlay reducer CUJ event-sequence tests
    void runModeReducerTests();
    // Stage 2: GestureRecognizer unit tests
    void runGestureRecognizerTests();
    // Stage 3: layerBanner() golden tests
    void runLayerBannerTests();
    // Stage 4: FuncReskin enter/exit/cancel CUJs
    void runFuncReskinTests();
    // Stage 5: LatchOps column membership + clearLatchColumnExcept invariants
    void runLatchOpsTests();
    // 9.12: InspectorModel context-region builder (grammar-derived)
    void runInspectorModelTests();
    // 9.13: unified Modal accessor (reducer foundation)
    void runModalStateTests();
    // 9.15: SurfaceDispatcher coalescing contract (one invalidation channel)
    void runSurfaceDispatcherTests();
}
