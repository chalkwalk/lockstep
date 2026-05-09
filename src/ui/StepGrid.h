#pragma once

#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Sequence.h"

namespace lockstep
{
    class LockstepProcessor;

    // 2×8 trig grid for one track, paginated in 16-step windows.
    // Shows a moving playhead driven by the shared clock position.
    // Track selector and length slider are built-in.
    // QWERTY page-change bindings land in M6; prev/next buttons serve for now.
    class StepGrid : public juce::Component, public juce::Timer
    {
    public:
        explicit StepGrid(LockstepProcessor& processor);
        ~StepGrid() override;

        void setActiveTrack(int t);
        int  getActiveTrack() const { return activeTrack_; }

        void nextPage();
        void prevPage();

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;

        static constexpr int kPageSteps = 16;
        static constexpr int kCols      = 8;
        static constexpr int kRows      = 2;

    private:
        int  trackLength() const;
        int  numPages() const;
        void clampPage();
        void rebuildLengthAttachment();

        LockstepProcessor& processor_;
        int activeTrack_ = 0;
        int stepPage_    = 0;

        std::array<juce::TextButton, kNumTracks> trackBtns_;
        juce::TextButton prevBtn_{ "<" };
        juce::TextButton nextBtn_{ ">" };

        juce::Slider     lengthSlider_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment_;

        static constexpr int kTrackRowH = 22;
        static constexpr int kNavRowH   = 26;
    };
}
