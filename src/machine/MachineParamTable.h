#pragma once

// MachineParamTable.h — ParamRow struct and toParamSpec() converter.
//
// ParamRow is a JUCE-free, constexpr-friendly descriptor shaped to mirror the
// planned LsmParamSpec POD from DESIGN §36. Machines store their parameter
// schemas as `static constexpr ParamRow kParams[]` tables and implement
// paramSpec(i) with a single `return toParamSpec(kParams[i])`.
//
// valueLabels: point to a static-lifetime NULL-terminated array of const char*
// (i.e. const char* const labels[] = { "A", "B", nullptr }).  toParamSpec()
// measures the span up to (but not including) the nullptr sentinel.
// Passing nullptr means no labels.
//
// This header must remain JUCE-free (no #include of any JUCE header) so that
// it can later be included by the machine SDK (DESIGN §36) without pulling in
// JUCE. The full ParamSpec (IMachine.h) is JUCE-bearing; MachineParamTable.h
// is the JUCE-free precursor.

#include <cstdint>

namespace lockstep
{
    // Forward-declared so toParamSpec() can return it without including IMachine.h
    // here.  Include IMachine.h before including this header, or include
    // MachineParamTable.cpp (which includes both).
    struct ParamSpec;

    // ------------------------------------------------------------------
    // ParamRow — field-order mirrors LsmParamSpec (DESIGN §36, ~line 4240)
    // so the Phase 6.7 SDK shim becomes a simple struct-copy.
    // ------------------------------------------------------------------
    struct ParamRow
    {
        const char* id = nullptr;  // stable serialization key
        const char* label = nullptr;  // Manipulation Zone label
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        float skew = 1.0f;     // JUCE skew (1.0 = linear)
        std::uint8_t isStepped = 0;        // bool: 1 = stepped/enum
        std::uint8_t unit = 0;        // ParamSpec::Unit cast to uint8
        std::uint8_t role = 0;        // ParamSpec::Role cast to uint8
        std::uint8_t variant = 0;        // ParamSpec::Variant cast to uint8
        std::int32_t sectionIndex = 0;
        std::uint8_t zeroCrossingSnap = 0;     // bool
        // NULL-terminated static array of label strings, or nullptr if none.
        const char* const* valueLabels = nullptr;
    };

    // Converts a ParamRow to the runtime ParamSpec.
    // Defined in MachineParamTable.cpp to keep the IMachine.h dependency out of
    // headers that only need the ParamRow shape.
    [[nodiscard]] ParamSpec toParamSpec(const ParamRow& row);

} // namespace lockstep
