#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

namespace lockstep
{
    // New / Open / Save / Save As… bar — shown only in the standalone wrapper.
    // Provides .lockstep project file I/O and displays the current project name.
    class StandaloneFileBar : public juce::Component
    {
    public:
        StandaloneFileBar(LockstepProcessor& proc, juce::ApplicationProperties& appProps);

        void resized() override;

        // Update the name label from the current project file (call after load/save/new).
        void refresh();

        // Wired to the editor's status display (same pattern as SoundBankOverlay::onStatus).
        std::function<void(const juce::String&)> onStatus;

        // If state is dirty, shows a three-way Save/Discard/Cancel dialog then calls fn.
        // fn is called immediately (synchronously) if the state is clean.
        // Public so PluginEditor can wire it as the D2 standalone close-button callback.
        void withDirtyGuard(std::function<void()> fn);

    private:
        LockstepProcessor& proc_;
        juce::ApplicationProperties& appProps_;

        juce::TextButton newBtn_{ "New" };
        juce::TextButton openBtn_{ "Open" };
        juce::TextButton saveBtn_{ "Save" };
        juce::TextButton saveAsBtn_{ "Save As..." };
        juce::Label nameLabel_;

        std::unique_ptr<juce::FileChooser> fileChooser_;

        void doNew();
        void doOpen();
        void doSave(std::function<void()> completion = {});
        void doSaveAs();
        void performOpen();
        void performSaveAs(std::function<void()> completion = {});
        void openFile(const juce::File& f);
        void saveFile(const juce::File& f);
        void persistLastFile(const juce::File& f);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneFileBar)
    };
}
