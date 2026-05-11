#include "ManipulationZone.h"
#include "../PluginProcessor.h"
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
                processor_.editContext().setActiveSlot(slotOffset_ + i);
            };
            sliders_[si].onValueChange = [this, i]
            {
                if (!updatingFromTimer_)
                    processor_.writeParam(grid_.getActiveTrack(), slotOffset_ + i,
                                          static_cast<float>(sliders_[static_cast<std::size_t>(i)].getValue()));
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

        const bool showMachineParts = (metaSection_ < 0);
        for (std::size_t i = 0; i < kNumSlots; ++i)
        {
            sliders_[i].setVisible(showMachineParts);
            valueLabels_[i].setVisible(showMachineParts);
            clearBtns_[i].setVisible(showMachineParts);
            labels_[i].setVisible(showMachineParts);
        }

        repaint();
    }

    void ManipulationZone::mouseDown(const juce::MouseEvent& e)
    {
        if (!e.mods.isRightButtonDown())
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

    static juce::String formatValue(float v, ParamMetadata::Unit unit, bool isStepped)
    {
        if (isStepped)
            return juce::String(static_cast<int>(std::round(v)));

        switch (unit)
        {
            case ParamMetadata::Unit::Ms:
                return v < 10.0f ? juce::String(v, 1) + " ms"
                                 : juce::String(static_cast<int>(v)) + " ms";
            case ParamMetadata::Unit::Semitones:
            {
                const int st = static_cast<int>(std::round(v));
                return (st >= 0 ? "+" : "") + juce::String(st) + " st";
            }
            case ParamMetadata::Unit::Percent:
                return juce::String(static_cast<int>(v * 100.0f)) + "%";
            case ParamMetadata::Unit::None:
            default:
                return juce::String(v, 2);
        }
    }

    void ManipulationZone::refreshSliders()
    {
        const int track = grid_.getActiveTrack();
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto& ctx = processor_.editContext();
        const auto& t   = processor_.sequence().tracks[static_cast<std::size_t>(track)];

        updatingFromTimer_ = true;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si     = static_cast<std::size_t>(i);
            const int  slot   = slotOffset_ + i;
            const auto slotSz = static_cast<std::size_t>(slot);

            const auto meta = processor_.paramMetadata(track, slot);

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

            const juce::String nameText = meta.label.empty()
                                              ? juce::String(slot)
                                              : juce::String(meta.label);
            labels_[si].setText(nameText, juce::dontSendNotification);

            clearBtns_[si].setEnabled(hasLock);
            clearBtns_[si].setAlpha(hasLock ? 1.0f : 0.3f);
        }
        updatingFromTimer_ = false;

        repaint();
    }

    void ManipulationZone::paint(juce::Graphics& g)
    {
        g.setColour(juce::Colour::fromRGB(28, 32, 38));
        g.fillAll();
        g.setColour(juce::Colour::fromRGB(60, 70, 85));
        g.drawRect(getLocalBounds(), 1);

        if (metaSection_ >= 0)
        {
            // Meta section placeholder — replaced by real widgets at M6.5/M6.6.
            static constexpr std::array<const char*, IMachine::kNumSections> kMetaNames = {
                "COND", "TRACK", "", "", "", "GLOBAL"
            };
            const juce::Colour amber = juce::Colour::fromRGB(255, 180, 50);
            g.setColour(amber.withAlpha(0.12f));
            g.fillAll();
            g.setColour(amber);
            g.drawRect(getLocalBounds(), 1);
            g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
            const auto name = metaSection_ < IMachine::kNumSections
                                  ? juce::String(kMetaNames[static_cast<std::size_t>(metaSection_)])
                                  : juce::String(metaSection_);
            g.drawText(name + "  —  coming in M6.5 / M6.6",
                       getLocalBounds().reduced(12, 0),
                       juce::Justification::centredLeft);
            return;
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
