#include "ManipulationZone.h"
#include "../ParameterIDs.h"
#include "../PluginProcessor.h"
#include "../core/TrigCondition.h"
#include "StepGrid.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    ManipulationZone::ManipulationZone(LockstepProcessor& processor, StepGrid& grid)
        : processor_(processor), grid_(grid)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);

            valueLabels_[si].setJustificationType(juce::Justification::centredLeft);
            valueLabels_[si].setFont(juce::Font(juce::FontOptions(11.0f)));
            addAndMakeVisible(valueLabels_[si]);

            labels_[si].setJustificationType(juce::Justification::centred);
            labels_[si].setFont(juce::Font(juce::FontOptions(10.0f)));
            addAndMakeVisible(labels_[si]);

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
                    case 1:  writeTrackField(i, v);  break;
                    case 5:  writeGlobalField(i, v); break;
                    default: processor_.writeParam(grid_.getActiveTrack(),
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
        // so in practice sliders are always shown.
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
        const int track = grid_.getActiveTrack();
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
            menu.addItem(1, "Fixed — track " + juce::String(track + 1)
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
            case 1: refreshTrackSliders();  return;
            case 5: refreshGlobalSliders(); return;
            default: break;
        }

        const int track = grid_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t   = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        const int numMachineParams = processor_.numParams(track);
        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si   = static_cast<std::size_t>(i);
            const int  slot = slotOffset_ + i;

            // Slot beyond this machine's schema — blank out the cell.
            if (slot >= numMachineParams)
            {
                sliders_[si].setEnabled(false);
                sliders_[si].setAlpha(0.0f);
                labels_[si].setText({}, juce::dontSendNotification);
                valueLabels_[si].setText({}, juce::dontSendNotification);
                clearBtns_[si].setEnabled(false);
                clearBtns_[si].setAlpha(0.0f);
                continue;
            }

            const auto slotSz = static_cast<std::size_t>(slot);
            const auto meta   = processor_.paramSpec(track, slot);

            sliders_[si].setEnabled(true);
            sliders_[si].setAlpha(1.0f);
            sliders_[si].setRange(static_cast<double>(meta.minValue),
                                  static_cast<double>(meta.maxValue),
                                  meta.isStepped ? 1.0 : 0.0);

            float value = t.baseParams[slotSz];

            const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
            const int  heldStep = ctx.heldStepIndex();
            bool hasLock = false;

            if (stepHeld && heldStep >= 0)
            {
                value   = t.steps[static_cast<std::size_t>(heldStep)].overrides.get(slot, value);
                hasLock = t.steps[static_cast<std::size_t>(heldStep)].overrides.has(slot);
            }

            sliders_[si].setValue(static_cast<double>(value), juce::dontSendNotification);

            juce::String valueText = formatValue(value, meta.unit, meta.isStepped);
            if (hasLock)
                valueText += " *";
            valueLabels_[si].setText(valueText, juce::dontSendNotification);

            const juce::String nameText = meta.label.isEmpty()
                                              ? juce::String(slot)
                                              : meta.label;
            labels_[si].setText(nameText, juce::dontSendNotification);

            clearBtns_[si].setEnabled(hasLock);
            clearBtns_[si].setAlpha(hasLock ? 1.0f : 0.3f);
        }
        updatingFromTimer_ = false;

        repaint();
    }

    void ManipulationZone::refreshCondSliders()
    {
        const int track = grid_.getActiveTrack();
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

        struct CondFieldDef { const char* label; float lo; float hi; };
        static constexpr std::array<CondFieldDef, kNumSlots> kDefs = {{
            { "Prob",  1.0f, 100.0f },
            { "m Num", 1.0f,   8.0f },
            { "m Den", 1.0f,   8.0f },
            { "Prev",  0.0f,   2.0f },
        }};

        const std::array<float, kNumSlots> vals = {
            static_cast<float>(display.probabilityPercent),
            static_cast<float>(display.iterNumerator),
            static_cast<float>(display.iterDenominator),
            static_cast<float>(display.prevDependency),
        };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi), 1.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);

            // Prev-dep (field 3) is only editable when a step is held.
            const bool prevDepField  = (i == 3);
            const bool enabled       = !prevDepField || stepHeld;
            sliders_[si].setEnabled(enabled);
            sliders_[si].setAlpha(enabled ? 1.0f : 0.35f);

            juce::String valueText;
            if (i == 0)
                valueText = juce::String(static_cast<int>(vals[si])) + "%";
            else if (i == 3)
            {
                const int pd = static_cast<int>(vals[si]);
                valueText = (pd == 0) ? "off" : (pd == 1 ? "fired" : "!fired");
            }
            else
                valueText = juce::String(static_cast<int>(vals[si]));

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);

            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
        repaint();
    }

    void ManipulationZone::writeCondField(int field, float value)
    {
        const int track = grid_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx  = processor_.editContext();
        const bool held  = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  step  = ctx.heldStepIndex();

        if (field == 3 && !held)  // Prev-dep only writable when a step is held
            return;

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
    // TRACK meta section (masterSection == 1)

    void ManipulationZone::refreshTrackSliders()
    {
        const int track = grid_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& t = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        const float length  = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackLength(track))->load();
        const float divider = processor_.apvts()
            .getRawParameterValue(ParamIDs::trackDivider(track))->load();
        const float gate = t.baseParams[3]; // slot 3 = sampler gate; moves to sequencer in MA.6
        const float noteMode = static_cast<float>(static_cast<int>(t.noteMode));

        struct TrackFieldDef { const char* label; float lo; float hi; bool stepped; };
        static constexpr std::array<TrackFieldDef, kNumSlots> kDefs = {{
            { "Length",  1.0f,  64.0f,    true  },
            { "Divider", 1.0f,  16.0f,    true  },
            { "Gate",    0.0f, 10000.0f,  false },
            { "Note",    0.0f,   1.0f,    true  },
        }};
        const std::array<float, kNumSlots> vals = { length, divider, gate, noteMode };

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            sliders_[si].setRange(static_cast<double>(kDefs[si].lo),
                                  static_cast<double>(kDefs[si].hi),
                                  kDefs[si].stepped ? 1.0 : 0.0);
            sliders_[si].setValue(static_cast<double>(vals[si]), juce::dontSendNotification);
            sliders_[si].setEnabled(true);
            sliders_[si].setAlpha(1.0f);

            juce::String valueText;
            if (i == 2)
                valueText = vals[si] < 10.0f
                    ? juce::String(vals[si], 1) + " ms"
                    : juce::String(static_cast<int>(vals[si])) + " ms";
            else if (i == 3)
                valueText = (static_cast<int>(vals[si]) == 0) ? "Pitch" : "Sample";
            else
                valueText = juce::String(static_cast<int>(vals[si]));

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
        repaint();
    }

    void ManipulationZone::writeTrackField(int field, float value)
    {
        const int track = grid_.getActiveTrack();
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

        auto& t = processor_.sequence().tracks[static_cast<std::size_t>(track)];
        switch (field)
        {
            case 0: writeApvts(ParamIDs::trackLength(track),  value, 1.0f, 64.0f);    break;
            case 1: writeApvts(ParamIDs::trackDivider(track), value, 1.0f, 16.0f);    break;
            case 2: t.baseParams[3] = value; break; // slot 3 = sampler gate; MA.6
            case 3: t.noteMode = (value >= 0.5f) ? NoteMode::SampleSelect
                                                  : NoteMode::Pitch;          break;
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
            { "—",       0.0f,  1.0f, false, false },
        }};
        const std::array<float, kNumSlots> vals = { gain, sync, chan, 0.0f };

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
                valueText = "—";

            valueLabels_[si].setText(valueText, juce::dontSendNotification);
            labels_[si].setText(kDefs[si].label, juce::dontSendNotification);
            clearBtns_[si].setEnabled(false);
            clearBtns_[si].setAlpha(0.0f);
        }
        updatingFromTimer_ = false;
        repaint();
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

        const int track   = grid_.getActiveTrack();
        const int slotW   = getWidth() / kNumSlots;
        const bool pulse  = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

        juce::ignoreUnused(track);

        for (int i = 0; i < kNumSlots; ++i)
        {
            const int slot = slotOffset_ + i;
            const int colX = i * slotW;
            const juce::Rectangle<int> col (colX, 0, slotW, getHeight());

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
        auto bounds = getLocalBounds().reduced(4);
        const int slotW = bounds.getWidth() / kNumSlots;

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto col = bounds.removeFromLeft(slotW).reduced(2, 0);

            // Top row: value display left, clear button right.
            auto topRow = col.removeFromTop(16);
            clearBtns_[si].setBounds(topRow.removeFromRight(18));
            valueLabels_[si].setBounds(topRow);

            // Bottom row: parameter name.
            labels_[si].setBounds(col.removeFromBottom(16));

            // Middle: rotary knob.
            sliders_[si].setBounds(col);
        }
    }
}
