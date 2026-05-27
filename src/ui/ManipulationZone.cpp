#include "ManipulationZone.h"
#include "../ParameterIDs.h"
#include "../PluginProcessor.h"
#include "../core/TrigCondition.h"
#include "KeyboardArea.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    ManipulationZone::ManipulationZone(LockstepProcessor& processor, KeyboardArea& area)
        : processor_(processor), area_(area)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);

            // MHZ.2.3: slim header — param name above the rotary.
            labels_[si].setJustificationType(juce::Justification::centredLeft);
            labels_[si].setFont(juce::Font(juce::FontOptions(9.0f)));
            addAndMakeVisible(labels_[si]);

            // MHZ.2.3: single value line below the rotary (textual or numeric).
            valueLabels_[si].setJustificationType(juce::Justification::centred);
            valueLabels_[si].setFont(juce::Font(juce::FontOptions(10.0f)));
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
                switch (metaSection_)
                {
                    case 0:  writeCondField(i, v);   break;
                    case 1:  writeTrigField(i, v);   break;
                    case 2:  writeTrackField(i, v);  break;
                    case 5:  writeGlobalField(i, v); break;
                    default: processor_.writeParam(area_.getActiveTrack(),
                                                   slotOffset_ + i, v); break;
                }
            };
            sliders_[si].addMouseListener(static_cast<juce::MouseListener*>(this), false);
            addAndMakeVisible(sliders_[si]);

            clearBtns_[si].setButtonText("x");
            clearBtns_[si].setWantsKeyboardFocus(false);
            clearBtns_[si].onClick = [this, i]
            {
                const auto& ctx = processor_.editContext();
                if (ctx.isActiveForEditing())
                    processor_.clearParam(ctx.heldTrackIndex(), ctx.heldStepIndex(),
                                          slotOffset_ + i);
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

    static juce::String formatValue(float v, ParamSpec::Unit unit, bool isStepped)  // NOLINT
    {
        if (isStepped)
            return juce::String(static_cast<int>(std::round(v)));

        switch (unit)
        {
            case ParamSpec::Unit::Ms:
                return v < 10.0f ? juce::String(v, 1) + " ms"
                                 : juce::String(static_cast<int>(v)) + " ms";
            case ParamSpec::Unit::Semitones:
            {
                const int st = static_cast<int>(std::round(v));
                return (st >= 0 ? "+" : "") + juce::String(st) + " st";
            }
            case ParamSpec::Unit::Percent:
                return juce::String(static_cast<int>(v * 100.0f)) + "%";
            case ParamSpec::Unit::None:
            default:
                return juce::String(v, 2);
        }
    }

    void ManipulationZone::refreshSliders()
    {
        switch (metaSection_)
        {
            case 0: refreshCondSliders();   return;
            case 1: refreshTrigSliders();   return;
            case 2: refreshTrackSliders();  return;
            case 5: refreshGlobalSliders(); return;
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
            const bool isSampleSlot = (meta.id == "sample_id");

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

            float value = processor_.baseParamValue(track, slot);

            const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
            const int  heldStep = ctx.heldStepIndex();
            bool hasLock = false;

            if (stepHeld && heldStep >= 0)
            {
                value   = t.steps[static_cast<std::size_t>(heldStep)].overrides.get(slot, value);
                hasLock = t.steps[static_cast<std::size_t>(heldStep)].overrides.has(slot);
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
                    valueText = formatValue(value, meta.unit, meta.isStepped);
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

        // Slot 3: Fill rule when no step held (track-level), Prev-dep when step held.
        // Fill rule is editable at both levels; Prev-dep only makes sense at step level.
        const bool slot3IsPrev = stepHeld;

        struct CondFieldDef { const char* label; float lo; float hi; };
        const std::array<CondFieldDef, kNumSlots> kDefs = {{
            { "Prob",  1.0f, 100.0f },
            { "m Num", 1.0f,   8.0f },
            { "m Den", 1.0f,   8.0f },
            { slot3IsPrev ? "Prev" : "Fill", 0.0f, slot3IsPrev ? 2.0f : 2.0f },
            { "",      0.0f,   1.0f },
            { "",      0.0f,   1.0f },
            { "",      0.0f,   1.0f },
            { "",      0.0f,   1.0f },
        }};

        const std::array<float, kNumSlots> vals = {
            static_cast<float>(display.probabilityPercent),
            static_cast<float>(display.iterNumerator),
            static_cast<float>(display.iterDenominator),
            slot3IsPrev ? static_cast<float>(display.prevDependency)
                        : static_cast<float>(display.fillRule),
            0.0f, 0.0f, 0.0f, 0.0f,
        };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi), 1.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);
            sliders_[si].setEnabled(true);
            sliders_[si].setAlpha(1.0f);

            juce::String valueText;
            if (i == 0)
                valueText = juce::String(static_cast<int>(vals[si])) + "%";
            else if (i == 3 && slot3IsPrev)
            {
                const int pd = static_cast<int>(vals[si]);
                valueText = (pd == 0) ? "off" : (pd == 1 ? "fired" : "!fired");
            }
            else if (i == 3 && !slot3IsPrev)
            {
                const int fr = static_cast<int>(vals[si]);
                valueText = (fr == 0) ? "always"
                          : (fr == 1) ? "fill only" : "no fill";
            }
            else
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
            case 3:
                // Slot 3: Prev-dep when step held, Fill rule otherwise.
                if (held && step >= 0)
                    target.prevDependency = u8(value);
                else
                    target.fillRule = static_cast<FillRule>(
                        std::clamp(static_cast<int>(std::round(value)), 0, 2));
                break;
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
        const auto* trig = (stepHeld && heldStep >= 0)
            ? &t.steps[static_cast<std::size_t>(heldStep)].trigOverride
            : nullptr;

        // Override-ELSE-Base per field.
        const bool  hasNote  = trig && trig->noteCount > 0;
        const int   note     = hasNote ? trig->notes[0] : t.trigDefaults.note;
        const int   velocity = (trig && trig->hasVelocity) ? trig->velocity : t.trigDefaults.velocity;
        const float gateMs   = (trig && trig->hasGate)     ? trig->gateMs   : t.trigDefaults.gateMs;
        const bool  hasVel   = trig && trig->hasVelocity;
        const bool  hasGate  = trig && trig->hasGate;
        // Extra notes beyond the primary (for chord display).
        const int   chordExtra = hasNote ? trig->noteCount - 1 : 0;

        const float noteSel = static_cast<float>(
            t.noteSelection == NoteSelection::BottomBias ? 1 : 0);

        struct TrigFieldDef { const char* label; float lo; float hi; bool stepped; bool active; };
        static constexpr std::array<TrigFieldDef, kNumSlots> kDefs = {{
            { "Note",   0.0f,   127.0f, true,  true  },
            { "Vel",    1.0f,   127.0f, true,  true  },
            { "Gate",   0.0f, 10000.0f, false, true  },
            { "Bias",   0.0f,     1.0f, true,  true  },
            { "",       0.0f,     1.0f, false, false },
            { "",       0.0f,     1.0f, false, false },
            { "",       0.0f,     1.0f, false, false },
            { "",       0.0f,     1.0f, false, false },
        }};
        const std::array<float, kNumSlots> vals  = { static_cast<float>(note),
                                                     static_cast<float>(velocity),
                                                     gateMs, noteSel,
                                                     0.0f, 0.0f, 0.0f, 0.0f };
        const std::array<bool,  kNumSlots> locks = { hasNote, hasVel, hasGate, false,
                                                     false, false, false, false };
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
                    valueText = vals[si] < 10.0f
                        ? juce::String(vals[si], 1) + " ms"
                        : juce::String(static_cast<int>(vals[si])) + " ms";
                else if (i == 3)
                    valueText = kBiasLabels[static_cast<int>(vals[si])];
                else
                    valueText = juce::String(static_cast<int>(vals[si]));
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
            auto& trig = t.steps[static_cast<std::size_t>(step)].trigOverride;
            switch (field)
            {
                case 0: // MZ note edit: always sets primary note; preserves chord size.
                        if (trig.noteCount == 0) trig.noteCount = 1;
                        trig.notes[0]    = std::clamp(static_cast<int>(value), 0, 127);   break;
                case 1: trig.hasVelocity = true;
                        trig.velocity    = std::clamp(static_cast<int>(value), 1, 127);   break;
                case 2: trig.hasGate     = true;
                        trig.gateMs      = std::max(0.0f, value);                         break;
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
                case 2: t.trigDefaults.gateMs   = std::max(0.0f, value);                        break;
                case 3: t.noteSelection = (value >= 0.5f)
                            ? NoteSelection::BottomBias
                            : NoteSelection::TopBias;
                        break;
                default: break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // TRACK meta section (masterSection == 2)

    void ManipulationZone::refreshTrackSliders()
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const float length  = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackLength(track))->load();
        const float divider = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackDivider(track))->load();

        struct TrackFieldDef { const char* label; float lo; float hi; bool stepped; bool active; };
        static constexpr std::array<TrackFieldDef, kNumSlots> kDefs = {{
            { "Length",  1.0f,  64.0f,  true,  true  },
            { "Divider", 1.0f,  16.0f,  true,  true  },
            { "",        0.0f,   1.0f,  false, false },
            { "",        0.0f,   1.0f,  false, false },
            { "",        0.0f,   1.0f,  false, false },
            { "",        0.0f,   1.0f,  false, false },
            { "",        0.0f,   1.0f,  false, false },
            { "",        0.0f,   1.0f,  false, false },
        }};
        const std::array<float, kNumSlots> vals = { length, divider, 0.0f, 0.0f,
                                                    0.0f,   0.0f,    0.0f, 0.0f };

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

            const juce::String valueText = kDefs[si].active
                ? juce::String(static_cast<int>(vals[si]))
                : juce::String{};

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
    }

    void ManipulationZone::writeTrackField(int field, float value)
    {
        const int track = area_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        // Suppress trig toggle if a step is held while editing track params.
        const auto& ctx = processor_.editContext();
        if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
            processor_.editContext().markParamWritten();

        const auto writeApvts = [this](const juce::String& id, float v, float lo, float hi)
        {
            auto* p = processor_.apvts().getParameter(id);
            if (p) p->setValueNotifyingHost(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f));
        };

        switch (field)
        {
            case 0: writeApvts(ParamIDs::trackLength(track),  value, 1.0f, 64.0f); break;
            case 1: writeApvts(ParamIDs::trackDivider(track), value, 1.0f, 16.0f); break;
            default: break;
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
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
            { "",        0.0f,  1.0f, false, false },
        }};
        const std::array<float, kNumSlots> vals = { gain, sync, chan, 0.0f,
                                                    0.0f, 0.0f, 0.0f, 0.0f };

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
                valueText = juce::String(u8"—");

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
            menu.addItem(0, "(pool is empty)", false);
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
            case 0: writeApvts(ParamIDs::outputGain,  value, -60.0f, 6.0f); break;
            case 1: writeApvts(ParamIDs::syncMode,    value,   0.0f, 1.0f); break;
            case 2: writeApvts(ParamIDs::channelMode, value,   0.0f, 1.0f); break;
            default: break;
        }
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
            g.setColour(juce::Colour::fromRGB(255, 180, 50).withAlpha(0.18f));
            g.fillAll();
            g.setColour(juce::Colour::fromRGB(255, 180, 50));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("P-LOCK  track " + juce::String(ctx.heldTrackIndex() + 1)
                           + "  step " + juce::String(ctx.heldStepIndex() + 1),
                       getLocalBounds().removeFromTop(14).reduced(4, 0),
                       juce::Justification::centredLeft);
        }
    }

    void ManipulationZone::paintOverChildren(juce::Graphics& g)
    {
        if (metaSection_ >= 0)
            return;  // meta sections: no CC badges or learn overlays

        const int track   = area_.getActiveTrack();
        static constexpr int kCols = kMZSlots / 2;
        const int baseW   = getWidth() / kCols;
        const int narrowW = baseW * 7 / 8;
        const int rowH    = getHeight() / 2;
        const int upperX  = getWidth() - kCols * narrowW;
        const bool pulse  = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

        juce::ignoreUnused(track);

        for (int i = 0; i < kMZSlots; ++i)
        {
            const int slot = slotOffset_ + i;
            const int row  = i / kCols;
            const int ci   = i % kCols;
            const int x    = (row == 0) ? upperX + ci * narrowW : ci * narrowW;
            const juce::Rectangle<int> col (x, row * rowH, narrowW, rowH);

            // Listening overlay: pulsing highlight on the slot being learned.
            if (i == learningSlotIndex_)
            {
                g.setColour(juce::Colour::fromRGB(80, 180, 255).withAlpha(pulse ? 0.35f : 0.15f));
                g.fillRect(col);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                const auto textArea = col.reduced(2).withTrimmedTop(col.getHeight() - 12);
                g.drawText("wiggle CC...", textArea, juce::Justification::centred);
                continue; // skip badge while listening
            }

            // CC mapping badge.
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
            }

            const juce::Rectangle<int> badgeArea = col.withWidth(22).withTrimmedLeft(col.getWidth() - 22)
                                                       .withHeight(14).reduced(2, 2);
            g.setColour(badgeColour.withAlpha(0.85f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
            g.setColour(juce::Colours::black);
            g.setFont(juce::Font(juce::FontOptions(8.0f)).boldened());
            g.drawText(badge, badgeArea, juce::Justification::centred);
        }
    }

    void ManipulationZone::resized()
    {
        // 4×2 staggered layout (MHX §26.2, §33.4):
        // Each cell is 7/8 of the base column width (12.5% narrower).
        // Lower row (row 1) is left-justified; upper row (row 0) is right-justified,
        // so upper cell centres land near the right edge of each lower cell.
        static constexpr int kCols   = kMZSlots / 2;  // 4
        auto bounds = getLocalBounds().reduced(4);
        const int baseW  = bounds.getWidth() / kCols;
        const int narrowW = baseW * 7 / 8;
        const int rowH    = bounds.getHeight() / 2;
        const int upperX  = bounds.getX() + (bounds.getWidth() - kCols * narrowW);

        for (int i = 0; i < kMZSlots; ++i)
        {
            const auto si  = static_cast<std::size_t>(i);
            const int  row = i / kCols;
            const int  col = i % kCols;

            const int x = (row == 0)
                ? upperX + col * narrowW           // upper: right-justified
                : bounds.getX() + col * narrowW;   // lower: left-justified

            auto cell = juce::Rectangle<int>(
                x, bounds.getY() + row * rowH,
                narrowW, rowH).reduced(2, 2);

            // Top strip: param name left, clear button right.
            auto header = cell.removeFromTop(10);
            clearBtns_[si].setBounds(header.removeFromRight(14));
            labels_[si].setBounds(header);

            // Bottom strip: value display.
            valueLabels_[si].setBounds(cell.removeFromBottom(10));

            // Middle: rotary knob (bigger than old layout).
            sliders_[si].setBounds(cell);
            if (i == 0)
                samplePickerBtn_.setBounds(cell.reduced(2, 2));
        }
    }
}
