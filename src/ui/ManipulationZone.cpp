#include "ManipulationZone.h"
#include "../PluginProcessor.h"
#include "StepGrid.h"
#include <algorithm>

namespace lockstep
{
    ManipulationZone::ManipulationZone(LockstepProcessor& processor, StepGrid& grid)
        : processor_(processor), grid_(grid)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            const auto meta = processor_.paramMetadata(0, i);

            const juce::String labelText = meta.label.empty()
                                              ? juce::String(i)
                                              : juce::String(meta.label);
            labels_[si].setText(labelText, juce::dontSendNotification);
            labels_[si].setJustificationType(juce::Justification::centred);
            addAndMakeVisible(labels_[si]);

            sliders_[si].setRange(static_cast<double>(meta.minValue),
                                  static_cast<double>(meta.maxValue),
                                  meta.isStepped ? 1.0 : 0.0);
            sliders_[si].setSliderStyle(juce::Slider::LinearVertical);
            sliders_[si].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
            sliders_[si].onValueChange = [this, i]
            {
                if (!updatingFromTimer_)
                    processor_.writeParam(grid_.getActiveTrack(), i,
                                          static_cast<float>(sliders_[static_cast<std::size_t>(i)].getValue()));
            };
            addAndMakeVisible(sliders_[si]);
        }

        startTimerHz(30);
    }

    ManipulationZone::~ManipulationZone() = default;

    void ManipulationZone::timerCallback()
    {
        refreshSliders();
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
            const auto si = static_cast<std::size_t>(i);
            float value = t.baseParams[si];

            if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
            {
                const int step = ctx.heldStepIndex();
                if (step >= 0)
                    value = t.steps[static_cast<std::size_t>(step)].overrides.get(i, value);
            }

            sliders_[si].setValue(static_cast<double>(value), juce::dontSendNotification);

            // Update label to show P-Lock indicator when an override exists.
            const auto meta = processor_.paramMetadata(track, i);
            juce::String labelText = meta.label.empty()
                                         ? juce::String(i)
                                         : juce::String(meta.label);
            if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
            {
                const int step = ctx.heldStepIndex();
                if (step >= 0 && t.steps[static_cast<std::size_t>(step)].overrides.has(i))
                    labelText += " *";
            }
            labels_[si].setText(labelText, juce::dontSendNotification);
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

    void ManipulationZone::resized()
    {
        auto bounds = getLocalBounds().reduced(4);
        const int slotW = bounds.getWidth() / kNumSlots;

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto col = bounds.removeFromLeft(slotW).reduced(2, 0);
            labels_[static_cast<std::size_t>(i)].setBounds(col.removeFromBottom(16));
            sliders_[static_cast<std::size_t>(i)].setBounds(col);
        }
    }
}
