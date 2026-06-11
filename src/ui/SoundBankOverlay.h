#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class LockstepProcessor;

    // MG.4: floating panel listing Sound Pool entries.
    // Lets the user save the focused track's sound, recall entries, and delete them.
    class SoundBankOverlay : public juce::Component
    {
    public:
        explicit SoundBankOverlay(LockstepProcessor& processor);

        void paint(juce::Graphics& g) override;
        void resized() override;

        // Callbacks set by the editor.
        std::function<void()> onClose;
        std::function<int()> getActiveTrack;

    private:
        void refresh();
        void onSaveClicked();
        void onRecallClicked(int entryIndex);
        void onDeleteClicked(int entryIndex);

        LockstepProcessor& processor_;

        juce::TextButton closeBtn_{ "X" };
        juce::TextButton saveBtn_{ "Save Track Sound" };
        juce::ListBox listBox_;

        // Thin ListBoxModel so we don't need a separate class file.
        struct Model : public juce::ListBoxModel
        {
            SoundBankOverlay* owner = nullptr;
            int getNumRows() override;
            void paintListBoxItem(int row, juce::Graphics& g,
                                  int w, int h, bool selected) override;
            void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

            juce::Component* refreshComponentForRow(int row, bool,
                                                    juce::Component* existing) override;
        } model_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundBankOverlay)
    };
}
