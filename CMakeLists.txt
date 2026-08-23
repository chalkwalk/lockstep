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

# Where JUCE comes from: this repository's submodule, or a shared checkout named
# by CHALKWALK_JUCE_DIR. Included BEFORE the patch block below, because that
# block writes into whichever tree this selects.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/JuceSource.cmake)

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
        WORKING_DIRECTORY "${CHALKWALK_JUCE_ROOT}"
        RESULT_VARIABLE _juce_patch_present
        ERROR_QUIET OUTPUT_QUIET)
    if(NOT _juce_patch_present EQUAL 0)
        message(STATUS "Applying JUCE patch: juce-standalone-onCloseRequested.patch")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${LOCKSTEP_JUCE_PATCH}"
            WORKING_DIRECTORY "${CHALKWALK_JUCE_ROOT}"
            RESULT_VARIABLE _juce_patch_result)
        if(NOT _juce_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply ${LOCKSTEP_JUCE_PATCH} to the JUCE tree at "
                "${CHALKWALK_JUCE_ROOT}. Ensure submodules are initialised (git "
                "submodule update --init --recursive), or that the shared "
                "checkout named by CHALKWALK_JUCE_DIR is clean enough to accept "
                "it.")
        endif()
    else()
        message(STATUS "JUCE patch already applied: juce-standalone-onCloseRequested.patch")
    endif()
endif()

# JUCE. A shared checkout lives outside this source tree, so add_subdirectory
# needs an explicit binary directory; the submodule case is unaffected by it.
add_subdirectory("${CHALKWALK_JUCE_ROOT}" "${CMAKE_BINARY_DIR}/JUCE")

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

# FluidLite — the SoundFont engine behind the `Tone` machine (4.10, DESIGN §29.3).
# LGPL-2.1+ ("or any later version"), so GPLv3-compatible under static linking.
#
# SF3 via the VENDORED stb_vorbis, never libogg/libvorbis: it keeps the
# no-external-dependency property, and the Xiph path in fluid_defsfont.c decodes
# through a FILE-STATIC `vorbisData` struct that is not reentrant. stb's
# stb_vorbis_decode_memory is stateless, so on this path the hazard does not exist.
set(ENABLE_SF3 ON CACHE BOOL "" FORCE)
set(STB_VORBIS ON CACHE BOOL "" FORCE)
set(FLUIDLITE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(FLUIDLITE_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# FluidLite carries an off-by-one that silently loops the WHOLE sample instead
# of the sustain loop for any SF3 sample whose loop runs to the end -- most of
# the bundled bank's piano. It is a submodule, so the one-character fix lives in
# a patch applied here; patches/fluidlite-sf3-loop-offbyone.patch has the full
# diagnosis and the measurements. `git apply --reverse --check` asks "is it
# already applied?", which makes re-configuring a no-op rather than an error.
#
# A patch that stops applying must not fail QUIETLY -- the symptom is a slightly
# wrong sound, not a broken build -- so this is a fatal error here AND covered
# behaviourally by ToneEngineTest's loop assertion, which fails on the audio
# itself if this step is ever skipped or removed.
find_package(Git QUIET)
if(GIT_FOUND)
    set(_fl_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/fluidlite-sf3-loop-offbyone.patch")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_fl_patch}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/fluidlite"
        RESULT_VARIABLE _fl_already OUTPUT_QUIET ERROR_QUIET)
    if(NOT _fl_already EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${_fl_patch}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/fluidlite"
            RESULT_VARIABLE _fl_applied ERROR_VARIABLE _fl_err)
        if(NOT _fl_applied EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply ${_fl_patch}:\n${_fl_err}\n"
                "FluidLite has probably moved. Re-check the SF3 loop-end test in "
                "fluid_defsfont_get_sample before dropping the patch.")
        endif()
        message(STATUS "Applied FluidLite SF3 loop-end patch")
    endif()
else()
    message(WARNING "Git not found -- the FluidLite SF3 loop patch was NOT applied; "
                    "looped SF3 instruments will sound wrong (ToneEngineTest will fail).")
endif()

add_subdirectory(modules/fluidlite EXCLUDE_FROM_ALL)
set_target_properties(fluidlite-static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Vendored DSP stays optimized even in Debug builds: we never step through it,
# and Bungee's Eigen internals collapse ~60x at -O0 (2.7x realtime per stereo
# stream vs 62x at -O2 — measured, S6 stretch spike). Debug's -g and asserts
# are unaffected; only these third-party targets gain -O2. Extend this list if
# another separately-compiled third-party DSP library is vendored. (JUCE and
# header-only libraries compile inside first-party targets and are not covered.)
if(NOT MSVC)
    foreach(third_party_target bungee_library pffft fluidlite-static)
        target_compile_options(${third_party_target} PRIVATE $<$<CONFIG:Debug>:-O2>)
    endforeach()
endif()

# Sources
add_subdirectory(src)

# Headless unit tests
add_subdirectory(tests)

# Developer tools (probe app, etc.)
add_subdirectory(tools)
