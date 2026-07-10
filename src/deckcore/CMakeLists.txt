# ---------------------------------------------------------------------------
# deck_core — the JUCE-free deck engine (DESIGN §40.11).
#
# Dependencies: the C++ standard library and the vendored header-only
# signalsmith-dsp. Nothing else, ever. Audio crosses the boundary as pointer
# views over caller-owned channels, transport arrives as a POD snapshot, and
# the medium is non-owning — so this library cannot reach for juce::File, the
# message manager, or an allocator on the audio thread even by accident. The
# seam is enforced by the dependency list, not by discipline.
#
# The payoff is licence independence (JUCE's dual AGPL/commercial terms never
# attach to our own DSP), pure tests that need no plugin host, and a partner
# app (docs/partner-app-concept.md) that consumes the tape without consuming
# the sequencer.
#
# ---------------------------------------------------------------------------
add_library(deck_core STATIC
    Medium.cpp
    LayerStack.cpp
    Deck.cpp)

target_include_directories(deck_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# signalsmith-dsp (MIT, header-only). SYSTEM so its warnings never trip the
# consumers' -Werror. Rooted at modules/ so headers namespace as
# <signalsmith-dsp/...>.
target_include_directories(deck_core SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/modules)

target_compile_features(deck_core PUBLIC cxx_std_20)
set_target_properties(deck_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(deck_core PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
        -Werror
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(deck_core PUBLIC ${LOCKSTEP_SANITIZE_FLAGS})
endif()
