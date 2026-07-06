#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

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

        // Wired by the editor so the overlay knows which track is active.
        std::function<int()> getActiveTrack;

        // Called by the editor when it wants to close the overlay.
        std::function<void()> onClose;

        // 9.23 S6: fired with the selected pool index when the user presses
        // Props… — the editor opens the sample-properties meta band on it.
        std::function<void(int)> onEditProps;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;
        // Self-suspend the pool poll: only run while the overlay is on screen
        // (9.15). When hidden — the common case — there is no timer.
        void visibilityChanged() override;

        // ListBoxModel
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                              int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent& e) override;

    private:
        LockstepProcessor& processor_;

        // W3a: the browser groups the flat pool under non-selectable headers —
        // SAMPLES (files), then RECORD / LOOP (captured volatile slots, empties
        // hidden). Display rows map back to absolute pool indices for click/preview
        // and the edit buttons.
        struct DisplayRow { bool isHeader = false; juce::String label; int poolIndex = -1; };
        std::vector<DisplayRow> rows_;
        void rebuildRows();
        // Absolute pool index of the currently-selected display row, or -1 if a
        // header / nothing is selected.
        int selectedPoolIndex() const;
        // After an in-place reorder swap, rebuild rows and re-select the display row
        // now carrying the moved sample so the ListBox actually repaints (bug 10).
        void reselectAfterReorder(int movedPoolIndex);

        juce::ListBox list_{ "pool", this };
        juce::TextButton loadBtn_{ "Load..." };
        juce::TextButton relinkBtn_{ "Relink..." };
        juce::TextButton promoteBtn_{ "Save..." };  // 9.18: volatile capture -> durable File
        juce::TextButton propsBtn_{ "Props..." };   // 9.23: edit BPM/key/tune/one-shot
        juce::TextButton removeBtn_{ "Remove" };
        juce::TextButton upBtn_{ juce::String(u8"↑") };
        juce::TextButton downBtn_{ juce::String(u8"↓") };
        juce::TextButton closeBtn_{ "X" };
        std::unique_ptr<juce::FileChooser> fileChooser_;

        void updateButtonStates();
    };
}
