#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class LockstepProcessor;

    // Renders the 2×8 trig grid for one track, paginated in 16-step windows.
    // QWERTY page-change bindings land in M6; for now prev/next buttons drive it.
    class StepGrid : public juce::Component
    {
    public:
        explicit StepGrid(LockstepProcessor& processor);

        void setActiveTrack(int t);
        int  getActiveTrack() const { return activeTrack_; }

        void nextPage();
        void prevPage();
        int  getStepPage() const { return stepPage_; }

        void paint(juce::Graphics& g) override;
        void resized() override;

        static constexpr int kPageSteps = 16;
        static constexpr int kCols      = 8;
        static constexpr int kRows      = 2;

    private:
        int trackLength() const;
        int numPages() const;
        void clampPage();

        LockstepProcessor& processor_;
        int activeTrack_ = 0;
        int stepPage_    = 0;

        juce::TextButton prevBtn_{ "<" };
        juce::TextButton nextBtn_{ ">" };
    };
}
