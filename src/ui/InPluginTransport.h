#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Clock.h"

namespace lockstep
{
    // Pure snapshot of clock state needed to update the transport UI.
    // Built by buildTransportModel() and passed to InPluginTransport::refresh().
    struct TransportModel
    {
        bool playing = false;
        bool recArmed = false;
        bool overdubArmed = false;
        bool metronomeOn = false;
    };

    [[nodiscard]] inline TransportModel buildTransportModel(const Clock& clock) noexcept
    {
        return {
            .playing = clock.inPluginPlaying(),
            .recArmed = clock.isRecordArmed(),
            .overdubArmed = clock.isOverdubArmed(),
            .metronomeOn = clock.isMetronomeEnabled()
        };
    }

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

        // Immediate update of all button labels/colours from a TransportModel.
        // Call this after any transport action so the UI is always in sync,
        // without waiting for the 15 Hz timer tick.
        void refresh(const TransportModel& m);

    private:
        Clock& clock_;
        bool ghosted_ = false;

        // Shadow to avoid redundant JUCE property-change notifications.
        TransportModel shadow_;

        juce::TextButton playBtn_{ "Play" };
        juce::TextButton resetBtn_{ "Stop" };
        juce::TextButton recBtn_{ "Rec" };
        juce::TextButton metroBtn_{ "Click" };

        void timerCallback() override;
        void onPlayClick();
        void onResetClick();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InPluginTransport)
    };
}
