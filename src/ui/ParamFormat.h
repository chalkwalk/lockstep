#pragma once

#include <cmath>
#include <juce_core/juce_core.h>
#include "../machine/IMachine.h"

namespace lockstep
{
    // Formats a parameter value as a display string.
    // Handles unit suffixes, stepped integers, and closed-enum valueLabels.
    // This is the single source of truth for value text — used by ManipulationZone
    // and SurfaceModel (for controller display lines).
    inline juce::String formatParamValue(float v, const ParamSpec& spec)
    {
        if (!spec.valueLabels.empty())
        {
            const int idx = std::clamp(static_cast<int>(std::round(v)),
                                       0,
                                       static_cast<int>(spec.valueLabels.size()) - 1);
            return juce::String(spec.valueLabels[static_cast<std::size_t>(idx)]);
        }

        if (spec.isStepped)
            return juce::String(static_cast<int>(std::round(v)));

        switch (spec.unit)
        {
            case ParamSpec::Unit::Ms:
                return v < 10.0f ? juce::String(v, 1) + " ms"
                                 : juce::String(static_cast<int>(v)) + " ms";
            case ParamSpec::Unit::Semitones: {
                const int st = static_cast<int>(std::round(v));
                return (st >= 0 ? "+" : "") + juce::String(st) + " st";
            }
            case ParamSpec::Unit::Percent:
                return juce::String(static_cast<int>(v * 100.0f)) + "%";
            case ParamSpec::Unit::None:
            default:
                return juce::String(v, 2);
        }
    }
}
