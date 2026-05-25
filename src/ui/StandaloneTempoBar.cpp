#include "StandaloneTempoBar.h"
#include <cmath>

namespace lockstep
{
    StandaloneTempoBar::StandaloneTempoBar(Clock& clock)
        : clock_(clock)
    {
        bpmSlider_.setRange(20.0, 300.0, 0.1);
        bpmSlider_.setValue(clock_.localBpm(), juce::dontSendNotification);
        bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 18);
        bpmSlider_.setWantsKeyboardFocus(false);
        bpmSlider_.onValueChange = [this]
        {
            clock_.setLocalBpm(bpmSlider_.getValue());
        };
        addAndMakeVisible(bpmSlider_);

        positionLabel_.setJustificationType(juce::Justification::centredRight);
        positionLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(positionLabel_);

        startTimerHz(30);
    }

    void StandaloneTempoBar::timerCallback()
    {
        const double bpm = clock_.localBpm();
        if (bpm != lastBpm_)
        {
            lastBpm_ = bpm;
            bpmSlider_.setValue(bpm, juce::dontSendNotification);
        }

        const juce::String pos = positionText();
        if (pos != lastPos_)
        {
            lastPos_ = pos;
            positionLabel_.setText(pos, juce::dontSendNotification);
        }
    }

    juce::String StandaloneTempoBar::positionText() const
    {
        const double ppq = clock_.cumulativePpq();
        // Assumes 4/4 time (the only time signature seq_play targets for now).
        const int bar  = static_cast<int>(ppq / 4.0) + 1;
        const int beat = static_cast<int>(std::fmod(ppq, 4.0)) + 1;
        const int tick = static_cast<int>(std::fmod(ppq, 1.0) * 96.0);  // 96 ticks/beat
        return juce::String(bar) + "." + juce::String(beat) + "." + juce::String::formatted("%02d", tick);
    }

    void StandaloneTempoBar::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(28, 32, 40));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(60, 70, 85));
        g.drawRect(getLocalBounds(), 1);
    }

    void StandaloneTempoBar::resized()
    {
        auto b = getLocalBounds().reduced(4, 2);
        positionLabel_.setBounds(b.removeFromRight(90));
        bpmSlider_.setBounds(b);
    }
}
