#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "core/TrackInputMode.h"
#include "machine/IMachine.h"
#include "machine/SamplerMachine.h"
#include "ui/ScopedSectionMatrix.h"
#include "ui/SurfaceModel.h"
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
          soundBankOverlay_(proc)
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
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            proc.apvts().addParameterListener(ParamIDs::trackMute(i), this);
            proc.apvts().addParameterListener(ParamIDs::trackSolo(i), this);
        }
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

        greyoutLayer_.onPaint = [this](juce::Graphics& g)
        {
            const juce::Colour emptyGrey { juce::uint32(0x66444444u) };
            g.setColour(emptyGrey);

            // (A) per-empty-track strip = number button ∪ mute ∪ solo.
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (!trackBtns_[t].isVisible()) continue;
                if (!processor_.isTrackEmpty(static_cast<int>(t))) continue;
                g.fillRect(trackBtns_[t].getBounds()
                               .getUnion(muteBtns_[t].getBounds())
                               .getUnion(soloBtns_[t].getBounds()));
            }

            // (B) focused track empty → grey the edit area (not the strip rows).
            const int at = keyboardArea_.getActiveTrack();
            if (at >= 0 && at < static_cast<int>(kNumTracks)
                && processor_.isTrackEmpty(at))
            {
                g.fillRect(manipulationZone_.getBounds().getUnion(crossfader_.getBounds()));
                g.fillRect(keyboardArea_.getBounds());
            }
        };
        addAndMakeVisible(greyoutLayer_);

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


        setSize(990, 596);  // MHX: taller for 4x2 MZ encoder band
        setWantsKeyboardFocus(true);

        // Controller surfaces (DESIGN §35).
        xTouchSurface_ = std::make_unique<XTouchMiniSurface>();
        push1Surface_  = std::make_unique<Push1Surface>();

        controllerPorts_.onStateChange = [this](bool open)
        {
            setStatus(open ? "Controller: X-Touch Mini connected"
                           : "Controller: X-Touch Mini disconnected");
        };

        push1Ports_.onStateChange = [this](bool open)
        {
            setStatus(open ? "Controller: Ableton Push 1 connected"
                           : "Controller: Ableton Push 1 disconnected");
        };

        startTimerHz(30);  // diagnostic VU meters / activity blinks
    }

    LockstepEditor::~LockstepEditor()
    {
        processor_.apvts().removeParameterListener(ParamIDs::syncMode, this);
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            processor_.apvts().removeParameterListener(ParamIDs::trackMute(i), this);
            processor_.apvts().removeParameterListener(ParamIDs::trackSolo(i), this);
        }
        if (keyListenerTarget_ != nullptr)
            keyListenerTarget_->removeKeyListener(this);
    }

    void LockstepEditor::parameterChanged(const juce::String& paramID, float /*newValue*/)
    {
        if (paramID == ParamIDs::syncMode)
        {
            juce::MessageManager::callAsync([this] { updateTransportGhosting(); });
            return;
        }
        // Mute or solo changed (via track-bar buttons, host automation, or MIDI
        // learn): repaint so the VU meter colour updates immediately.
        for (int i = 0; i < static_cast<int>(kNumTracks); ++i)
        {
            if (paramID == juce::String(ParamIDs::trackMute(i))
                || paramID == juce::String(ParamIDs::trackSolo(i)))
            {
                juce::MessageManager::callAsync([this] { repaint(); });
                return;
            }
        }
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
            if (std::abs(floored - trackMeter_[i]) > 0.0f) { trackMeter_[i] = floored; dirty = true; }

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
        if (std::abs(flooredMaster - masterMeter_) > 0.0f) { masterMeter_ = flooredMaster; dirty = true; }

        // Transport state change: repaint so the PLAY/PAUSE label updates promptly.
        const bool nowPlaying = processor_.clock().inPluginPlaying();
        if (nowPlaying != lastPlayingState_) { lastPlayingState_ = nowPlaying; dirty = true; }

        if (dirty) repaint();

        // Controller: drain MIDI FIFO → surface.onInput(), then render feedback LEDs.
        // Build the model once and share it with all connected surfaces.
        if ((xTouchSurface_ && controllerPorts_.isOpen())
            || (push1Surface_ && push1Ports_.isOpen()))
        {
            auto sink = buildControllerSink();
            const auto model = buildSurfaceModel(uiState_,
                                                  processor_.editContext(),
                                                  &pressTracker_,
                                                  processor_,
                                                  keyboardArea_.getActiveTrack(),
                                                  keyboardArea_.currentPage(),
                                                  gridMode_,
                                                  manipulationZone_.slotOffset(),
                                                  static_cast<float>(crossfader_.getValue()));
            if (xTouchSurface_ && controllerPorts_.isOpen())
                controllerPorts_.drain(*xTouchSurface_, sink, model);
            if (push1Surface_ && push1Ports_.isOpen())
                push1Ports_.drain(*push1Surface_, sink, model);
        }

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
            // pattern mute = orange, solo-exclusion = purple, soloed = teal,
            // audible = grey.
            juce::Colour bg = juce::Colour::fromRGB(28, 32, 38);
            if      (gMuted) bg = juce::Colour::fromRGB(70, 20, 20);
            else if (pMuted) bg = juce::Colour::fromRGB(80, 50, 16);
            else if (soloEx) bg = juce::Colour::fromRGB(50, 24, 70);
            else if (soloed) bg = juce::Colour::fromRGB(20, 70, 50);
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
        // Persistent per-track state overlays FIRST — they must show in every mode
        // (incl. the unmodified resting state). The held-context preview below
        // early-returns when nothing is held, so these have to precede it.

        // ---- Deviation badge: an amber corner triangle on every track playing
        // off its scene's home (global) phrase (DESIGN §4.7) — persistent in the
        // track / VU row, visible in every mode (no modifier needed).
        {
            const int home = processor_.section().globalPhrase;
            g.setColour(juce::Colour(juce::uint32(0xFFFFC020u)));
            for (std::size_t t = 0; t < kNumTracks; ++t)
            {
                if (!trackBtns_[t].isVisible()) continue;
                const int ti  = static_cast<int>(t);
                const int cur = processor_.isTrackDeviated(ti)
                    ? processor_.deviationPhraseIdxForTrack(ti)
                    : processor_.section().globalPhrase;
                if (cur == home) continue;
                const auto r = trackBtns_[t].getBounds();
                const float s = 7.0f;
                juce::Path tri;
                tri.addTriangle(static_cast<float>(r.getX()),     static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()) + s,  static_cast<float>(r.getY()),
                                static_cast<float>(r.getX()),      static_cast<float>(r.getY()) + s);
                g.fillPath(tri);
            }
        }

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
                // Song / Scene identity pill.
                const int sg = processor_.activePieceIdx() + 1;
                const int sc = processor_.activeSectionIdx() + 1;
                const juce::String identity = "Sg:" + juce::String(sg)
                                            + "  Sc:" + juce::String(sc);
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
                    case ClipboardType::Pattern: cbLabel = "CPY:PHR"; break;
                    case ClipboardType::Scene:   cbLabel = "CPY:SCN"; break;
                    case ClipboardType::All:     cbLabel = "CPY:ALL"; break;
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

                // CK:N checkpoint badge — depth of the currently-held scope's stack.
                int ckTrack = 0;
                const CheckpointScope ckScp = ckScope(ckTrack);
                const int checkpointDepth = processor_.checkpointDepth(ckScp, ckTrack);
                if (checkpointDepth > 0 && bx + 38 < kSplitX)
                {
                    const auto r = juce::Rectangle<int>(bx, by, 38, kBadgeH);
                    g.setColour(juce::Colour(0xFF40A080u));
                    g.fillRoundedRectangle(r.toFloat(), 3.0f);
                    g.setColour(juce::Colours::white);
                    g.drawText("CK:" + juce::String(checkpointDepth), r, juce::Justification::centred);
                    bx += 38 + kGap;
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
                if (ui.funcTrackHeld)
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
                    // Primary scope token. MHZ.9.7: show mode-cycle hint when Track+Control-All.
                    else if (ui.trackHeld && processor_.controlAllActive())
                    {
                        ctx = juce::String(u8"TRACK  |  ↑↓ cycle PLAY/CHROM/LEVLS");
                    }
                    else if (ui.trackHeld)        ctx = "TRACK " + juce::String(keyboardArea_.getActiveTrack() + 1);
                    else if (ui.phraseScopeHeld) ctx = "PHRASE";
                    else if (ui.sceneHeld)         ctx = "SCENE";
                    else if (ui.morphHeld)        ctx = "MORPH";
                    else if (ui.songHeld)       ctx = "SONG";
                    else if (ui.muteHeld)         ctx = "MUTE";
                    else if (ui.fillHeld)         ctx = "FILL";
                    else if (ui.funcHeld)         ctx = "FUNC";
                }

                if (ctx.isEmpty()) return;   // nothing held — preview is blank

                // Qualify with Func if held alongside another modifier (normal path only).
                if (!ui.funcTrackHeld && !ui.pLockClearMode
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

                // Transient CPC status overlays the context-HUD for ~1.5s.
                paintStatus(g, r);
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
        if (prevLatch.phrase && !physHeld_.phrase)
            dispatchUp({ T::ButtonUp, CB::PhraseScope });
        if (prevLatch.morph && !physHeld_.morph)
            dispatchUp({ T::ButtonUp, CB::MorphScope });
        if (prevLatch.mute && !physHeld_.mute)
            dispatchUp({ T::ButtonUp, CB::MuteScope });
        if (prevLatch.track && !physHeld_.track)
            dispatchUp({ T::ButtonUp, CB::TrackScope });
        if (prevLatch.scene && !physHeld_.scene)
            dispatchUp({ T::ButtonUp, CB::SceneScope });
        if (prevLatch.song && !physHeld_.song)
            dispatchUp({ T::ButtonUp, CB::SongScope });
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
            const bool isCol1 = (cb == CB::PhraseScope || cb == CB::MorphScope
                                 || cb == CB::MuteScope);

            auto releaseOther = [&](bool& latchBool, bool physHeld, CB btn) {
                if (!latchBool || cb == btn) return;
                latchBool = false;
                if (!physHeld)
                    dispatchUp({ T::ButtonUp, btn });
            };

            if (isCol1)
            {
                releaseOther(uiState_.latch.phrase, physHeld_.phrase, CB::PhraseScope);
                releaseOther(uiState_.latch.morph,   physHeld_.morph,   CB::MorphScope);
                releaseOther(uiState_.latch.mute,    physHeld_.mute,    CB::MuteScope);
            }
            else
            {
                releaseOther(uiState_.latch.track,  physHeld_.track,  CB::TrackScope);
                releaseOther(uiState_.latch.scene,   physHeld_.scene,   CB::SceneScope);
                releaseOther(uiState_.latch.song, physHeld_.song, CB::SongScope);
                releaseOther(uiState_.latch.fill,   physHeld_.fill,   CB::FillScope);
            }
        }

        switch (cb)
        {
            case CB::PhraseScope: uiState_.latch.phrase = set; break;
            case CB::MorphScope:   uiState_.latch.morph   = set; break;
            case CB::MuteScope:    uiState_.latch.mute    = set; break;
            case CB::TrackScope:   uiState_.latch.track   = set; break;
            case CB::SceneScope:    uiState_.latch.scene    = set; break;
            case CB::SongScope:  uiState_.latch.song  = set; break;
            case CB::FillScope:    uiState_.latch.fill    = set; break;
            case CB::Func:
            case CB::CueScope:
            case CB::VerbYes:
            case CB::VerbRecord:
            case CB::VerbPlay:
            case CB::VerbStop:
            case CB::VerbNo:
            case CB::Snapshot:
            case CB::Restore:
            case CB::NavUp:
            case CB::NavLeft:
            case CB::NavDown:
            case CB::NavRight:
            case CB::Section:
            case CB::MetaSection:
            case CB::Step:
            case CB::SelectTrack:
            case CB::ToggleMute:
            case CB::ForkPart:
            case CB::RecordArm:
            case CB::TapTempo:
            case CB::MetronomeToggle:
            case CB::PlayStop:
            case CB::StopReset:
            case CB::VerbClear:
            case CB::VerbDelete:
            case CB::VerbPanic:
            case CB::None:
                break;
        }
    }

    // -------------------------------------------------------------------------
    // MHZ.9.x: per-modifier tap router.
    // Single tap on a latched modifier unlatches it; double-tap toggles latch.

    void LockstepEditor::handleModifierTap(ControllerButton cb, bool currentlyLatched)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool dbl = doubleTap_.recordAndCheck(1000 + static_cast<int>(cb), now);
        if (currentlyLatched && !dbl)
        {
            setModifierLatch(cb, false);
            doubleTap_.invalidate();  // prevent follow-up read as re-latch
        }
        else if (dbl)
        {
            setModifierLatch(cb, !currentlyLatched);
        }
    }

    // MHZ.9.x: auto-release a transient latch after a terminal action completes.
    // Only releases when the mode is *latched* (not just physically held).

    void LockstepEditor::releaseTransientLatch(ControllerButton cb)
    {
        using T = ControllerEvent::Type;
        bool latched = false;
        bool phys    = false;
        if (cb == ControllerButton::TrackScope)
        {
            latched = uiState_.latch.track; phys = physHeld_.track;
        }
        else if (cb == ControllerButton::SceneScope)
        {
            latched = uiState_.latch.scene; phys = physHeld_.scene;
        }
        else
        {
            return;
        }
        if (!latched) return;
        setModifierLatch(cb, false);
        if (!phys) dispatchUp({ T::ButtonUp, cb });
        keyboardArea_.repaint();
        repaint();
    }

    // -------------------------------------------------------------------------
    // Key handling (9x4 layout)

    // A section-suite scope (Track/Pattern/Part/Scene/Master) qualifies the next
    // verb. While one is held, bare-Func global ops (Snapshot/Restore) are reserved:
    // Func+scope+verb is that scope's secondary variant — not a global checkpoint.
    static bool sectionSuiteScopeHeld(const UiState& ui) noexcept
    {
        return ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld
            || ui.morphHeld || ui.songHeld;
    }

    // dispatchDown — source-agnostic button-down handler fed by both keyboard
    // and mouse.  rawCode is the physical key code (keyboard) or 0 (mouse).
    bool LockstepEditor::dispatchDown(ControllerEvent ev, int rawCode)
    {
        using CB = ControllerButton;
        switch (ev.button)
        {
            case CB::Func:
                uiState_.funcHeld = true;
                // Func+Track is the machine/Kit picker gesture (§4.7.2) — entering
                // the compound re-skins the step grid to machine names directly.
                uiState_.funcTrackHeld = uiState_.trackHeld;
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
                // Func+Track = machine/Kit picker (§4.7.2): arm when Track pressed with Func held.
                uiState_.funcTrackHeld = uiState_.funcHeld;
                processor_.setControlAllActive(true);  // MD.10: active until a track is selected
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::TrackScope, uiState_.latch.track);
                repaint();
                return true;

            case CB::PhraseScope:
                physHeld_.phrase = true;
                uiState_.phraseScopeHeld = true;
                uiState_.phraseScopeUsed = false;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::PhraseScope, uiState_.latch.phrase);
                repaint();
                return true;

            case CB::MuteScope:
                physHeld_.mute = true;
                uiState_.muteHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::MuteScope, uiState_.latch.mute);
                repaint();
                return true;

            case CB::FillScope:
                physHeld_.fill = true;
                uiState_.fillHeld = true;
                editMode_.onScopeEvent(ev);
                updateFillActivation();
                handleModifierTap(CB::FillScope, uiState_.latch.fill);
                repaint();
                return true;

            case CB::CueScope:
                physHeld_.cue = true;
                uiState_.cueHeld = true;
                editMode_.onScopeEvent(ev);
                repaint();
                return true;

            case CB::MorphScope:
                physHeld_.morph = true;
                uiState_.morphHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::MorphScope, uiState_.latch.morph);
                repaint();
                return true;

            case CB::SongScope:
                physHeld_.song = true;
                uiState_.songHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::SongScope, uiState_.latch.song);
                repaint();
                return true;

            case CB::SceneScope:
                physHeld_.scene = true;
                // Track + Scene: re-sync focused musician to current Scene (Phase 7).
                if (uiState_.trackHeld)
                {
                    processor_.resyncTrackToScene(processor_.focusTrack());
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                uiState_.sceneHeld = true;
                editMode_.onScopeEvent(ev);
                handleModifierTap(CB::SceneScope, uiState_.latch.scene);
                keyboardArea_.repaint();
                repaint();
                return true;

            case ControllerButton::Section:
            {
                // Determine whether a section-suite scope modifier is held.
                using PS = EditMode::PrimaryScope;
                PS sectionScope = PS::None;
                if      (uiState_.trackHeld)        sectionScope = PS::Track;
                else if (uiState_.phraseScopeHeld) sectionScope = PS::Phrase;
                else if (uiState_.sceneHeld)         sectionScope = PS::Scene;
                else if (uiState_.morphHeld)        sectionScope = PS::Morph;
                else if (uiState_.songHeld)       sectionScope = PS::Song;

                if (sectionScope != PS::None)
                {
                    // Dim under this scope — no content, block entirely.
                    if (!scopedCell(sectionScope, ev.index).hasContent) return true;

                    // Scope-specific dispatch for cells whose content is implemented.
                    if (sectionScope == PS::Phrase && ev.index == 0)
                    {
                        // Pattern+LEN: track length/divider lives in the TRACK meta section.
                        keyboardArea_.selectMetaSection(2);
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
                if (uiState_.funcHeld && ev.index == 1)
                    uiState_.funcSrcHeld = true;  // Func+Src(NOTE) — enables note-edit gesture
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
                        )
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

                        // Poly + velocity: sustain the note until the pad is
                        // released (gate). Push pads supply velocity; QWERTY/mouse
                        // leave ev.velocity 0 → fall back to a default.
                        const int vel = ev.velocity > 0 ? ev.velocity : 100;
                        const auto pad = static_cast<std::size_t>(ev.index);
                        // Release a stale note still parked on this pad (e.g. a
                        // missed note-off) before re-sounding it.
                        if (chromaticHeldNote_[pad] >= 0)
                            processor_.liveNoteOff(chromaticHeldTrack_[pad],
                                                   chromaticHeldNote_[pad]);
                        chromaticHeldNote_[pad]  = note;
                        chromaticHeldTrack_[pad] = at;
                        processor_.liveNoteOn(at, note, vel);
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
                        )
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

                // Master + step: Song (song) select (Phase 7 / DESIGN §16).
                if (uiState_.songHeld && !uiState_.morphHeld)
                {
                    if (ev.index >= 0 && ev.index < kNumSongs)
                        processor_.setActiveSong(ev.index);
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Phrase + step (Phase 7 / DESIGN §4.7/§16). Track+Phrase routes to
                // the SelectTrack case (deviate the focused track); here:
                //   Phrase + step → set the scene's global phrase (the focused
                //                   track un-deviates and rejoins the unison).
                // (Force-all was dropped — baseline launch, Func+Scene+step,
                //  covers clearing deviations; DESIGN §4.7, PRINCIPLES §13/§15.)
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.setGlobalPhrase(keyboardArea_.getActiveTrack(), ev.index);
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Scene + step: scene launch or create-on-empty (DESIGN §16/§23.3).
                //   occupied + no-func:  single-tap = overlay launch;
                //                        double-tap = floor launch (revert)
                //   occupied + func:     floor launch unconditionally
                //   empty    + no-func:  baked-copy create → launch
                //   empty    + func:     default create → launch
                // The active scene is always treated as occupied (it is live).
                if (uiState_.sceneHeld && !uiState_.funcTrackHeld)
                {
                    if (ev.index >= 0 && ev.index < kScenesPerSong)
                    {
                        const bool funcHeld = uiState_.funcHeld;
                        const bool isActive = (ev.index == processor_.activeSectionIdx());
                        const bool occupied = isActive
                                              || processor_.sceneSlotOccupied(ev.index);
                        if (occupied && !funcHeld)
                        {
                            // Existing: single-tap = overlay, double-tap = floor.
                            const double now = juce::Time::getMillisecondCounterHiRes();
                            const bool toFloor =
                                doubleTap_.recordAndCheck(3000 + ev.index, now);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, toFloor);
                            else if (toFloor)
                                processor_.setActiveSceneToFloor(ev.index);
                            else
                                processor_.setActiveScene(ev.index);
                        }
                        else if (occupied && funcHeld)
                        {
                            // Func + occupied: floor launch.
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, true);
                            else
                                processor_.setActiveSceneToFloor(ev.index);
                        }
                        else if (!occupied && !funcHeld)
                        {
                            // Empty + no-func: baked copy of current state.
                            // Conflict gate: target phrase slot may have content.
                            if (phraseConflictAndConfirm(ev.index,
                                                         PendingConfirm::CreateScene))
                                break;   // waiting for Yes/No
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.createBakedCopyScene(ev.index);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                            setStatus("Scene " + juce::String(ev.index + 1) + " created");
                        }
                        else
                        {
                            // Empty + func: blank default scene.
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.createDefaultScene(ev.index);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(ev.index, false);
                            else
                                processor_.setActiveScene(ev.index);
                            setStatus("Scene " + juce::String(ev.index + 1) + " (default)");
                        }
                    }
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }

                // Func+Track (machine/Kit picker) + step: assign machine by index (§4.7.2).
                if (uiState_.funcTrackHeld)
                {
                    const int numMachines = processor_.numAvailableMachines();
                    if (ev.index >= 0 && ev.index < numMachines)
                    {
                        const std::string machineId {
                            processor_.availableMachineInfo(ev.index).id };
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(), machineId);
                        keyboardArea_.syncToActiveTrack();
                        releaseTransientLatch(CB::TrackScope);
                    }
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
                                if (s.trigOverride.notes[static_cast<std::size_t>(n)] == absNote) { inNotes = true; break; }
                            if (inNotes && staged.count(absNote) == 0)
                                staged.insert(absNote);        // stage for removal
                            else if (inNotes && staged.count(absNote) > 0)
                                staged.erase(absNote);         // cancel removal
                            else if (!inNotes && s.trigOverride.noteCount < kMaxNotesPerStep)
                            {
                                s.trigOverride.notes[static_cast<std::size_t>(s.trigOverride.noteCount++)] = absNote;
                                s.trig = true;
                            }
                        }
                    }
                    keyboardArea_.repaint();
                    return true;
                }

                // Func+Src+step: tentative note-edit entry.
                // Suppress pLockClearMode; NoteEdit mode activates on step key release.
                if (uiState_.funcSrcHeld && !uiState_.noteEditMode)
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
                // MHZ.9.5: use ctx.heldSteps().empty() so latched steps keep the edit context alive.
                if (uiState_.funcHeld && processor_.editContext().heldSteps().empty())
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

                // MHZ.9.5: any plain step press while a P-lock latch is active exits
                // the latch (consumed — no trig toggle, no new hold), so you no longer
                // need Func+Func. Checked before the key-repeat guard below so it also
                // fires when you re-press the latched step itself.
                if (processor_.editContext().hasAnyLatchedStep())
                {
                    escapeAllLatches();
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }

                // Ignore key-repeat (same physical key already in list).
                {
                    bool alreadyHeld = false;
                    for (auto& [code, _] : heldStepKeys_)
                    {
                        if (code == rawCode) { alreadyHeld = true; break; }
                    }
                    if (alreadyHeld) return true;
                }

                {
                    const int absStep = keyboardArea_.currentPage() * KeyboardArea::kPageSteps
                                        + ev.index;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool isDouble = doubleTap_.recordAndCheck(absStep, now);

                    // MHZ.9.5: double-tap on a step = virtual-hold (latch operand).
                    // On the 2nd key-down, re-hold the step and mark it latched; also
                    // revert the first key-up's trig toggle (if it happened) so the
                    // net effect is zero trig changes.
                    if (isDouble)
                    {
                        auto& ctx = processor_.editContext();
                        ctx.hold(keyboardArea_.getActiveTrack(), absStep);
                        ctx.setLatched(absStep);
                        uiState_.latch.anySteps = ctx.hasAnyLatchedStep();
                        heldStepKeys_.push_back({ rawCode, absStep });
                        uiState_.stepHeld = true;
                        editMode_.setTrigHeld(true);

                        // Revert the first key-up trig flip if it happened on this step.
                        if (lastTrigToggleApplied_
                            && lastTrigToggleStep_  == absStep
                            && lastTrigToggleTrack_ == ctx.heldTrackIndex())
                        {
                            auto& s = processor_.sequence()
                                .tracks[static_cast<std::size_t>(lastTrigToggleTrack_)]
                                .steps[static_cast<std::size_t>(lastTrigToggleStep_)];
                            s.trig = !s.trig;  // undo the first-tap's toggle
                        }
                        lastTrigToggleApplied_ = false;
                        lastTrigToggleStep_    = -1;
                        lastTrigToggleTrack_   = -1;
                        repaint();
                        return true;
                    }

                    // Normal (first) press: hold the step.
                    heldStepKeys_.push_back({ rawCode, absStep });
                    uiState_.stepHeld = true;
                    processor_.editContext().hold(keyboardArea_.getActiveTrack(), absStep);
                    editMode_.setTrigHeld(true);
                    // Clear stale trig-toggle tracking on any new step press.
                    lastTrigToggleApplied_ = false;
                    lastTrigToggleStep_    = -1;
                    lastTrigToggleTrack_   = -1;
                    repaint();
                }
                return true;
            }

            case ControllerButton::SelectTrack:
            {
                // When Track is held, QwertyOverlay routes step keys to this case
                // (not the Step case). So the Track-compound gestures must be
                // handled HERE, before the plain track-select fallback below.

                // Func+Track (machine/Kit picker) + step = assign the indexed
                // machine to the focused track (§4.7.2).
                if (uiState_.funcTrackHeld)
                {
                    if (ev.index >= 0 && ev.index < processor_.numAvailableMachines())
                    {
                        processor_.setTrackMachine(keyboardArea_.getActiveTrack(),
                            std::string(processor_.availableMachineInfo(ev.index).id));
                        keyboardArea_.syncToActiveTrack();
                    }
                    keyboardArea_.repaint();
                    return true;
                }
                // Track+Phrase+step = sticky per-track deviation for the focused
                // musician (Phrase-alone = unison swap, handled in the Step case).
                if (uiState_.phraseScopeHeld)
                {
                    if (ev.index >= 0 && ev.index < kPhrasesPerTrack)
                        processor_.swapPhraseForTrack(keyboardArea_.getActiveTrack(), ev.index);
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                // (Track + No delete gesture removed; use Track + Func+O to delete.)
                if (uiState_.trackHeld && processor_.isTrackEmpty(ev.index))
                {
                    // Track+empty step = copy current track's machine+params (no steps),
                    // then select the destination so edits land on the new track.
                    processor_.copyKitTrack(keyboardArea_.getActiveTrack(), ev.index);
                    keyboardArea_.setActiveTrack(ev.index);
                    releaseTransientLatch(CB::TrackScope);
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                keyboardArea_.setActiveTrack(ev.index);
                processor_.setControlAllActive(false);  // specific track chosen; disable control-all
                releaseTransientLatch(CB::TrackScope);
                return true;
            }

            case ControllerButton::NavUp:
            {
                const int t = keyboardArea_.getActiveTrack();
                // Func+↑ = double the focused track's pattern length.
                if (uiState_.funcHeld && !uiState_.trackHeld)
                {
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        processor_.doubleTrackLength(t);
                    keyboardArea_.repaint();
                    return true;
                }
                // MHZ.9.7: Track (no specific track selected) + NavUp → cycle input mode upward.
                if (uiState_.trackHeld && processor_.controlAllActive()
                    && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Levels;     break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Play;       break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Chromatic;  break;
                        default: break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                // Normal: next higher track number.
                keyboardArea_.setActiveTrack(
                    std::min(static_cast<int>(kNumTracks) - 1, t + 1));
                return true;
            }

            case ControllerButton::NavDown:
            {
                const int t = keyboardArea_.getActiveTrack();
                // Func+↓ = halve the focused track's pattern length.
                if (uiState_.funcHeld && !uiState_.trackHeld)
                {
                    if (t >= 0 && t < static_cast<int>(kNumTracks))
                        processor_.halveTrackLength(t);
                    keyboardArea_.repaint();
                    return true;
                }
                // MHZ.9.7: Track (no specific track selected) + NavDown → cycle input mode downward.
                if (uiState_.trackHeld && processor_.controlAllActive()
                    && t >= 0 && t < static_cast<int>(kNumTracks))
                {
                    auto& mode = uiState_.trackInputMode[static_cast<std::size_t>(t)];
                    switch (mode)
                    {
                        case TrackInputMode::Play:      mode = TrackInputMode::Chromatic;  break;
                        case TrackInputMode::Chromatic: mode = TrackInputMode::Levels;     break;
                        case TrackInputMode::Levels:    mode = TrackInputMode::Play;       break;
                        default: break;
                    }
                    escapeAllLatches();  // entering new modality exits current latch
                    keyboardArea_.repaint();
                    repaint();
                    return true;
                }
                // Normal: previous (lower) track number.
                keyboardArea_.setActiveTrack(std::max(0, t - 1));
                return true;
            }

            case ControllerButton::NavLeft:
            {
                // Note-edit mode and CHROMATIC mode both use NavLeft/Right for octave shift.
                const int tl = keyboardArea_.getActiveTrack();
                const bool chromL = tl >= 0 && tl < static_cast<int>(kNumTracks)
                    && uiState_.trackInputMode[static_cast<std::size_t>(tl)] == TrackInputMode::Chromatic;
                // Func+← = rotate the focused track's sequence one step left.
                // In note-edit or Chromatic mode, Func+← keeps its octave-shift role.
                if (uiState_.funcHeld && !uiState_.noteEditMode && !chromL)
                {
                    if (tl >= 0 && tl < static_cast<int>(kNumTracks))
                        processor_.rotateTrackSteps(tl, -1);
                    keyboardArea_.repaint();
                    return true;
                }
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
                // Func+→ = rotate the focused track's sequence one step right.
                // In note-edit or Chromatic mode, Func+→ keeps its octave-shift role.
                if (uiState_.funcHeld && !uiState_.noteEditMode && !chromR)
                {
                    if (tr >= 0 && tr < static_cast<int>(kNumTracks))
                        processor_.rotateTrackSteps(tr, +1);
                    keyboardArea_.repaint();
                    return true;
                }
                if (uiState_.noteEditMode || chromR)
                {
                    uiState_.noteEditOctave = std::min(uiState_.noteEditOctave + 1, 8);
                    keyboardArea_.repaint();
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
                // Scope held → grammar verb (Paste).
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                // Func+I (no non-trivial scope) = unqualified paste.
                // Stamps the single captured layer; rejects when type is All.
                if (editMode_.scopeState().func)
                {
                    using CT = ClipboardType;
                    if (clipboard_.type == CT::None)
                    {
                        setStatus("Nothing copied");
                    }
                    else if (clipboard_.type == CT::All)
                    {
                        setStatus("Paste: pick a scope");
                    }
                    else
                    {
                        // Map clipboard type → matching scope and dispatch paste.
                        PS synScope = PS::None;
                        switch (clipboard_.type)
                        {
                            case CT::None:    break;
                            case CT::Step:    synScope = PS::Trig;    break;
                            case CT::Section: synScope = PS::Section; break;
                            case CT::Track:   synScope = PS::Track;   break;
                            case CT::Pattern: synScope = PS::Phrase;  break;
                            case CT::Scene:   synScope = PS::Scene;   break;
                            case CT::All:     break;  // handled above
                        }
                        if (synScope != PS::None)
                            dispatchVerb(synScope, ev.button);
                    }
                    return true;
                }
                if (playKeyHeld_) return true;  // ignore key repeat
                playKeyHeld_ = true;

                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = (now - lastPlayPressTime_) < kDoublePressMsThreshold;
                lastPlayPressTime_ = now;

                if (isDouble)
                {
                    // Double-tap = stop + reset: next start re-anchors to step 0.
                    processor_.clock().setInPluginPlaying(false);
                    processor_.clock().resetPhase();
                    processor_.requestFreshStart();
                }
                else
                {
                    // Single tap toggles play/pause; resume continues in phase.
                    processor_.clock().setInPluginPlaying(!processor_.clock().inPluginPlaying());
                }
                return true;
            }

            case ControllerButton::VerbStop:
                // Legacy — superseded by VerbClear on the O key; nothing emits this anymore.
                return true;

            case ControllerButton::VerbClear:
            {
                using PS = EditMode::PrimaryScope;
                // Scene scope held → cancel queued scene.
                if (uiState_.sceneHeld)
                {
                    processor_.cancelQueuedScene();
                    repaint();
                    return true;
                }
                // Phrase scope → cancel queued scene.
                if (uiState_.phraseScopeHeld)
                {
                    processor_.cancelQueuedScene();
                    uiState_.phraseScopeUsed = true;
                    repaint();
                    return true;
                }
                // Non-trivial scope → grammar verb (Clear scope contents).
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                // No scope: clear the active P-Lock slot if one is active.
                {
                    auto& ctx = processor_.editContext();
                    if (ctx.isActiveForEditing() && ctx.activeSlot() >= 0)
                    {
                        processor_.clearParam(ctx.heldTrackIndex(),
                                              ctx.heldStepIndex(),
                                              ctx.activeSlot());
                        ctx.markParamWritten();
                    }
                }
                return true;
            }

            case ControllerButton::VerbDelete:
            {
                using PS = EditMode::PrimaryScope;
                // Build a description of the entity to delete based on current scope.
                juce::String entityName;
                switch (editMode_.primaryScope())
                {
                    case PS::Track:
                        entityName = "Track " + juce::String(keyboardArea_.getActiveTrack() + 1);
                        break;
                    case PS::Phrase:
                        entityName = "Phrase";
                        break;
                    case PS::Scene:
                        entityName = "Part";
                        break;
                    default:
                        return true;  // No operand — inert.
                }
                pendingConfirm_ = PendingConfirm::Delete;
                setStatus("Delete " + entityName + "?  P=Yes  Func+P=No");
                keyboardArea_.repaint();
                return true;
            }

            case ControllerButton::VerbPanic:
                processor_.requestPanic();
                return true;

            case ControllerButton::VerbRecord:
            {
                using PS = EditMode::PrimaryScope;
                // Scene + Record: bake live deviations into home-row phrase content.
                // Destructive — requires Yes/No confirmation.
                // Func+Scene+Record is "copy scene" — falls through to dispatchVerb.
                if (uiState_.sceneHeld && !editMode_.scopeState().func)
                {
                    const int nd = processor_.countDeviatedTracks();
                    if (nd == 0)
                    {
                        setStatus("No deviations to bake");
                        return true;
                    }
                    pendingConfirm_ = PendingConfirm::BakeScene;
                    {
                        const int ns = processor_.scenesSharingHomePhrase();
                        juce::String msg = "Bake " + juce::String(nd) + " track(s)?";
                        if (ns > 0) msg += "  SHR:" + juce::String(ns);
                        msg += "  P=Yes  Func+P=No";
                        setStatus(msg);
                    }
                    repaint();
                    return true;
                }
                // Scope held → grammar verb (e.g. copy).  No scope → arm recording.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                // Func+U with no non-trivial scope = omni copy (capture all layers).
                if (editMode_.scopeState().func
                    && editMode_.primaryScope() == PS::Func)
                {
                    captureScene();           // fills scene layer
                    {
                        const int t = keyboardArea_.getActiveTrack();
                        if (t >= 0 && t < static_cast<int>(kNumTracks))
                            clipboard_.clipTrack =
                                processor_.sequence().tracks[static_cast<std::size_t>(t)];
                    }
                    clipboard_.clipSequence = processor_.sequence();
                    clipboard_.type = ClipboardType::All;
                    setStatus("Captured all");
                    return true;
                }
                // Double-tap = overdub record; single tap = plain (overwrite) record.
                {
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const bool isDouble = doubleTap_.recordAndCheck(
                        1000 + static_cast<int>(ControllerButton::VerbRecord), now);
                    if (isDouble)
                    {
                        processor_.clock().setRecordArmed(true);
                        processor_.clock().setOverdubArmed(true);
                    }
                    else
                    {
                        processor_.clock().setOverdubArmed(false);
                        processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                    }
                }
                return true;
            }

            case ControllerButton::VerbYes:
            {
                using PS = EditMode::PrimaryScope;
                // Y = Snapshot. Under scene scope → re-sync all to scene (scope-specific snapshot).
                if (uiState_.sceneHeld)
                {
                    processor_.resyncAllToScene();
                    repaint();
                    keyboardArea_.repaint();
                    return true;
                }
                // Non-trivial scope → scope-specific snapshot via dispatchVerb.
                if (editMode_.primaryScope() != PS::None
                    && editMode_.primaryScope() != PS::Func)
                {
                    editMode_.onVerb(ev.button);
                    return true;
                }
                // No scope → Song-scope snapshot.
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                repaint();
                return true;
            }

            case ControllerButton::VerbNo:
            {
                using PS = EditMode::PrimaryScope;
                const bool funcHeld = editMode_.scopeState().func;

                // Both primary P (Yes/confirm) and Func+P (No/cancel) arrive here as VerbNo.
                // Distinguish by whether Func is held.

                // Pending-confirm: P = execute, Func+P = cancel.
                if (pendingConfirm_ != PendingConfirm::None)
                {
                    if (!funcHeld)
                    {
                        if (pendingConfirm_ == PendingConfirm::BakeScene)
                        {
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.bakeSceneState();
                            setStatus("Baked");
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        if (pendingConfirm_ == PendingConfirm::CreateScene)
                        {
                            const int tgt = pendingTarget_;
                            processor_.snapshot(CheckpointScope::Song, 0);
                            processor_.createBakedCopyScene(tgt);
                            if (processor_.clock().inPluginPlaying())
                                processor_.queueScene(tgt, false);
                            else
                                processor_.setActiveScene(tgt);
                            setStatus("Scene " + juce::String(tgt + 1) + " created");
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        if (pendingConfirm_ == PendingConfirm::PasteScene)
                        {
                            const int destG = pendingTarget_;
                            processor_.snapshot(CheckpointScope::Song, 0);
                            auto& dst = processor_.section();
                            dst.activeMask  = clipboard_.scene.floor.activeMask;
                            dst.coreTime    = clipboard_.scene.floor.coreTime;
                            dst.morphA      = clipboard_.scene.floor.morphA;
                            dst.morphB      = clipboard_.scene.floor.morphB;
                            dst.initialised = true;
                            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            {
                                auto& ph =
                                    processor_.song().tracks[static_cast<std::size_t>(t)]
                                        .phrases[static_cast<std::size_t>(destG)];
                                ph = clipboard_.scene.phrases[static_cast<std::size_t>(t)];
                                ph.initialised = true;
                            }
                            processor_.refreshWorkingFromModel();
                            setStatus("Pasted Scene");
                            pendingConfirm_ = PendingConfirm::None;
                            repaint();
                            keyboardArea_.repaint();
                            return true;
                        }
                        // Execute the pending delete against the current scope.
                        // Snapshot first so the delete is undoable via Restore.
                        switch (editMode_.primaryScope())
                        {
                            case PS::Track:
                            {
                                const int t = keyboardArea_.getActiveTrack();
                                if (t >= 0 && t < static_cast<int>(kNumTracks))
                                {
                                    processor_.snapshot(CheckpointScope::Track, t);
                                    processor_.deleteTrack(t);
                                    setStatus("Deleted Track " + juce::String(t + 1));
                                }
                                break;
                            }
                            case PS::Phrase:
                            {
                                int ckTrk = 0;
                                processor_.snapshot(ckScope(ckTrk), ckTrk);
                                for (auto& trk : processor_.sequence().tracks)
                                {
                                    for (auto& s : trk.steps)
                                    {
                                        s.trig              = false;
                                        s.condition         = TrigCondition{};
                                        s.overrides         = PLock{};
                                        s.trigOverride      = TrigOverride{};
                                        s.fillTrigState     = FillTrigState::Off;
                                        s.fillOverrides     = PLock{};
                                        s.fillTrigOverride  = TrigOverride{};
                                    }
                                }
                                setStatus("Deleted Phrase");
                                break;
                            }
                            case PS::Scene:
                            {
                                int ckTrk = 0;
                                processor_.snapshot(ckScope(ckTrk), ckTrk);
                                processor_.deletePart();
                                releaseTransientLatch(CB::SceneScope);
                                setStatus("Deleted Part");
                                break;
                            }
                            default:
                                break;
                        }
                    }
                    else
                    {
                        setStatus("Cancelled");
                    }
                    pendingConfirm_ = PendingConfirm::None;
                    repaint();
                    return true;
                }

                // No pending confirm. Bare Yes (no Func) = snapshot/confirm verb.
                if (!funcHeld)
                {
                    keyboardArea_.repaint();
                    editMode_.onVerb(ev.button);
                    return true;
                }

                // Func+P = No/cancel. No scope → scope-aware Restore (resolved on key-up).
                if (editMode_.primaryScope() == PS::None
                    || editMode_.primaryScope() == PS::Func)
                {
                    restoreActive_    = true;
                    restoreKeyDownMs_ = juce::Time::getMillisecondCounterHiRes();
                    return true;
                }
                editMode_.onVerb(ev.button);
                return true;
            }

            case ControllerButton::Snapshot:
                // Reserved while a section-suite scope is held (see helper above):
                // Func+scope+Yes is that scope's secondary, not a global snapshot.
                if (sectionSuiteScopeHeld(uiState_)) return true;
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                repaint();
                return true;
            case ControllerButton::Restore:
                // Resolve on key-up (tap = pop one, hold = jump to floor).
                if (sectionSuiteScopeHeld(uiState_)) return true;
                restoreKeyDownMs_ = juce::Time::getMillisecondCounterHiRes();
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
                processor_.requestFreshStart();
                return true;
            case ControllerButton::RecordArm:
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                const bool isDouble = doubleTap_.recordAndCheck(
                    1000 + static_cast<int>(ControllerButton::RecordArm), now);
                if (isDouble)
                {
                    processor_.clock().setRecordArmed(true);
                    processor_.clock().setOverdubArmed(true);
                }
                else
                {
                    processor_.clock().setOverdubArmed(false);
                    processor_.clock().setRecordArmed(!processor_.clock().isRecordArmed());
                }
                return true;
            }

            // MD.6/MD.7: Mute toggle (PRINCIPLES §15 rungs; DESIGN §13.0).
            // Func+Mute+step → additive solo toggle (rung 4 — solo is the
            //   secondary/advanced layer of mute).
            // Scene+Mute+step → per-scene mute (active-mask, rung 5).
            // Mute+step → immediate global mute toggle (rung 3, hold-tap-many).
            case ControllerButton::ToggleMute:
            {
                const int trackIdx = ev.index;
                if (trackIdx < 0 || trackIdx >= static_cast<int>(kNumTracks))
                    return true;
                if (uiState_.funcHeld)
                {
                    // Func+Mute+step = solo (additive toggle).
                    processor_.toggleSolo(trackIdx);
                }
                else if (uiState_.sceneHeld)
                {
                    // Scene+Mute+step = scene mute: toggle this track's active-mask
                    // for the current scene (immediate). Moved off Func+Mute so the
                    // scope+verb grammar's cross-column compound owns it (DESIGN §13).
                    processor_.togglePatternMute(trackIdx);
                }
                else
                {
                    // Immediate global mute (MD.6). Func+Mute = solo and
                    // Scene+Mute = scene mute are the compounds above.
                    processor_.toggleGlobalMute(trackIdx);
                }
                repaint();
                return true;
            }

            case ControllerButton::ForkPart:
                // Part fork removed in Phase 7; gesture is a no-op until repurposed.
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
                            if (staged.count(s.trigOverride.notes[static_cast<std::size_t>(n)]) == 0)
                                kept[static_cast<std::size_t>(newCount++)] = s.trigOverride.notes[static_cast<std::size_t>(n)];
                        s.trigOverride.noteCount = newCount;
                        for (int n = 0; n < newCount; ++n)
                            s.trigOverride.notes[static_cast<std::size_t>(n)] = kept[static_cast<std::size_t>(n)];
                    }
                    uiState_.noteEditMode = false;
                    uiState_.noteEditSteps.clear();
                    uiState_.noteEditStaged.clear();
                }
                uiState_.funcSrcHeld = false;
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
                uiState_.funcTrackHeld = false;
                editMode_.onScopeEvent({ T::ButtonUp, CB::Func });
                updateFillActivation();
                keyboardArea_.repaint();
                repaint();
                break;

            case CB::TrackScope:
                physHeld_.track = false;
                uiState_.funcTrackHeld = false;  // exit machine/Kit picker on Track release
                if (!uiState_.latch.track)
                {
                    uiState_.trackHeld = false;
                    processor_.setControlAllActive(false);  // MD.10
                    editMode_.onScopeEvent({ T::ButtonUp, CB::TrackScope });
                    repaint();
                }
                break;

            case CB::PhraseScope:
                physHeld_.phrase = false;
                if (!uiState_.latch.phrase)
                {
                    uiState_.phraseScopeHeld = false;
                    uiState_.phraseScopeUsed = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::PhraseScope });
                    repaint();
                }
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

            case CB::MorphScope:
                physHeld_.morph = false;
                if (!uiState_.latch.morph)
                {
                    uiState_.morphHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::MorphScope });
                    repaint();
                }
                break;

            case CB::SongScope:
                physHeld_.song = false;
                if (!uiState_.latch.song)
                {
                    uiState_.songHeld = false;
                    editMode_.onScopeEvent({ T::ButtonUp, CB::SongScope });
                    repaint();
                }
                break;

            case CB::Section:
            case CB::MetaSection:
                heldSectionRawCode_ = -1;
                uiState_.funcSrcHeld = false;  // Src released: no longer in Func+Src compound
                editMode_.setSectionHeld(false);
                break;

            case CB::VerbPlay:
                playKeyHeld_ = false;
                break;

            case CB::Step:
            {
                // CHROMATIC gate: a pad-release always ends the note it sounded —
                // before any mode-specific handling, and regardless of the current
                // mode/octave/track (we release the exact note we stored on press),
                // so notes can never hang. Poly: each pad releases independently.
                if (ev.index >= 0 && ev.index < 16)
                {
                    const auto pad = static_cast<std::size_t>(ev.index);
                    if (chromaticHeldNote_[pad] >= 0)
                    {
                        processor_.liveNoteOff(chromaticHeldTrack_[pad],
                                               chromaticHeldNote_[pad]);
                        chromaticHeldNote_[pad] = -1;
                    }
                }

                // Func+Src+step: step release while funcSrcHeld → enter NoteEdit mode.
                if (uiState_.funcSrcHeld && !uiState_.noteEditMode
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
                        if (m == TrackInputMode::Chromatic || m == TrackInputMode::Levels)
                        {
                            keyboardArea_.repaint();
                            break;
                        }
                    }
                }

                // Normal step release: look up absStep by rawCode and commit.
                for (int i = static_cast<int>(heldStepKeys_.size()) - 1; i >= 0; --i)
                {
                    auto [code, stepIdx] = heldStepKeys_[static_cast<std::size_t>(i)];
                    if (code == rawCode)
                    {
                        auto& ctx = processor_.editContext();

                        // MHZ.9.5: latched step — keep it in the edit context, just remove
                        // the physical key entry. Suppress trig toggle for the latch.
                        if (ctx.isLatched(stepIdx))
                        {
                            heldStepKeys_.erase(heldStepKeys_.begin() + i);
                            break;
                        }

                        const int  track      = ctx.heldTrackIndex();
                        const bool paramWrote = ctx.wasParamWritten();
                        ctx.release(stepIdx);
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
                                // MHZ.9.5: record for possible revert if double-tap follows.
                                lastTrigToggleStep_    = stepIdx;
                                lastTrigToggleTrack_   = track;
                                lastTrigToggleApplied_ = true;
                            }
                        }
                        heldStepKeys_.erase(heldStepKeys_.begin() + i);
                        break;
                    }
                }
                // MHZ.9.5: clear stepHeld/trigHeld only when no physical or latched steps remain.
                if (processor_.editContext().heldSteps().empty())
                {
                    uiState_.stepHeld = false;
                    editMode_.setTrigHeld(false);
                }
                repaint();
                break;
            }

            case CB::VerbYes:
                break;  // Y = Snapshot; no held-state to clear.

            case CB::Restore:
            {
                if (!restoreActive_) break;
                const double held = juce::Time::getMillisecondCounterHiRes() - restoreKeyDownMs_;
                restoreActive_ = false;
                int ckTrk = 0;
                const CheckpointScope scp = ckScope(ckTrk);
                if (held >= kHoldRestoreMs)
                    processor_.restoreToFloor(scp, ckTrk);
                else
                    processor_.restoreOne(scp, ckTrk);
                repaint();
                break;
            }

            case CB::VerbNo:
            {
                // Func+P "Restore" path (recorded press time in dispatchDown).
                if (restoreActive_)
                {
                    const double held = juce::Time::getMillisecondCounterHiRes() - restoreKeyDownMs_;
                    restoreActive_ = false;
                    int ckTrk = 0;
                    const CheckpointScope scp = ckScope(ckTrk);
                    if (held >= kHoldRestoreMs)
                        processor_.restoreToFloor(scp, ckTrk);
                    else
                        processor_.restoreOne(scp, ckTrk);
                    repaint();
                }
                break;
            }

            case CB::VerbRecord:
            case CB::VerbStop:
            case CB::VerbClear:
            case CB::VerbDelete:
            case CB::VerbPanic:
            case CB::Snapshot:
            case CB::NavUp:
            case CB::NavLeft:
            case CB::NavDown:
            case CB::NavRight:
            case CB::SelectTrack:
            case CB::ToggleMute:
            case CB::ForkPart:
            case CB::RecordArm:
            case CB::TapTempo:
            case CB::MetronomeToggle:
            case CB::PlayStop:
            case CB::StopReset:
            case CB::None:
                break;

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

        // Repaint on every physical key transition — press AND release — so that
        // *every* key flashes when struck, including disabled section keys and the
        // off-grid edge keys (` Tab Caps Shift - = [ ] '), which dispatch no bound
        // event and would otherwise never trigger a repaint. keyStateChanged fires
        // for modifier keys too (Shift), which never reach keyPressed. The paint
        // path reads live physical state, so this is order-independent with
        // keyPressed. (Orientation aid: PRINCIPLES §8 — no silent keys.)
        juce::ignoreUnused(isKeyDown);
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

        greyoutLayer_.setBounds(getLocalBounds());

        poolOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));

        soundBankOverlay_.setBounds(manipulationZone_.getBounds()
            .withBottom(keyboardArea_.getY() + keyboardArea_.stepRowsLocalY()));

    }

    // -------------------------------------------------------------------------
    // Clipboard helpers

    void LockstepEditor::captureScene()
    {
        auto& cl = clipboard_;
        cl.scene.floor = processor_.section();
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            cl.scene.phrases[static_cast<std::size_t>(t)] = processor_.activePhrase(t);
        cl.type = ClipboardType::Scene;
    }

    bool LockstepEditor::phraseConflictAndConfirm(int phraseSlot, PendingConfirm action)
    {
        const int sharers = processor_.phraseSlotSharers(phraseSlot);
        bool slotHasContent = false;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (processor_.song().tracks[static_cast<std::size_t>(t)]
                    .phrases[static_cast<std::size_t>(phraseSlot)].initialised)
            {
                slotHasContent = true;
                break;
            }
        }
        if (sharers == 0 && !slotHasContent)
            return false;   // clean — no conflict

        pendingConfirm_ = action;
        pendingTarget_  = phraseSlot;
        const int freeSlot = processor_.firstFreePhraseSlot();
        juce::String msg = "Overwrite phrase slot " + juce::String(phraseSlot) + "?";
        if (sharers > 0) msg += "  SHR:" + juce::String(sharers);
        if (freeSlot >= 0) msg += "  free:P" + juce::String(freeSlot);
        msg += "  P=Yes  Func+P=No";
        setStatus(msg);
        repaint();
        return true;    // conflict raised — caller must wait for Yes/No
    }

    // -------------------------------------------------------------------------
    // Transient status line

    void LockstepEditor::setStatus(const juce::String& msg)
    {
        statusMessage_ = msg;
        statusSetMs_   = juce::Time::getMillisecondCounter();
        repaint();
    }

    void LockstepEditor::paintStatus(juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (statusMessage_.isEmpty()) return;
        const auto elapsed = juce::Time::getMillisecondCounter() - statusSetMs_;
        if (elapsed > kStatusDurationMs) return;
        const float alpha = juce::jlimit(0.0f, 1.0f,
            1.0f - static_cast<float>(elapsed) / static_cast<float>(kStatusDurationMs));
        g.setColour(juce::Colour(0xFF1E2028u).withAlpha(alpha));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFF80FFB0u).withAlpha(alpha));
        g.drawText(statusMessage_, area.reduced(4, 0), juce::Justification::centredLeft, true);
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
                    if (clipboard_.type != ClipboardType::Step
                        && clipboard_.type != ClipboardType::All) break;
                    const int anchor  = ctx.heldStepIndex();
                    const int trkLen  = trk.length;
                    for (const auto& entry : clipboard_.stepEntries)
                    {
                        int dst = (anchor + entry.relOffset);
                        dst = ((dst % trkLen) + trkLen) % trkLen;
                        trk.steps[static_cast<std::size_t>(dst)] = entry.data;
                    }
                }
                else if (verb == CB::VerbClear)
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
                    // Clearing a P-Lock (or the whole step) is an edit, exactly
                    // like writing one — mark it so the step-release handler does
                    // not also toggle the trig (it suppresses the toggle whenever
                    // a param was written during the hold).
                    processor_.editContext().markParamWritten();
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
                    if (clipboard_.type != ClipboardType::Section
                        && clipboard_.type != ClipboardType::All) break;
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
                else if (verb == CB::VerbClear)
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
            // MD.4  Track copy / paste / clear / delete
            // -----------------------------------------------------------------------
            case PS::Track:
            {
                const int activeTrack = keyboardArea_.getActiveTrack();
                auto& trk = processor_.sequence()
                                .tracks[static_cast<std::size_t>(activeTrack)];
                const juce::String trkName = "Track " + juce::String(activeTrack + 1);

                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipTrack = trk;
                    clipboard_.type      = ClipboardType::Track;
                    setStatus("Copied " + trkName);
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Track
                        && clipboard_.type != ClipboardType::All) break;
                    trk = clipboard_.clipTrack;
                    setStatus("Pasted → " + trkName);
                }
                else if (verb == CB::VerbClear)
                {
                    // Clear steps; preserve length, divider, and base params.
                    for (auto& s : trk.steps)
                    {
                        s.trig       = false;
                        s.condition  = TrigCondition{};
                        s.overrides  = PLock{};
                        s.trigOverride = TrigOverride{};
                    }
                    setStatus("Cleared " + trkName);
                }
                releaseTransientLatch(CB::TrackScope);
                break;
            }

            // -----------------------------------------------------------------------
            // MD.5  Pattern copy / paste / clear / delete
            // -----------------------------------------------------------------------
            case PS::Phrase:
            {
                if (verb == CB::VerbRecord)
                {
                    clipboard_.clipSequence = processor_.sequence();
                    clipboard_.type = ClipboardType::Pattern;
                    setStatus("Copied Phrase");
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Pattern
                        && clipboard_.type != ClipboardType::All) break;
                    processor_.sequence() = clipboard_.clipSequence;
                    setStatus("Pasted Phrase");
                }
                else if (verb == CB::VerbClear || verb == CB::VerbDelete)
                {
                    if (verb == CB::VerbDelete)
                    {
                        int ckTrk = 0;
                        processor_.snapshot(ckScope(ckTrk), ckTrk);
                    }
                    for (auto& trk : processor_.sequence().tracks)
                    {
                        for (auto& s : trk.steps)
                        {
                            s.trig              = false;
                            s.condition         = TrigCondition{};
                            s.overrides         = PLock{};
                            s.trigOverride      = TrigOverride{};
                            s.fillTrigState     = FillTrigState::Inherit;
                            s.fillOverrides     = PLock{};
                            s.fillTrigOverride  = TrigOverride{};
                        }
                    }
                    setStatus("Cleared Phrase");
                }
                break;
            }

            // -----------------------------------------------------------------------
            // Scene copy / paste — DESIGN §23.3
            // Gate on Func because bare Scene+Record = bake (handled in dispatchDown).
            // VerbDelete goes through the pending-confirm path in dispatchDown.
            // -----------------------------------------------------------------------
            case PS::Scene:
            {
                const bool funcHeld = editMode_.scopeState().func;
                const bool muteHeld = editMode_.scopeState().mute;
                if (!funcHeld) break;

                if (verb == CB::VerbRecord)
                {
                    captureScene();
                    setStatus("Copied Scene");
                }
                else if (verb == CB::VerbPlay)
                {
                    if (clipboard_.type != ClipboardType::Scene
                        && clipboard_.type != ClipboardType::All) break;

                    const int destG = processor_.section().globalPhrase;
                    if (muteHeld)
                    {
                        // Mute+Func+Scene+Play: floor-only paste (no phrase content).
                        auto& dst = processor_.section();
                        dst.activeMask = clipboard_.scene.floor.activeMask;
                        dst.coreTime   = clipboard_.scene.floor.coreTime;
                        dst.morphA     = clipboard_.scene.floor.morphA;
                        dst.morphB     = clipboard_.scene.floor.morphB;
                        dst.initialised = true;
                        setStatus("Pasted Scene floor");
                    }
                    else
                    {
                        // Func+Scene+Play: baked paste — phrase content + floor.
                        if (phraseConflictAndConfirm(destG, PendingConfirm::PasteScene))
                            break;  // waiting for Yes/No
                        // Snapshot first (snapshot calls writeBackWorkingToActive
                        // so the working buffer is safe; then we overwrite the model).
                        processor_.snapshot(CheckpointScope::Song, 0);
                        auto& dst = processor_.section();
                        dst.activeMask  = clipboard_.scene.floor.activeMask;
                        dst.coreTime    = clipboard_.scene.floor.coreTime;
                        dst.morphA      = clipboard_.scene.floor.morphA;
                        dst.morphB      = clipboard_.scene.floor.morphB;
                        dst.initialised = true;
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                        {
                            auto& ph =
                                processor_.song().tracks[static_cast<std::size_t>(t)]
                                    .phrases[static_cast<std::size_t>(destG)];
                            ph = clipboard_.scene.phrases[static_cast<std::size_t>(t)];
                            ph.initialised = true;
                        }
                        processor_.refreshWorkingFromModel();
                        setStatus("Pasted Scene");
                    }
                }
                break;
            }

            case PS::Func:
            case PS::Mute:
            case PS::Fill:
            case PS::Cue:
            case PS::Morph:
                break;

            // Song+Clear: Panic (kill all voices). Previously Func+I.
            case PS::Song:
                if (verb == CB::VerbClear)
                {
                    processor_.requestPanic();
                    setStatus("Panic");
                }
                break;

            case PS::None:
                // VerbYes (Y = Snapshot) with no scope: Song-scope snapshot.
                // VerbClear no-scope P-Lock clear is handled in dispatchDown.
                // VerbNo (P = Yes/confirm) routing is handled in dispatchDown.
                if (verb == CB::VerbYes)
                {
                    int ckTrk = 0;
                    processor_.snapshot(ckScope(ckTrk), ckTrk);
                }
                break;

            default:
                break;
        }

        // Chrome must repaint after any verb that may change clipboard or checkpoint state.
        repaint();
    }

    // -------------------------------------------------------------------------
    // Checkpoint scope helper

    CheckpointScope LockstepEditor::ckScope(int& outTrack) const
    {
        using PS = EditMode::PrimaryScope;
        outTrack = keyboardArea_.getActiveTrack();
        switch (editMode_.primaryScope())
        {
            case PS::Track:  return CheckpointScope::Track;
            case PS::Scene:  return CheckpointScope::Scene;
            case PS::Phrase: return CheckpointScope::Phrase;
            default:         return CheckpointScope::Song;
        }
    }

    // -------------------------------------------------------------------------
    // Controller surface — sink construction

    ControllerEventSink LockstepEditor::buildControllerSink()
    {
        ControllerEventSink sink;

        sink.emitEvent = [this](ControllerEvent ev)
        {
            // Mirror QwertyOverlay::resolve() modifier priority for Step events:
            // trackHeld → SelectTrack, muteHeld → ToggleMute (same index).
            // This makes the controller equivalent to keyboard for these modes.
            if (ev.button == ControllerButton::Step)
            {
                if (uiState_.trackHeld)
                    ev.button = ControllerButton::SelectTrack;
                else if (uiState_.muteHeld)
                    ev.button = ControllerButton::ToggleMute;
            }

            if (ev.type == ControllerEvent::Type::ButtonDown)
            {
                pressTracker_.press(PressTracker::kControllerSource, ev.button, ev.index);
                dispatchDown(ev, PressTracker::kControllerSource);
            }
            else if (ev.type == ControllerEvent::Type::ButtonUp)
            {
                pressTracker_.release(PressTracker::kControllerSource);
                dispatchUp(ev, PressTracker::kControllerSource);
            }
            // Mirror the mouse/keyboard paths: repaint so a controller press/release
            // updates the on-screen highlight immediately (else a released key's
            // highlight lingers until the next unrelated repaint).
            keyboardArea_.repaint();
        };

        sink.applyParamDelta = [this](int mzSlot, int rawDelta)
        {
            const int track   = keyboardArea_.getActiveTrack();
            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            const auto  spec  = processor_.paramSpec(track, absSlot);
            const float range = spec.maxValue - spec.minValue;
            if (range <= 0.0f) return;

            // Read the OEB-resolved current value (Override-ELSE-Base) so that
            // encoder deltas accumulate correctly when P-lock editing is active.
            // Without this, every turn would restart from the track base value
            // causing the parameter to oscillate instead of advancing.
            float cur = processor_.baseParamValue(track, absSlot);
            const auto& ec = processor_.editContext();
            if (ec.isActiveForEditing() && ec.heldTrackIndex() == track)
            {
                const int heldStep = ec.heldStepIndex();
                if (heldStep >= 0)
                {
                    const auto& s = processor_.sequence()
                        .tracks[static_cast<std::size_t>(track)]
                        .steps[static_cast<std::size_t>(heldStep)];
                    cur = s.overrides.get(absSlot, cur);
                }
            }

            const float norm    = juce::jlimit(0.0f, 1.0f, (cur - spec.minValue) / range);
            const float newNorm = juce::jlimit(0.0f, 1.0f,
                                               norm + static_cast<float>(rawDelta) / 128.0f);
            processor_.writeParam(track, absSlot, spec.minValue + newNorm * range);
            processor_.editContext().markParamWritten();
        };

        sink.resetSlot = [this](int mzSlot)
        {
            const int track   = keyboardArea_.getActiveTrack();
            const int absSlot = manipulationZone_.slotOffset() + mzSlot;
            if (absSlot >= processor_.numParams(track)) return;

            auto& ctx = processor_.editContext();
            if (ctx.isActiveForEditing())
            {
                processor_.clearParam(ctx.heldTrackIndex(), ctx.heldStepIndex(), absSlot);
            }
            else
            {
                const auto spec = processor_.paramSpec(track, absSlot);
                processor_.writeParam(track, absSlot, spec.defaultValue);
            }
            ctx.markParamWritten();
        };

        sink.setCrossfader = [this](float normValue)
        {
            crossfader_.setValue(static_cast<double>(normValue), juce::sendNotificationAsync);
        };

        sink.applyGlobalDelta = [this](GlobalTarget target, int rawDelta)
        {
            switch (target)
            {
                case GlobalTarget::Tempo:
                {
                    const double cur = processor_.clock().localBpm();
                    processor_.clock().setLocalBpm(
                        std::clamp(cur + static_cast<double>(rawDelta) * 0.5,
                                   20.0, 300.0));
                    break;
                }
                case GlobalTarget::Master:
                {
                    auto* p = processor_.apvts().getParameter(ParamIDs::outputGain);
                    if (p)
                    {
                        const float cur    = p->getValue();  // normalised 0..1
                        const float newVal = juce::jlimit(0.0f, 1.0f,
                                                          cur + static_cast<float>(rawDelta) / 128.0f);
                        p->setValueNotifyingHost(newVal);
                    }
                    break;
                }
                case GlobalTarget::Swing:
                    break;  // not yet implemented in sequencer core
            }
        };

        return sink;
    }
}
