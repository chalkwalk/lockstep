#include "KeyButton.h"

namespace lockstep
{
    void paintKeyButton(juce::Graphics&       g,
                        juce::Rectangle<int>  cell,
                        const juce::String&   keyHint,
                        const juce::String&   primary,
                        const juce::String&   secondary,
                        const KeyGroup&       group,
                        KeyButtonState        state,
                        bool                  showKeyHint)
    {
        const auto inner = cell.reduced(2, 2);

        const bool isPressed    = (state == KeyButtonState::Pressed);
        const bool isModeActive = (state == KeyButtonState::ModeActive);
        const bool isFuncHeld   = (state == KeyButtonState::FuncHeld);
        const bool isDisabled   = (state == KeyButtonState::Disabled);

        // Background
        const juce::Colour bg = (isPressed || isModeActive)
            ? juce::Colour(group.active)
            : juce::Colour(group.inactive);

        g.setColour(isDisabled ? bg.withAlpha(0.2f) : bg);
        g.fillRoundedRectangle(inner.toFloat(), 4.0f);

        // Border: 2 px for ModeActive, 1 px otherwise
        const juce::Colour accent = juce::Colour(group.accent);
        const float borderW = isModeActive ? 2.0f : 1.0f;
        g.setColour(isDisabled ? accent.withAlpha(0.2f) : accent.withAlpha(isModeActive ? 1.0f : 0.5f));
        g.drawRoundedRectangle(inner.toFloat(), 4.0f, borderW);

        if (isDisabled)
            return;

        // Key hint — top-left, small, dim
        if (showKeyHint && keyHint.isNotEmpty())
        {
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.setColour(juce::Colour(0xFF4A5C6Eu));
            g.drawText(keyHint,
                       inner.withHeight(10).reduced(2, 0),
                       juce::Justification::topLeft);
        }

        // Layout: reserve bottom 12px for secondary when one exists; primary fills the rest.
        const bool hasSec = secondary.isNotEmpty();
        const int secH   = hasSec ? 12 : 0;
        const auto primArea = inner.withTrimmedBottom(secH);
        const auto secArea  = inner.withTrimmedTop(inner.getHeight() - secH).reduced(2, 0);

        // Primary label — centred in its area; dims to 0.35 when FuncHeld
        if (primary.isNotEmpty())
        {
            const float alpha = (isFuncHeld && !isPressed) ? 0.35f : 1.0f;
            const juce::Colour primCol = isPressed
                ? juce::Colours::white
                : juce::Colour(0xFFA0B8D0u).withAlpha(alpha);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.setColour(primCol);
            g.drawText(primary, primArea, juce::Justification::centred);
        }

        // Secondary label — always visible (0.5α), full brightness when FuncHeld.
        // White keeps it legible across all group background colours.
        if (hasSec)
        {
            const float alpha = isFuncHeld ? 1.0f : 0.5f;
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawText(secondary, secArea, juce::Justification::centredBottom);
        }
    }
}
