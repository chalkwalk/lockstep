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
# INTERFACE, not STATIC. The translation units this target used to compile --
# Medium.cpp, LayerStack.cpp, Deck.cpp -- are chalkwalk-tape's now; what is left
# here is the seam that keeps them reachable as `dc::` (see any header in this
# directory) plus one Hermite read that was never part of the deck.
add_library(deck_core INTERFACE)

target_link_libraries(deck_core INTERFACE chalkwalk::tape)

target_include_directories(deck_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# signalsmith-dsp (MIT, header-only). SYSTEM so its warnings never trip the
# consumers' -Werror. Rooted at modules/ so headers namespace as
# <signalsmith-dsp/...>.
target_include_directories(deck_core SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/modules)

target_compile_features(deck_core INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(deck_core INTERFACE ${LOCKSTEP_SANITIZE_FLAGS})
endif()
