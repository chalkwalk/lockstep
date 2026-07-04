#include "ManipulationZone.h"
#include "MetaBand.h"
#include "ParamFormat.h"
#include "../PluginProcessor.h"
#include "../core/OutputDest.h"
#include "../machine/InputSource.h"
#include "KeyboardArea.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    // WS4: the CHANNEL "Out" routing slot id lives in OutputDest.h (kOutSlotId) —
    // shared with the editor's encoder path so both drive Out as a filtered
    // candidate-index rotary (valid bus targets only) rather than the raw
    // encoding. See refreshSliders / onValueChange.

    // Label for an encoded OutputDest value (matches the kOutDestLabels scheme).
    static juce::String outDestLabelText(float enc)
    {
        const auto sel = decodeOutputDest(enc);
        switch (sel.kind)
        {
            case OutputDestKind::Off:    return "Off";
            case OutputDestKind::Master: return "Master";
            case OutputDestKind::Track:  return "Trk" + juce::String(sel.track + 1);
            case OutputDestKind::Aux:    return "Aux" + juce::String(sel.track + 1);
        }
        return "Master";
    }

    // Label for an encoded input_source value (matches kInputSourceLabels).
    static juce::String inputSrcLabelText(float enc)
    {
        const auto sel = decodeInputSource(enc);
        switch (sel.kind)
        {
            case InputSourceKind::None:     return "None";
            case InputSourceKind::External: return "Ext";
            case InputSourceKind::Master:   return "Master";
            case InputSourceKind::Track:    return "T" + juce::String(sel.track + 1);
        }
        return "None";
    }

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
                // Harmony voice slots use an incremental Func-chromatic delta, so
                // seed the reference to the start value — otherwise the first detent
                // (the whole gesture, for a stepped encoder) is swallowed.
                if (band_ == MetaBand::Harmony && (i < kHarmonyVoices || i == 6))
                {
                    lastSlotValue_[static_cast<std::size_t>(i)] =
                        static_cast<float>(sliders_[static_cast<std::size_t>(i)].getValue());
                    lastSlotValid_ = true;
                }
                else
                {
                    lastSlotValid_ = false;  // start each drag with a clean reference point
                }
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

                    // Harmony + Func held over a voice cell: a chromatic (borrowed-
                    // tone) nudge. Use an incremental delta off the slider's own last
                    // position (it doesn't map to the rung axis), like density-master.
                    if (band_ == MetaBand::Harmony && uiState_ && uiState_->funcHeld
                        && (i < kHarmonyVoices || i == 6))
                    {
                        if (lastSlotValid_)
                        {
                            const int semis = static_cast<int>(
                                std::lround(v - lastSlotValue_[static_cast<std::size_t>(i)]));
                            if (semis != 0)
                            {
                                if (i < kHarmonyVoices)
                                    nudgeHarmonyChroma(processor_, *uiState_, i, semis);
                                else  // i == 6 (MOVE): chromatic whole-chord slide.
                                    nudgeHarmonyChromaAll(processor_, *uiState_, semis);
                            }
                        }
                        lastSlotValue_[static_cast<std::size_t>(i)] = v;
                        lastSlotValid_ = true;
                        refreshMetaValueText();
                        if (onHarmonyParamChanged) onHarmonyParamChanged();
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
                    if (band_ == MetaBand::Harmony && onHarmonyParamChanged)
                        onHarmonyParamChanged();
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
                // WS4: the "Out" routing slot runs over a filtered candidate
                // index — map it back to an OutputDest encoding and update the
                // value label live (every selectable index is already valid).
                if (i == outSlotIndex_)
                {
                    const auto cands = processor_.validOutTargets(track);
                    if (!cands.empty())
                    {
                        const int idx = juce::jlimit(
                            0, static_cast<int>(cands.size()) - 1,
                            static_cast<int>(std::lround(v)));
                        const float enc = cands[static_cast<std::size_t>(idx)];
                        processor_.writeParam(track, slot, enc);
                        valueLabels_[static_cast<std::size_t>(i)].setText(
                            outDestLabelText(enc), juce::dontSendNotification);
                    }
                    return;
                }
                // #3: input_source (tap/fork) slot runs over a filtered candidate
                // index too — map back to an InputSource encoding (only safe,
                // non-feedback sources are selectable) and update the live label.
                if (i == inSrcSlotIndex_)
                {
                    const auto cands = processor_.validInputSources(track);
                    if (!cands.empty())
                    {
                        const int idx = juce::jlimit(
                            0, static_cast<int>(cands.size()) - 1,
                            static_cast<int>(std::lround(v)));
                        const float enc = cands[static_cast<std::size_t>(idx)];
                        processor_.writeParam(track, slot, enc);
                        valueLabels_[static_cast<std::size_t>(i)].setText(
                            inputSrcLabelText(enc), juce::dontSendNotification);
                    }
                    return;
                }
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
                const int track = ctx.heldTrackIndex();
                // Part 2 multi-step holds: clear the slot on EVERY held step, not just
                // the primary, so a clear mirrors the multi-step write fan-out.
                for (const int step : ctx.heldSteps())
                {
                    if (band_ == MetaBand::Trig)
                        processor_.clearTrigOverrideField(track, step, i);
                    else if (processor_.fillActive())
                        processor_.clearFillParam(track, step, slotOffset_ + i);
                    else
                        processor_.clearParam(track, step, slotOffset_ + i);
                }
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

        resized();   // the harmony CHORD view re-lays voice slots 0-3 as columns
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
                mv.harmonyVoiceCell = v.harmonyVoiceCell;
                mv.harmonyKnobTop = v.harmonyKnobTop;
                mv.harmonyVoiceOff = v.harmonyVoiceOff;
                mv.harmonyChromatic = v.harmonyChromatic;
                mv.reelPrev = v.reelPrev;
                mv.reelNow = v.reelNow;
                mv.reelNext = v.reelNext;
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
        outSlotIndex_ = -1;  // WS4: recomputed below when the Out slot is visible
        inSrcSlotIndex_ = -1;  // #3: recomputed below when input_source is visible
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

            // WS4: the CHANNEL "Out" routing slot — a filtered candidate rotary.
            // It steps only through {Off, Master, currently-valid buses}, so the
            // performer never jogs through synths / MIDI-out / cycles, and the
            // value label tracks the live selection. Routing reads the base value
            // only (per-step Out P-locks are ignored), so drive it off base.
            if (meta.id == kOutSlotId)
            {
                outSlotIndex_ = i;
                const auto cands = processor_.validOutTargets(track);
                const float baseEnc = processor_.baseParamValue(track, slot);
                int curIdx = 0;
                for (std::size_t c = 0; c < cands.size(); ++c)
                    if (std::lround(cands[c]) == std::lround(baseEnc))
                    {
                        curIdx = static_cast<int>(c);
                        break;
                    }

                MetaRotary::View ov;
                ov.rangeLo = 0.0;
                ov.rangeHi = std::max<double>(1.0, static_cast<double>(cands.size()) - 1.0);
                ov.interval = 1.0;
                ov.ringMode = RingMode::Dot;
                ov.enabled = true;
                ov.alpha = 1.0f;
                ov.value = static_cast<double>(curIdx);
                sliders_[si].applyView(ov);

                labels_[si].setText(meta.label, juce::dontSendNotification);
                valueLabels_[si].setText(outDestLabelText(baseEnc), juce::dontSendNotification);
                clearBtns_[si].setEnabled(false);
                clearBtns_[si].setAlpha(0.3f);
                if (i == 0) samplePickerBtn_.setVisible(false);
                continue;
            }

            // #3: the input_source (tap/fork) slot — a filtered candidate rotary,
            // exactly like Out. It steps only through feedback-safe sources (None,
            // Ext, Master-when-safe, non-cyclic taps), so the performer can never
            // jog onto a source that would feed back. Drive off the base value.
            if (meta.id == kInputSourceSlotId)
            {
                inSrcSlotIndex_ = i;
                const auto cands = processor_.validInputSources(track);
                const float baseEnc = processor_.baseParamValue(track, slot);
                int curIdx = 0;
                for (std::size_t c = 0; c < cands.size(); ++c)
                    if (std::lround(cands[c]) == std::lround(baseEnc))
                    {
                        curIdx = static_cast<int>(c);
                        break;
                    }

                MetaRotary::View iv;
                iv.rangeLo = 0.0;
                iv.rangeHi = std::max<double>(1.0, static_cast<double>(cands.size()) - 1.0);
                iv.interval = 1.0;
                iv.ringMode = RingMode::Dot;
                iv.enabled = true;
                iv.alpha = 1.0f;
                iv.value = static_cast<double>(curIdx);
                sliders_[si].applyView(iv);

                labels_[si].setText(meta.label, juce::dontSendNotification);
                valueLabels_[si].setText(inputSrcLabelText(baseEnc), juce::dontSendNotification);
                clearBtns_[si].setEnabled(false);
                clearBtns_[si].setAlpha(0.3f);
                if (i == 0) samplePickerBtn_.setVisible(false);
                continue;
            }

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
        // 9.18: only offer entries the track's machine can actually play — a PCM
        // player sees File + volatile captures, a StreamMachine sees Stream entries.
        // The item id still encodes the true pool index (+1), so selection writes the
        // correct index even though the list is filtered.
        int shown = 0;
        for (int i = 0; i < poolSize; ++i)
        {
            if (!processor_.sampleAcceptedByTrack(track, i)) continue;
            // C4: a per-group ordinal (FILE 1, STREAM 1, REC 1, ...) instead of the
            // raw pool index, which jumps when the reserved volatile slots re-seed at
            // the pool front on reload. The item id still encodes the true pool index
            // (+1) so selection writes the correct entry.
            menu.addItem(i + 1, juce::String(processor_.samplePool().groupOrdinal(i))
                                    + "  " + processor_.sampleShortName(i));
            ++shown;
        }
        if (shown == 0)
        {
            // A disabled section header, not addItem(0) — JUCE asserts on item id 0.
            menu.addSectionHeader(poolSize == 0 ? "(pool is empty)"
                                                : "(no matching samples)");
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

    juce::Rectangle<int> ManipulationZone::harmonyVoiceColBounds(int v) const
    {
        // The four voice slots span the left two of the four columns; lay them out
        // as four equal full-height columns there (the chord reel reads across).
        static constexpr int kCols = kMZSlots / 2;
        const auto bounds = getLocalBounds().reduced(4).withTrimmedTop(kHeaderH);
        const int baseW = bounds.getWidth() / kCols;
        const int narrowW = baseW * 7 / 8;
        const int regionW = 2 * narrowW;                 // columns 0-1
        const int colW = regionW / kHarmonyVoices;
        const int x = bounds.getX() + std::clamp(v, 0, kHarmonyVoices - 1) * colW;
        return juce::Rectangle<int>(x, bounds.getY(), colW, bounds.getHeight()).reduced(2, 2);
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

        // P6: when a normal (machine-param) page is actually showing a track-level
        // DSP block (FLTR/AMP/CHANNEL+ENV/FX inserts live at slot >= numParams),
        // tint the header cyan (the Track scope colour, kScopeTrack) so a track
        // page is never mistaken for a machine page — the FLTR-vs-FLTR confusion.
        const int scopeTrk = area_.getActiveTrack();
        const bool trackScopePage =
            scopeTrk >= 0 && band_ == MetaBand::None
            && !processor_.editContext().isActiveForEditing()
            && slotOffset_ >= processor_.numParams(scopeTrk);
        const juce::Colour kTrackScopeCol{ 0xFF30A0C0u };  // theme::kScopeTrack

        // Part 3: when the current page is a track FX-insert whose slot is
        // bypassed, wash it amber + tag BYP so a bypassed effect never looks like
        // an active one. Params stay interactive (dim, not disabled).
        const int fxSlot =
            (scopeTrk >= 0 && band_ == MetaBand::None
             && !processor_.editContext().isActiveForEditing())
                ? processor_.insertSlotForParamOffset(scopeTrk, slotOffset_)
                : -1;
        const bool bypassedFxPage =
            fxSlot >= 0 && processor_.trackInsertBypass(scopeTrk, fxSlot);
        const juce::Colour kBypassCol{ 0xFFFFB432u };  // amber (aligns with picker)
        const juce::Colour kFuncCol{ 0xFFD07820u };    // theme::kScopeFunc

        // Origin scope of the current page → header colour + (for a Func-origin
        // page) a Func border. The MZ marks the scope of the page it is *showing*,
        // reached via a scope+section selection — not whatever modifier is
        // momentarily held. Scoped section-secondary meta bands map to their scope
        // (COND=Func+TRIG, DIVIDER=Track+TRIG, PHRASE LEN=Phrase+TRIG, MASTER FX/
        // GLOBAL=Song+FX); generator/overlay/step pages keep their own identity.
        struct BandScopeInfo { juce::Colour colour; bool funcOrigin; };
        const auto bandScopeInfo = [](MetaBand b) -> BandScopeInfo {
            switch (b)
            {
                case MetaBand::Cond:                                                    // Func+TRIG
                case MetaBand::Transport: return { juce::Colour(0xFFD07820u), true };   // Func
                case MetaBand::Divider:   return { juce::Colour(0xFF30A0C0u), false };  // Track
                case MetaBand::PhraseLen: return { juce::Colour(0xFF7050C8u), false };  // Phrase
                case MetaBand::Global:                                                  // Song+FX master params
                case MetaBand::MasterFx:  return { juce::Colour(0xFFD8B020u), false };  // Song
                case MetaBand::Trig:      return { juce::Colour::fromRGB(180, 195, 210), false }; // Machine
                default:                  return { juce::Colour::fromRGB(160, 120, 240), false }; // overlay → violet
            }
        };
        const bool isMetaPage = (band_ != MetaBand::None);
        BandScopeInfo bs = bandScopeInfo(band_);

        // SWING / TIME / KEY target a scope chosen at runtime (not a fixed origin),
        // so tint them by that active target scope. The scope-int meanings differ:
        // swingScopeFor → 1 Song / 2 Scene / 3 Track; timeScopeFor → 1 Set / 2 Song
        // / 3 Scene (KEY shares timeScopeFor). Set/global has no scope hue → slate.
        if (uiState_ != nullptr)
        {
            const juce::Colour kSong { 0xFFD8B020u };  // kScopeSong (gold)
            const juce::Colour kScene{ 0xFF20A060u };  // kScopeScene (green)
            const juce::Colour kTrackC{ 0xFF30A0C0u }; // kScopeTrack (cyan)
            const juce::Colour kSet  { 0xFF8898A8u };  // kScopeStep (neutral slate)
            if (band_ == MetaBand::Swing)
            {
                switch (swingScopeFor(*uiState_))
                {
                    case 1: bs.colour = kSong;   break;
                    case 2: bs.colour = kScene;  break;
                    case 3: bs.colour = kTrackC; break;
                    default: break;  // 0 = none → keep violet
                }
            }
            else if (band_ == MetaBand::Time || band_ == MetaBand::Key)
            {
                switch (timeScopeFor(*uiState_))
                {
                    case 1: bs.colour = kSet;   break;
                    case 2: bs.colour = kSong;  break;
                    case 3: bs.colour = kScene; break;
                    default: break;
                }
            }
        }
        const bool funcOriginPage = isMetaPage && bs.funcOrigin;

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
                // Banner always names the scope the params belong to. Colour is a
                // learned shorthand; the word is the durable signal (DESIGN §6.1.1).
                const char* scopeWord = trackScopePage ? "TRACK" : "MACHINE";
                title = juce::String(scopeWord) + "  " + normalTitle_;
                if (bypassedFxPage)
                    title += " (BYP)";
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
            const juce::Colour headerFg = isStepEdit    ? juce::Colour::fromRGB(255, 180, 50)
                                        : isMeta        ? bs.colour
                                        : bypassedFxPage ? kBypassCol
                                        : trackScopePage ? kTrackScopeCol
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
        else if (isMetaPage)
        {
            // Meta bands wash in their origin-scope colour (scoped section-
            // secondaries) or violet (generator/overlay), distinguishing them from
            // P-Lock (amber) at a glance.
            g.setColour(bs.colour.withAlpha(0.07f));
            g.fillAll();
        }
        else if (bypassedFxPage)
        {
            // Part 3: amber wash marks a bypassed FX page (intentionally-off, not
            // a disabled/greyed UI element — the params stay editable).
            g.setColour(kBypassCol.withAlpha(0.10f));
            g.fillAll();
        }
        else if (trackScopePage)
        {
            // P6: faint cyan wash marks a track-level DSP page (Track scope colour).
            g.setColour(kTrackScopeCol.withAlpha(0.06f));
            g.fillAll();
        }

        // Func parallel stack (DESIGN §6.1.1): a Func-coloured border wraps the MZ
        // when the *current page* is Func-origin (e.g. the COND band via Func+TRIG),
        // mirroring the section-key marker — not merely when Func is held.
        if (funcOriginPage)
        {
            g.setColour(kFuncCol.withAlpha(0.9f));
            g.drawRect(getLocalBounds(), 2);
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

            // Harmony CHORD view: voice slots 0-3 become four full-height columns
            // (the slider fills the column so its reel + centred knob span it); the
            // header label sits at the top, the value strip and clear button hide.
            if (band_ == MetaBand::Harmony && i < kHarmonyVoices)
            {
                const auto col = harmonyVoiceColBounds(i);
                labels_[si].setBounds(col.withHeight(kCellNameH));
                valueLabels_[si].setBounds({});
                sliders_[si].setBounds(col.withTrimmedTop(kCellNameH));
                clearBtns_[si].setBounds({});
                continue;
            }

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
