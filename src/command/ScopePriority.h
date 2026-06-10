#pragma once
#include <array>
#include "../io/EditMode.h"

namespace lockstep
{
  struct UiState;

  // Canonical scope priority, highest first (DESIGN §13).
  // This is the ONE encoding of "which scope wins" — EditMode::recomputePrimary,
  // all label/colour resolvers, and binding-row tiebreaks derive from this array.
  inline constexpr std::array<EditMode::PrimaryScope, 10> kScopePriority = { {
    EditMode::PrimaryScope::Trig,    EditMode::PrimaryScope::Section,
    EditMode::PrimaryScope::Track,   EditMode::PrimaryScope::Phrase,
    EditMode::PrimaryScope::Scene,   EditMode::PrimaryScope::Mute,
    EditMode::PrimaryScope::Morph,   EditMode::PrimaryScope::Song,
    EditMode::PrimaryScope::Fill,    EditMode::PrimaryScope::Func,
  } };

  // Section-suite subset: {Track, Phrase, Scene, Morph, Song} — the scopes that
  // re-skin the section row and step grid when held. Returns the highest-priority
  // section-suite scope currently held in UiState, or PrimaryScope::None.
  EditMode::PrimaryScope firstHeldSectionSuiteScope(const UiState& ui) noexcept;

} // namespace lockstep
