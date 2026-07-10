# Headless unit tests — no audio device, no plugin host required.
# Tests pure functions (resolveKeyLabel, StateResolver OEB merge, the
# HierarchyNav Phrase/Kit projection seam) from the core/model layer.

add_executable(lockstep_tests
    TestMain.cpp
    TestEditorStub.cpp
    SamplePoolTest.cpp
    TempoEstimateTest.cpp
    KeyEstimateTest.cpp
    SampleHintsTest.cpp
    SyncSliceTest.cpp
    StretchEngineTest.cpp
    StretchMachineTest.cpp
    ClockLocateTest.cpp
    MediumTest.cpp
    HeadsTest.cpp
    LayerStackTest.cpp
    DeckTest.cpp
    DeckAdapterTest.cpp
    ResamplerTest.cpp
    SamplePlayerTest.cpp
    RecordMachineTest.cpp
    LoopMachineTest.cpp
    StreamMachineTest.cpp
    MetricGridTest.cpp
    SurfaceModelTest.cpp
    StateResolverTest.cpp
    HierarchyNavTest.cpp
    ArrangementTest.cpp
    CheckpointTest.cpp
    SerializerTest.cpp
    AmpDspTest.cpp
    EuclideanTest.cpp
    ScaleTest.cpp
    MelodyGenTest.cpp
    HarmonyGenTest.cpp
    CaptureControllerTest.cpp
    MotionRecorderTest.cpp
    EditModeTest.cpp
    LayerResolveTest.cpp
    SurfaceLayerTest.cpp
    KeyBindingTest.cpp
    SerializerRoundTripTest.cpp
    SampleIdRoundTripTest.cpp
    MultiStepHoldTest.cpp
    TransportGateTest.cpp
    PluginStateUpgradeTest.cpp
    GestureTest.cpp
    StatusTextTest.cpp
    ParamSpecTest.cpp
    MachineDspTest.cpp
    EngineTest.cpp
    ControllerTest.cpp
    CaptureRecorderTest.cpp
    DensityTest.cpp
    MetaBandTest.cpp
    AccentTest.cpp
    MetricSelectTest.cpp
    ModeReducerTest.cpp
    GestureRecognizerTest.cpp
    LayerBannerTest.cpp
    FuncReskinTest.cpp
    LatchOpsTest.cpp
    InspectorModelTest.cpp
    ModalStateTest.cpp
    SurfaceDispatcherTest.cpp
    InputSourceTest.cpp
    RoutingGraphTest.cpp
    ScopeSectionSelectTest.cpp
    SectionStackTest.cpp
    SectionResolveTest.cpp
    LaunchQuantTest.cpp
    # UI modules needed by MetaBandTest + ModeReducerTest + SurfaceModelTest
    # (live only in the plugin target otherwise)
    ../src/ui/MetaBand.cpp
    ../src/ui/MetaRotary.cpp
    ../src/ui/SurfaceModel.cpp
    ../src/ui/SectionResolve.cpp
    ../src/ui/InspectorModel.cpp
    ../src/ui/mode/ModeReducer.cpp
    ../src/ui/mode/FuncReskin.cpp
    ../src/ui/mode/ModalState.cpp
    ../src/ui/mode/LatchOps.cpp)

target_link_libraries(lockstep_tests PRIVATE
    lockstep_engine
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_data_structures)

# Inherit the project's strict warning flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lockstep_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
        -Werror
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(lockstep_tests PRIVATE ${LOCKSTEP_SANITIZE_FLAGS})
else()
    target_link_libraries(lockstep_tests PUBLIC juce::juce_recommended_warning_flags)
endif()

enable_testing()
add_test(NAME SurfaceModelTest COMMAND lockstep_tests)
