#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class LockstepProcessor;

    // Floating panel listing Project::soundPool entries.
    // Single-click row = recall; double-click label = inline rename; per-row Del button.
    // Bottom hint strip documents the Fill+SRC keyboard performance-recall gesture.
    class SoundBankOverlay : public juce::Component
    {
    public:
        explicit SoundBankOverlay(LockstepProcessor& processor);

        void paint(juce::Graphics& g) override;
        void resized() override;

        // Callbacks set by the editor.
        std::function<void()> onClose;
        std::function<int()> getActiveTrack;
        std::function<void(const juce::String&)> onStatus;

    private:
        void refresh();
        void doRecall(int entryIndex);
        void doDelete(int entryIndex);
        void doRename(int entryIndex, const juce::String& newName);
        void onSaveClicked();

        LockstepProcessor& processor_;

        juce::TextButton closeBtn_{ "X" };
        juce::TextButton saveBtn_{ "Save Track Sound" };
        juce::ListBox listBox_;
        juce::Label hintLabel_;

        // Row component: name Label + Recall + Del buttons.
        struct Row : public juce::Component
        {
            explicit Row(SoundBankOverlay& owner);
            void resized() override;
            void mouseDown(const juce::MouseEvent& e) override;

            void update(int rowIndex, const juce::String& name);

            SoundBankOverlay& owner;
            int rowIndex = -1;
            juce::Label nameLabel_;
            juce::TextButton recallBtn_{ "Recall" };
            juce::TextButton delBtn_{ "Del" };
        };

        struct Model : public juce::ListBoxModel
        {
            SoundBankOverlay* owner = nullptr;
            int getNumRows() override;
            void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
            juce::Component* refreshComponentForRow(int row, bool,
                                                    juce::Component* existing) override;
        } model_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundBankOverlay)
    };
}
