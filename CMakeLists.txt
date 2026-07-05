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

# ---------------------------------------------------------------------------
# Vendored-JUCE patch (see docs/PATCHES.md).
# The standalone quit guard (9.2/D2) needs an `onCloseRequested` hook in
# juce_StandaloneFilterWindow.h. Rather than carry a forked JUCE submodule
# commit — a fresh clone can't fetch that SHA from upstream juce-framework/JUCE
# — the submodule stays pinned to a real upstream commit and we apply the patch
# here at configure time. Idempotent: a second configure (patch already in the
# working tree) is a no-op, so re-running cmake never double-applies or errors.
# ---------------------------------------------------------------------------
set(LOCKSTEP_JUCE_PATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-standalone-onCloseRequested.patch")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${LOCKSTEP_JUCE_PATCH}")
    # `git apply --reverse --check` succeeds iff the patch is already present in
    # the working tree; non-zero means it still needs applying.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${LOCKSTEP_JUCE_PATCH}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/JUCE"
        RESULT_VARIABLE _juce_patch_present
        ERROR_QUIET OUTPUT_QUIET)
    if(NOT _juce_patch_present EQUAL 0)
        message(STATUS "Applying JUCE patch: juce-standalone-onCloseRequested.patch")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${LOCKSTEP_JUCE_PATCH}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/JUCE"
            RESULT_VARIABLE _juce_patch_result)
        if(NOT _juce_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply ${LOCKSTEP_JUCE_PATCH} to the JUCE submodule. "
                "Ensure submodules are initialised (git submodule update --init "
                "--recursive) and the JUCE working tree is clean.")
        endif()
    else()
        message(STATUS "JUCE patch already applied: juce-standalone-onCloseRequested.patch")
    endif()
endif()

# JUCE
add_subdirectory(JUCE)

# CLAP JUCE extensions
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Bungee real-time time/pitch stretch engine (9.23). MPL-2.0; vendors Eigen
# (MPL-2.0) + PFFFT (BSD-like) as its own submodules — all GPL-compatible.
# Build it static (we fold it into lockstep_core) rather than the default shared.
set(BUNGEE_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/bungee EXCLUDE_FROM_ALL)
# lockstep_core is POSITION_INDEPENDENT_CODE ON and folds into the plugin shared
# objects, so its static dependencies must be PIC too. Bungee only forces PIC on
# itself when building shared, so set it here for both static archives.
set_target_properties(bungee_library pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Sources
add_subdirectory(src)

# Headless unit tests
add_subdirectory(tests)

# Developer tools (probe app, etc.)
add_subdirectory(tools)
