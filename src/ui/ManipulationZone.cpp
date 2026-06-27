#include "ManipulationZone.h"
#include "MetaBand.h"
#include "ParamFormat.h"
#include "../PluginProcessor.h"
#include "KeyboardArea.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    // Fraction of the inner band height used for each row.  Two rows at this
    // fraction overlap vertically; the stagger keeps their knobs clear.
    static constexpr float kRowHeightFrac = 0.62f;
    static constexpr int kCellNameH = 16;   // name label strip height
    static constexpr int kCellValueH = 15;   // value label strip height

    ManipulationZone::ManipulationZone(LockstepProcessor& processor, KeyboardArea& area)
        : processor_(processor), area_(area)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);

            labels_[si].setJustificationType(juce::Justification::centred);
            labels_[si].setFont(juce::Font(juce::FontOptions(14.0f)));
            labels_[si].setInterceptsMouseClicks(false, false);
            addAndMakeVisible(labels_[si]);

            valueLabels_[si].setJustificationType(juce::Justification::centred);
            valueLabels_[si].setFont(juce::Font(juce::FontOptions(13.0f)));
            valueLabels_[si].setInterceptsMouseClicks(false, false);
            addAndMakeVisible(valueLabels_[si]);

            sliders_[si].setLookAndFeel(&laf_);
            sliders_[si].setWantsKeyboardFocus(false);
            sliders_[si].onDragStart = [this, i] {
                if (band_ == MetaBand::None)
                    processor_.editContext().setActiveSlot(slotOffset_ + i);
                lastSlotValid_ = false;  // start each drag with a clean reference point
                juce::ignoreUnused(i);
            };
            sliders_[si].onValueChange = [this, i] {
                if (updatingFromTimer_) return;
                const float v = static_cast<float>(
                    sliders_[static_cast<std::size_t>(i)].getValue());
                if (band_ != MetaBand::None)
                {
                    // Density + Song: use incremental delta so JUCE's accumulating
                    // absolute value doesn't snap the master when dragging starts.
                    if (band_ == MetaBand::Density && uiState_ && densityEditsMaster(*uiState_))
                    {
                        if (lastSlotValid_)
                        {
                            const float dMaster = (v - lastSlotValue_[static_cast<std::size_t>(i)]) / 100.0f;
                            processor_.setMasterDensity(juce::jlimit(-1.0f, 1.0f,
                                processor_.masterDensity() + dMaster));
                        }
                        lastSlotValue_[static_cast<std::size_t>(i)] = v;
                        lastSlotValid_ = true;
                        refreshSliders();
                        return;
                    }

                    static UiState kEmptyUiState{};
                    writeMetaField(band_, swingScope_, i, v, processor_,
                                   area_.getActiveTrack(), processor_.editContext(),
                                   uiState_ ? *uiState_ : kEmptyUiState);
                    // Update the value-text labels live (stepped meta bands like
                    // KEY/TIME carry enum labels that don't follow the slider on
                    // their own). Text-only — does not reset the active drag.
                    refreshMetaValueText();
                    if (band_ == MetaBand::Euclidean && onEuclidParamChanged)
                        onEuclidParamChanged();
                    if (band_ == MetaBand::Melodic && onMelodyParamChanged)
                        onMelodyParamChanged();
                    // Step-Position: the visible effect (the step hopping) is on the
                    // grid, not the MZ. Run the editor's canonical surface refresh so
                    // it previews in realtime exactly like the nav ←/→ keys. Do NOT
                    // refreshSliders here — setting the dragged slider's value mid-drag
                    // resets JUCE's drag reference and stalls the encoder until release.
                    if (band_ == MetaBand::StepPosition && onStepPositionChanged)
                        onStepPositionChanged();
                    return;
                }
                // Machine-param path.
                if (processor_.fillActive())
                {
                    processor_.writeFillParam(area_.getActiveTrack(), slotOffset_ + i, v);
                    return;
                }
                const int track = area_.getActiveTrack();
                const int slot = slotOffset_ + i;
                if (morphQualifier_ == 1)
                {
                    processor_.writeMorphPole(track, slot, v, 0);
                    return;
                }
                if (morphQualifier_ == 2)
                {
                    processor_.writeMorphPole(track, slot, v, 1);
                    return;
                }
                if (morphHeld_)
                {
                    const float cur = processor_.morphEffectiveValue(track, slot);
                    processor_.writeMorph(track, slot, v - cur, processor_.morphFader());
                    return;
                }
                const auto mInfo = processor_.morphWidgetInfo(track, slot);
                if (mInfo.exists)
                {
                    if (mInfo.inA && !mInfo.inB)
                        processor_.writeMorphPole(track, slot, v, 0);
                    else if (!mInfo.inA && mInfo.inB)
                        processor_.writeMorphPole(track, slot, v, 1);
                    else
                    {
                        const float cur = processor_.morphEffectiveValue(track, slot);
                        processor_.writeMorph(track, slot, v - cur, processor_.morphFader());
                    }
                }
                else
                    processor_.writeParam(track, slot, v);
            };
            sliders_[si].addMouseListener(static_cast<juce::MouseListener*>(this), false);
            addAndMakeVisible(sliders_[si]);

            clearBtns_[si].setButtonText("x");
            clearBtns_[si].setWantsKeyboardFocus(false);
            clearBtns_[si].onClick = [this, i] {
                auto& ctx = processor_.editContext();
                if (!ctx.isActiveForEditing()) return;
                if (band_ == MetaBand::Trig)
                    processor_.clearTrigOverrideField(ctx.heldTrackIndex(),
                                                      ctx.heldStepIndex(), i);
                else if (processor_.fillActive())
                    processor_.clearFillParam(ctx.heldTrackIndex(), ctx.heldStepIndex(),
                                              slotOffset_ + i);
                else
                    processor_.clearParam(ctx.heldTrackIndex(), ctx.heldStepIndex(),
                                          slotOffset_ + i);
                // Removing a P-Lock is an edit, like writing one — mark it so the
                // held-step release does not also toggle the step's trig.
                ctx.markParamWritten();
            };
            addAndMakeVisible(clearBtns_[si]);
        }

        samplePickerBtn_.setWantsKeyboardFocus(false);
        samplePickerBtn_.addMouseListener(static_cast<juce::MouseListener*>(this), false);
        samplePickerBtn_.onClick = [this] {
            const int track = area_.getActiveTrack();
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
            showSamplePicker(slotOffset_);
        };
        addChildComponent(samplePickerBtn_);

        processor_.setMZSlots(slotOffset_);
        // No perpetual timer (9.15): the slider view is refreshed from the
        // editor's surface frame (refreshSliders), and the timer runs only while
        // a CC-learn is pending — see showMappingMenu / timerCallback.
    }

    ManipulationZone::~ManipulationZone()
    {
        for (auto& s : sliders_)
        {
            s.setLookAndFeel(nullptr);
            s.removeMouseListener(static_cast<juce::MouseListener*>(this));
        }
    }

    void ManipulationZone::setSlotOffset(int offset)
    {
        slotOffset_ = offset;
        processor_.setMZSlots(slotOffset_);
        setBand(MetaBand::None, 0);
        refreshSliders();
    }

    void ManipulationZone::setNormalTitle(const juce::String& title, int page, int pageCount)
    {
        normalTitle_    = title;
        normalPage_     = page;
        normalPageCount_ = pageCount;
        repaint();
    }

    void ManipulationZone::setBand(MetaBand band, int swingScope)
    {
        band_ = band;
        swingScope_ = swingScope;
        lastSlotValid_ = false;

        samplePickerBtn_.setVisible(false);
        for (std::size_t i = 0; i < kNumSlots; ++i)
        {
            sliders_[i].setVisible(true);
            valueLabels_[i].setVisible(true);
            clearBtns_[i].setVisible(true);
            labels_[i].setVisible(true);
        }

        repaint();
    }

    void ManipulationZone::mouseDown(const juce::MouseEvent& e)
    {
        if (!e.mods.isRightButtonDown() || band_ != MetaBand::None)
            return;

        if (e.eventComponent == &samplePickerBtn_)
        {
            showMappingMenu(0);
            return;
        }

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (e.eventComponent == &sliders_[static_cast<std::size_t>(i)])
            {
                showMappingMenu(i);
                return;
            }
        }
    }

    void ManipulationZone::showMappingMenu(int slotIndex)
    {
        const int track = area_.getActiveTrack();
        const int slot = slotOffset_ + slotIndex;
        const auto info = processor_.queryWidgetMapping(slot, slotIndex);

        juce::PopupMenu menu;

        if (info.exists)
        {
            juce::String header = "CC " + juce::String(info.ccNumber) + " mapped (";
            switch (info.scope)
            {
                case CCScope::Track:         header += "T" + juce::String(info.trackIndex + 1); break;
                case CCScope::SelectedTrack: header += "S"; break;
                case CCScope::Contextual:    header += "C"; break;
                case CCScope::Global:        header += "G"; break;
                case CCScope::Crossfader:    header += "X"; break;
            }
            header += ")";
            menu.addSectionHeader(header);
            menu.addItem(1, "Clear mapping");
        }
        else
        {
            menu.addSectionHeader("Map this control via MIDI Learn:");
            menu.addItem(1, juce::String(u8"Fixed — track ") + juce::String(track + 1) + ", slot " + juce::String(slot));
            menu.addItem(2, "Selected track (follows focus)");
            menu.addItem(3, "Contextual (this display position)");
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(sliders_[static_cast<std::size_t>(slotIndex)]),
            [this, info, track, slot, slotIndex](int result) {
                if (result == 0)
                    return;

                if (info.exists)
                {
                    processor_.ccMappingTable().removeMapping(
                        info.ccNumber, info.scope,
                        info.trackIndex, info.slot, info.mzPosition);
                    learningSlotIndex_ = -1;
                }
                else
                {
                    CCScope scope = CCScope::Track;
                    if (result == 2) scope = CCScope::SelectedTrack;
                    if (result == 3) scope = CCScope::Contextual;
                    processor_.startLearn(scope, track, slot, slotIndex);
                    learningSlotIndex_ = slotIndex;
                    // Drive the learn pulse + completion detection. Self-suspends
                    // when learn ends (timerCallback stops it), so the MZ has no
                    // perpetual poll.
                    startTimerHz(30);
                }
            });
    }

    void ManipulationZone::timerCallback()
    {
        // Runs only while a CC-learn is pending (started in showMappingMenu).
        // Detect completion (audio thread cleared the flag), pulse the overlay
        // (~3 Hz), then self-suspend so the MZ has no perpetual timer (9.15).
        // The step-position grid preview and external param sync are now handled
        // by the surface-invalidation channel, not a per-tick poll here.
        if (learningSlotIndex_ >= 0 && !processor_.isLearning())
            learningSlotIndex_ = -1;

        if (learningSlotIndex_ >= 0)
            repaint();
        else
            stopTimer();
    }

    void ManipulationZone::refreshMetaValueText()
    {
        if (band_ == MetaBand::None) return;
        const int track = area_.getActiveTrack();
        static const UiState kEmptyUiState{};
        const auto views = buildMetaBand(band_, swingScope_, processor_, track,
                                         processor_.editContext(),
                                         uiState_ ? *uiState_ : kEmptyUiState);
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            labels_[si].setText(views[si].label, juce::dontSendNotification);
            valueLabels_[si].setText(views[si].valueText, juce::dontSendNotification);
            clearBtns_[si].setEnabled(views[si].hasOverride);
            clearBtns_[si].setAlpha(views[si].hasOverride ? 1.0f : 0.3f);
        }
    }

    void ManipulationZone::refreshSliders()
    {
        if (band_ != MetaBand::None)
        {
            samplePickerBtn_.setVisible(false);
            const int track = area_.getActiveTrack();
            static const UiState kEmptyUiState{};
            const auto views = buildMetaBand(band_, swingScope_, processor_, track,
                                             processor_.editContext(),
                                             uiState_ ? *uiState_ : kEmptyUiState);
            updatingFromTimer_ = true;
            for (int i = 0; i < kNumSlots; ++i)
            {
                const auto si = static_cast<std::size_t>(i);
                const auto& v = views[si];
                const double lo = static_cast<double>(v.minValue);
                const double hi = v.maxValue > v.minValue
                                      ? static_cast<double>(v.maxValue)
                                      : lo + 1.0;
                MetaRotary::View mv;
                mv.rangeLo = lo;
                mv.rangeHi = hi;
                mv.interval = v.stepped ? 1.0 : 0.0;
                mv.value = static_cast<double>(v.value);
                mv.enabled = v.writable;
                mv.alpha = v.active ? 1.0f : 0.0f;
                mv.ringMode = v.ringMode;
                mv.marks = v.marks;
                mv.densityCell = v.densityCell;
                mv.densityMasterOffset = v.densityMasterOffset;
                mv.densityEffective = v.densityEffective;
                // skew defaults to 1.0; doubleClickEnabled defaults to false.
                sliders_[si].applyView(mv);
                labels_[si].setText(v.label, juce::dontSendNotification);
                valueLabels_[si].setText(v.valueText, juce::dontSendNotification);
                clearBtns_[si].setEnabled(v.hasOverride);
                clearBtns_[si].setAlpha(v.hasOverride ? 1.0f : 0.3f);
                sliders_[si].repaint();
            }
            updatingFromTimer_ = false;
            return;
        }

        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        const int numMachineParams = processor_.numParams(track);

        // Determine the active section from the first visible slot so we can
        // blank trailing cells that belong to a different section.
        const int activeSectionIndex = (slotOffset_ < numMachineParams)
                                           ? processor_.paramSpec(track, slotOffset_).sectionIndex
                                           : -1;

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const int slot = slotOffset_ + i;

            // Slot beyond this machine's schema, or belonging to a different
            // section than the page anchor — blank out the cell.
            const bool outOfSection = (slot < numMachineParams) && (processor_.paramSpec(track, slot).sectionIndex != activeSectionIndex);
            if (slot >= numMachineParams || outOfSection)
            {
                MetaRotary::View dv;
                dv.enabled = false;
                dv.alpha = 0.0f;
                sliders_[si].applyView(dv);
                labels_[si].setText({}, juce::dontSendNotification);
                valueLabels_[si].setText({}, juce::dontSendNotification);
                clearBtns_[si].setEnabled(false);
                clearBtns_[si].setAlpha(0.0f);
                continue;
            }

            const auto meta = processor_.paramSpec(track, slot);

            // Sample slot: replace rotary with a name button + picker popup.
            const bool isSampleSlot = (meta.id == "sample_id" || meta.id == "slicer_sample_id");

            const double lo = static_cast<double>(meta.minValue);
            const double hi = static_cast<double>(meta.maxValue);

            // Show the morph-appropriate value when morph data exists:
            //   qualifier=1 → raw A endpoint (pole preview while editing A)
            //   qualifier=2 → raw B endpoint
            //   qualifier=0 → fader-blended value (animates with crossfader)
            //   no morph data → kit base (existing behaviour)
            const auto mInfo = processor_.morphWidgetInfo(track, slot);
            float value = processor_.baseParamValue(track, slot);
            if (mInfo.exists)
            {
                const float base = value;
                if (morphQualifier_ == 1)
                    value = mInfo.inA ? mInfo.aValue : base;
                else if (morphQualifier_ == 2)
                    value = mInfo.inB ? mInfo.bValue : base;
                else
                    value = processor_.morphEffectiveValue(track, slot);
            }

            const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
            const int heldStep = ctx.heldStepIndex();
            const bool fillHeld = processor_.fillActive();
            const bool fillEdit = stepHeld && fillHeld && heldStep >= 0;
            bool hasLock = false;

            if (stepHeld && heldStep >= 0)
            {
                const auto& s = t.steps[static_cast<std::size_t>(heldStep)];
                // Resolved fill view: FillOverride → Override → Base.
                value = s.overrides.get(slot, value);
                if (fillEdit)
                {
                    // Show fill layer value if present; otherwise resolved base+override.
                    if (s.fillOverrides.has(slot))
                        value = s.fillOverrides.get(slot, value);
                    hasLock = s.fillOverrides.has(slot);
                }
                else
                {
                    hasLock = s.overrides.has(slot);
                }
            }

            MetaRotary::View sv;
            sv.rangeLo = lo;
            sv.rangeHi = hi > lo ? hi : lo + 1.0;
            sv.interval = meta.isStepped ? 1.0 : 0.0;
            sv.skew = meta.isStepped ? 1.0 : static_cast<double>(meta.skew);
            sv.doubleClickEnabled = true;
            sv.doubleClickValue = static_cast<double>(meta.defaultValue);
            sv.ringMode = meta.isStepped           ? RingMode::Dot
                          : (meta.minValue < 0.0f) ? RingMode::BipolarFromCentre
                                                   : RingMode::UnipolarFill;
            sv.enabled = !isSampleSlot;
            sv.alpha = isSampleSlot ? 0.0f : 1.0f;
            sv.value = static_cast<double>(value);
            sliders_[si].applyView(sv);

            if (isSampleSlot)
            {
                const int poolIdx = static_cast<int>(value);
                const juce::String shortName = processor_.sampleShortName(poolIdx);
                samplePickerBtn_.setButtonText(shortName);
                samplePickerBtn_.setVisible(true);
                const juce::String idxStr = juce::String(poolIdx) + ": ";
                valueLabels_[si].setText(idxStr + shortName + (hasLock ? " *" : ""), juce::dontSendNotification);
            }
            else
            {
                if (i == 0) samplePickerBtn_.setVisible(false);
                // MHZ.2.5: use textual valueLabels for stepped/enum slots if available.
                juce::String valueText;
                if (!meta.valueLabels.empty())
                {
                    const int idx = std::clamp(static_cast<int>(std::round(value)),
                                               0,
                                               static_cast<int>(meta.valueLabels.size()) - 1);
                    valueText = juce::String(meta.valueLabels[static_cast<std::size_t>(idx)]);
                }
                else
                {
                    valueText = formatParamValue(value, meta);
                }
                if (hasLock)
                    valueText += " *";
                valueLabels_[si].setText(valueText, juce::dontSendNotification);
            }

            // §6.10: use contextLabel hook when set; otherwise static label.
            juce::String nameText;
            if (meta.contextLabel != nullptr)
                nameText = meta.contextLabel(t.baseParams);
            else if (!meta.label.isEmpty())
                nameText = meta.label;
            else
                nameText = juce::String(slot);
            labels_[si].setText(nameText, juce::dontSendNotification);

            clearBtns_[si].setEnabled(hasLock);
            clearBtns_[si].setAlpha(hasLock ? 1.0f : 0.3f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::showSamplePicker(int absoluteSlot)
    {
        const int track = area_.getActiveTrack();
        const int poolSize = processor_.samplePool().size();

        juce::PopupMenu menu;
        if (poolSize == 0)
        {
            // A disabled section header, not addItem(0) — JUCE asserts on item id 0.
            menu.addSectionHeader("(pool is empty)");
        }
        else
        {
            for (int i = 0; i < poolSize; ++i)
                menu.addItem(i + 1, juce::String(i) + "  " + processor_.sampleShortName(i));
        }
        menu.addSeparator();
        menu.addItem(1000, "Manage pool...");

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(samplePickerBtn_),
            [this, track, absoluteSlot](int result) {
                if (result == 1000)
                {
                    if (onOpenPoolManager) onOpenPoolManager();
                    return;
                }
                if (result < 1) return;
                processor_.writeParam(track, absoluteSlot, static_cast<float>(result - 1));
            });
    }


    juce::Rectangle<int> ManipulationZone::slotCellBounds(int i) const
    {
        static constexpr int kCols = kMZSlots / 2;
        // §26.4.1: reserve kHeaderH pixels at the top for the persistent header strip.
        const auto bounds = getLocalBounds().reduced(4).withTrimmedTop(kHeaderH);
        const int baseW  = bounds.getWidth() / kCols;
        const int narrowW = baseW * 7 / 8;
        const int rowH = static_cast<int>(bounds.getHeight() * kRowHeightFrac);
        const int upperX = bounds.getX() + (bounds.getWidth() - kCols * narrowW);
        const bool isUpper = (i % 2 != 0);
        const int ci = i / 2;
        const int x = isUpper ? upperX + ci * narrowW : bounds.getX() + ci * narrowW;
        const int y = isUpper ? bounds.getY() : bounds.getBottom() - rowH;
        return juce::Rectangle<int>(x, y, narrowW, rowH).reduced(2, 2);
    }

    juce::Rectangle<int> ManipulationZone::slotKnobBounds(int i) const
    {
        const auto cell = slotCellBounds(i);
        const int knobSize = cell.getHeight() - kCellNameH - kCellValueH;
        const int knobX = cell.getX() + (cell.getWidth() - knobSize) / 2;
        const int knobY = cell.getY() + kCellNameH;
        return juce::Rectangle<int>(knobX, knobY, knobSize, knobSize);
    }

    void ManipulationZone::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(28, 32, 38));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(60, 70, 85));
        g.drawRect(getLocalBounds(), 1);

        // §26.4.1 — Persistent header strip: always visible, shows active band / section.
        {
            const auto headerRect = getLocalBounds().withHeight(4 + kHeaderH);

            const auto& ctx = processor_.editContext();
            const bool isStepEdit = ctx.isActiveForEditing();
            const bool isMeta     = (band_ != MetaBand::None);

            juce::String title;
            juce::String pageStr;

            if (isStepEdit)
            {
                const bool fillEdit = processor_.fillActive();
                title = fillEdit ? "FILL" : "P-LOCK";
                title += " T" + juce::String(ctx.heldTrackIndex() + 1)
                       + " S" + juce::String(ctx.heldStepIndex() + 1);
            }
            else if (isMeta)
            {
                title = bandTitle(band_);
                // Bank indicator for paginated meta bands (density/vel).
                if ((band_ == MetaBand::Density || band_ == MetaBand::Vel) && uiState_ != nullptr)
                {
                    const int bank = (band_ == MetaBand::Density) ? uiState_->densityBank
                                                                   : uiState_->velBank;
                    pageStr = juce::String(bank + 1) + "/2";
                }
            }
            else
            {
                title = normalTitle_;
                if (normalPageCount_ > 1)
                    pageStr = juce::String(normalPage_ + 1) + "/" + juce::String(normalPageCount_);
            }

            // Header background (slightly lighter than MZ body).
            g.setColour(juce::Colour::fromRGB(38, 43, 52));
            g.fillRect(headerRect);
            g.setColour(juce::Colour::fromRGB(60, 70, 85));
            g.drawLine(0.0f, static_cast<float>(headerRect.getBottom()),
                       static_cast<float>(getWidth()), static_cast<float>(headerRect.getBottom()), 1.0f);

            // Title text and optional page indicator.
            const juce::Colour headerFg = isStepEdit ? juce::Colour::fromRGB(255, 180, 50)
                                        : isMeta     ? juce::Colour::fromRGB(160, 120, 240)
                                                     : juce::Colour::fromRGB(180, 195, 210);
            g.setColour(headerFg);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            const auto textArea = headerRect.reduced(6, 2);
            g.drawText(title, textArea, juce::Justification::centredLeft, true);
            if (pageStr.isNotEmpty())
                g.drawText(pageStr, textArea, juce::Justification::centredRight, true);
        }

        // §26.4.2 — Modal colour families: amber for P-Lock/step-override,
        // violet for meta-modals, none for normal machine params.
        const auto& ctx = processor_.editContext();
        if (ctx.isActiveForEditing())
        {
            const bool fillEdit = processor_.fillActive();
            const juce::Colour editCol = fillEdit
                                             ? juce::Colour::fromRGB(80, 200, 255)
                                             : juce::Colour::fromRGB(255, 180, 50);
            g.setColour(editCol.withAlpha(0.18f));
            g.fillAll();
        }
        else if (band_ != MetaBand::None)
        {
            // Cool/violet tint distinguishes meta-modal bands from P-Lock (amber) at a glance.
            g.setColour(juce::Colour::fromRGB(120, 80, 200).withAlpha(0.07f));
            g.fillAll();
        }
    }

    void ManipulationZone::paintOverChildren(juce::Graphics& g)
    {
        if (band_ != MetaBand::None)
            return;  // meta bands: no CC badges or learn overlays

        const int track = area_.getActiveTrack();
        const bool pulse = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

        juce::ignoreUnused(track);

        for (int i = 0; i < kMZSlots; ++i)
        {
            const int slot = slotOffset_ + i;
            const auto knob = slotKnobBounds(i);

            // Listening overlay: pulsing highlight on the knob being learned.
            if (i == learningSlotIndex_)
            {
                g.setColour(juce::Colour::fromRGB(80, 180, 255).withAlpha(pulse ? 0.35f : 0.15f));
                g.fillRect(knob);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText("wiggle CC...", knob.reduced(2).withTrimmedTop(knob.getHeight() - 12),
                           juce::Justification::centred);
                continue; // skip badge while listening
            }

            // CC mapping badge — anchored to knob top-right corner.
            const auto info = processor_.queryWidgetMapping(slot, i);
            if (!info.exists)
                continue;

            juce::String badge;
            juce::Colour badgeColour;
            switch (info.scope)
            {
                case CCScope::Track:
                    badge = "T" + juce::String(info.trackIndex + 1);
                    badgeColour = juce::Colour::fromRGB(255, 140, 50);
                    break;
                case CCScope::SelectedTrack:
                    badge = "S";
                    badgeColour = juce::Colour::fromRGB(100, 220, 120);
                    break;
                case CCScope::Contextual:
                    badge = "C";
                    badgeColour = juce::Colour::fromRGB(120, 160, 255);
                    break;
                case CCScope::Global:
                    badge = "G";
                    badgeColour = juce::Colour::fromRGB(200, 200, 200);
                    break;
                case CCScope::Crossfader:
                    badge = "X";
                    badgeColour = juce::Colour::fromRGB(180, 96, 208);
                    break;
            }

            const juce::Rectangle<int> badgeArea(knob.getRight() + 2, knob.getCentreY() - 6, 18, 12);
            g.setColour(badgeColour.withAlpha(0.85f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
            g.setColour(juce::Colours::black);
            g.setFont(juce::Font(juce::FontOptions(8.0f)).boldened());
            g.drawText(badge, badgeArea, juce::Justification::centred);
        }

        // Morph A/B chips — anchored to the knob's right edge (A top, B bottom).
        static const juce::Colour kMorphMagenta{ 0xffb060d0 };
        const int nmp = processor_.numParams(track);
        const int activeSec = (slotOffset_ < nmp)
                                  ? processor_.paramSpec(track, slotOffset_).sectionIndex
                                  : -1;
        for (int i = 0; i < kMZSlots; ++i)
        {
            const int slot = slotOffset_ + i;
            if (slot >= nmp) continue;
            if (processor_.paramSpec(track, slot).sectionIndex != activeSec) continue;
            const auto mInfo = processor_.morphWidgetInfo(track, slot);
            if (!mInfo.exists) continue;

            const auto knob = slotKnobBounds(i);
            const int chipW = 10, chipH = 8;
            const int chipX = knob.getRight() + 2;  // just right of knob, inside cell
            if (mInfo.inA)
            {
                const juce::Rectangle<int> aChip(chipX, knob.getY() + 2, chipW, chipH);
                g.setColour(kMorphMagenta.withAlpha(0.85f));
                g.fillRoundedRectangle(aChip.toFloat(), 2.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(7.0f)).boldened());
                g.drawText("A", aChip, juce::Justification::centred);
            }
            if (mInfo.inB)
            {
                const juce::Rectangle<int> bChip(chipX, knob.getBottom() - chipH - 2, chipW, chipH);
                g.setColour(kMorphMagenta.darker(0.3f).withAlpha(0.85f));
                g.fillRoundedRectangle(bChip.toFloat(), 2.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(7.0f)).boldened());
                g.drawText("B", bChip, juce::Justification::centred);
            }
        }
    }

    void ManipulationZone::resized()
    {
        // Geometry is fully delegated to slotCellBounds / slotKnobBounds so
        // paint, paintOverChildren, and layout are always in lockstep.
        for (int i = 0; i < kMZSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const auto cell = slotCellBounds(i);
            const auto knob = slotKnobBounds(i);

            // Name strip: full width minus room for the clear button on the right.
            labels_[si].setBounds(cell.withHeight(kCellNameH).withTrimmedRight(16));
            valueLabels_[si].setBounds(cell.withTop(cell.getBottom() - kCellValueH));
            sliders_[si].setBounds(knob);
            // Clear button lives in the name strip at the cell's right edge so it
            // never overlaps the rotary and is unambiguously tied to this cell.
            clearBtns_[si].setBounds(cell.getRight() - 14, cell.getY(), 14, kCellNameH);

            if (i == 0)
            {
                // Sample picker spans the full cell body (between name and value strips)
                // so it renders as a proper-width button rather than a tiny square.
                samplePickerBtn_.setBounds(juce::Rectangle<int>(
                    cell.getX(), cell.getY() + kCellNameH,
                    cell.getWidth(), cell.getHeight() - kCellNameH - kCellValueH));
            }
        }
    }
}
