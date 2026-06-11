cmake_minimum_required(VERSION 3.22)

project(Lockstep VERSION 0.0.1)

# C++20 — applies to every target in the tree, including the format-specific
# plugin wrappers (Lockstep_Standalone / _VST3 / _CLAP) that juce_add_plugin
# creates. A per-target PUBLIC cxx_std_20 on lockstep_core does NOT reach those
# wrappers because JUCE compiles their sources independently with its own
# cxx_std_17 floor (see JUCEUtils.cmake). Setting it here guarantees a single
# C++ standard across the whole build so __cpp_char8_t-gated JUCE inlines
# stay consistent across translation units.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Sanitizer support — set LOCKSTEP_SANITIZE=asan or tsan at configure time.
# Applied to lockstep_core and lockstep_tests only (not the plugin wrappers,
# which drag in JUCE GUI code that isn't sanitizer-clean under all hosts).
# Usage:
#   cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOCKSTEP_SANITIZE=asan
#   cmake --build build-asan -j && build-asan/tests/lockstep_tests
# ---------------------------------------------------------------------------
set(LOCKSTEP_SANITIZE "OFF" CACHE STRING "Sanitizer: OFF | asan | tsan")
set_property(CACHE LOCKSTEP_SANITIZE PROPERTY STRINGS OFF asan tsan)

if(LOCKSTEP_SANITIZE STREQUAL "asan")
    set(LOCKSTEP_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(LOCKSTEP_SANITIZE STREQUAL "tsan")
    set(LOCKSTEP_SANITIZE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
else()
    set(LOCKSTEP_SANITIZE_FLAGS "")
endif()

# JUCE
add_subdirectory(JUCE)

# CLAP JUCE extensions
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Sources
add_subdirectory(src)

# Headless unit tests
add_subdirectory(tests)

# Developer tools (probe app, etc.)
add_subdirectory(tools)
