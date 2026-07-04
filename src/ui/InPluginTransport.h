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
        // v27 hosted-Locked arm gate. When armRegime is true the transport follows
        // the DAW and the Play button parks/unparks the plugin (pluginArmed)
        // instead of driving in-plugin Play; `armed` is the current gate state.
        bool armRegime = false;
        bool armed = true;
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
    class InPluginTransport : public juce::Component
    {
    public:
        explicit InPluginTransport(Clock& clock);

        void paint(juce::Graphics& g) override;
        void resized() override;

        // Called by the editor when the in-plugin Play button should be
        // ghosted (Locked mode, hosted). A ghost click shows a warning popup
        // (Stage 3); for Stage 2 the buttons are always active.
        void setGhosted(bool ghosted);

        // v27: in hosted Locked the header transport parks/unparks the plugin
        // (arm gate) rather than being inert. The editor supplies the mode-aware
        // verb handlers (processor.transportPlay/StopReset) and an arm-state
        // reader; when both are set and the arm regime is active (setGhosted true),
        // the Play/Stop buttons drive them and show Armed/Parked. Message thread.
        std::function<void()> onPlayVerb;
        std::function<void()> onStopVerb;
        std::function<bool()> isArmed;

        // Immediate update of all button labels/colours from a TransportModel.
        // Call this after any transport action so the UI is always in sync.
        void refresh(const TransportModel& m);

        // Re-read transport state from the clock and update the buttons
        // (shadow-gated, so it's a no-op when nothing changed). Driven by the
        // editor's always-on tick (9.15) instead of an own timer, so host-driven
        // transport changes still surface.
        void pollState() { refresh(buildTransportModel(clock_)); }

    private:
        Clock& clock_;
        bool ghosted_ = false;

        // Shadow to avoid redundant JUCE property-change notifications.
        TransportModel shadow_;

        juce::TextButton playBtn_{ "Play" };
        juce::TextButton resetBtn_{ "Stop" };
        juce::TextButton recBtn_{ "Rec" };
        juce::TextButton metroBtn_{ "Click" };

        void onPlayClick();
        void onResetClick();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InPluginTransport)
    };
}
