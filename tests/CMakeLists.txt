# Headless unit tests — no audio device, no plugin host required.
# Tests pure functions (resolveKeyLabel, StateResolver OEB merge, the
# HierarchyNav Phrase/Kit projection seam) from the core/model layer.

add_executable(lockstep_tests
    TestMain.cpp
    SurfaceModelTest.cpp
    StateResolverTest.cpp
    HierarchyNavTest.cpp
    ArrangementTest.cpp
    CheckpointTest.cpp
    SerializerTest.cpp
    AmpDspTest.cpp
    EuclideanTest.cpp
    EditModeTest.cpp
    LayerResolveTest.cpp
    SurfaceLayerTest.cpp
    KeyBindingTest.cpp
    SerializerRoundTripTest.cpp
    GestureTest.cpp
    StatusTextTest.cpp
    ParamSpecTest.cpp)

target_link_libraries(lockstep_tests PRIVATE
    lockstep_core
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_data_structures)

# Inherit the project's strict warning flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lockstep_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(lockstep_tests PRIVATE ${LOCKSTEP_SANITIZE_FLAGS})
else()
    target_link_libraries(lockstep_tests PUBLIC juce::juce_recommended_warning_flags)
endif()

enable_testing()
add_test(NAME SurfaceModelTest COMMAND lockstep_tests)
