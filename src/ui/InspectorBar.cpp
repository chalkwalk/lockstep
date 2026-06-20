#include "InspectorBar.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Layout: full width, four equal columns.
    // Each column: 10px caption in dim text at top, body text below in lighter text.
    // Background: near-black chrome strip; thin dividers between columns.
    // -------------------------------------------------------------------------

    void InspectorBar::paintColumn(juce::Graphics& g,
                                   juce::Rectangle<int> area,
                                   const char* caption,
                                   const juce::String& text)
    {
        constexpr int kCaptionH = 9;
        const auto captionArea = area.removeFromTop(kCaptionH).reduced(3, 0);
        const auto bodyArea    = area.reduced(3, 1);

        // Caption in dim amber/grey.
        g.setColour(juce::Colour(0xFF7A6A50u).withAlpha(0.7f));
        g.setFont(juce::FontOptions(7.5f).withStyle("Bold"));
        g.drawText(caption, captionArea, juce::Justification::centredLeft, false);

        // Body text in pale white.
        g.setColour(juce::Colour(0xFFCCCCBBu).withAlpha(0.85f));
        g.setFont(juce::FontOptions(9.5f));
        g.drawText(text, bodyArea, juce::Justification::centredLeft, false);
    }

    void InspectorBar::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds();

        // Background — slightly lighter than the plugin body chrome.
        g.fillAll(juce::Colour(0xFF141820u));

        // Thin 1px top edge.
        g.setColour(juce::Colour(0xFF2A2E38u));
        g.fillRect(bounds.removeFromTop(1));

        const int w = bounds.getWidth();
        const int colW = w / 4;
        const juce::Colour kDivider{ 0xFF2A2E38u };

        auto remaining = bounds;
        const auto c0 = remaining.removeFromLeft(colW);
        const auto c1 = remaining.removeFromLeft(colW);
        const auto c2 = remaining.removeFromLeft(colW);
        const auto c3 = remaining; // final column takes remaining width

        paintColumn(g, c0, "KEY",     model_.key);
        g.setColour(kDivider); g.fillRect(c0.withLeft(c0.getRight()).withWidth(1));

        paintColumn(g, c1, "HELD",    model_.held);
        g.setColour(kDivider); g.fillRect(c1.withLeft(c1.getRight()).withWidth(1));

        paintColumn(g, c2, "OVERLAY", model_.overlay);
        g.setColour(kDivider); g.fillRect(c2.withLeft(c2.getRight()).withWidth(1));

        paintColumn(g, c3, "EDIT",    model_.edit);
    }

} // namespace lockstep
