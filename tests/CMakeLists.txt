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
    SeamTest.cpp
    MarkerLaneTest.cpp
    TakeSheetTest.cpp
    TakePickerTest.cpp
    DeckTest.cpp
    DeckAdapterTest.cpp
    ResamplerTest.cpp
    SamplePlayerTest.cpp
    RecordMachineTest.cpp
    LoopMachineTest.cpp
    TapeMachineTest.cpp
    StreamMachineTest.cpp
    MetricGridTest.cpp
    SurfaceModelTest.cpp
    SurfaceInvalidationGuardTest.cpp
    ClockFunnelGuardTest.cpp
    ChromeStyleGuardTest.cpp
    StateResolverTest.cpp
    HierarchyNavTest.cpp
    ArrangementTest.cpp
    CheckpointTest.cpp
    SerializerTest.cpp
    AmpDspTest.cpp
    EuclideanTest.cpp
    NameGenTest.cpp
    PhraseShareTest.cpp
    PhraseOpsTest.cpp
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
    TrigConditionTest.cpp
    MeterMathTest.cpp
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
    TimelineModelTest.cpp
    ModalStateTest.cpp
    SurfaceDispatcherTest.cpp
    InputSourceTest.cpp
    RoutingGraphTest.cpp
    ScopeSectionSelectTest.cpp
    SectionStackTest.cpp
    SectionResolveTest.cpp
    LaunchQuantTest.cpp
    LoopFitTest.cpp
    CueBalanceTest.cpp
    # UI modules needed by MetaBandTest + ModeReducerTest + SurfaceModelTest
    # (live only in the plugin target otherwise)
    ../src/ui/MetaBand.cpp
    ../src/ui/MetaRotary.cpp
    ../src/ui/NameGen.cpp
    ../src/ui/SurfaceModel.cpp
    ../src/ui/SectionResolve.cpp
    ../src/ui/InspectorModel.cpp
    ../src/ui/TimelineModel.cpp
    ../src/ui/mode/ModeReducer.cpp
    ../src/ui/mode/FuncReskin.cpp
    ../src/ui/mode/ModalState.cpp
    ../src/ui/mode/LatchOps.cpp)

target_link_libraries(lockstep_tests PRIVATE
    lockstep_engine
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_data_structures)

# SurfaceInvalidationGuardTest scans the surface-owning sources for unrouted
# repaint() calls (PRINCIPLES §22), so it needs the source tree's location.
target_compile_definitions(lockstep_tests PRIVATE
    LOCKSTEP_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src")

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

# ---------------------------------------------------------------------------
# lockstep_dispatch_tests (9.12 Stage 5): the dispatch golden net. Drives a real
# headless LockstepEditor, so it needs the editor + UI sources that the main test
# binary deliberately stubs out (TestEditorStub). Kept a SEPARATE binary so the
# fast core suite does not pay for compiling the UI.
# ---------------------------------------------------------------------------
add_executable(lockstep_dispatch_tests
    DispatchGoldenTest.cpp
    UiDriverSelfTest.cpp
    SyntheticKeyboardTest.cpp
    SyntheticMouseTest.cpp
    ProductTypefaceTest.cpp
    SurfaceRenderTest.cpp
    RegionOracleTest.cpp
    RepaintRegionTest.cpp
    MzDriveTest.cpp
    SceneGoldenTest.cpp
    CujTrigAuthoringTest.cpp
    CujGeneratorsTest.cpp
    CujRecordTest.cpp
    CujClipboardTest.cpp
    CujSoundTest.cpp
    CujPerformanceTest.cpp
    ../src/PluginEditorFactory.cpp   # the real createEditor() (not the test stub)
    ../src/PluginEditor.cpp
    ../src/ui/ChromeLookAndFeel.cpp
    ../src/ui/KeyButton.cpp
    ../src/ui/KeyboardArea.cpp
    ../src/ui/MetaBand.cpp
    ../src/ui/MetaRotary.cpp
    ../src/ui/NameGen.cpp
    ../src/ui/SurfaceModel.cpp
    ../src/ui/SectionResolve.cpp
    ../src/ui/InspectorModel.cpp
    ../src/ui/TimelineModel.cpp
    ../src/ui/InspectorBar.cpp
    ../src/ui/TimelineStrip.cpp
    ../src/ui/InPluginTransport.cpp
    ../src/ui/ManipulationZone.cpp
    ../src/ui/SamplePoolOverlay.cpp
    ../src/ui/SoundBankOverlay.cpp
    ../src/ui/StandaloneFileBar.cpp
    ../src/ui/mode/ModeReducer.cpp
    ../src/ui/mode/FuncReskin.cpp
    ../src/ui/mode/ModalState.cpp
    ../src/ui/mode/LatchOps.cpp)

target_link_libraries(lockstep_dispatch_tests PRIVATE
    lockstep_engine
    lockstep_fonts
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_data_structures)

# runDispatchLoopUntil() (used to deliver the SurfaceDispatcher's async frame) is
# compiled out unless modal loops are permitted.
target_compile_definitions(lockstep_dispatch_tests PRIVATE
    LOCKSTEP_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME DispatchGoldenTest COMMAND lockstep_dispatch_tests)
