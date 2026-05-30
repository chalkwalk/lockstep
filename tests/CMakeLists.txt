# Headless unit tests — no audio device, no plugin host required.
# Tests pure functions (resolveKeyLabel, etc.) from the UI/model layer.

add_executable(lockstep_tests SurfaceModelTest.cpp)

target_link_libraries(lockstep_tests PRIVATE
    lockstep_core
    juce::juce_gui_basics
    juce::juce_core)

# Inherit the project's strict warning flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lockstep_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(lockstep_tests PUBLIC juce::juce_recommended_warning_flags)
endif()

enable_testing()
add_test(NAME SurfaceModelTest COMMAND lockstep_tests)
