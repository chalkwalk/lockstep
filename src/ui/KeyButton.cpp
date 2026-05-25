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
                        bool                  showKeyHint,
                        bool                  compoundOverlay)
    {
        // MHZ.1.1: reduced inner margin (was 2,2) to use more of the cell area.
        const auto inner = cell.reduced(1, 1);

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

        // Key hint — top-left; always shown, slightly dimmed when disabled.
        if (showKeyHint && keyHint.isNotEmpty())
        {
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.setColour(juce::Colour(0xFFAAC4D8u).withAlpha(isDisabled ? 0.45f : 1.0f));
            g.drawText(keyHint,
                       inner.withHeight(14).reduced(2, 0),
                       juce::Justification::topLeft);
        }

        // MHZ.1.1: secondary band grown from 12 px to 14 px.
        const bool hasSec = secondary.isNotEmpty();
        const int secH   = hasSec ? 14 : 0;
        const auto primArea = inner.withTrimmedBottom(secH);
        const auto secArea  = inner.withTrimmedTop(inner.getHeight() - secH).reduced(2, 0);

        // Primary label — ghost-dim when Disabled; dims to 0.35 when FuncHeld.
        // MHZ.1.1: primary font grown from 10pt to 15pt.
        // MHZ.1.2: labels up to 6 chars fit at 15pt; over-6 falls back to
        //          auto-fit (juce truncation disabled, JUCE clips).
        if (primary.isNotEmpty())
        {
            const float alpha = isDisabled               ? 0.28f
                              : (isFuncHeld && !isPressed) ? 0.35f
                              : 1.0f;
            const juce::Colour primCol = isPressed
                ? juce::Colours::white
                : juce::Colour(0xFFB8D0E0u).withAlpha(alpha);

            // Choose font size: 15pt for ≤ 6 chars, auto-shrink for longer labels.
            const float fontSize = (primary.length() <= 6) ? 15.0f
                                 : (primary.length() <= 8) ? 11.0f : 9.0f;
            g.setFont(juce::Font(juce::FontOptions(fontSize)));
            g.setColour(primCol);
            g.drawText(primary, primArea, juce::Justification::centred, false);
        }

        if (isDisabled)
            return;

        // Secondary label — always visible (0.5α), full brightness when FuncHeld.
        // MHZ.1.1: hint font grown from 8pt to 9pt.
        if (hasSec)
        {
            const float alpha = isFuncHeld ? 1.0f : 0.5f;
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawText(secondary, secArea, juce::Justification::centredBottom, false);
        }

        // Register 4: compound-chord overlay — amber top strip (DESIGN §13).
        if (compoundOverlay)
        {
            const auto strip = inner.withHeight(3).reduced(3, 0);
            g.setColour(juce::Colour(0xFFD0A020u));
            g.fillRect(strip);
        }
    }
}
