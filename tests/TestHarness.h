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
    void runSamplePoolTests();   // 6.2 volatile REC buffers (DESIGN §28)
    void runTempoEstimateTests(); // WI-4 energy-based BPM detection (DESIGN §28)
    void runKeyEstimateTests();   // 4.9 key + tuning detection (DESIGN §28)
    void runSampleHintsTests();   // 4.9 filename/ACID hint parsing + fusion
    void runSyncSliceTests();     // 4.9 beat-grid slicing (placeSyncSlices)
    void runStretchEngineTests(); // 9.23 Bungee IStretchEngine seam (pull model)
    void runStretchMachineTests(); // C3 Flex-analog Player (independent pitch+tempo)
    void runClockLocateTests();    // 11.1 transport absolute position + locate
    void runMediumTests();         // 11.7a dc::Medium — topology, depth, high-water
    void runHeadsTests();          // 11.7a dc::ReadHead/WriteHead + the tape-delay proof
    void runLayerStackTests();     // 11.7b dc::LayerStack — overdub, undo, decay, punch
    void runSeamTests();           // C6 dc::spliceLoopEnd — the loop splice
    void runMarkerLaneTests();     // 11.4 dc::MarkerLane — navigation markers
    void runDeckTests();           // 11.7b dc::Deck — state machine + quantized edges
    void runDeckAdapterTests();    // 11.2 deck_juce — buffer/transport glue
    void runResamplerTests();      // 9.25 R1 shared bandlimited resampler
    void runSamplePlayerTests();   // C1 loop-seam crossfade (borrow-tail / eat-in)
    void runRecordMachineTests();  // 6.2 RecordMachine capture (DESIGN §29.2/§30)
    void runLoopMachineTests();    // 6.3 LoopMachine state machine (DESIGN §29.2)
    void runTapeMachineTests();    // 11.4 TapeMachine — position-slaved reel (§40.2)
    void runStreamMachineTests();    // 4.5 StreamMachine disk streaming (DESIGN §29.2)
    void runStateResolverTests();
    void runHierarchyNavTests();
    void runArrangementTests();
    void runCheckpointTests();
    void runSerializerTests();
    void runAmpDspTests();
    void runEuclideanTests();
    // Phase 10.2: tonal core (KeySig, brightness, modifiers, quantize)
    void runScaleTests();
    // Phase 10.7: melodic generator (deterministic print model)
    void runMelodyGenTests();
    // Phase 10.8: harmonic voice-mover (scale-constrained chord buffer)
    void runHarmonyGenTests();
    // Performance capture: tape-deck state machine (CaptureController)
    void runCaptureControllerTests();
    void runMotionRecorderTests();
    // Phase 8 characterisation tests
    void runEditModeTests();
    void runLayerResolveTests();
    void runSurfaceLayerTests();
    void runKeyBindingTests();
    void runSerializerRoundTripTests();
    void runSampleIdRoundTripTests();  // 9.18 pool identity: promote + hash round-trip
    // Part 2 multi-step holds (block move, EditContext, relative P-Lock fan-out)
    void runMultiStepHoldTests();
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
    // v27 hosted-Locked transport AND-gate + restart re-floor
    void runTransportGateTests();
    // Serializer upgrade-chain guard (ported off the plugin load path)
    void runPluginStateUpgradeTests();
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
    void runTimelineModelTests();
    void runTimelineModelTests();  // 11.5 tape timeline strip model (§40.6)
    // 9.13: unified Modal accessor (reducer foundation)
    void runModalStateTests();
    // 9.15: SurfaceDispatcher coalescing contract (one invalidation channel)
    void runSurfaceDispatcherTests();
    // 6.1: input_source slot decode (audio routing, DESIGN §27)
    void runInputSourceTests();
    // A2: output-routing topological order + cycle refusal (DESIGN §27)
    void runRoutingGraphTests();
    // P6: scope-aware section page-list selection (grab-bag items 6+7)
    void runScopeSectionSelectTests();
    // Item 7: static section-stack table (meta/sticky chords)
    void runSectionStackTests();
    // Item 7: the section-key resolver (schema + stack, scope peel)
    void runSectionResolveTests();
    // 9.17: LaunchQuant authority — grid periods, boundary maths, legacy map
    void runLaunchQuantTests();
}
