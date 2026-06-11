#include "StandaloneFileBar.h"

namespace lockstep
{
    static constexpr const char* kLastFileKey = "lastProjectFile";
    static constexpr const char* kFileFilter = "*.lockstep";

    StandaloneFileBar::StandaloneFileBar(LockstepProcessor& proc,
                                         juce::ApplicationProperties& appProps)
        : proc_(proc), appProps_(appProps)
    {
        for (auto* btn : { &newBtn_, &openBtn_, &saveBtn_, &saveAsBtn_ })
        {
            btn->setWantsKeyboardFocus(false);
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 44, 54));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 200, 220));
            addAndMakeVisible(btn);
        }

        newBtn_.onClick = [this] { doNew(); };
        openBtn_.onClick = [this] { doOpen(); };
        saveBtn_.onClick = [this] { doSave(); };
        saveAsBtn_.onClick = [this] { doSaveAs(); };

        nameLabel_.setFont(juce::Font(juce::FontOptions(10.5f)));
        nameLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 180, 200));
        nameLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(nameLabel_);

        refresh();
    }

    void StandaloneFileBar::resized()
    {
        auto r = getLocalBounds().reduced(2, 1);
        newBtn_.setBounds(r.removeFromLeft(38).reduced(1));
        openBtn_.setBounds(r.removeFromLeft(44).reduced(1));
        saveBtn_.setBounds(r.removeFromLeft(40).reduced(1));
        saveAsBtn_.setBounds(r.removeFromLeft(64).reduced(1));
        r.removeFromLeft(4);
        nameLabel_.setBounds(r);
    }

    void StandaloneFileBar::refresh()
    {
        const auto f = proc_.currentProjectFile();
        if (f.existsAsFile())
            nameLabel_.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
        else
            nameLabel_.setText("(unsaved)", juce::dontSendNotification);
    }

    // ──────────────────────────────────────────────────────────────────────────────
    // File operations

    void StandaloneFileBar::persistLastFile(const juce::File& f)
    {
        if (auto* prefs = appProps_.getUserSettings())
        {
            prefs->setValue(kLastFileKey, f.getFullPathName());
            prefs->saveIfNeeded();
        }
    }

    void StandaloneFileBar::openFile(const juce::File& f)
    {
        if (!proc_.loadProjectFile(f))
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withTitle("Load failed")
                    .withMessage("Could not parse: " + f.getFullPathName())
                    .withButton("OK"),
                nullptr);
            return;
        }
        persistLastFile(f);
        refresh();
        if (onStatus) onStatus("Opened: " + f.getFileNameWithoutExtension());
    }

    void StandaloneFileBar::saveFile(const juce::File& f)
    {
        if (!proc_.saveProjectFile(f))
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withTitle("Save failed")
                    .withMessage("Could not write: " + f.getFullPathName())
                    .withButton("OK"),
                nullptr);
            return;
        }
        persistLastFile(f);
        refresh();
        if (onStatus) onStatus("Saved: " + f.getFileNameWithoutExtension());
    }

    void StandaloneFileBar::withDirtyGuard(std::function<void()> fn)
    {
        if (proc_.stateHash() == proc_.savedStateHash())
        {
            fn();
            return;
        }
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Unsaved changes")
                .withMessage("Save the current project before continuing?")
                .withButton("Save")
                .withButton("Discard")
                .withButton("Cancel"),
            [this, fn = std::move(fn)](int result) {
                if (result == 1)
                {
                    doSave();
                    fn();
                }
                else if (result == 2)
                {
                    fn();
                }
                // result == 3 or 0 = Cancel — do nothing
            });
    }

    void StandaloneFileBar::doNew()
    {
        withDirtyGuard([this] {
            proc_.newProject();
            refresh();
            if (onStatus) onStatus("New project");
        });
    }

    void StandaloneFileBar::doOpen()
    {
        withDirtyGuard([this] { performOpen(); });
    }

    void StandaloneFileBar::performOpen()
    {
        const auto startDir = proc_.currentProjectFile().existsAsFile()
                                  ? proc_.currentProjectFile().getParentDirectory()
                                  : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        fileChooser_ = std::make_unique<juce::FileChooser>("Open Project", startDir, kFileFilter);
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto results = fc.getResults();
                if (results.isEmpty()) return;
                openFile(results[0]);
            });
    }

    void StandaloneFileBar::doSave()
    {
        const auto f = proc_.currentProjectFile();
        if (f.existsAsFile())
            saveFile(f);
        else
            performSaveAs();
    }

    void StandaloneFileBar::doSaveAs()
    {
        performSaveAs();
    }

    void StandaloneFileBar::performSaveAs()
    {
        const auto startDir = proc_.currentProjectFile().existsAsFile()
                                  ? proc_.currentProjectFile().getParentDirectory()
                                  : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        fileChooser_ = std::make_unique<juce::FileChooser>("Save Project As", startDir, kFileFilter);
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                const auto results = fc.getResults();
                if (results.isEmpty()) return;
                auto f = results[0];
                if (f.getFileExtension().isEmpty())
                    f = f.withFileExtension(".lockstep");
                saveFile(f);
            });
    }
}
