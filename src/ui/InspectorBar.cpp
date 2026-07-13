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

    // ── The STATUS lane (9.30 st.1) ───────────────────────────────────────────
    // One organ for what used to have six homes. The KIND decides the treatment, and
    // the kind decides it because the kind is the thing that differs: a State never
    // fades, an Alert never fades, an Event does. Rendering is therefore a switch over
    // the taxonomy, not a pile of special cases -- which is what makes it impossible to
    // render a state as an event again (§42.2).
    void InspectorBar::paintStatusLane(juce::Graphics& g,
                                       juce::Rectangle<int> area,
                                       const InspectorModel& m)
    {
        const auto text = area.reduced(6, 0);
        switch (m.statusKind)
        {
            case StatusKind::None:
                g.setColour(juce::Colour(0xFF0E1116u));
                g.fillRect(area);
                return;

            case StatusKind::Confirm:
                // The lane itself only shows the danger band; the PROMPT is the pop-over,
                // painted by the editor over the MZ. Both are re-derived every frame.
                g.setColour(juce::Colour(0xFF4A1220u));
                g.fillRect(area);
                g.setColour(juce::Colour(0xFFFF6A80u));
                g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
                g.drawText(m.confirmPrompt, text, juce::Justification::centredLeft, true);
                return;

            case StatusKind::State:
                // Armed, and waiting for the next key. Never fades: that is the rule.
                g.setColour(juce::Colour(0xFF14243Au));
                g.fillRect(area);
                g.setColour(juce::Colour(0xFF7ABFFFu));
                g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
                g.drawText(m.status, text, juce::Justification::centredLeft, true);
                return;

            case StatusKind::Alert:
                g.setColour(juce::Colour(0xFF3A2A12u));
                g.fillRect(area);
                g.setColour(juce::Colour(0xFFFFA032u));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(m.status, text, juce::Justification::centredLeft, true);
                return;

            case StatusKind::Event:
                g.setColour(juce::Colour(0xFF0E1116u));
                g.fillRect(area);
                g.setColour(juce::Colour(0xFF80FFB0u).withAlpha(m.statusAlpha));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(m.status, text, juce::Justification::centredLeft, true);
                return;
        }
    }

    void InspectorBar::paintConfirmPopover(juce::Graphics& g,
                                           juce::Rectangle<int> area,
                                           const InspectorModel& m)
    {
        if (m.statusKind != StatusKind::Confirm) return;

        g.setColour(juce::Colour(0xFF5A1526u));
        g.fillRect(area);
        g.setColour(juce::Colour(0xFFFF6A80u));
        g.drawRect(area, 2);

        auto inner = area.reduced(10, 4);
        const auto promptArea = inner.removeFromTop(inner.getHeight() / 2);

        g.setColour(juce::Colour(0xFFFFD5DDu));
        g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
        g.drawText(m.confirmPrompt, promptArea, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xFFFF9AAAu));
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        g.drawText(m.confirmActions, inner, juce::Justification::centredLeft, true);
    }

    void InspectorBar::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds();

        // Background — slightly lighter than the plugin body chrome.
        g.fillAll(juce::Colour(0xFF141820u));

        // Thin 1px top edge.
        g.setColour(juce::Colour(0xFF2A2E38u));
        g.fillRect(bounds.removeFromTop(1));

        // The STATUS lane takes the bottom row, full width — it is one lane, not a
        // fifth column, because status is not a peer of the four regions: it is what
        // just happened, and what is about to happen if you press another key.
        paintStatusLane(g, bounds.removeFromBottom(kStatusLaneH), model_);

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
