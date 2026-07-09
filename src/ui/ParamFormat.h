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
    // `bpm` is only consulted for Unit::Beats, where an off-detent value has no
    // musical name and reads as milliseconds at the current tempo instead. Callers
    // without a tempo to hand (controller displays) get the beat fraction.
    inline juce::String formatParamValue(float v, const ParamSpec& spec, double bpm = 0.0)
    {
        // A continuous slot with a detent lattice (A4): name the detent it rests on,
        // otherwise fall through and describe where it is between them. Checked
        // before the stepped/valueLabels branch, whose labels index the value.
        if (!spec.detents.empty())
        {
            const int di = detentIndexAt(spec, v);
            if (di >= 0 && di < static_cast<int>(spec.valueLabels.size()))
                return juce::String(spec.valueLabels[static_cast<std::size_t>(di)]);

            if (spec.unit == ParamSpec::Unit::Beats)
            {
                if (bpm > 0.0)
                {
                    const double ms = static_cast<double>(v) * 60000.0 / bpm;
                    return juce::String(static_cast<int>(std::round(ms))) + " ms";
                }
                return juce::String(v, 2) + " bt";
            }
            return juce::String(v, 2);
        }

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
            case ParamSpec::Unit::Cents: {
                const int c = static_cast<int>(std::round(v));
                return (c >= 0 ? "+" : "") + juce::String(c) + " c";
            }
            case ParamSpec::Unit::Percent:
                return juce::String(static_cast<int>(v * 100.0f)) + "%";
            case ParamSpec::Unit::Beats:
                return (bpm > 0.0)
                    ? juce::String(static_cast<int>(
                          std::round(static_cast<double>(v) * 60000.0 / bpm))) + " ms"
                    : juce::String(v, 2) + " bt";
            case ParamSpec::Unit::None:
            default:
                return juce::String(v, 2);
        }
    }
}
