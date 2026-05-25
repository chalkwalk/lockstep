#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "machine/FMMachine.h"
#include "machine/IMachine.h"
#include "machine/MidiOutMachine.h"
#include "ui/ScopedSectionMatrix.h"
#include <algorithm>

namespace lockstep
{
    LockstepEditor::LockstepEditor(LockstepProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor_(proc),
          transport_(proc.clock()),
          keyboardArea_(proc, uiState_),
          manipulationZone_(proc, keyboardArea_),
          poolOverlay_(proc),
          soundBankOverlay_(proc),
          machineSelectOverlay_(proc)
    {
        // Load persisted display mode.
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Lockstep";
            o.filenameSuffix      = ".xml";
            o.folderName          = "Lockstep";
            o.osxLibrarySubFolder = "Application Support";
            appProps_.setStorageParameters(o);
        }
        if (auto* prefs = appProps_.getUserSettings())
            gridMode_ = static_cast<GridDisplayMode>(
                prefs->getIntValue("gridMode", static_cast<int>(GridDisplayMode::Ortholinear)));
        applyDisplayMode(gridMode_);
        addAndMakeVisible(transport_);

        if (juce::PluginHostType::getPluginLoadedAs()
                == juce::AudioProcessor::wrapperType_Standalone)
        {
            tempoBar_ = std::make_unique<StandaloneTempoBar>(proc.clock());
            addAndMakeVisible(tempoBar_.get());
        }

        // Sync mode ComboBox + APVTS attachment
        syncModeBox_.addItem("Locked", 1);
        syncModeBox_.addItem("Auto",   2);
        syncModeBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(syncModeBox_);
        syncModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts(), ParamIDs::syncMode, syncModeBox_);

