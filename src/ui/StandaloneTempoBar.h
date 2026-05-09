#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Clock.h"

namespace lockstep
{
    // BPM slider + bar.beat.tick position readout.
    // Built and shown only in the standalone wrapper.
    class StandaloneTempoBar : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit StandaloneTempoBar(Clock& clock);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        Clock& clock_;

        juce::Slider bpmSlider_;
        juce::Label  positionLabel_;

        void timerCallback() override;
        juce::String positionText() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneTempoBar)
    };
}
