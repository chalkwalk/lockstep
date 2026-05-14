#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Clock.h"

namespace lockstep
{
    // Play/Pause toggle + Reset button in the editor header.
    // Always present in both standalone and hosted builds.
    // Buttons honour syncMode ghosting (Stage 3); for Stage 2 they're active.
    class InPluginTransport : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit InPluginTransport(Clock& clock);

        void paint(juce::Graphics& g) override;
        void resized() override;

        // Called by the editor when the in-plugin Play button should be
        // ghosted (Locked mode, hosted). A ghost click shows a warning popup
        // (Stage 3); for Stage 2 the buttons are always active.
        void setGhosted(bool ghosted);

    private:
        Clock& clock_;
        bool ghosted_ = false;

        juce::TextButton playBtn_  { "Play" };
        juce::TextButton resetBtn_ { "Stop" };
        juce::TextButton recBtn_   { "Rec" };

        void timerCallback() override;
        void onPlayClick();
        void onResetClick();
        void syncPlayLabel();
        void syncRecColour();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InPluginTransport)
    };
}
