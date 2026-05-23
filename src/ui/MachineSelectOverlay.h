#pragma once

#include <functional>
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Bank.h"   // kPartsPerBank

namespace lockstep
{
    class LockstepProcessor;

    // MGX.6: floating panel for per-track machine reassignment and Part selection.
    class MachineSelectOverlay : public juce::Component
    {
    public:
        explicit MachineSelectOverlay(LockstepProcessor& processor);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void visibilityChanged() override;

        // Callbacks set by the editor.
        std::function<void()> onClose;
        std::function<int()>  getActiveTrack;   // currently focused track index

    private:
        void refresh();
        void onMachineRowClicked(int row);

        LockstepProcessor& processor_;

        juce::TextButton closeBtn_{ "X" };

        // Part selector: one button per Part slot.
        std::array<juce::TextButton, kPartsPerBank> partBtns_;

        juce::ListBox listBox_;

        struct Model : juce::ListBoxModel
        {
            MachineSelectOverlay* owner = nullptr;
            int getNumRows() override;
            void paintListBoxItem(int row, juce::Graphics& g,
                                  int w, int h, bool selected) override;
            void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        } model_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineSelectOverlay)
    };
}