        // Channel mode ComboBox + APVTS attachment
        channelModeBox_.addItem("Omni",      1);
        channelModeBox_.addItem("Per-Track", 2);
        channelModeBox_.setWantsKeyboardFocus(false);
        addAndMakeVisible(channelModeBox_);
        channelModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts(), ParamIDs::channelMode, channelModeBox_);

        proc.apvts().addParameterListener(ParamIDs::syncMode, this);
        updateTransportGhosting();

        // Wire section-change callbacks -> update ManipulationZone.
        keyboardArea_.onSectionChanged = [this](int /*section*/, int /*page*/, int firstSlot)
        {
            manipulationZone_.setSlotOffset(firstSlot);
        };
        keyboardArea_.onMetaSectionChanged = [this](int metaSection)
        {
            manipulationZone_.setMetaSection(metaSection);
        };

        // Track page toggle: flips between tracks 1-8 and 9-16.
        trackPageBtn_.setWantsKeyboardFocus(false);
        trackPageBtn_.onClick = [this]
        {
            trackPage_ = 1 - trackPage_;
            trackPageBtn_.setButtonText(trackPage_ == 0 ? "1-8" : "9-16");
            resized();
            repaint();
        };
        addAndMakeVisible(trackPageBtn_);

        // Track header: selector + mute/solo.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            const auto ti = static_cast<std::size_t>(i);

            trackBtns_[ti].setButtonText(juce::String(i + 1));
            trackBtns_[ti].setClickingTogglesState(false);
            trackBtns_[ti].setWantsKeyboardFocus(false);
            trackBtns_[ti].onClick = [this, i] { keyboardArea_.setActiveTrack(i); };
            // Transparent background so the underlaid per-track VU meter (drawn
            // behind in paint()) shows through; the number paints on top.
            trackBtns_[ti].setColour(juce::TextButton::buttonColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::buttonOnColourId,
                                     juce::Colours::transparentBlack);
            trackBtns_[ti].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            trackBtns_[ti].setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
            addAndMakeVisible(trackBtns_[ti]);

            muteBtns_[ti].setButtonText("M");
            muteBtns_[ti].setWantsKeyboardFocus(false);
            addAndMakeVisible(muteBtns_[ti]);
            muteAttachments_[ti] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    proc.apvts(), ParamIDs::trackMute(i), muteBtns_[ti]);

            soloBtns_[ti].setButtonText("S");
            soloBtns_[ti].setWantsKeyboardFocus(false);
            addAndMakeVisible(soloBtns_[ti]);
            soloAttachments_[ti] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    proc.apvts(), ParamIDs::trackSolo(i), soloBtns_[ti]);
        }
        trackBtns_[0].setToggleState(true, juce::dontSendNotification);

        keyboardArea_.onActiveTrackChanged = [this](int newTrack)
        {
            // Auto-flip page when the active track changes bank.
            const int newPage = (newTrack >= 8) ? 1 : 0;
            if (newPage != trackPage_)
            {
                trackPage_ = newPage;
                trackPageBtn_.setButtonText(trackPage_ == 0 ? "1-8" : "9-16");
                resized();
            }
            for (auto& b : trackBtns_)
                b.setToggleState(false, juce::dontSendNotification);
            trackBtns_[static_cast<std::size_t>(newTrack)].setToggleState(
                true, juce::dontSendNotification);
            keyboardArea_.syncToActiveTrack();
        };

        displayModeBtn_.setWantsKeyboardFocus(false);
        displayModeBtn_.onClick = [this]
        {
            applyDisplayMode(static_cast<GridDisplayMode>(
                (static_cast<int>(gridMode_) + 1) % 3));
        };
        addAndMakeVisible(displayModeBtn_);
        // MHX.5: vertical crossfader — Scene A at top, Scene B at bottom.
        crossfader_.setSliderStyle(juce::Slider::LinearBarVertical);
        crossfader_.setRange(0.0, 1.0, 0.0);
        crossfader_.setValue(0.5, juce::dontSendNotification);
        crossfader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        crossfader_.setColour(juce::Slider::trackColourId,
                              juce::Colour::fromRGB(100, 80, 200).withAlpha(0.6f));
        crossfader_.setWantsKeyboardFocus(false);
        crossfader_.setTooltip("Scene crossfader (A=top / B=bottom)");
        addAndMakeVisible(crossfader_);

        addAndMakeVisible(manipulationZone_);
        addAndMakeVisible(keyboardArea_);

        poolBtn_.setWantsKeyboardFocus(false);
        poolBtn_.onClick = [this]
        {
            poolOverlay_.setVisible(!poolOverlay_.isVisible());
            if (poolOverlay_.isVisible())
                poolOverlay_.toFront(false);
        };
        addAndMakeVisible(poolBtn_);

        poolOverlay_.onClose = [this] { poolOverlay_.setVisible(false); };
        poolOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        addChildComponent(poolOverlay_);

        soundBankBtn_.setWantsKeyboardFocus(false);
        soundBankBtn_.onClick = [this]
        {
            soundBankOverlay_.setVisible(!soundBankOverlay_.isVisible());
            if (soundBankOverlay_.isVisible())
                soundBankOverlay_.toFront(false);
        };
        addAndMakeVisible(soundBankBtn_);

        soundBankOverlay_.onClose = [this] { soundBankOverlay_.setVisible(false); };
        soundBankOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        addChildComponent(soundBankOverlay_);

        machineSelectBtn_.setWantsKeyboardFocus(false);
        machineSelectBtn_.onClick = [this]
        {
            machineSelectOverlay_.setVisible(!machineSelectOverlay_.isVisible());
            if (machineSelectOverlay_.isVisible())
                machineSelectOverlay_.toFront(false);
        };
        addAndMakeVisible(machineSelectBtn_);

        machineSelectOverlay_.onClose = [this] { machineSelectOverlay_.setVisible(false); };
        machineSelectOverlay_.getActiveTrack = [this]() { return keyboardArea_.getActiveTrack(); };
        addChildComponent(machineSelectOverlay_);

        manipulationZone_.onOpenPoolManager = [this]
        {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        };

        // Wire verb dispatch to this editor's handler.
        editMode_.onVerbDispatched = [this](EditMode::PrimaryScope scope,
                                             ControllerButton verb)
        {
            dispatchVerb(scope, verb);
        };

        // Wire mouse button events from KeyboardArea to the same handlers as QWERTY.
        keyboardArea_.onButtonDown = [this](ControllerEvent ev) { handleMouseButtonDown(ev); };
        keyboardArea_.onButtonUp   = [this](ControllerEvent ev) { handleMouseButtonUp(ev); };

        // Repaint chrome when a queued pattern switch fires.
        proc.onActivePatternChanged = [this] { repaint(); };

        setSize(990, 596);  // MHX: taller for 4x2 MZ encoder band
        setWantsKeyboardFocus(true);

        startTimerHz(30);  // diagnostic VU meters / activity blinks
    }

    LockstepEditor::~LockstepEditor()
    {
        processor_.onActivePatternChanged = nullptr;
        processor_.apvts().removeParameterListener(ParamIDs::syncMode, this);
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
    }

    void LockstepEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
    {
        if (paramID == ParamIDs::syncMode)
            juce::MessageManager::callAsync([this] { updateTransportGhosting(); });
    }

    void LockstepEditor::timerCallback()
    {
        // Peak meters: fast attack, slow ballistic decay. Activity blinks: a
        // pulse from the audio thread snaps to 1.0, then decays each tick.
        // Only repaint if any value actually changed; floor tiny values to zero
        // so decay terminates and the repaint loop stops when transport is idle.
        static constexpr float kMeterFloor = 0.001f;
        static constexpr float kBlinkFloor = 0.005f;
        bool dirty = false;

        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const float peak     = processor_.trackPeak(static_cast<int>(i));
            const float newMeter = std::max(peak, trackMeter_[i] * 0.80f);
            const float floored  = (newMeter < kMeterFloor) ? 0.0f : newMeter;
            if (floored != trackMeter_[i]) { trackMeter_[i] = floored; dirty = true; }

            if (processor_.takeTrigPulse(static_cast<int>(i)) > 0.5f)
            {
                trigBlink_[i] = 1.0f;
                dirty = true;
            }
            else if (trigBlink_[i] > 0.0f)
            {
                trigBlink_[i] = (trigBlink_[i] > kBlinkFloor) ? trigBlink_[i] * 0.70f : 0.0f;
                dirty = true;
            }

            if (processor_.takeMidiPulse(static_cast<int>(i)) > 0.5f)
            {
                midiBlink_[i] = 1.0f;
                dirty = true;
            }
            else if (midiBlink_[i] > 0.0f)
            {
                midiBlink_[i] = (midiBlink_[i] > kBlinkFloor) ? midiBlink_[i] * 0.70f : 0.0f;
                dirty = true;
            }
        }

        const float newMaster = std::max(processor_.masterPeak(), masterMeter_ * 0.80f);
        const float flooredMaster = (newMaster < kMeterFloor) ? 0.0f : newMaster;
        if (flooredMaster != masterMeter_) { masterMeter_ = flooredMaster; dirty = true; }

        if (dirty) repaint();
    }

    // Maps a linear meter level [0,1] to a green→yellow→red colour.
    static juce::Colour meterColour(float level)
    {
        if (level < 0.5f)  return juce::Colour::fromRGB(60, 200, 90);
        if (level < 0.85f) return juce::Colour::fromRGB(220, 200, 60);
        return juce::Colour::fromRGB(230, 80, 60);
    }

    void LockstepEditor::paintMeters(juce::Graphics& g)
    {
        // Determine which tracks are silenced (muted or solo-excluded) so the VU
        // can flag them — a silenced track's machine is skipped entirely, which
        // is a common cause of "no sound" confusion.
        bool anySoloed = false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            if (auto* p = processor_.apvts().getRawParameterValue(ParamIDs::trackSolo(t)))
                if (p->load() >= 0.5f) { anySoloed = true; break; }

        // Per-track VU underlaid behind the (transparent) track-number buttons.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const int  t       = static_cast<int>(i);
            const bool gMuted  = processor_.getGlobalMute(t);
            const bool pMuted  = processor_.getPatternMute(t);
            const auto* sp     = processor_.apvts().getRawParameterValue(ParamIDs::trackSolo(t));
            const bool soloed  = sp && sp->load() >= 0.5f;
            const bool soloEx  = anySoloed && !soloed;

            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;

            // Distinct background per silencing source: global mute = red,
            // pattern mute = orange, solo-exclusion = purple, audible = grey.
            juce::Colour bg = juce::Colour::fromRGB(28, 32, 38);
            if      (gMuted) bg = juce::Colour::fromRGB(70, 20, 20);
            else if (pMuted) bg = juce::Colour::fromRGB(80, 50, 16);
            else if (soloEx) bg = juce::Colour::fromRGB(50, 24, 70);
            g.setColour(bg);
            g.fillRect(r);

            const float level = juce::jlimit(0.0f, 1.0f, trackMeter_[i]);
            if (level > 0.001f)
            {
                const int fillW = juce::roundToInt(static_cast<float>(r.getWidth()) * level);
                g.setColour(meterColour(level).withAlpha(0.55f));
                g.fillRect(r.getX(), r.getY(), fillW, r.getHeight());
            }

            // Active-track outline so selection survives the transparent button.
            if (t == keyboardArea_.getActiveTrack())
            {
                g.setColour(juce::Colour::fromRGB(90, 160, 230));
                g.drawRect(r, 2);
            }

            // Machine type badge: abbreviated type in top-right corner for non-sampler tracks.
            {
                const juce::String mid = processor_.getMachineId(t);
                juce::String badge;
                if (mid == juce::String(MidiOutMachine::kMachineId))       badge = "M";
                else if (mid == juce::String(FMMachine::kMachineId))       badge = "FM";
                else if (mid.startsWith("lockstep.stub"))                  badge = "?";
                if (badge.isNotEmpty())
                {
                    g.setColour(juce::Colour::fromRGB(120, 200, 120).withAlpha(0.85f));
                    g.setFont(9.0f);
                    g.drawText(badge, r.reduced(1).withHeight(10), juce::Justification::topRight, false);
                }
            }
        }
    }

    void LockstepEditor::updateTransportGhosting()
    {
        const bool isStandalone =
            (juce::PluginHostType::getPluginLoadedAs()
             == juce::AudioProcessor::wrapperType_Standalone);
        const auto* modeParam =
            processor_.apvts().getRawParameterValue(ParamIDs::syncMode);
        const bool isLocked = modeParam && static_cast<int>(modeParam->load()) == 0;
        const bool ghost    = isLocked && !isStandalone;
        transport_.setGhosted(ghost);
    }

    void LockstepEditor::parentHierarchyChanged()
    {
        auto* newTop = getTopLevelComponent();
        if (newTop == keyListenerTarget_)
            return;
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
        keyListenerTarget_ = newTop;
        if (keyListenerTarget_ != nullptr && keyListenerTarget_ != this)
            keyListenerTarget_->addKeyListener(this);
    }

    void LockstepEditor::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));
        paintMeters(g);  // per-track VU underlaid behind the track buttons
    }

    void LockstepEditor::paintOverChildren(juce::Graphics& g)
    {
        // ---- MHZ.2.2: top-bar dashboard (free space between left controls and right buttons) ----
        // Left zone (~420..640): Bank/Pattern/Part identity + state badges (CK, CHN, QUE, SHR, CPY).
        // Right zone (~640..800): Held-context preview derived from modifier cluster state.
        {
            static constexpr int kBadgeH   = 16;
            static constexpr int kGap       = 3;
            static constexpr int kDashStartX = 420;   // right edge of left controls
            static constexpr int kRightBtnX  = 800;   // left edge of the three right buttons
            static constexpr int kSplitX     = 640;   // dashboard/preview divider
            const int by = (36 - kBadgeH) / 2;

            g.setFont(juce::Font(juce::FontOptions(10.0f)));

            // ---- Left dashboard ----
            {
                // Bank / Pattern / Part identity pill.
                const int bk = processor_.activeBankIdx() + 1;
                const int pt = processor_.activePatternIdx() + 1;
                const int pr = processor_.activePattern().partRef + 1;
                const juce::String identity = "Bk:" + juce::String(bk)
                                            + "  Pt:" + juce::String(pt)
                                            + "  Pr:" + juce::String(pr);
                {
                    const auto r = juce::Rectangle<int>(kDashStartX, by, 120, kBadgeH);
                    g.setColour(juce::Colour(0xFF262830u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colour(0xFFBBCCDDu));
                    g.drawText(identity, r, juce::Justification::centred);
                }

                int bx = kDashStartX + 120 + kGap;

                // Clipboard badge.
                const char* cbLabel = nullptr;
                switch (clipboard_.type)
                {
                    case ClipboardType::None:    break;
                    case ClipboardType::Step:    cbLabel = "CPY:STP"; break;
                    case ClipboardType::Section: cbLabel = "CPY:SEC"; break;
                    case ClipboardType::Track:   cbLabel = "CPY:TRK"; break;
                    case ClipboardType::Pattern: cbLabel = "CPY:PAT"; break;
                }
                if (cbLabel != nullptr && bx + 56 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 56, kBadgeH);
                    g.setColour(juce::Colour(0xFF50B0C8u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::black);
                    g.drawText(cbLabel, r, juce::Justification::centred);
                    bx += 56 + kGap;
                }

                // CK:N checkpoint badge.
                const int checkpointDepth = processor_.checkpointDepth();
                if (checkpointDepth > 0 && bx + 38 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 38, kBadgeH);
                    g.setColour(juce::Colour(0xFF40A080u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText("CK:" + juce::String(checkpointDepth), r, juce::Justification::centred);
                    bx += 38 + kGap;
                }

                // QUE:B.P queued pattern badge.
                if (processor_.hasQueuedPattern() && bx + 52 < kSplitX)
                {
                    const int qBank = processor_.queuedPatternBankIdx() + 1;
                    const int qPat  = processor_.queuedPatternPatIdx()  + 1;
                    const auto r = juce::Rectangle<int>(bx, by, 52, kBadgeH);
                    g.setColour(juce::Colour(0xFFC08020u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText("Q:" + juce::String(qBank) + "." + juce::String(qPat),
                               r, juce::Justification::centred);
                    bx += 52 + kGap;
                }

                // SHR:N part-share badge.
                const int shareCount = processor_.activePartShareCount();
                if (shareCount > 1 && bx + 40 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 40, kBadgeH);
                    g.setColour(juce::Colour(0xFF9040C0u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText("SHR:" + juce::String(shareCount), r, juce::Justification::centred);
                    bx += 40 + kGap;
                }

                // CHN:N chain badge.
                const int chainLen = processor_.chainLength();
                if (chainLen > 0 && bx + 50 < kSplitX)
                {
                    const bool looping = processor_.chainLoopEnabled();
                    const int badgeW = looping ? 42 : 52;
                    const auto r = juce::Rectangle<int>(bx, by, badgeW, kBadgeH);
                    g.setColour(juce::Colour(0xFF20A0C0u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText(juce::String(looping ? "CHN:" : "CHN1:") + juce::String(chainLen),
                               r, juce::Justification::centred);
                }
            }

            // ---- Right held-context preview ----
            // Compose a short description of the currently held modifier cluster.
            {
                juce::String ctx;
                const auto& ui = uiState_;
                // Primary scope token.
                if      (ui.trackHeld)        ctx = "TRACK " + juce::String(keyboardArea_.getActiveTrack() + 1);
                else if (ui.patternScopeHeld) ctx = "PATTERN";
                else if (ui.partHeld)         ctx = "PART";
                else if (ui.sceneHeld)        ctx = "SCENE";
                else if (ui.masterHeld)       ctx = "MASTER";
                else if (ui.muteHeld)         ctx = "MUTE";
                else if (ui.fillHeld)         ctx = "FILL";
                else if (ui.funcHeld)         ctx = "FUNC";

                if (ctx.isEmpty()) return;   // nothing held — preview is blank

                // Qualify with Func if held alongside another modifier.
                if (ui.funcHeld && ctx != "FUNC")
                    ctx = "FUNC + " + ctx;

                // Active section suffix.
                const int activeTrack = keyboardArea_.getActiveTrack();
                if (activeTrack >= 0)
                {
                    const int sec = ui.trackSection[static_cast<std::size_t>(activeTrack)];
                    if (sec >= 0 && sec < IMachine::kMaxSections)
                        ctx += juce::String("  |  ") + juce::String(IMachine::kCanonicalSectionNames[static_cast<std::size_t>(sec)]);
                }

                const int previewW = kRightBtnX - kSplitX - kGap;
                const auto r = juce::Rectangle<int>(kSplitX, by, previewW, kBadgeH);
                g.setColour(juce::Colour(0xFF1E2028u));
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xFFDDEEFFu));
                g.drawText(ctx, r.reduced(4, 0), juce::Justification::centredLeft, true);
            }
        }

        // ---- Diagnostic meters drawn over children ----
        // Master output meter: a thin bar along the very top edge.
        {
            const float level = juce::jlimit(0.0f, 1.0f, masterMeter_);
            const int   w     = juce::roundToInt(static_cast<float>(getWidth()) * level);
            g.setColour(juce::Colour::fromRGB(30, 34, 40));
            g.fillRect(0, 0, getWidth(), 3);
            if (w > 0)
            {
                g.setColour(meterColour(level));
                g.fillRect(0, 0, w, 3);
            }
        }
        // Per-track trig (left, cyan) + MIDI-in (right, magenta) activity dots.
        for (std::size_t i = 0; i < kNumTracks; ++i)
        {
            const auto r = trackBtns_[i].getBounds();
            if (r.isEmpty()) continue;
            constexpr int d = 5;
            if (trigBlink_[i] > 0.02f)
            {
                // White: high contrast against the green/yellow/red VU bar.
                g.setColour(juce::Colours::white.withAlpha(trigBlink_[i]));
                g.fillEllipse(static_cast<float>(r.getX() + 2),
                              static_cast<float>(r.getY() + 2), d, d);
            }
            if (midiBlink_[i] > 0.02f)
            {
                g.setColour(juce::Colour::fromRGB(230, 80, 220).withAlpha(midiBlink_[i]));
                g.fillEllipse(static_cast<float>(r.getRight() - 2 - d),
                              static_cast<float>(r.getY() + 2), d, d);
            }
        }

        if (!isDraggingFiles_)
            return;
        g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.12f));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.7f));
        g.drawRect(getLocalBounds().reduced(4), 2);
        g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
        g.drawText("Drop to add to pool",
                   getLocalBounds(),
                   juce::Justification::centred);
    }

    // -------------------------------------------------------------------------
    // FileDragAndDropTarget

    static bool isAudioFile(const juce::String& path)
    {
        const juce::String ext = juce::File(path).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || ext == ".flac" || ext == ".ogg";
    }

    bool LockstepEditor::isInterestedInFileDrag(const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (isAudioFile(f)) return true;
        return false;
    }

    void LockstepEditor::fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/)
    {
        isDraggingFiles_ = true;
        repaint();
    }

    void LockstepEditor::fileDragExit(const juce::StringArray& /*files*/)
    {
        isDraggingFiles_ = false;
        repaint();
    }

    void LockstepEditor::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
    {
        isDraggingFiles_ = false;
        int loaded = 0;
        for (const auto& path : files)
        {
            if (!isAudioFile(path)) continue;
            if (processor_.samplePool().load(path) >= 0)
                ++loaded;
        }
        if (loaded > 0)
        {
            poolOverlay_.setVisible(true);
            poolOverlay_.toFront(false);
        }
        repaint();
    }

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    bool LockstepEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
    {
        const int rawCode = key.getKeyCode();
        const int uCode   = (rawCode >= 'a' && rawCode <= 'z')
                                ? rawCode - ('a' - 'A')
                                : rawCode;

        // Suppress OS key-repeat: if we already saw this key go down, ignore.
        if (!heldKeys_.insert(uCode).second)
            return true;

        if (QwertyOverlay::isEdgeKey(uCode))
            return true;

        const auto ev = qwerty_.resolve(uCode,
                                        uiState_.funcHeld,
                                        uiState_.trackHeld,
                                        uiState_.muteHeld);

        switch (ev.button)
        {
            case ControllerButton::Func:
                uiState_.funcHeld = true;
                editMode_.onScopeEvent(ev);
                keyboardArea_.repaint();
                repaint();
                return true;

            case ControllerButton::TrackScope:
                uiState_.trackHeld = true;
                processor_.setControlAllActive(true);  // MD.10: active until a track is selected
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::PatternScope:
                uiState_.patternScopeHeld = true;
                uiState_.patternScopeUsed = false;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::MuteScope:
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::FillScope:
                uiState_.fillHeld = true;
                processor_.setFillActive(true);
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::CueScope:
                uiState_.cueHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::SceneScope:
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::MasterScope:
                uiState_.masterHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::PartScope:
                uiState_.partHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case ControllerButton::Section:
            {
                // Determine whether a section-suite scope modifier is held.
                using PS = EditMode::PrimaryScope;
                PS sectionScope = PS::None;
                if      (uiState_.trackHeld)        sectionScope = PS::Track;
                else if (uiState_.patternScopeHeld) sectionScope = PS::Pattern;
                else if (uiState_.partHeld)         sectionScope = PS::Part;
                else if (uiState_.sceneHeld)        sectionScope = PS::Scene;
                else if (uiState_.masterHeld)       sectionScope = PS::Master;

                if (sectionScope != PS::None)
                {
                    // Dim under this scope — no content, block entirely.
                    if (!scopedCell(sectionScope, ev.index).hasContent) return true;

                    // Scope-specific dispatch for cells whose content is implemented.
                    if (sectionScope == PS::Pattern && ev.index == 0)
                    {
                        // Pattern+LEN: track length/divider lives in the TRACK meta section.
                        keyboardArea_.selectMetaSection(2);
                        return true;
                    }
                    if (sectionScope == PS::Part && ev.index == 1)
                    {
                        // Part+MACH: open the machine-select overlay.
                        machineSelectOverlay_.setVisible(!machineSelectOverlay_.isVisible());
                        if (machineSelectOverlay_.isVisible())
                            machineSelectOverlay_.toFront(false);
                        return true;
                    }
                    // All other non-dim scope cells fall through to the machine's own
                    // section (e.g. Track+FLTR → section 2 = post-machine FLTR block).
                }

                // KeyboardArea gates on machine slot availability.
                keyboardArea_.selectSection(ev.index);
                // Track section key hold for Section-scope verb dispatch (MD.3).
                if (heldSectionRawCode_ < 0)
                {
                    heldSectionRawCode_ = rawCode;
                    editMode_.setSectionHeld(true);
                }
                return true;
            }

            case ControllerButton::MetaSection:
                keyboardArea_.selectMetaSection(ev.index);
                return true;

            case ControllerButton::Step:
            {
                // MG.5: Sound Pool mode — step keys select pool entries by index.
                // Holding a key live-swaps the focused track's sound; record-arm
                // captures the pool index as a sound_id override on the held step.
                if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                {
                    const int activeTrack = keyboardArea_.getActiveTrack();
                    if (!uiState_.soundPoolKeyHeld && ev.index < processor_.soundPoolSize())
                    {
                        auto& ctx = processor_.editContext();
                        if (ctx.isActiveForEditing()
                            && ctx.heldTrackIndex() == activeTrack)
                        {
                            auto& trk = processor_.sequence()
                                            .tracks[static_cast<std::size_t>(activeTrack)];
                            for (int heldIdx : ctx.heldSteps())
                            {
                                if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                s.trigOverride.hasSoundId = true;
                                s.trigOverride.soundId    = ev.index;
                                s.trig = true;
                            }
                            ctx.markParamWritten();
                        }
                        uiState_.soundPoolKeyHeld = true;
                        uiState_.soundPoolKeyCode = rawCode;
                        processor_.liveSwapTrackSound(activeTrack, ev.index);
                    }
                    return true;
                }

                // MG.2/MG.3: Retrig mode.
                // Func+step cycles the retrig rate.
                // On a sampler track with slice data, keys play slices (Slice sub-mode).
                // Otherwise, holding any key retrigs the focused track continuously.
                if (uiState_.trigGridMode == TrigGridMode::Retrig)
                {
                    const int activeTrack = keyboardArea_.getActiveTrack();
                    if (uiState_.funcHeld)
                    {
                        uiState_.retrigRateIndex = (uiState_.retrigRateIndex + 1) % 4;
                        keyboardArea_.repaint();
                    }
                    else if (processor_.hasTrackSlices(activeTrack))
                    {
                        // MG.3: Slice sub-mode — each key plays a different slice.
                        // Use note numbers 0-15 so the sampler can identify them as slice triggers.
                        processor_.triggerNote(activeTrack, ev.index, 300);
                    }
                    else if (!uiState_.retrigKeyHeld)
                    {
                        uiState_.retrigKeyHeld = true;
                        uiState_.retrigKeyCode = rawCode;
                        processor_.setRetrigActive(
                            activeTrack,
                            true,
                            UiState::retrigRatePpq(uiState_.retrigRateIndex));
                    }
                    return true;
                }

                // MG.1: Keyboard mode — step keys play chromatic notes; no step editing.
                if (uiState_.trigGridMode == TrigGridMode::Keyboard)
                {
                    const int note = juce::jlimit(0, 127, uiState_.keyboardRoot + ev.index);
                    const int activeTrack = keyboardArea_.getActiveTrack();

                    // If a step is held in the EditContext, write noteOverride to it.
                    auto& ctx = processor_.editContext();
                    if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == activeTrack)
                    {
                        auto& trk = processor_.sequence()
                                        .tracks[static_cast<std::size_t>(activeTrack)];
                        for (int heldIdx : ctx.heldSteps())
                        {
                            if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                            auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                            if (s.trigOverride.noteCount == 0)
                                s.trigOverride.noteCount = 1;
                            s.trigOverride.notes[0] = note;
                            s.trig                 = true;
                        }
                        ctx.markParamWritten();
                    }

                    processor_.triggerNote(activeTrack, note);
                    return true;
                }

                // PatternScope + step:
                //   Stopped — first press: instant swap; subsequent: build chain.
                //   Playing — every press: queue/append (fires at pattern boundary).
                if (uiState_.patternScopeHeld)
                {
                    const int bank     = processor_.activeBankIdx();
                    const bool playing = processor_.clock().inPluginPlaying();
                    if (!uiState_.patternScopeUsed && !playing)
                    {
                        // Immediate swap when stopped; wipes any existing chain.
                        processor_.clearChain();
                        processor_.setActivePattern(bank, ev.index);
                    }
                    else if (!uiState_.patternScopeUsed)
                    {
                        // First press while playing: queue for end of current pattern.
                        processor_.clearChain();
                        processor_.queuePattern(bank, ev.index);
                    }
                    else
                    {
                        // Subsequent presses: append to chain regardless of transport state.
                        processor_.appendToChain(bank, ev.index);
                    }
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }

                // PartScope + step: immediately assign the pattern's Part reference.
                if (uiState_.partHeld)
                {
                    processor_.setActivePatternPart(ev.index);
                    repaint();
                    return true;
                }

                // Ignore key-repeat (same physical key already in list).
                bool alreadyHeld = false;
                for (auto& [code, _] : heldStepKeys_)
                {
                    if (code == rawCode) { alreadyHeld = true; break; }
                }
                if (!alreadyHeld)
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    heldStepKeys_.push_back({ rawCode, absStep });
                    uiState_.stepHeld = true;
                    processor_.editContext().hold(keyboardArea_.getActiveTrack(), absStep);
                    editMode_.setTrigHeld(true);
                }
                return true;
            }

            case ControllerButton::SelectTrack:
                keyboardArea_.setActiveTrack(ev.index);
                processor_.setControlAllActive(false);  // specific track chosen; disable control-all
                return true;

            case ControllerButton::NavUp:
                // Up = next higher track number (user expectation).
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1,
                             keyboardArea_.getActiveTrack() + 1));
                return true;

            case ControllerButton::NavDown:
                // Down = previous (lower) track number.
                keyboardArea_.setActiveTrack(std::max(0, keyboardArea_.getActiveTrack() - 1));
                return true;

            case ControllerButton::NavLeft:
                keyboardArea_.prevPage();
                return true;

            case ControllerButton::NavRight:
                if (uiState_.patternScopeHeld)
                {
                    // PatternScope + NavRight (Func+2 + R): toggle chain loop mode.
                    processor_.setChainLoopEnabled(!processor_.chainLoopEnabled());
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }
                keyboardArea_.nextPage();
                return true;

            // MHY.4: right-utility verbs. Without a scope modifier these perform their
            // default transport / confirmation action; with a scope held, EditMode
            // routes them as grammar verbs (CPY / PST / CLR / confirm / cancel).

            case ControllerButton::VerbPlay:
            {
                using PS = EditMode::PrimaryScope;
                // Scope held → grammar verb (e.g. paste).  No scope → play/stop.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = (now - lastPlayPressTime_) < kDoublePressMsThreshold;
                lastPlayPressTime_ = now;

                if (isDouble)
                {
                    processor_.clock().setInPluginPlaying(false);
                    processor_.clock().resetPhase();
                }
                else
                {
                    processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                }
                return true;
            }

            case ControllerButton::VerbStop:
            {
                using PS = EditMode::PrimaryScope;
                // Scope held → grammar verb (e.g. clear).  No scope → stop transport.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                if (uiState_.patternScopeHeld)
                {
                    processor_.cancelQueuedPattern();
                    uiState_.patternScopeUsed = true;
                    repaint();
                    return true;
                }
                processor_.clock().setInPluginPlaying(false);
                return true;
            }

            case ControllerButton::VerbRecord:
            {
                using PS = EditMode::PrimaryScope;
                // Scope held → grammar verb (e.g. copy).  No scope → arm recording.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                return true;
            }

            case ControllerButton::VerbYes:
                editMode_.onVerb(ev.button);
                return true;

            case ControllerButton::VerbNo:
                editMode_.onVerb(ev.button);
                return true;

            case ControllerButton::Snapshot:
                processor_.pushCheckpoint();
                repaint();
                return true;
            case ControllerButton::Restore:
                processor_.popCheckpoint();
                repaint();
                return true;

            // Legacy transport buttons — kept for any code paths that still emit them.
            case ControllerButton::PlayStop:
                processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                return true;
            case ControllerButton::StopReset:
                processor_.clock().setInPluginPlaying(false);
                processor_.clock().resetPhase();
                return true;
            case ControllerButton::RecordArm:
                processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                return true;

            // Trig grid mode selection (Func+T/Y/U). Pressing the active mode
            // a second time resets to Default (MG.6: exit cleanly — cancel any
            // in-flight retrig or live sound swap on mode exit).
            case ControllerButton::TrigModeKeyboard:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::Keyboard)
                                  ? TrigGridMode::Default : TrigGridMode::Keyboard;
                // MG.6: exiting SoundPool or Retrig when switching to Keyboard.
                if (uiState_.trigGridMode == TrigGridMode::Retrig)
                {
                    uiState_.retrigKeyHeld = false;
                    uiState_.retrigKeyCode = -1;
                    processor_.setRetrigActive(0, false);
                }
                else if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                {
                    uiState_.soundPoolKeyHeld = false;
                    uiState_.soundPoolKeyCode = -1;
                    processor_.clearLiveSwap(keyboardArea_.getActiveTrack());
                }
                uiState_.trigGridMode = next;
                keyboardArea_.repaint();
                return true;
            }
            case ControllerButton::TrigModeRetrig:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::Retrig)
                                  ? TrigGridMode::Default : TrigGridMode::Retrig;
                // MG.6: cancel retrig when exiting the mode.
                if (uiState_.trigGridMode == TrigGridMode::Retrig)
                {
                    uiState_.retrigKeyHeld = false;
                    uiState_.retrigKeyCode = -1;
                    processor_.setRetrigActive(0, false);
                }
                else if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                {
                    uiState_.soundPoolKeyHeld = false;
                    uiState_.soundPoolKeyCode = -1;
                    processor_.clearLiveSwap(keyboardArea_.getActiveTrack());
                }
                uiState_.trigGridMode = next;
                keyboardArea_.repaint();
                return true;
            }
            case ControllerButton::TrigModeSoundPool:
            {
                const auto next = (uiState_.trigGridMode == TrigGridMode::SoundPool)
                                  ? TrigGridMode::Default : TrigGridMode::SoundPool;
                // MG.6: cancel retrig or live swap when exiting the mode.
                if (uiState_.trigGridMode == TrigGridMode::Retrig)
                {
                    uiState_.retrigKeyHeld = false;
                    uiState_.retrigKeyCode = -1;
                    processor_.setRetrigActive(0, false);
                }
                else if (uiState_.trigGridMode == TrigGridMode::SoundPool)
                {
                    uiState_.soundPoolKeyHeld = false;
                    uiState_.soundPoolKeyCode = -1;
                    processor_.clearLiveSwap(keyboardArea_.getActiveTrack());
                }
                uiState_.trigGridMode = next;
                keyboardArea_.repaint();
                return true;
            }

            // MD.6/MD.7: Mute toggle.
            // A+step (no Func) → immediate global mute toggle.
            // Func+A+step → deferred pattern mute (applied atomically on Func release).
            case ControllerButton::ToggleMute:
            {
                const int trackIdx = ev.index;
                if (trackIdx < 0 || trackIdx >= static_cast<int>(kNumTracks))
                    return true;
                if (uiState_.funcHeld)
                {
                    // Deferred pattern mute (MD.7 + MD.8).
                    deferredPatternMutes_.push_back(trackIdx);
                }
                else
                {
                    // Immediate global mute (MD.6).
                    processor_.toggleGlobalMute(trackIdx);
                }
                repaint();
                return true;
            }

            case ControllerButton::ForkPart:
                processor_.forkActivePart();
                repaint();
                return true;

            case ControllerButton::MachineSelect:
                machineSelectOverlay_.setVisible(!machineSelectOverlay_.isVisible());
                if (machineSelectOverlay_.isVisible())
                    machineSelectOverlay_.toFront(false);
                return true;

            case ControllerButton::MetronomeToggle:
                processor_.clock().setMetronomeEnabled(!processor_.clock().isMetronomeEnabled());
                return true;

            case ControllerButton::TapTempo:
                handleTapTempo();
                return true;

            case ControllerButton::None:
                return false;

            default:
                return false;
        }
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
        // Purge any released keys from the repeat-suppression set.
        std::erase_if(heldKeys_, [](int code) {
            return !juce::KeyPress::isKeyCurrentlyDown(code);
        });

        bool handled = false;

        // Left-column modifier key releases.
        if (!isKeyDown && uiState_.funcHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('1')))
        {
            // MD.7/MD.8: apply deferred pattern mute toggles atomically.
            for (const int t : deferredPatternMutes_)
                processor_.togglePatternMute(t);
            deferredPatternMutes_.clear();

            uiState_.funcHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::Func });
            keyboardArea_.repaint();
            keyboardArea_.repaint();
            repaint();
            handled = true;
        }

        // MHY cluster: Track lives on key 2.
        if (!isKeyDown && uiState_.trackHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('2')))
        {
            uiState_.trackHeld = false;
            processor_.setControlAllActive(false);  // MD.10
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::TrackScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Pattern lives on key Q.
        if (!isKeyDown && uiState_.patternScopeHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Q')))
        {
            uiState_.patternScopeHeld = false;
            uiState_.patternScopeUsed = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::PatternScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Part lives on key W (new scope).
        if (!isKeyDown && uiState_.partHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('W')))
        {
            uiState_.partHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::PartScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Mute on Z (unchanged).
        if (!isKeyDown && uiState_.muteHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('Z')))
        {
            uiState_.muteHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::MuteScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Fill on key X.
        if (!isKeyDown && uiState_.fillHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('X')))
        {
            uiState_.fillHeld = false;
            processor_.setFillActive(false);
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::FillScope });
            repaint();
            handled = true;
        }

        // MHY: Cue is not bound to any cluster key post-MHY. The held-state
        // tracking remains in case a future input source emits CueScope events.
        if (!isKeyDown && uiState_.cueHeld)
        {
            uiState_.cueHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::CueScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Scene on key A.
        if (!isKeyDown && uiState_.sceneHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('A')))
        {
            uiState_.sceneHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::SceneScope });
            repaint();
            handled = true;
        }

        // MHY cluster: Master on key S.
        if (!isKeyDown && uiState_.masterHeld
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('S')))
        {
            uiState_.masterHeld = false;
            editMode_.onScopeEvent({ ControllerEvent::Type::ButtonUp, ControllerButton::MasterScope });
            repaint();
            handled = true;
        }

        // Section key release: clear section-held scope.
        if (heldSectionRawCode_ >= 0
            && !juce::KeyPress::isKeyCurrentlyDown(heldSectionRawCode_))
        {
            heldSectionRawCode_ = -1;
            editMode_.setSectionHeld(false);
            handled = true;
        }

        if (!isKeyDown && playKeyHeld_
            && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>('I')))
        {
            playKeyHeld_ = false;
            handled = true;
        }

        // MG.2: retrig key release.
        if (uiState_.retrigKeyHeld
            && uiState_.retrigKeyCode >= 0
            && !juce::KeyPress::isKeyCurrentlyDown(uiState_.retrigKeyCode))
        {
            uiState_.retrigKeyHeld = false;
            uiState_.retrigKeyCode = -1;
            processor_.setRetrigActive(0, false);  // track arg ignored for cancel
            handled = true;
        }

        // MG.5: Sound Pool key release — restore track's original sound.
        if (uiState_.soundPoolKeyHeld
            && uiState_.soundPoolKeyCode >= 0
            && !juce::KeyPress::isKeyCurrentlyDown(uiState_.soundPoolKeyCode))
        {
            uiState_.soundPoolKeyHeld = false;
            uiState_.soundPoolKeyCode = -1;
            processor_.clearLiveSwap(keyboardArea_.getActiveTrack());
            handled = true;
        }

        // Step key releases: for each held step whose physical key is no longer down,
        // release it from the edit context and optionally toggle its trig.
        for (int i = static_cast<int>(heldStepKeys_.size()) - 1; i >= 0; --i)
        {
            auto [code, stepIdx] = heldStepKeys_[static_cast<std::size_t>(i)];
            if (!juce::KeyPress::isKeyCurrentlyDown(code))
            {
                auto& ctx = processor_.editContext();
                const int  track          = ctx.heldTrackIndex();
                const bool paramWasWritten = ctx.wasParamWritten();

                processor_.editContext().release(stepIdx);

                if (!paramWasWritten && track >= 0 && stepIdx >= 0)
                {
                    auto& s = processor_.sequence()
                        .tracks[static_cast<std::size_t>(track)]
                        .steps[static_cast<std::size_t>(stepIdx)];
                    s.trig = !s.trig;
                }

                heldStepKeys_.erase(heldStepKeys_.begin() + i);
                handled = true;
            }
        }
        if (handled && heldStepKeys_.empty())
        {
            uiState_.stepHeld = false;
            editMode_.setTrigHeld(false);
        }

        // Key releases change visual state: step trigs toggle on release, and nav/step
        // keys paint their pressed state via isKeyCurrentlyDown(). Always repaint on
        // key-up so the display doesn't lag behind physical state.
        if (!isKeyDown)
            keyboardArea_.repaint();

        return handled;
    }

    // -------------------------------------------------------------------------
    // Mouse button routing (mirror of keyPressed / keyStateChanged for click input)

    void LockstepEditor::handleTapTempo()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        int kept = 0;
        for (int i = 0; i < tapCount_; ++i)
        {
            if (now - tapTimes_[static_cast<std::size_t>(i)] <= kTapWindowMs)
                tapTimes_[static_cast<std::size_t>(kept++)] = tapTimes_[static_cast<std::size_t>(i)];
        }
        tapCount_ = kept;

        if (tapCount_ >= kTapMaxCount)
        {
            for (int i = 1; i < kTapMaxCount; ++i)
                tapTimes_[static_cast<std::size_t>(i - 1)] = tapTimes_[static_cast<std::size_t>(i)];
            tapCount_ = kTapMaxCount - 1;
        }
        tapTimes_[static_cast<std::size_t>(tapCount_++)] = now;

        if (tapCount_ >= 2)
        {
            const double spanMs    = tapTimes_[static_cast<std::size_t>(tapCount_ - 1)] - tapTimes_[0];
            const double intervals = static_cast<double>(tapCount_ - 1);
            const double bpm       = (60000.0 * intervals) / spanMs;
            if (bpm >= kTapMinBpm && bpm <= kTapMaxBpm)
            {
                processor_.clock().setLocalBpm(bpm);
                repaint();
            }
        }
    }

    void LockstepEditor::handleMouseButtonDown(ControllerEvent ev)
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        switch (ev.button)
        {
            // ---- Scope modifiers ----
            case CB::Func:
                uiState_.funcHeld = true;
                editMode_.onScopeEvent({ T::ButtonDown, CB::Func });
                keyboardArea_.repaint();
                repaint();
                break;

            case CB::TrackScope:
                uiState_.trackHeld = true;
                processor_.setControlAllActive(true);
                editMode_.onScopeEvent({ T::ButtonDown, CB::TrackScope });
                repaint();
                break;

            case CB::PatternScope:
                uiState_.patternScopeHeld = true;
                uiState_.patternScopeUsed = false;
                editMode_.onScopeEvent({ T::ButtonDown, CB::PatternScope });
                repaint();
                break;

            case CB::PartScope:
                uiState_.partHeld = true;
                editMode_.onScopeEvent({ T::ButtonDown, CB::PartScope });
                repaint();
                break;

            case CB::SceneScope:
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent({ T::ButtonDown, CB::SceneScope });
                repaint();
                break;

            case CB::MasterScope:
                uiState_.masterHeld = true;
                editMode_.onScopeEvent({ T::ButtonDown, CB::MasterScope });
                repaint();
                break;

            case CB::MuteScope:
                uiState_.muteHeld = true;
                editMode_.onScopeEvent({ T::ButtonDown, CB::MuteScope });
                repaint();
                break;

            case CB::FillScope:
                uiState_.fillHeld = true;
                processor_.setFillActive(true);
                editMode_.onScopeEvent({ T::ButtonDown, CB::FillScope });
                repaint();
                break;

            // ---- Navigation ----
            case CB::NavUp:
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1,
                             keyboardArea_.getActiveTrack() + 1));
                break;

            case CB::NavDown:
                keyboardArea_.setActiveTrack(std::max(0, keyboardArea_.getActiveTrack() - 1));
                break;

            case CB::NavLeft:
                keyboardArea_.prevPage();
                break;

            case CB::NavRight:
                keyboardArea_.nextPage();
                break;

            // ---- Utility / transport ----
            case CB::TapTempo:
                handleTapTempo();
                break;

            case CB::VerbYes:
            case CB::VerbNo:
                editMode_.onVerb(ev.button);
                break;

            case CB::VerbPlay:
                // Simple toggle (no double-press detection for mouse path).
                processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                break;

            case CB::VerbStop:
                processor_.clock().setInPluginPlaying(false);
                break;

            case CB::VerbRecord:
                processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                break;

            default:
                break;
        }
    }

    void LockstepEditor::handleMouseButtonUp(ControllerEvent ev)
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        switch (ev.button)
        {
            case CB::Func:
                for (const int t : deferredPatternMutes_)
                    processor_.togglePatternMute(t);
                deferredPatternMutes_.clear();
                uiState_.funcHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
                keyboardArea_.repaint();
                repaint();
                break;

            case CB::TrackScope:
                uiState_.trackHeld = false;
                processor_.setControlAllActive(false);
                editMode_.onScopeEvent({ T::ButtonUp, CB::TrackScope });
                repaint();
                break;

            case CB::PatternScope:
                uiState_.patternScopeHeld = false;
                uiState_.patternScopeUsed = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::PatternScope });
                repaint();
                break;

            case CB::PartScope:
                uiState_.partHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::PartScope });
                repaint();
                break;

            case CB::SceneScope:
                uiState_.sceneHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::SceneScope });
                repaint();
                break;

            case CB::MasterScope:
                uiState_.masterHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::MasterScope });
                repaint();
                break;

            case CB::MuteScope:
                uiState_.muteHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::MuteScope });
                repaint();
                break;

            case CB::FillScope:
                uiState_.fillHeld = false;
                processor_.setFillActive(false);
                editMode_.onScopeEvent({ T::ButtonUp, CB::FillScope });
                repaint();
                break;

            default:
                break;  // Nav/verb buttons have no held state to clear.
        }
    }

    void LockstepEditor::applyDisplayMode(GridDisplayMode mode)
    {
        gridMode_ = mode;
        keyboardArea_.setDisplayMode(mode);

        static constexpr const char* kModeLabels[] = { "STG", "ORL", "CLN" };
        displayModeBtn_.setButtonText(kModeLabels[static_cast<int>(mode)]);

        if (auto* prefs = appProps_.getUserSettings())
        {
            prefs->setValue("gridMode", static_cast<int>(mode));
            prefs->saveIfNeeded();
        }
    }

    void LockstepEditor::resized()
    {
        auto bounds = getLocalBounds();

        // Header row: transport | sync mode box | channel mode | display mode | pool button
        auto header = bounds.removeFromTop(36);
        transport_.setBounds(header.removeFromLeft(200).reduced(4));
        syncModeBox_.setBounds(header.removeFromLeft(80).reduced(4));
        channelModeBox_.setBounds(header.removeFromLeft(90).reduced(4));
        displayModeBtn_.setBounds(header.removeFromLeft(46).reduced(4));
        poolBtn_.setBounds(header.removeFromRight(80).reduced(4));
        soundBankBtn_.setBounds(header.removeFromRight(60).reduced(4));
        machineSelectBtn_.setBounds(header.removeFromRight(50).reduced(4));

        // Tempo bar + Manipulation Zone are anchored to the top at fixed heights;
        // the key rows below fill the remaining space, so growing the window makes
        // the (QWERTY-emulating) buttons taller/squarer while the MZ stays put.
        if (tempoBar_)
            tempoBar_->setBounds(bounds.removeFromTop(28).reduced(8, 2));
        bounds.removeFromTop(2);

        // MHX.5: encoder band (MZ 4x2) + vertical crossfader to its right.
        static constexpr int kMZHeight     = 160; // MHX 4x2 MZ (two rows of 4 slots)
        static constexpr int kFaderW       = 28;  // crossfader strip width
        static constexpr int kTrackRowH    = 26;  // track-number + VU row
        static constexpr int kMsRowH       = 22;  // mute/solo row
        {
            auto mzStrip = bounds.removeFromTop(kMZHeight).reduced(8, 4);
            auto faderArea = mzStrip.removeFromRight(kFaderW).reduced(2, 0);
            crossfader_.setBounds(faderArea);
            manipulationZone_.setBounds(mzStrip);
        }

        // Remaining region, laid out top->bottom: track row, mute/solo row,
        // section bar, function bar, step grid.
        {
            auto trackRow = bounds.removeFromTop(kTrackRowH).reduced(8, 2);
            // Page toggle sits at the left edge; the 8 visible track buttons fill the rest.
            static constexpr int kPageBtnW = 36;
            trackPageBtn_.setBounds(trackRow.removeFromLeft(kPageBtnW).reduced(1, 1));
            const int pageStart = trackPage_ * 8;
            const int colW = trackRow.getWidth() / 8;
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const bool visible = (static_cast<int>(i) >= pageStart
                                   && static_cast<int>(i) < pageStart + 8);
                trackBtns_[i].setVisible(visible);
                if (visible)
                    trackBtns_[i].setBounds(trackRow.removeFromLeft(colW).reduced(1, 1));
                else
                    trackBtns_[i].setBounds({});
            }
        }
        {
            auto msRow = bounds.removeFromTop(kMsRowH).reduced(8, 2);
            // Reserve the same width as the page toggle button above.
            static constexpr int kPageBtnW = 36;
            msRow.removeFromLeft(kPageBtnW);
            const int pageStart = trackPage_ * 8;
            const int colW = msRow.getWidth() / 8;
            for (std::size_t i = 0; i < kNumTracks; ++i)
            {
                const bool visible = (static_cast<int>(i) >= pageStart
                                   && static_cast<int>(i) < pageStart + 8);
                muteBtns_[i].setVisible(visible);
                soloBtns_[i].setVisible(visible);
                if (visible)
                {
                    auto col  = msRow.removeFromLeft(colW);
                    auto mute = col.removeFromLeft(col.getWidth() / 2);
                    muteBtns_[i].setBounds(mute.reduced(1, 1));
                    soloBtns_[i].setBounds(col.reduced(1, 1));
                }
                else
                {
                    muteBtns_[i].setBounds({});
                    soloBtns_[i].setBounds({});
                }
            }
        }

        // KeyboardArea owns all four button rows (section bar, function bar, two step rows)
        // and the nav row. Give it the remaining space; it handles the internal layout.
        keyboardArea_.setBounds(bounds);

        poolOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));

        soundBankOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));

        machineSelectOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));
    }

    // -------------------------------------------------------------------------
    // Verb dispatch (MB.3)

    void LockstepEditor::dispatchVerb(EditMode::PrimaryScope scope, ControllerButton verb)
    {
        using PS = EditMode::PrimaryScope;
        using CB = ControllerButton;

        switch (scope)
        {
            // -----------------------------------------------------------------------
            // MD.2  Step copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Trig:
            {
                const auto& ctx = processor_.editContext();
                if (!ctx.isActiveForEditing()) break;
                const int track = ctx.heldTrackIndex();
                auto& trk = processor_.sequence().tracks[static_cast<std::size_t>(track)];

                if (verb == CB::VerbRecord)
                {
                    const auto& held = ctx.heldSteps();
                    if (held.empty()) break;
                    int anchor = *std::min_element(held.begin(), held.end());
                    clipboard_.stepEntries.clear();
                    for (int idx : held)
                    {
                        if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                        clipboard_.stepEntries.push_back(
                            { idx - anchor, trk.steps[static_cast<std::size_t>(idx)] });
                    }
                    std::sort(clipboard_.stepEntries.begin(), clipboard_.stepEntries.end(),
                              [](const StepClipEntry& a, const StepClipEntry& b)
                              { return a.relOffset < b.relOffset; });
                    clipboard_.type = ClipboardType::Step;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Step) break;
                    const int anchor  = ctx.heldStepIndex();
                    const int trkLen  = trk.length;
                    for (const auto& entry : clipboard_.stepEntries)
                    {
                        int dst = (anchor + entry.relOffset);
                        dst = ((dst % trkLen) + trkLen) % trkLen;
                        trk.steps[static_cast<std::size_t>(dst)] = entry.data;
                    }
                }
                else if (verb == CB::VerbStop)
                {
                    for (int idx : ctx.heldSteps())
                    {
                        processor_.clearStepLocks(track, idx);
                        auto& s = trk.steps[static_cast<std::size_t>(idx)];
                        s.trig      = false;
                        s.condition = TrigCondition{};
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.3  Section copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Section:
            {
                const int activeTrack = keyboardArea_.getActiveTrack();
                const int secIdx      = uiState_.trackSection[static_cast<std::size_t>(activeTrack)];
                auto& trk = processor_.sequence()
                                .tracks[static_cast<std::size_t>(activeTrack)];
                const int trkLen    = trk.length;
                const int numSlots  = processor_.numParams(activeTrack);

                if (verb == CB::VerbRecord)
                {
                    clipboard_.sectionSlots.clear();
                    clipboard_.sectionTrackLength = trkLen;
                    for (int sl = 0; sl < numSlots; ++sl)
                    {
                        if (processor_.paramSpec(activeTrack, sl).sectionIndex != secIdx)
                            continue;
                        SectionClipSlot entry;
                        entry.slot = sl;
                        entry.perStep.reserve(static_cast<std::size_t>(trkLen));
                        for (int st = 0; st < trkLen; ++st)
                        {
                            const auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                            const bool  has   = plock.has(sl);
                            entry.perStep.push_back({ has, has ? plock.get(sl, 0.0f) : 0.0f });
                        }
                        clipboard_.sectionSlots.push_back(std::move(entry));
                    }
                    clipboard_.type = ClipboardType::Section;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Section) break;
                    for (const auto& entry : clipboard_.sectionSlots)
                    {
                        const int steps = std::min(static_cast<int>(entry.perStep.size()), trkLen);
                        for (int st = 0; st < steps; ++st)
                        {
                            auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                            if (entry.perStep[static_cast<std::size_t>(st)].first)
                                plock.set(entry.slot, entry.perStep[static_cast<std::size_t>(st)].second);
                            else
                                plock.clear(entry.slot);
                        }
                    }
                }
                else if (verb == CB::VerbStop)
                {
                    for (int sl = 0; sl < numSlots; ++sl)
                    {
                        if (processor_.paramSpec(activeTrack, sl).sectionIndex != secIdx)
                            continue;
                        for (int st = 0; st < trkLen; ++st)
                            trk.steps[static_cast<std::size_t>(st)].overrides.clear(sl);
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.4  Track copy / paste / clear
            // -----------------------------------------------------------------------
            case PS::Track:
            {
                const int activeTrack = keyboardArea_.getActiveTrack();
                auto& trk = processor_.sequence()
                                .tracks[static_cast<std::size_t>(activeTrack)];

                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipTrack = trk;
                    clipboard_.type      = ClipboardType::Track;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Track) break;
                    trk = clipboard_.clipTrack;
                }
                else if (verb == CB::VerbStop)
                {
                    // Reset all steps; preserve length, divider, and base params.
                    for (auto& s : trk.steps)
                    {
                        s.trig       = false;
                        s.condition  = TrigCondition{};
                        s.overrides  = PLock{};
                        s.trigOverride = TrigOverride{};
                    }
                }
                break;
            }

            // -----------------------------------------------------------------------
            // MD.5  Pattern copy / paste / clear  (Pattern+Record = copy, not fork)
            // -----------------------------------------------------------------------
            case PS::Pattern:
            {
                auto& pat = processor_.activePattern();

                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipSequence    = pat.sequence;
                    clipboard_.clipPatternMutes = pat.patternMutes;
                    clipboard_.type            = ClipboardType::Pattern;
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Pattern) break;
                    pat.sequence     = clipboard_.clipSequence;
                    pat.patternMutes = clipboard_.clipPatternMutes;
                }
                else if (verb == CB::VerbStop)
                {
                    for (auto& trk : pat.sequence.tracks)
                    {
                        for (auto& s : trk.steps)
                        {
                            s.trig       = false;
                            s.condition  = TrigCondition{};
                            s.overrides  = PLock{};
                            s.trigOverride = TrigOverride{};
                        }
                    }
                    pat.patternMutes.fill(false);
                }
                break;
            }

            case PS::Func:
            case PS::Mute:
            case PS::Fill:
            case PS::Cue:
            case PS::Scene:
            case PS::Master:
            case PS::Part:  // MHY: Part-scope verbs land here once the kit verbs are wired
                break;

            case PS::None:
            {
                // VerbStop with no scope: clear the active-slot P-Lock.
                if (verb == CB::VerbStop)
                {
                    const auto& ctx = processor_.editContext();
                    if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                        processor_.clearParam(ctx.heldTrackIndex(),
                                              ctx.heldStepIndex(),
                                              ctx.activeSlot());
                }
                // VerbYes / VerbNo with no scope: checkpoint push / pop.
                else if (verb == CB::VerbYes)
                {
                    processor_.pushCheckpoint();
                }
                else if (verb == CB::VerbNo)
                {
                    processor_.popCheckpoint();
                }
                break;
            }

            default:
                break;
        }

        // Chrome must repaint after any verb that may change clipboard or checkpoint state.
        repaint();
    }
}
