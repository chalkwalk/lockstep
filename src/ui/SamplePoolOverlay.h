#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace lockstep
{
    class LockstepProcessor;

    // Overlays the ManipulationZone area and shows the full sample pool.
    // Load/remove/reorder operations are all message-thread only.
    class SamplePoolOverlay : public juce::Component,
                              public juce::ListBoxModel,
                              public juce::Timer
    {
    public:
        explicit SamplePoolOverlay(LockstepProcessor& processor);
        ~SamplePoolOverlay() override;

        // Called by the editor when it wants to close the overlay.
        std::function<void()> onClose;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;

        // ListBoxModel
        int  getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override;

    private:
        LockstepProcessor& processor_;
        juce::ListBox list_{ "pool", this };
        juce::TextButton loadBtn_{ "Load..." };
        juce::TextButton removeBtn_{ "Remove" };
        juce::TextButton upBtn_{ "^" };
        juce::TextButton downBtn_{ "v" };
        juce::TextButton closeBtn_{ "X" };
        std::unique_ptr<juce::FileChooser> fileChooser_;
    };
}
