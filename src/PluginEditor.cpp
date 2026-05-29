#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "core/TrackInputMode.h"
#include "machine/IMachine.h"
#include "machine/SamplerMachine.h"
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

        // Wire mouse button events from KeyboardArea through the unified dispatch.
        keyboardArea_.onButtonDown = [this](ControllerEvent ev)
        {
            pressTracker_.press(PressTracker::kMouseSource, ev.button, ev.index);
            dispatchDown(ev, PressTracker::kMouseSource);
            keyboardArea_.repaint();
        };
        keyboardArea_.onButtonUp = [this](ControllerEvent ev)
        {
            pressTracker_.release(PressTracker::kMouseSource);
            dispatchUp(ev, PressTracker::kMouseSource);
            keyboardArea_.repaint();
        };

        // Give KeyboardArea a pointer to the shared PressTracker so its paint
        // methods can query pressed state from both keyboard and mouse sources.
        keyboardArea_.setPressTracker(&pressTracker_);

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

        // Reconcile: release any keyboard press whose key is no longer physically
        // down (catches stuck modifiers/steps after Alt-Tab or window deactivation).
        std::vector<std::pair<int, ControllerEvent>> toRelease;
        pressTracker_.forEachReleasedKeyboard([&](int src, ControllerButton btn, int idx) {
            toRelease.push_back({ src, { ControllerEvent::Type::ButtonUp, btn, idx, 0 } });
        });
        if (!toRelease.empty())
        {
            std::erase_if(heldKeys_, [](int code) {
                return !juce::KeyPress::isKeyCurrentlyDown(code);
            });
            for (auto& [src, relEv] : toRelease)
            {
                pressTracker_.release(src);
                dispatchUp(relEv, src);
            }
            repaint();
        }
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
                const juce::String badge { processor_.trackBadge(t) };
                if (badge.isNotEmpty())
                {
                    g.setColour(juce::Colour::fromRGB(180, 185, 190).withAlpha(0.85f));
                    g.setFont(9.0f);
                    g.drawText(badge, r.reduced(1).withHeight(10).withTrimmedRight(6),
                               juce::Justification::topRight, false);
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

    void LockstepEditor::focusLost(FocusChangeType /*cause*/)
    {
        // Release any held mouse state so a focus-loss mid-click doesn't leave
        // a modifier or step stuck (keyboard is reconciled in timerCallback).
        if (const auto entry = pressTracker_.mouseEntry())
        {
            pressTracker_.release(PressTracker::kMouseSource);
            dispatchUp({ ControllerEvent::Type::ButtonUp, entry->button, entry->index, 0 },
                       PressTracker::kMouseSource);
            keyboardArea_.repaint();
        }
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
                    bx += badgeW + kGap;
                }

                // MHZ.7.1: per-track input-mode badge — shown when focused track is not in PLAY mode.
                {
                    const int track = keyboardArea_.getActiveTrack();
                    const auto mode = (track >= 0 && track < static_cast<int>(kNumTracks))
                                      ? uiState_.trackInputMode[static_cast<std::size_t>(track)]
                                      : TrackInputMode::Play;
                    const char* modeLabel = nullptr;
                    juce::Colour modeCol;
                    switch (mode)
                    {
                        case TrackInputMode::Play:      break;  // no badge
                        case TrackInputMode::Chromatic: modeLabel = "CHROM"; modeCol = juce::Colour(0xFF4090E0u); break;
                        case TrackInputMode::Levels:    modeLabel = "LEVLS"; modeCol = juce::Colour(0xFFE07030u); break;
                    }
                    if (modeLabel != nullptr && bx + 46 < kSplitX)
                    {
                        const auto r = juce::Rectangle<int>(bx, by, 46, kBadgeH);
                        g.setColour(modeCol);
                        g.fillRoundedRectangle(r.toFloat(), 3.0f);
                        g.setColour(juce::Colours::white);
                        g.drawText(modeLabel, r, juce::Justification::centred);
                    }
                }
            }

            // ---- Right held-context preview ----
            // Compose a short description of the currently held modifier cluster.
            {
                juce::String ctx;
                const auto& ui = uiState_;
                // MHZ.3.5: Func+Part = machine picker — show dedicated hint.
                if (ui.funcPartHeld)
                {
                    ctx = "FUNC + MACH  |  press step to select machine";
                }
                // MHZ.3.4: P-lock clear mode.
                else if (ui.pLockClearMode)
                {
                    ctx = "FUNC + STEP " + juce::String(ui.pLockClearStep + 1)
                          + "  |  press cell to clear P-Lock slot";
                }
                else
                {
                    // Step held (no modifier) → P-Lock edit mode.
                    const auto& ec = processor_.editContext();
                    if (ui.stepHeld && ec.isActiveForEditing())
                    {
                        const int stepNum = ec.heldStepIndex() + 1;
                        const int cnt     = static_cast<int>(ec.heldSteps().size());
                        ctx = cnt > 1
                            ? juce::String(cnt) + " STEPS  |  turn knob to P-Lock"
                            : "STEP " + juce::String(stepNum) + "  |  turn knob to P-Lock";
                    }
                    // Primary scope token. MHZ.7.2: show mode hint when Track+Control-All.
                    else if (ui.trackHeld && processor_.controlAllActive())
                    {
                        ctx = "TRACK  |  I=CHROM  O=LEVLS  Y=PLAY";
                    }
                    else if (ui.trackHeld)        ctx = "TRACK " + juce::String(keyboardArea_.getActiveTrack() + 1);
                    else if (ui.patternScopeHeld) ctx = "PATTERN";
                    else if (ui.partHeld)         ctx = "PART";
                    else if (ui.sceneHeld)        ctx = "SCENE";
                    else if (ui.masterHeld)       ctx = "MASTER";
                    else if (ui.muteHeld)         ctx = "MUTE";
                    else if (ui.fillHeld)         ctx = "FILL";
                    else if (ui.funcHeld)         ctx = "FUNC";
                }

                if (ctx.isEmpty()) return;   // nothing held — preview is blank

                // Qualify with Func if held alongside another modifier (normal path only).
                if (!ui.funcPartHeld && !ui.pLockClearMode
                    && ui.funcHeld && ctx != "FUNC")
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

    void LockstepEditor::updateFillActivation()
    {
        const bool fillHeld = uiState_.fillHeld;
        const bool funcHeld = uiState_.funcHeld;
        if (fillHeld && funcHeld)
        {
            if (fillLockedTrack_ < 0)
                fillLockedTrack_ = keyboardArea_.getActiveTrack();
            processor_.setFillActive(true, /*allTracks=*/false, fillLockedTrack_);
        }
        else
        {
            fillLockedTrack_ = -1;
            processor_.setFillActive(fillHeld, /*allTracks=*/true, -1);
        }
    }

    // -------------------------------------------------------------------------
    // MHZ.9.4: universal latch escape

    void LockstepEditor::escapeAllLatches()
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        const auto prevLatch = uiState_.latch;
        uiState_.latch = {};  // clear all latches before calling dispatchUp so guards pass

        // For each latched modifier that isn't physically held, do a full release.
        // dispatchUp now checks !uiState_.latch.xxx (already false), so it runs completely.
        if (prevLatch.pattern && !physHeld_.pattern)
            dispatchUp({ T::ButtonUp, CB::PatternScope });
        if (prevLatch.scene && !physHeld_.scene)
            dispatchUp({ T::ButtonUp, CB::SceneScope });
        if (prevLatch.mute && !physHeld_.mute)
            dispatchUp({ T::ButtonUp, CB::MuteScope });
        if (prevLatch.track && !physHeld_.track)
            dispatchUp({ T::ButtonUp, CB::TrackScope });
        if (prevLatch.part && !physHeld_.part)
            dispatchUp({ T::ButtonUp, CB::PartScope });
        if (prevLatch.master && !physHeld_.master)
            dispatchUp({ T::ButtonUp, CB::MasterScope });
        if (prevLatch.fill && !physHeld_.fill)
            dispatchUp({ T::ButtonUp, CB::FillScope });

        // Release latched steps (keep them in heldStepKeys_ if still physically held).
        auto& ctx = processor_.editContext();
        for (const int s : ctx.latchedSteps())
        {
            // Only release from EditContext if the physical key is not held.
            bool physDown = false;
            for (const auto& [code, idx] : heldStepKeys_)
                if (idx == s) { physDown = true; break; }
            if (!physDown)
                ctx.release(s);
        }
        ctx.clearAllLatched();

        if (ctx.heldSteps().empty())
        {
            heldStepKeys_.clear();
            uiState_.stepHeld = false;
            editMode_.setTrigHeld(false);
        }

        uiState_.latch.anySteps = ctx.hasAnyLatchedStep();
        keyboardArea_.repaint();
        repaint();
    }

    // -------------------------------------------------------------------------
    // MHZ.9.3: column-exclusivity-aware modifier latch toggle.

    void LockstepEditor::setModifierLatch(ControllerButton cb, bool set)
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        if (set)
        {
            // Enforce column exclusivity by releasing any existing latch in the same column.
            // Calling dispatchUp (after clearing the latch bool) does the full release with
            // side effects (setControlAllActive, updateFillActivation, etc.).
            const bool isCol1 = (cb == CB::PatternScope || cb == CB::SceneScope
                                 || cb == CB::MuteScope);

            auto releaseOther = [&](bool& latchBool, bool physHeld, CB btn) {
                if (!latchBool || cb == btn) return;
                latchBool = false;
                if (!physHeld)
                    dispatchUp({ T::ButtonUp, btn });
            };

            if (isCol1)
            {
                releaseOther(uiState_.latch.pattern, physHeld_.pattern, CB::PatternScope);
                releaseOther(uiState_.latch.scene,   physHeld_.scene,   CB::SceneScope);
                releaseOther(uiState_.latch.mute,    physHeld_.mute,    CB::MuteScope);
            }
            else
            {
                releaseOther(uiState_.latch.track,  physHeld_.track,  CB::TrackScope);
                releaseOther(uiState_.latch.part,   physHeld_.part,   CB::PartScope);
                releaseOther(uiState_.latch.master, physHeld_.master, CB::MasterScope);
                releaseOther(uiState_.latch.fill,   physHeld_.fill,   CB::FillScope);
            }
        }

        switch (cb)
        {
            case CB::PatternScope: uiState_.latch.pattern = set; break;
            case CB::SceneScope:   uiState_.latch.scene   = set; break;
            case CB::MuteScope:    uiState_.latch.mute    = set; break;
            case CB::TrackScope:   uiState_.latch.track   = set; break;
            case CB::PartScope:    uiState_.latch.part    = set; break;
            case CB::MasterScope:  uiState_.latch.master  = set; break;
            case CB::FillScope:    uiState_.latch.fill    = set; break;
            default: break;
        }
    }

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    // dispatchDown — source-agnostic button-down handler fed by both keyboard
    // and mouse.  rawCode is the physical key code (keyboard) or 0 (mouse).
    bool LockstepEditor::dispatchDown(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;
        switch (ev.button)
        {
            case CB::Func:
                uiState_.funcHeld = true;
                editMode_.onScopeEvent(ev);
                updateFillActivation();
                keyboardArea_.repaint();
                repaint();
                // MHZ.9.4: Func never latches; double-tap = universal escape (only if latches engaged).
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::Func), now)
                        && (uiState_.latch.any() || processor_.editContext().hasAnyLatchedStep()))
                    {
                        escapeAllLatches();
                    }
                }
                return true;

            case CB::TrackScope:
                physHeld_.track = true;
                uiState_.trackHeld = true;
                processor_.setControlAllActive(true);  // MD.10: active until a track is selected
                editMode_.onScopeEvent(ev);
                // MHZ.9.3: double-tap toggles latch.
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::TrackScope), now))
                        setModifierLatch(CB::TrackScope, !uiState_.latch.track);
                }
                repaint();
                return true;

            case CB::PatternScope:
                physHeld_.pattern = true;
                uiState_.patternScopeHeld = true;
                uiState_.patternScopeUsed = false;
                editMode_.onScopeEvent(ev);
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::PatternScope), now))
                        setModifierLatch(CB::PatternScope, !uiState_.latch.pattern);
                }
                repaint();
                return true;

            case CB::MuteScope:
                physHeld_.mute = true;
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::MuteScope), now))
                        setModifierLatch(CB::MuteScope, !uiState_.latch.mute);
                }
                repaint();
                return true;

            case CB::FillScope:
                physHeld_.fill = true;
                uiState_.fillHeld = true;
                editMode_.onScopeEvent(ev);
                updateFillActivation();
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::FillScope), now))
                        setModifierLatch(CB::FillScope, !uiState_.latch.fill);
                }
                repaint();
                return true;

            case CB::CueScope:
                physHeld_.cue = true;
                uiState_.cueHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case CB::SceneScope:
                physHeld_.scene = true;
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent(ev);
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::SceneScope), now))
                        setModifierLatch(CB::SceneScope, !uiState_.latch.scene);
                }
                repaint();
                return true;

            case CB::MasterScope:
                physHeld_.master = true;
                uiState_.masterHeld = true;
                editMode_.onScopeEvent(ev);
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::MasterScope), now))
                        setModifierLatch(CB::MasterScope, !uiState_.latch.master);
                }
                repaint();
                return true;

            case CB::PartScope:
                physHeld_.part = true;
                uiState_.partHeld = true;
                editMode_.onScopeEvent(ev);
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (doubleTap_.recordAndCheck(1000 + static_cast<int>(CB::PartScope), now))
                        setModifierLatch(CB::PartScope, !uiState_.latch.part);
                }
                keyboardArea_.repaint();
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
                        // Part+SRC: machine picker — re-skin the step grid to show machines.
                        uiState_.funcPartHeld = true;
                        keyboardArea_.repaint();
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
                if (uiState_.funcHeld && ev.index == 0)
                    uiState_.funcTrigHeld = true;  // Func+Trig compound — enables note-edit gesture
                keyboardArea_.selectMetaSection(ev.index);
                return true;

            case ControllerButton::Step:
            {
                // MHZ.7.3: CHROMATIC mode — step keys are a piano keyboard.
                // Piano layout via kPianoNoteOffset; dead keys (offset -1) are no-ops.
                // With step held: write noteOverride. Always: trigger live note.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks)
                        && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Chromatic
                        && uiState_.trigGridMode == TrigGridMode::Default)
                    {
                        if (ev.index < 0 || ev.index >= 16) return true;
                        const int semitone = kPianoNoteOffset[static_cast<std::size_t>(ev.index)];
                        if (semitone < 0) return true;  // dead key (D, H, ;)
                        const int note = juce::jlimit(0, 127,
                            (uiState_.noteEditOctave + 1) * 12 + semitone);

                        // If a step is held in the EditContext, write noteOverride to it.
                        auto& ctx = processor_.editContext();
                        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                        {
                            auto& trk = processor_.sequence()
                                            .tracks[static_cast<std::size_t>(at)];
                            for (int heldIdx : ctx.heldSteps())
                            {
                                if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                if (s.trigOverride.noteCount == 0)
                                    s.trigOverride.noteCount = 1;
                                s.trigOverride.notes[0] = note;
                                s.trig                  = true;
                            }
                            ctx.markParamWritten();
                        }

                        uiState_.lastPlayedNote[static_cast<std::size_t>(at)] = note;
                        processor_.triggerNote(at, note);
                        return true;
                    }
                }

                // MHZ.7.4: LEVELS mode — step cells are 16 velocity buckets.
                // Vel = roundToInt((cellIdx+1)/16 * 127).
                // Always auditions the note at the chosen velocity.
                // Step held → also write velocity override to held step (bypass editorial).
                // No step held + stopped → also set track base velocity.
                // No step held + record-arm + playing → records trig+velocity to nearest step.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks)
                        && uiState_.trackInputMode[static_cast<std::size_t>(at)] == TrackInputMode::Levels
                        && uiState_.trigGridMode == TrigGridMode::Default)
                    {
                        const int vel = juce::roundToInt((ev.index + 1.0f) / 16.0f * 127.0f);
                        const int pitch = uiState_.lastPlayedNote[static_cast<std::size_t>(at)];
                        const int note  = pitch > 0 ? pitch : 60;
                        auto& ctx = processor_.editContext();

                        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == at)
                        {
                            // Step held → write velocity override; audition bypasses editorial.
                            auto& trk = processor_.sequence()
                                            .tracks[static_cast<std::size_t>(at)];
                            for (int heldIdx : ctx.heldSteps())
                            {
                                if (heldIdx < 0 || heldIdx >= kMaxStepsPerTrack) continue;
                                auto& s = trk.steps[static_cast<std::size_t>(heldIdx)];
                                s.trigOverride.hasVelocity = true;
                                s.trigOverride.velocity    = vel;
                            }
                            ctx.markParamWritten();
                            processor_.triggerNote(at, note, 350, vel, /*bypassEditorial=*/true);
                        }
                        else
                        {
                            // No step held: set base velocity when stopped; always audition.
                            if (!processor_.clock().inPluginPlaying())
                                processor_.sequence()
                                    .tracks[static_cast<std::size_t>(at)]
                                    .trigDefaults.velocity = vel;
                            // Goes through onNoteOn: records if armed+playing; else just plays.
                            processor_.triggerNote(at, note, 350, vel);
                        }

                        keyboardArea_.repaint();
                        return true;
                    }
                }

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
                    // Materialise empty slot before selecting it.
                    // Pat+step = copy current; Func+Pat+step = blank (inherits Part ref).
                    if (!processor_.isPatternInitialised(bank, ev.index))
                        processor_.materialisePattern(bank, ev.index, !uiState_.funcHeld);
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
                    keyboardArea_.repaint();
                    return true;
                }

                // PartScope + step: assign the pattern's Part reference.
                // Part+step = copy current Part into empty slot (or plain select if occupied).
                // Func+Part+step = create default Part in empty slot (or plain select if occupied).
                if (uiState_.partHeld && !uiState_.funcPartHeld)
                {
                    const int bank = processor_.activeBankIdx();
                    if (ev.index >= 0 && ev.index < static_cast<int>(kPartsPerBank)
                        && !processor_.isPartInitialised(bank, ev.index))
                    {
                        // copy=true → copy current Part; copy=false → default Part.
                        processor_.materialisePart(bank, ev.index, !uiState_.funcHeld);
                    }
                    processor_.setActivePatternPart(ev.index);
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // MHZ.3.5: Func+Part (machine picker) + step: assign machine by index.
                if (uiState_.funcPartHeld)
                {
                    const int numMachines = processor_.numAvailableMachines();
                    if (ev.index >= 0 && ev.index < numMachines)
                    {
                        const std::string machineId {
                            processor_.availableMachineInfo(ev.index).id };
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(), machineId);
                        keyboardArea_.syncToActiveTrack();
                    }
                    // Stay in picker mode until Func/Part is released.
                    keyboardArea_.repaint();
                    return true;
                }

                // NoteEdit mode: step keys are a 1-octave chromatic keyboard.
                // Cells 0-11 = semitones C through B; cells 12-15 = unused.
                if (uiState_.noteEditMode)
                {
                    const int semitone = ev.index;
                    if (semitone >= 0 && semitone < 12)
                    {
                        const int absNote = (uiState_.noteEditOctave + 1) * 12 + semitone;
                        const int activeTrack = keyboardArea_.getActiveTrack();
                        auto& trk = processor_.sequence()
                                        .tracks[static_cast<std::size_t>(activeTrack)];
                        for (const int stepIdx : uiState_.noteEditSteps)
                        {
                            if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                            auto& s = trk.steps[static_cast<std::size_t>(stepIdx)];
                            auto& staged = uiState_.noteEditStaged[stepIdx];
                            bool inNotes = false;
                            for (int n = 0; n < s.trigOverride.noteCount; ++n)
                                if (s.trigOverride.notes[n] == absNote) { inNotes = true; break; }
                            if (inNotes && staged.count(absNote) == 0)
                                staged.insert(absNote);        // stage for removal
                            else if (inNotes && staged.count(absNote) > 0)
                                staged.erase(absNote);         // cancel removal
                            else if (!inNotes && s.trigOverride.noteCount < kMaxNotesPerStep)
                            {
                                s.trigOverride.notes[s.trigOverride.noteCount++] = absNote;
                                s.trig = true;
                            }
                        }
                    }
                    keyboardArea_.repaint();
                    return true;
                }

                // Func+Trig+step: tentative note-edit entry.
                // Suppress pLockClearMode; NoteEdit mode activates on step key release.
                if (uiState_.funcTrigHeld && !uiState_.noteEditMode)
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    uiState_.noteEditSteps = { absStep };
                    keyboardArea_.repaint();
                    return true;
                }

                // MHZ.3.4: P-Lock clear mode — a second step press stages/un-stages a slot.
                // Cell index maps into a packed list of the step's P-locked slots (not by
                // raw slot index). Staged removals are committed on Func release.
                if (uiState_.pLockClearMode)
                {
                    const int cellIdx  = ev.index;
                    const int track    = uiState_.pLockClearTrack;
                    const int step     = uiState_.pLockClearStep;
                    const int numSlots = processor_.numParams(track);
                    if (track >= 0 && step >= 0 && step < kMaxStepsPerTrack)
                    {
                        // Rebuild the packed slot list (same order as the render).
                        const auto& stepData = processor_.sequence()
                            .tracks[static_cast<std::size_t>(track)]
                            .steps[static_cast<std::size_t>(step)];
                        std::vector<int> lockedSlots;
                        const auto& tov = stepData.trigOverride;
                        if (tov.hasVelocity)   lockedSlots.push_back(-2);
                        if (tov.hasGate)       lockedSlots.push_back(-3);
                        for (int s = 0; s < numSlots; ++s)
                            if (stepData.overrides.has(s))
                                lockedSlots.push_back(s);

                        if (cellIdx >= 0 && cellIdx < static_cast<int>(lockedSlots.size()))
                        {
                            const int slotIdx = lockedSlots[static_cast<std::size_t>(cellIdx)];
                            if (uiState_.pLockClearStaged.count(slotIdx) > 0)
                                uiState_.pLockClearStaged.erase(slotIdx);  // cancel
                            else
                                uiState_.pLockClearStaged.insert(slotIdx); // stage
                        }
                    }
                    keyboardArea_.repaint();
                    return true;
                }

                // MHZ.3.4: Func + step (no existing step held) → enter P-Lock clear mode.
                if (uiState_.funcHeld && heldStepKeys_.empty())
                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    uiState_.pLockClearStaged.clear();  // fresh session
                    uiState_.pLockClearMode  = true;
                    uiState_.pLockClearTrack = keyboardArea_.getActiveTrack();
                    uiState_.pLockClearStep  = absStep;
                    keyboardArea_.repaint();
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
                    repaint();
                }
                return true;
            }

            case ControllerButton::SelectTrack:
                if (uiState_.trackHeld && processor_.isTrackEmpty(ev.index))
                {
                    // Track+empty step = copy current track's machine+params (no steps).
                    // Func+Track+empty step = create a default sampler track.
                    if (uiState_.funcHeld)
                        processor_.setTrackMachine(ev.index,
                            std::string(SamplerMachine::kMachineId));
                    else
                        processor_.copyPartTrack(keyboardArea_.getActiveTrack(), ev.index);
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                keyboardArea_.setActiveTrack(ev.index);
                processor_.setControlAllActive(false);  // specific track chosen; disable control-all
                return true;

            case ControllerButton::NavUp:
                // Normal: next higher track number.
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1,
                             keyboardArea_.getActiveTrack() + 1));
                return true;

            case ControllerButton::NavDown:
                // Normal: previous (lower) track number.
                keyboardArea_.setActiveTrack(std::max(0, keyboardArea_.getActiveTrack() - 1));
                return true;

            case ControllerButton::NavLeft:
            {
                // Note-edit mode and CHROMATIC mode both use NavLeft/Right for octave shift.
                const int tl = keyboardArea_.getActiveTrack();
                const bool chromL = tl >= 0 && tl < static_cast<int>(kNumTracks)
                    && uiState_.trackInputMode[static_cast<std::size_t>(tl)] == TrackInputMode::Chromatic;
                if (uiState_.noteEditMode || chromL)
                {
                    uiState_.noteEditOctave = std::max(uiState_.noteEditOctave - 1, 0);
                    keyboardArea_.repaint();
                    return true;
                }
                keyboardArea_.prevPage();
                return true;
            }

            case ControllerButton::NavRight:
            {
                const int tr = keyboardArea_.getActiveTrack();
                const bool chromR = tr >= 0 && tr < static_cast<int>(kNumTracks)
                    && uiState_.trackInputMode[static_cast<std::size_t>(tr)] == TrackInputMode::Chromatic;
                if (uiState_.noteEditMode || chromR)
                {
                    uiState_.noteEditOctave = std::min(uiState_.noteEditOctave + 1, 8);
                    keyboardArea_.repaint();
                    return true;
                }
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
            }

            // MHY.4: right-utility verbs. Without a scope modifier these perform their
            // default transport / confirmation action; with a scope held, EditMode
            // routes them as grammar verbs (CPY / PST / CLR / confirm / cancel).

            case ControllerButton::VerbPlay:
            {
                using PS = EditMode::PrimaryScope;
                // MHZ.7.2: Track (no specific track selected) + VerbPlay → CHROMATIC mode.
                if (uiState_.trackHeld && processor_.controlAllActive())
                {
                    const int t = keyboardArea_.getActiveTrack();
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        uiState_.trackInputMode[static_cast<std::size_t>(t)] = TrackInputMode::Chromatic;
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
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
                // MHZ.7.2: Track (no specific track selected) + VerbStop → LEVELS mode.
                if (uiState_.trackHeld && processor_.controlAllActive())
                {
                    const int t = keyboardArea_.getActiveTrack();
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        uiState_.trackInputMode[static_cast<std::size_t>(t)] = TrackInputMode::Levels;
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
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
                // MHZ.7.2: Track (no specific track selected) + VerbYes → PLAY mode.
                if (uiState_.trackHeld && processor_.controlAllActive())
                {
                    const int t = keyboardArea_.getActiveTrack();
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        uiState_.trackInputMode[static_cast<std::size_t>(t)] = TrackInputMode::Play;
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
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
                if (uiState_.noteEditMode)  // Func+E = NavLeft: octave down
                {
                    uiState_.noteEditOctave = std::max(uiState_.noteEditOctave - 1, 0);
                    keyboardArea_.repaint();
                    return true;
                }
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
                if (uiState_.noteEditMode)  // Func+T = NavRight: octave up
                {
                    uiState_.noteEditOctave = std::min(uiState_.noteEditOctave + 1, 8);
                    keyboardArea_.repaint();
                    return true;
                }
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
                    // Flip the pending-display flag so the re-skin shows the
                    // net result immediately (XOR on each press, same as commit).
                    const auto ti = static_cast<std::size_t>(trackIdx);
                    uiState_.pendingPatternMuteToggle[ti] =
                        !uiState_.pendingPatternMuteToggle[ti];
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

        pressTracker_.press(uCode, ev.button, ev.index);
        return dispatchDown(ev, uCode);
    }

    // dispatchUp — source-agnostic button-up handler.
    // rawCode: physical key code (keyboard) or 0 (mouse / no key).
    void LockstepEditor::dispatchUp(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;
        using T  = ControllerEvent::Type;

        switch (ev.button)
        {
            case CB::Func:
                // MD.7/MD.8: apply deferred pattern mute toggles atomically on Func release.
                for (const int t : deferredPatternMutes_)
                    processor_.togglePatternMute(t);
                deferredPatternMutes_.clear();
                uiState_.pendingPatternMuteToggle.fill(false);
                uiState_.funcHeld = false;
                // NoteEdit: Func release commits staged note removals, then exits mode.
                if (uiState_.noteEditMode)
                {
                    const int activeTrack = keyboardArea_.getActiveTrack();
                    auto& trk = processor_.sequence()
                                    .tracks[static_cast<std::size_t>(activeTrack)];
                    for (auto& [stepIdx, staged] : uiState_.noteEditStaged)
                    {
                        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                        auto& s = trk.steps[static_cast<std::size_t>(stepIdx)];
                        int newCount = 0;
                        std::array<int, kMaxNotesPerStep> kept{};
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                            if (staged.count(s.trigOverride.notes[n]) == 0)
                                kept[static_cast<std::size_t>(newCount++)] = s.trigOverride.notes[n];
                        s.trigOverride.noteCount = newCount;
                        for (int n = 0; n < newCount; ++n)
                            s.trigOverride.notes[n] = kept[static_cast<std::size_t>(n)];
                    }
                    uiState_.noteEditMode = false;
                    uiState_.noteEditSteps.clear();
                    uiState_.noteEditStaged.clear();
                }
                uiState_.funcTrigHeld = false;
                // MHZ.3.4: Func release commits staged P-Lock clears, then exits mode.
                if (uiState_.pLockClearMode)
                {
                    for (const int slot : uiState_.pLockClearStaged)
                    {
                        if (slot < 0)
                            processor_.clearTrigOverrideField(uiState_.pLockClearTrack,
                                                              uiState_.pLockClearStep,
                                                              -(slot + 1));
                        else
                            processor_.clearParam(uiState_.pLockClearTrack,
                                                  uiState_.pLockClearStep, slot);
                    }
                    uiState_.pLockClearStaged.clear();
                }
                uiState_.pLockClearMode  = false;
                uiState_.pLockClearTrack = -1;
                uiState_.pLockClearStep  = -1;
                // MHZ.3.5: Func release exits machine picker mode.
                uiState_.funcPartHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
                updateFillActivation();
                keyboardArea_.repaint();
                repaint();
                break;

            case CB::TrackScope:
                physHeld_.track = false;
                if (!uiState_.latch.track)
                {
                    uiState_.trackHeld = false;
                    processor_.setControlAllActive(false);  // MD.10
                    editMode_.onScopeEvent({ T::ButtonUp, CB::TrackScope });
                    repaint();
                }
                break;

            case CB::PatternScope:
                physHeld_.pattern = false;
                if (!uiState_.latch.pattern)
                {
                    uiState_.patternScopeHeld = false;
                    uiState_.patternScopeUsed = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::PatternScope });
                    repaint();
                }
                break;

            case CB::PartScope:
                physHeld_.part = false;
                if (!uiState_.latch.part)
                {
                    uiState_.partHeld = false;
                    uiState_.funcPartHeld = false;  // MHZ.3.5
                    editMode_.onScopeEvent({ T::ButtonUp, CB::PartScope });
                    repaint();
                }
                break;

            case CB::MuteScope:
                physHeld_.mute = false;
                if (!uiState_.latch.mute)
                {
                    uiState_.muteHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::MuteScope });
                    repaint();
                }
                break;

            case CB::FillScope:
                physHeld_.fill = false;
                if (!uiState_.latch.fill)
                {
                    uiState_.fillHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::FillScope });
                    updateFillActivation();
                    repaint();
                }
                break;

            case CB::CueScope:
                physHeld_.cue = false;
                // Cue is not latchable (reserved for MU); always release.
                uiState_.cueHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::CueScope });
                repaint();
                break;

            case CB::SceneScope:
                physHeld_.scene = false;
                if (!uiState_.latch.scene)
                {
                    uiState_.sceneHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::SceneScope });
                    repaint();
                }
                break;

            case CB::MasterScope:
                physHeld_.master = false;
                if (!uiState_.latch.master)
                {
                    uiState_.masterHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::MasterScope });
                    repaint();
                }
                break;

            case CB::Section:
            case CB::MetaSection:
                heldSectionRawCode_ = -1;
                uiState_.funcTrigHeld = false;  // Trig released: no longer in Func+Trig compound
                editMode_.setSectionHeld(false);
                break;

            case CB::VerbPlay:
                playKeyHeld_ = false;
                break;

            case CB::Step:
            {
                // Func+Trig+step: step release while funcTrigHeld → enter NoteEdit mode.
                if (uiState_.funcTrigHeld && !uiState_.noteEditMode
                    && !uiState_.noteEditSteps.empty())
                {
                    uiState_.noteEditMode = true;
                    uiState_.noteEditStaged.clear();

                    // Auto-set the view octave to match the step's existing notes so
                    // cells are immediately live without needing a NavUp/Down first.
                    if (!uiState_.noteEditSteps.empty())
                    {
                        const int firstStep = *uiState_.noteEditSteps.begin();
                        const int track     = processor_.editContext().heldTrackIndex();
                        if (track >= 0 && firstStep >= 0)
                        {
                            const auto& s = processor_.sequence()
                                .tracks[static_cast<std::size_t>(track)]
                                .steps[static_cast<std::size_t>(firstStep)];
                            if (s.trigOverride.noteCount > 0)
                            {
                                // Use the octave of the first note on the step.
                                const int noteVal = s.trigOverride.notes[0];
                                uiState_.noteEditOctave = noteVal / 12 - 1;
                            }
                        }
                    }

                    // Restore MZ to machine params — dismiss the TRIG meta section
                    // that Func+Trig brought up, so the user returns to where they were.
                    uiState_.masterSection = -1;
                    manipulationZone_.setMetaSection(-1);

                    keyboardArea_.repaint();
                    repaint();
                    break;
                }

                // NoteEdit mode: key-up on a chromatic cell; no further processing needed.
                if (uiState_.noteEditMode)
                {
                    keyboardArea_.repaint();
                    break;
                }

                // CHROMATIC/LEVELS mode: key-up clears the pressed highlight.
                {
                    const int at = keyboardArea_.getActiveTrack();
                    if (at >= 0 && at < static_cast<int>(kNumTracks))
                    {
                        const auto m = uiState_.trackInputMode[static_cast<std::size_t>(at)];
                        if ((m == TrackInputMode::Chromatic || m == TrackInputMode::Levels)
                            && uiState_.trigGridMode == TrigGridMode::Default)
                        {
                            keyboardArea_.repaint();
                            break;
                        }
                    }
                }

                // MG.2: retrig key release — this step key started continuous retrig.
                if (uiState_.retrigKeyHeld && uiState_.retrigKeyCode == rawCode)
                {
                    uiState_.retrigKeyHeld = false;
                    uiState_.retrigKeyCode = -1;
                    processor_.setRetrigActive(0, false);
                    break;
                }
                // MG.5: sound-pool key release — restore track's original sound.
                if (uiState_.soundPoolKeyHeld && uiState_.soundPoolKeyCode == rawCode)
                {
                    uiState_.soundPoolKeyHeld = false;
                    uiState_.soundPoolKeyCode = -1;
                    processor_.clearLiveSwap(keyboardArea_.getActiveTrack());
                    break;
                }
                // Normal step release: look up absStep by rawCode and commit.
                for (int i = static_cast<int>(heldStepKeys_.size()) - 1; i >= 0; --i)
                {
                    auto [code, stepIdx] = heldStepKeys_[static_cast<std::size_t>(i)];
                    if (code == rawCode)
                    {
                        auto& ctx              = processor_.editContext();
                        const int  track       = ctx.heldTrackIndex();
                        const bool paramWrote  = ctx.wasParamWritten();
                        processor_.editContext().release(stepIdx);
                        // MHZ.3.1: next press starts a fresh chord capture.
                        if (track >= 0)
                            processor_.cancelChordCapture(track, stepIdx);
                        if (!paramWrote && track >= 0 && stepIdx >= 0)
                        {
                            auto& s = processor_.sequence()
                                .tracks[static_cast<std::size_t>(track)]
                                .steps[static_cast<std::size_t>(stepIdx)];
                            if (uiState_.fillHeld)
                            {
                                // Cycle fill trig state: Inherit → On → Off → Inherit.
                                using FTS = FillTrigState;
                                switch (s.fillTrigState)
                                {
                                    case FTS::Inherit: s.fillTrigState = FTS::On;      break;
                                    case FTS::On:      s.fillTrigState = FTS::Off;     break;
                                    case FTS::Off:     s.fillTrigState = FTS::Inherit; break;
                                }
                            }
                            else
                            {
                                s.trig = !s.trig;
                            }
                        }
                        heldStepKeys_.erase(heldStepKeys_.begin() + i);
                        break;
                    }
                }
                if (heldStepKeys_.empty())
                {
                    uiState_.stepHeld = false;
                    editMode_.setTrigHeld(false);
                }
                repaint();
                break;
            }

            default:
                break;
        }
    }

    bool LockstepEditor::keyStateChanged(bool isKeyDown, juce::Component*)
    {
        // Purge released keys from the repeat-suppression set.
        std::erase_if(heldKeys_, [](int code) {
            return !juce::KeyPress::isKeyCurrentlyDown(code);
        });

        // Diff PressTracker against physical key state; synthesize ButtonUp for
        // any keyboard entry that is no longer down and dispatch release logic.
        bool handled = false;
        std::vector<std::pair<int, ControllerEvent>> toRelease;
        pressTracker_.forEachReleasedKeyboard([&](int src, ControllerButton btn, int idx)
        {
            toRelease.push_back({ src,
                { ControllerEvent::Type::ButtonUp, btn, idx, 0 } });
        });
        for (auto& [src, relEv] : toRelease)
        {
            pressTracker_.release(src);
            dispatchUp(relEv, src);
            handled = true;
        }

        // Always repaint on key-up so pressed indicators clear immediately.
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
                    const bool funcIsHeld = editMode_.scopeState().func;
                    const int  activeSlot = ctx.activeSlot();
                    if (funcIsHeld)
                    {
                        // MHZ.3.3: Trig + Func + Stop — clear all P-Locks on held
                        // step(s), leaving the trig and condition intact.
                        for (int idx : ctx.heldSteps())
                            processor_.clearStepLocks(track, idx);
                    }
                    else if (activeSlot >= 0)
                    {
                        // MHZ.3.3: Trig + (active MZ slot) + Stop — clear only
                        // that slot's P-Lock on all held steps.
                        for (int idx : ctx.heldSteps())
                            processor_.clearParam(track, idx, activeSlot);
                    }
                    else
                    {
                        // Full clear: trig off + condition reset + all P-Locks.
                        for (int idx : ctx.heldSteps())
                        {
                            processor_.clearStepLocks(track, idx);
                            auto& s = trk.steps[static_cast<std::size_t>(idx)];
                            s.trig      = false;
                            s.condition = TrigCondition{};
                        }
                    }
                }
                else if (verb == CB::VerbNo && editMode_.scopeState().func)
                {
                    // MHZ.5.4: Trig + Func + No — clear notes, velocity and gate
                    // override on held step(s), leaving step.trig and P-Locks intact.
                    for (int idx : ctx.heldSteps())
                    {
                        auto& s = trk.steps[static_cast<std::size_t>(idx)];
                        s.trigOverride.noteCount  = 0;
                        s.trigOverride.notes      = {};
                        s.trigOverride.hasVelocity = false;
                        s.trigOverride.velocity    = 100;
                        s.trigOverride.hasGate     = false;
                        s.trigOverride.gateValue   = MusicalGate::None;
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
                            s.trig           = false;
                            s.condition      = TrigCondition{};
                            s.overrides      = PLock{};
                            s.trigOverride   = TrigOverride{};
                            s.fillTrigState  = FillTrigState::Inherit;
                            s.fillOverrides  = PLock{};
                            s.fillTrigOverride = TrigOverride{};
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
