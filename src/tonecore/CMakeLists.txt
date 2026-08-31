# ---------------------------------------------------------------------------
# tone_core — the shared SoundFont engine behind the `Tone` machine
# (DESIGN §29.3).
#
# JUCE-free, like deck_core: the C++ standard library and FluidLite, nothing
# else. Audio leaves as pointer views over engine-owned buffers, and the bank
# arrives as a caller-owned memory block, so this library cannot reach for
# juce::File or the message manager even by accident.
#
# It is the first engine in the catalogue that is NOT per-track: one instance
# serves every track holding a Tone, with MIDI channel N rendering into audio
# group N and Lockstep assigning channel N to track N.
# ---------------------------------------------------------------------------
add_library(tone_core STATIC
    ToneEngine.cpp
    tone_preset_swap.c)

target_include_directories(tone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# FluidLite, patched, with its public headers -- for ToneEngine.cpp.
target_link_libraries(tone_core PUBLIC chalkwalk::soundfont)

# FluidLite's PRIVATE headers, and the generated fluid_config.h, for
# tone_preset_swap.c ONLY. Kept PRIVATE so no consumer of tone_core can reach
# FluidLite's internals: the coupling is one C file with a written contract
# (see tone_preset_swap.c), not an ambient capability.
#
# The paths used to be spelled out against modules/fluidlite. They are a named
# target now because FluidLite lives in chalkwalk-soundfont, and writing that
# repository's directory layout in here is exactly what breaks the next time
# FluidLite moves -- which is the event chalkwalk-soundfont exists to absorb.
target_link_libraries(tone_core PRIVATE chalkwalk::soundfont-internals)

target_compile_features(tone_core PUBLIC cxx_std_20)
set_target_properties(tone_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # C++ only: tone_preset_swap.c includes FluidLite's own headers, whose
    # warnings are not ours to fix and would trip -Werror.
    target_compile_options(tone_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
            -Werror>
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(tone_core PUBLIC ${LOCKSTEP_SANITIZE_FLAGS})
endif()
