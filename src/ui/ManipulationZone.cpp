#include "ManipulationZone.h"
#include "ParamFormat.h"
#include "../ParameterIDs.h"
#include "../PluginProcessor.h"
#include "../core/Swing.h"
#include "../core/TrigCondition.h"
#include "KeyboardArea.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    // Fraction of the inner band height used for each row.  Two rows at this
    // fraction overlap vertically; the stagger keeps their knobs clear.
    static constexpr float kRowHeightFrac = 0.62f;
    static constexpr int   kCellNameH     = 16;   // name label strip height
    static constexpr int   kCellValueH    = 15;   // value label strip height

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

            sliders_[si].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sliders_[si].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            sliders_[si].setWantsKeyboardFocus(false);
            sliders_[si].onDragStart = [this, i]
            {
                if (metaSection_ < 0)  // machine params only
                    processor_.editContext().setActiveSlot(slotOffset_ + i);
            };
            sliders_[si].onValueChange = [this, i]
            {
                if (updatingFromTimer_) return;
                const float v = static_cast<float>(
                    sliders_[static_cast<std::size_t>(i)].getValue());
                // Scope-held swing surface takes priority over normal machine params
                // but is overridden by any explicit meta section.
                if (metaSection_ < 0 && swingScope_ != 0) { writeSwingField(i, v); return; }
                switch (metaSection_)
                {
                    case 0:  writeCondField(i, v);     break;
                    case 1:  writeTrigField(i, v);     break;
                    case 3:  writeDivField(i, v);      break;
                    case 4:  writePhraseLenField(i, v); break;
                    case 5:  writeGlobalField(i, v);   break;
                    default:
                    {
                        // Route to fill P-Lock layer when fill is held + step is held.
                        if (processor_.fillActive()) {
                            processor_.writeFillParam(area_.getActiveTrack(), slotOffset_ + i, v);
                            break;
                        }
                        const int track = area_.getActiveTrack();
                        const int slot  = slotOffset_ + i;
                        // If a pole qualifier is active (Morph+^/v), write directly
                        // to that pole irrespective of fader position or existing data.
                        if (morphQualifier_ == 1)
                        {
                            processor_.writeMorphPole(track, slot, v, 0);
                            break;
                        }
                        if (morphQualifier_ == 2)
                        {
                            processor_.writeMorphPole(track, slot, v, 1);
                            break;
                        }
                        // Bare Morph held: write overlay at fader split (matches encoder §17.6).
                        if (morphHeld_)
                        {
                            const float cur = processor_.morphEffectiveValue(track, slot);
                            processor_.writeMorph(track, slot, v - cur, processor_.morphFader());
                            break;
                        }
                        // Auto-morph-aware: if morph data exists, write into the
                        // overlay rather than kit base (mirrors encoder delta logic).
                        const auto mInfo = processor_.morphWidgetInfo(track, slot);
                        if (mInfo.exists)
                        {
                            if (mInfo.inA && !mInfo.inB)
                                processor_.writeMorphPole(track, slot, v, 0);
                            else if (!mInfo.inA && mInfo.inB)
                                processor_.writeMorphPole(track, slot, v, 1);
                            else
                            {
                                // Both set: delta from current blend → proportional split.
                                const float cur = processor_.morphEffectiveValue(track, slot);
                                processor_.writeMorph(track, slot, v - cur, processor_.morphFader());
                            }
                        }
                        else
                            processor_.writeParam(track, slot, v);
                        break;
                    }
                }
            };
            sliders_[si].addMouseListener(static_cast<juce::MouseListener*>(this), false);
            addAndMakeVisible(sliders_[si]);

            clearBtns_[si].setButtonText("x");
            clearBtns_[si].setWantsKeyboardFocus(false);
            clearBtns_[si].onClick = [this, i]
            {
                auto& ctx = processor_.editContext();
                if (!ctx.isActiveForEditing()) return;
                if (metaSection_ == 1)
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
        samplePickerBtn_.onClick = [this]
        {
            const int track = area_.getActiveTrack();
            if (track < 0 || track >= static_cast<int>(kNumTracks)) return;
            showSamplePicker(slotOffset_);
        };
        addChildComponent(samplePickerBtn_);

        processor_.setMZSlots(slotOffset_);
        startTimerHz(30);
    }

    ManipulationZone::~ManipulationZone()
    {
        for (auto& s : sliders_)
            s.removeMouseListener(static_cast<juce::MouseListener*>(this));
    }

    void ManipulationZone::setSlotOffset(int offset)
    {
        slotOffset_ = offset;
        processor_.setMZSlots(slotOffset_);
        setMetaSection(-1);   // restores widget visibility, then refreshSliders() below
        refreshSliders();
    }

    void ManipulationZone::setMetaSection(int metaSection)
    {
        metaSection_ = metaSection;

        // Reserved meta slots (2–4) can't be selected (guarded in SectionBar),
        // so in practice sliders are always shown. Hide the sample picker —
        // refreshSliders() will restore it if still needed.
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
        if (!e.mods.isRightButtonDown() || metaSection_ >= 0)
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
        const int slot  = slotOffset_ + slotIndex;
        const auto info = processor_.queryWidgetMapping(slot, slotIndex);

        juce::PopupMenu menu;

        if (info.exists)
        {
            juce::String header = "CC " + juce::String(info.ccNumber) + " mapped (";
            switch (info.scope)
            {
                case CCScope::Track:         header += "T" + juce::String(info.trackIndex + 1); break;
                case CCScope::SelectedTrack: header += "S";  break;
                case CCScope::Contextual:    header += "C";  break;
                case CCScope::Global:        header += "G";  break;
                case CCScope::Crossfader:    header += "X";  break;
            }
            header += ")";
            menu.addSectionHeader(header);
            menu.addItem(1, "Clear mapping");
        }
        else
        {
            menu.addSectionHeader("Map this control via MIDI Learn:");
            menu.addItem(1, juce::String(u8"Fixed — track ") + juce::String(track + 1)
                            + ", slot " + juce::String(slot));
            menu.addItem(2, "Selected track (follows focus)");
            menu.addItem(3, "Contextual (this display position)");
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(sliders_[static_cast<std::size_t>(slotIndex)]),
            [this, info, track, slot, slotIndex](int result)
            {
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
                }
            });
    }

    void ManipulationZone::timerCallback()
    {
        // Detect when a pending learn completes (audio thread cleared the flag).
        if (learningSlotIndex_ >= 0 && !processor_.isLearning())
            learningSlotIndex_ = -1;

        refreshSliders();

        // The CC-learn overlay pulses at ~3 Hz; repaint to drive that animation.
        // Outside of learn mode the child sliders/labels repaint themselves when
        // their values change, so no explicit repaint() is needed here.
        if (learningSlotIndex_ >= 0)
            repaint();
    }

    void ManipulationZone::refreshSliders()
    {
        // Scope-held swing surface: shown when a scope key is held and no meta is open.
        if (metaSection_ < 0 && swingScope_ != 0) { refreshSwingSliders(); return; }
        switch (metaSection_)
        {
            case 0: refreshCondSliders();      return;
            case 1: refreshTrigSliders();      return;
            case 3: refreshDivSliders();       return;
            case 4: refreshPhraseLenSliders(); return;
            case 5: refreshGlobalSliders();    return;
            default: break;
        }

        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t   = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        const int numMachineParams = processor_.numParams(track);

        // Determine the active section from the first visible slot so we can
        // blank trailing cells that belong to a different section.
        const int activeSectionIndex = (slotOffset_ < numMachineParams)
            ? processor_.paramSpec(track, slotOffset_).sectionIndex
            : -1;

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si   = static_cast<std::size_t>(i);
            const int  slot = slotOffset_ + i;

            // Slot beyond this machine's schema, or belonging to a different
            // section than the page anchor — blank out the cell.
            const bool outOfSection = (slot < numMachineParams)
                && (processor_.paramSpec(track, slot).sectionIndex != activeSectionIndex);
            if (slot >= numMachineParams || outOfSection)
            {
                sliders_[si].setEnabled(false);
                sliders_[si].setAlpha(0.0f);
                labels_[si].setText({}, juce::dontSendNotification);
                valueLabels_[si].setText({}, juce::dontSendNotification);
                clearBtns_[si].setEnabled(false);
                clearBtns_[si].setAlpha(0.0f);
                continue;
            }

            const auto meta = processor_.paramSpec(track, slot);

            // Sample slot: replace rotary with a name button + picker popup.
            const bool isSampleSlot = (meta.id == "sample_id"
                                    || meta.id == "slicer_sample_id");

            sliders_[si].setEnabled(!isSampleSlot);
            sliders_[si].setAlpha(isSampleSlot ? 0.0f : 1.0f);
            // Guard: JUCE Slider asserts on a zero-extent range (min == max).
            const double lo = static_cast<double>(meta.minValue);
            const double hi = static_cast<double>(meta.maxValue);
            sliders_[si].setRange(lo, (hi > lo ? hi : lo + 1.0),
                                  meta.isStepped ? 1.0 : 0.0);
            // MHZ.5.2: non-linear encoder curve for time params (attack/decay/release).
            sliders_[si].setSkewFactor(meta.isStepped ? 1.0 : static_cast<double>(meta.skew));
            // MHZ.2.4: double-click resets to parameter default.
            sliders_[si].setDoubleClickReturnValue(true,
                                                   static_cast<double>(meta.defaultValue));

            // Show the morph-appropriate value when morph data exists:
            //   qualifier=1 → raw A endpoint (pole preview while editing A)
            //   qualifier=2 → raw B endpoint
            //   qualifier=0 → fader-blended value (animates with crossfader)
            //   no morph data → kit base (existing behaviour)
            const auto  mInfo = processor_.morphWidgetInfo(track, slot);
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

            const bool stepHeld  = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
            const int  heldStep  = ctx.heldStepIndex();
            const bool fillHeld  = processor_.fillActive();
            const bool fillEdit  = stepHeld && fillHeld && heldStep >= 0;
            bool hasLock = false;

            if (stepHeld && heldStep >= 0)
            {
                const auto& s = t.steps[static_cast<std::size_t>(heldStep)];
                // Resolved fill view: FillOverride → Override → Base.
                value   = s.overrides.get(slot, value);
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

            sliders_[si].setValue(static_cast<double>(value), juce::dontSendNotification);

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

            const juce::String nameText = meta.label.isEmpty()
                                              ? juce::String(slot)
                                              : meta.label;
            labels_[si].setText(nameText, juce::dontSendNotification);

            clearBtns_[si].setEnabled(hasLock);
            clearBtns_[si].setAlpha(hasLock ? 1.0f : 0.3f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::refreshCondSliders()
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t   = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  heldStep = ctx.heldStepIndex();

        // Override-ELSE-Base for conditions: step cond if held and non-trivial.
        const TrigCondition& baseCond  = t.baseCond;
        const TrigCondition* stepCond  = (stepHeld && heldStep >= 0)
            ? &t.steps[static_cast<std::size_t>(heldStep)].condition
            : nullptr;
        const TrigCondition& display   = (stepCond && !stepCond->isTrivial())
            ? *stepCond : baseCond;

        // Slot 3 is always Prev-dep (fillRule has been removed from TrigCondition;
        // fill trig state is set via Hold-Fill + step-tap gesture).
        struct CondFieldDef { const char* label; float lo; float hi; bool enabled; };
        const std::array<CondFieldDef, kNumSlots> kDefs = {{
            { "Prob",  1.0f, 100.0f, true  },
            { "m Num", 1.0f,   8.0f, true  },
            { "m Den", 1.0f,   8.0f, true  },
            { "Prev",  0.0f,   2.0f, true  },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
        }};

        const std::array<float, kNumSlots> vals = {
            static_cast<float>(display.probabilityPercent),
            static_cast<float>(display.iterNumerator),
            static_cast<float>(display.iterDenominator),
            static_cast<float>(display.prevDependency),
            0.0f, 0.0f, 0.0f, 0.0f,
        };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi), 1.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);
            sliders_[si].setEnabled(kDefs[si].enabled);
            sliders_[si].setAlpha(kDefs[si].enabled ? 1.0f : 0.0f);

            juce::String valueText;
            if (i == 0)
                valueText = juce::String(static_cast<int>(vals[si])) + "%";
            else if (i == 3)
            {
                const int pd = static_cast<int>(vals[si]);
                valueText = (pd == 0) ? "off" : (pd == 1 ? "fired" : "!fired");
            }
            else if (kDefs[si].enabled)
                valueText = juce::String(static_cast<int>(vals[si]));

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);

            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writeCondField(int field, float value)
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx  = processor_.editContext();
        const bool held  = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  step  = ctx.heldStepIndex();

        auto& t = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        TrigCondition& target = (held && step >= 0)
            ? t.steps[static_cast<std::size_t>(step)].condition
            : t.baseCond;

        if (held && step >= 0)
            processor_.editContext().markParamWritten();

        const auto u8 = [](float v)
        {
            return static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(std::round(v)), 0, 255));
        };

        switch (field)
        {
            case 0: target.probabilityPercent = u8(value); break;
            case 1: target.iterNumerator      = u8(value); break;
            case 2: target.iterDenominator    = u8(value); break;
            case 3: target.prevDependency     = u8(value); break;
            default: break;
        }
    }

    // -------------------------------------------------------------------------
    // TRIG meta section (masterSection == 1): sequencer-scope note/vel/gate

    void ManipulationZone::refreshTrigSliders()
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t   = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  heldStep = ctx.heldStepIndex();
        const bool stepValid = stepHeld && heldStep >= 0 && heldStep < kMaxStepsPerTrack;
        const auto* step = stepValid
            ? &t.steps[static_cast<std::size_t>(heldStep)]
            : nullptr;
        const auto* trig = step ? &step->trigOverride : nullptr;

        // Override-ELSE-Base per field.
        const bool  hasNote  = trig && trig->noteCount > 0;
        const int   note     = hasNote ? trig->notes[0] : t.trigDefaults.note;
        const int   velocity = (trig && trig->hasVelocity) ? trig->velocity : t.trigDefaults.velocity;
        const MusicalGate gateVal = (trig && trig->hasGate) ? trig->gateValue : t.trigDefaults.gateValue;
        const bool  hasVel   = trig && trig->hasVelocity;
        const bool  hasGate  = trig && trig->hasGate;
        // Extra notes beyond the primary (for chord display).
        const int   chordExtra = hasNote ? trig->noteCount - 1 : 0;

        // MicroOffset: per-step only; reads from the step when held, else 0.
        const float microVal  = step ? step->microOffset : 0.0f;
        const bool  hasMicro  = stepValid;

        const float noteSel = static_cast<float>(
            t.noteSelection == NoteSelection::BottomBias ? 1 : 0);

        struct TrigFieldDef { const char* label; float lo; float hi; bool stepped; bool active; };
        const std::array<TrigFieldDef, kNumSlots> kDefs = {{
            { "Note",  0.0f,  127.0f,                                  true,  true     },
            { "Vel",   1.0f,  127.0f,                                  true,  true     },
            { "Gate",  0.0f,  static_cast<float>(kMusicalGateCount-1), true,  true     },
            { "Bias",  0.0f,    1.0f,                                  true,  true     },
            { "Micro", -0.5f,  0.5f,                                   false, stepValid},
            { "",      0.0f,   1.0f,                                   false, false    },
            { "",      0.0f,   1.0f,                                   false, false    },
            { "",      0.0f,   1.0f,                                   false, false    },
        }};
        const std::array<float, kNumSlots> vals  = {
            static_cast<float>(note),
            static_cast<float>(velocity),
            static_cast<float>(static_cast<uint8_t>(gateVal)),
            noteSel,
            microVal,
            0.0f, 0.0f, 0.0f
        };
        // Notes are not P-locks (not set via the param area) — no lock indicator or clear button.
        const std::array<bool, kNumSlots> locks = { false, hasVel, hasGate, false,
                                                    hasMicro, false, false, false };
        static constexpr const char* kBiasLabels[] = { "TOP", "BOT" };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi),
                                  kDefs[si].stepped ? 1.0 : 0.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);
            sliders_[si].setEnabled(kDefs[si].active);
            sliders_[si].setAlpha(kDefs[si].active ? 1.0f : 0.0f);

            juce::String valueText;
            if (kDefs[si].active)
            {
                if (i == 2)
                {
                    const auto gidx = static_cast<int>(vals[si]);
                    if (gidx >= 0 && gidx < kMusicalGateCount)
                        valueText = juce::String(kMusicalGateLabels[gidx]);
                }
                else if (i == 3)
                {
                    valueText = kBiasLabels[static_cast<int>(vals[si])];
                }
                else if (i == 4)
                {
                    // MicroTime: display as percentage of step (e.g. "+25%", "-50%").
                    const int pct = static_cast<int>(std::round(vals[si] * 100.0f));
                    valueText = (pct >= 0 ? "+" : "") + juce::String(pct) + "%";
                }
                else
                {
                    valueText = juce::String(static_cast<int>(vals[si]));
                }
                if (i == 0 && chordExtra > 0)
                    valueText += "+" + juce::String(chordExtra);
                if (locks[si])
                    valueText += " *";
            }

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);
            clearBtns_[si].setEnabled(kDefs[si].active && locks[si]);
            clearBtns_[si].setAlpha(locks[si] ? 1.0f : 0.3f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writeTrigField(int field, float value)
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const bool held = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  step = ctx.heldStepIndex();

        auto& t = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        if (held && step >= 0)
        {
            auto& stepRef = t.steps[static_cast<std::size_t>(step)];
            auto& trig    = stepRef.trigOverride;
            switch (field)
            {
                case 0: // MZ note edit: always sets primary note; preserves chord size.
                        if (trig.noteCount == 0) trig.noteCount = 1;
                        trig.notes[0]    = std::clamp(static_cast<int>(value), 0, 127);   break;
                case 1: trig.hasVelocity = true;
                        trig.velocity    = std::clamp(static_cast<int>(value), 1, 127);   break;
                case 2: trig.hasGate  = true;
                        trig.gateValue  = static_cast<MusicalGate>(
                            std::clamp(static_cast<int>(value), 0, kMusicalGateCount - 1)); break;
                case 4: stepRef.microOffset = std::clamp(value, -0.5f, 0.5f);              break;
                default: break;
            }
            processor_.editContext().markParamWritten();
        }
        else
        {
            switch (field)
            {
                case 0: t.trigDefaults.note     = std::clamp(static_cast<int>(value), 0, 127);  break;
                case 1: t.trigDefaults.velocity = std::clamp(static_cast<int>(value), 1, 127);  break;
                case 2: t.trigDefaults.gateValue = static_cast<MusicalGate>(
                            std::clamp(static_cast<int>(value), 0, kMusicalGateCount - 1)); break;
                case 3: t.noteSelection = (value >= 0.5f)
                            ? NoteSelection::BottomBias
                            : NoteSelection::TopBias;
                        break;
                default: break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // DIV meta section (metaSection == 3) — Track scope, TRIG key.
    // Shows the kit divider for the active track (Kit-owned, shared across phrases).

    void ManipulationZone::refreshDivSliders()
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const float divider = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackDivider(track))->load();

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const bool active = (i == 0);
            sliders_[si].setRange(active ? 1.0 : 0.0, active ? 16.0 : 1.0, active ? 1.0 : 0.0);
            sliders_[si].setValue(active ? static_cast<double>(divider) : 0.0,
                                  juce::dontSendNotification);
            sliders_[si].setEnabled(active);
            sliders_[si].setAlpha(active ? 1.0f : 0.0f);
            labels_[si].setText(active ? "Divider" : juce::String{}, juce::dontSendNotification);
            valueLabels_[si].setText(active ? juce::String(static_cast<int>(divider)) : juce::String{},
                                     juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writeDivField(int field, float value)
    {
        if (field != 0) return;
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;

        const auto& ctx = processor_.editContext();
        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
            processor_.editContext().markParamWritten();

        auto* p = processor_.apvts().getParameter(ParamIDs::trackDivider(track));
        if (p) p->setValueNotifyingHost(std::clamp((value - 1.0f) / 15.0f, 0.0f, 1.0f));
    }

    // -------------------------------------------------------------------------
    // PHRASELEN meta section (metaSection == 4) — Phrase scope, TRIG key.
    // Shows the active phrase's length (Phrase-owned, per active phrase).

    void ManipulationZone::refreshPhraseLenSliders()
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const float length = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackLength(track))->load();

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const bool active = (i == 0);
            sliders_[si].setRange(active ? 1.0 : 0.0, active ? 64.0 : 1.0, active ? 1.0 : 0.0);
            sliders_[si].setValue(active ? static_cast<double>(length) : 0.0,
                                  juce::dontSendNotification);
            sliders_[si].setEnabled(active);
            sliders_[si].setAlpha(active ? 1.0f : 0.0f);
            labels_[si].setText(active ? "Length" : juce::String{}, juce::dontSendNotification);
            valueLabels_[si].setText(active ? juce::String(static_cast<int>(length)) : juce::String{},
                                     juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writePhraseLenField(int field, float value)
    {
        if (field != 0) return;
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks)) return;

        const auto& ctx = processor_.editContext();
        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
            processor_.editContext().markParamWritten();

        auto* p = processor_.apvts().getParameter(ParamIDs::trackLength(track));
        if (p) p->setValueNotifyingHost(std::clamp((value - 1.0f) / 63.0f, 0.0f, 1.0f));
    }

    // -------------------------------------------------------------------------
    // Scope-held swing surface — shown when a scope key is held and no meta is open.
    // swingScope_: 1=song-all, 2=scene-all delta, 3=song-track delta.

    void ManipulationZone::setSwingScope(int scope)
    {
        if (swingScope_ == scope) return;
        swingScope_ = scope;
        refreshSliders();
    }

    void ManipulationZone::refreshSwingSliders()
    {
        const int track = area_.getActiveTrack();

        // Determine which level is being edited and what values to show.
        const float swingShown = [&]() -> float {
            switch (swingScope_)
            {
                case 2:  return processor_.swingSceneAllShown();
                case 3:  return (track >= 0) ? processor_.swingSongTrackShown(track) : 0.0f;
                default: return processor_.swingSongAll();  // scope 1 = song-all
            }
        }();
        const float effSw = (track >= 0) ? processor_.swingEffective(track) : 0.0f;

        const char* swingLabel = (swingScope_ == 2) ? "SwScn"
                               : (swingScope_ == 3) ? "SwTrk"
                                                    : "Swing";
        const bool isRoot = (swingScope_ == 1);

        auto fmtSwing = [](float v, bool delta) -> juce::String {
            const int pct = static_cast<int>(std::round(v * 100.0f));
            juce::String s = (pct >= 0 ? "+" : "") + juce::String(pct) + "%";
            if (delta) s += " (D)";
            return s;
        };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const bool isSwing = (i == 0);
            const bool isEffct = (i == 1);
            const bool active  = isSwing || isEffct;
            sliders_[si].setRange(-0.5, 0.5, 0.0);
            sliders_[si].setValue(isSwing ? static_cast<double>(swingShown)
                                          : (isEffct ? static_cast<double>(effSw) : 0.0),
                                  juce::dontSendNotification);
            sliders_[si].setEnabled(isSwing);          // Effct is display-only
            sliders_[si].setAlpha(active ? 1.0f : 0.0f);

            juce::String label, value;
            if (isSwing)  { label = swingLabel; value = fmtSwing(swingShown, !isRoot); }
            if (isEffct)  { label = "Effct";    value = fmtSwing(effSw, false); }

            labels_[si].setText(label, juce::dontSendNotification);
            valueLabels_[si].setText(value, juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writeSwingField(int field, float value)
    {
        if (field != 0) return;  // only slot 0 (Swing) is writable; slot 1 is Effct readout
        const int track = area_.getActiveTrack();
        switch (swingScope_)
        {
            case 2:  processor_.setSwingSceneAll(value); break;
            case 3:  if (track >= 0) processor_.setSwingSongTrack(track, value); break;
            default: processor_.setSwingSongAll(value); break;  // scope 1 = song-all
        }
    }

    // -------------------------------------------------------------------------
    // GLOBAL meta section (masterSection == 5)

    void ManipulationZone::refreshGlobalSliders()
    {
        const float gain = processor_.apvts()
            .getRawParameterValue(ParamIDs::outputGain)->load();
        const float sync = processor_.apvts()
            .getRawParameterValue(ParamIDs::syncMode)->load();
        const float chan = processor_.apvts()
            .getRawParameterValue(ParamIDs::channelMode)->load();

        struct GlobalFieldDef { const char* label; float lo; float hi; bool stepped; bool enabled; };
        static constexpr std::array<GlobalFieldDef, kNumSlots> kDefs = {{
            { "Gain",  -60.0f,  6.0f, false, true  },
            { "Sync",    0.0f,  1.0f, true,  true  },
            { "Chan",    0.0f,  1.0f, true,  true  },
            { "",        0.0f,  1.0f, false, false },  // Swing moved to Song/Scene state
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
        }};
        const std::array<float, kNumSlots> vals = { gain, sync, chan, 0.0f,
                                                    0.0f, 0.0f, 0.0f, 0.0f  };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi),
                                  kDefs[si].stepped ? 1.0 : 0.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);
            sliders_[si].setEnabled(kDefs[si].enabled);
            sliders_[si].setAlpha(kDefs[si].enabled ? 1.0f : 0.2f);

            juce::String valueText;
            if (i == 0)
            {
                const float g = vals[si];
                valueText = (g >= 0.0f ? "+" : "") + juce::String(g, 1) + " dB";
            }
            else if (i == 1)
                valueText = (static_cast<int>(vals[si]) == 0) ? "Locked" : "Auto";
            else if (i == 2)
                valueText = (static_cast<int>(vals[si]) == 0) ? "Omni" : "Per-Trk";
            else
                valueText = juce::String(u8"—");  // em-dash

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
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
            [this, track, absoluteSlot](int result)
            {
                if (result == 1000)
                {
                    if (onOpenPoolManager) onOpenPoolManager();
                    return;
                }
                if (result < 1) return;
                processor_.writeParam(track, absoluteSlot, static_cast<float>(result - 1));
            });
    }

    void ManipulationZone::writeGlobalField(int field, float value)
    {
        const auto writeApvts = [this](const juce::String& id, float v, float lo, float hi)
        {
            auto* p = processor_.apvts().getParameter(id);
            if (p) p->setValueNotifyingHost(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f));
        };

        switch (field)
        {
            case 0: writeApvts(ParamIDs::outputGain,  value, -60.0f,  6.0f); break;
            case 1: writeApvts(ParamIDs::syncMode,    value,   0.0f,  1.0f); break;
            case 2: writeApvts(ParamIDs::channelMode, value,   0.0f,  1.0f); break;
            // Slot 3 (Swing) removed from GLOBAL meta; song-all swing edited via TRACK meta.
            default: break;
        }
    }

    juce::Rectangle<int> ManipulationZone::slotCellBounds(int i) const
    {
        static constexpr int kCols = kMZSlots / 2;
        const auto bounds  = getLocalBounds().reduced(4);
        const int  baseW   = bounds.getWidth() / kCols;
        const int  narrowW = baseW * 7 / 8;
        const int  rowH    = static_cast<int>(bounds.getHeight() * kRowHeightFrac);
        const int  upperX  = bounds.getX() + (bounds.getWidth() - kCols * narrowW);
        const bool isUpper = (i % 2 != 0);
        const int  ci      = i / 2;
        const int  x       = isUpper ? upperX + ci * narrowW : bounds.getX() + ci * narrowW;
        const int  y       = isUpper ? bounds.getY() : bounds.getBottom() - rowH;
        return juce::Rectangle<int>(x, y, narrowW, rowH).reduced(2, 2);
    }

    juce::Rectangle<int> ManipulationZone::slotKnobBounds(int i) const
    {
        const auto cell     = slotCellBounds(i);
        const int  knobSize = cell.getHeight() - kCellNameH - kCellValueH;
        const int  knobX    = cell.getX() + (cell.getWidth() - knobSize) / 2;
        const int  knobY    = cell.getY() + kCellNameH;
        return juce::Rectangle<int>(knobX, knobY, knobSize, knobSize);
    }

    void ManipulationZone::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(28, 32, 38));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(60, 70, 85));
        g.drawRect(getLocalBounds(), 1);

        if (metaSection_ >= 0)
        {
            // Amber tint for all meta sections — sliders handle the content.
            g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.06f));
            g.fillAll();
        }

        const auto& ctx = processor_.editContext();
        if (ctx.isActiveForEditing())
        {
            const bool fillEdit = processor_.fillActive();
            const juce::Colour editCol = fillEdit
                ? juce::Colour::fromRGB(80, 200, 255)
                : juce::Colour::fromRGB(255, 180, 50);
            g.setColour(editCol.withAlpha(0.18f));
            g.fillAll();

            // Banner sits in the top-left deliberately-empty half-cell (the gap
            // to the left of where the upper row begins), so it never collides
            // with name labels of the cells above.
            const auto bounds  = getLocalBounds().reduced(4);
            const int  baseW   = bounds.getWidth() / (kMZSlots / 2);
            const int  narrowW = baseW * 7 / 8;
            const int  rowH    = static_cast<int>(bounds.getHeight() * kRowHeightFrac);
            const int  upperX  = bounds.getX() + (bounds.getWidth() - (kMZSlots / 2) * narrowW);
            const juce::Rectangle<int> banner(bounds.getX(), bounds.getY(),
                                              upperX - bounds.getX(), rowH);

            g.setColour(editCol);
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            const auto topHalf = banner.withHeight(banner.getHeight() / 2).reduced(2, 0);
            const auto btmHalf = banner.withTop(banner.getCentreY()).reduced(2, 0);
            g.drawText(fillEdit ? "FILL" : "LOCK", topHalf, juce::Justification::centred);
            g.drawText("T" + juce::String(ctx.heldTrackIndex() + 1)
                       + " S" + juce::String(ctx.heldStepIndex() + 1),
                       btmHalf, juce::Justification::centred);
        }
    }

    void ManipulationZone::paintOverChildren(juce::Graphics& g)
    {
        if (metaSection_ >= 0)
            return;  // meta sections: no CC badges or learn overlays

        const int track  = area_.getActiveTrack();
        const bool pulse = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

        juce::ignoreUnused(track);

        for (int i = 0; i < kMZSlots; ++i)
        {
            const int  slot = slotOffset_ + i;
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
        static const juce::Colour kMorphMagenta { 0xffb060d0 };
        const int nmp = processor_.numParams(track);
        const int activeSec = (slotOffset_ < nmp)
            ? processor_.paramSpec(track, slotOffset_).sectionIndex : -1;
        for (int i = 0; i < kMZSlots; ++i)
        {
            const int slot = slotOffset_ + i;
            if (slot >= nmp) continue;
            if (processor_.paramSpec(track, slot).sectionIndex != activeSec) continue;
            const auto mInfo = processor_.morphWidgetInfo(track, slot);
            if (!mInfo.exists) continue;

            const auto knob  = slotKnobBounds(i);
            const int chipW  = 10, chipH = 8;
            const int chipX  = knob.getRight() + 2;  // just right of knob, inside cell
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
            const auto si   = static_cast<std::size_t>(i);
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
