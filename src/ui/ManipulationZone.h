#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    // The 4-parameter quadrant. The active page selects which 4 IMachine
    // slots are exposed; the underlying values come from either the Step
    // Override (when EditContext is active) or the Track Base. Real layout
    // and APVTS attachments land in M6.
    class ManipulationZone : public juce::Component
    {
    public:
        ManipulationZone();
        void paint(juce::Graphics& g) override;
    };
}
