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

        // Floor / ceiling labels (e.g. "Auto" / "Off" / "Full"): the value stays the
        // canonical float; these only name the extremes. A small epsilon lets a value
        // resting a hair from the extreme still read as the label (the DSP uses the
        // same threshold so the readout and behaviour agree).
        constexpr float kEdgeEps = 1.0e-3f;
        if (spec.minLabel != nullptr && v <= spec.minValue + kEdgeEps)
            return juce::String(spec.minLabel);
        if (spec.maxLabel != nullptr && v >= spec.maxValue - kEdgeEps)
            return juce::String(spec.maxLabel);

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
