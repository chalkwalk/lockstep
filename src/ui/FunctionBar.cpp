#include "FunctionBar.h"
#include "../PluginProcessor.h"

namespace lockstep
{
    namespace
    {
        struct QKeyDef
        {
            int         keyCode;
            const char* keyLabel;    // physical key letter
            const char* primLabel;   // primary function (short)
            const char* shiftLabel;  // Shift function ("" = reserved)
            bool        isNav;       // nav key (different base colour)
        };

        // Q W E R T Y U I
        constexpr std::array<QKeyDef, FunctionBar::kNumKeys> kDefs = {{
            { 'Q', "Q", "<",   "",  true  },   // NavLeft
            { 'W', "W", "v",   "",  true  },   // NavDown
            { 'E', "E", ">",   "",  true  },   // NavRight
            { 'R', "R", "REC", "",  false },
            { 'T', "T", "TAP", "",  false },
            { 'Y', "Y", "CPY", "",  false },
            { 'U', "U", "PST", "",  false },
            { 'I', "I", "CLR", "",  false },
        }};
    }

    FunctionBar::FunctionBar(LockstepProcessor& processor, UiState& uiState)
        : processor_(processor), uiState_(uiState)
    {
        startTimerHz(20);
    }

    void FunctionBar::setDisplayMode(GridDisplayMode mode)
    {
        mode_ = mode;
        repaint();
    }

    void FunctionBar::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour::fromRGB(20, 22, 26));

        // CLN hides key letters but still shows function labels.
        const bool showKeyLetters = (mode_ != GridDisplayMode::Clean);

        int leftPad, cellW;
        if (mode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(getWidth());
            cellW   = staggerCellW(hu);
            leftPad = staggerOffsetQ(hu);
        }
        else
        {
            cellW   = getWidth() / kNumKeys;
            leftPad = 0;
        }
        const int  h           = getHeight();

        for (int i = 0; i < kNumKeys; ++i)
        {
            const auto& def = kDefs[static_cast<std::size_t>(i)];
            const int   x   = leftPad + i * cellW;
            const auto  cell = juce::Rectangle<int>(x, 0, cellW, h).reduced(2, 2);

            const bool isPressed  = juce::KeyPress::isKeyCurrentlyDown(def.keyCode);
            const bool isRecArmed = (def.keyCode == 'R') && processor_.clock().isRecordArmed();

            // Background
            juce::Colour bg;
            if (isPressed)
                bg = juce::Colour::fromRGB(80, 120, 165);
            else if (isRecArmed)
                bg = juce::Colour::fromRGB(90, 35, 35);
            else if (def.isNav)
                bg = juce::Colour::fromRGB(30, 50, 70);
            else
                bg = juce::Colour::fromRGB(38, 40, 50);

            g.setColour(bg);
            g.fillRoundedRectangle(cell.toFloat(), 3.0f);
            g.setColour(juce::Colour::fromRGB(55, 68, 82));
            g.drawRoundedRectangle(cell.toFloat(), 3.0f, 1.0f);

            // Key letter — top-left, omitted in CLN mode
            if (showKeyLetters)
            {
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour::fromRGB(75, 92, 108));
                g.drawText(def.keyLabel,
                           cell.withHeight(10).reduced(2, 0),
                           juce::Justification::topLeft);
            }

            // Primary function — centre (always shown)
            const juce::Colour labelColour = isPressed
                ? juce::Colours::white
                : juce::Colour::fromRGB(160, 185, 210);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.setColour(labelColour);
            g.drawText(def.primLabel, cell, juce::Justification::centred);

            // Shift function — bottom (always shown when non-empty)
            if (def.shiftLabel[0] != '\0')
            {
                const float alpha = uiState_.shiftHeld ? 1.0f : 0.3f;
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour::fromRGB(180, 200, 220).withAlpha(alpha));
                g.drawText(def.shiftLabel,
                           cell.withTrimmedTop(cell.getHeight() - 11).reduced(2, 0),
                           juce::Justification::centredBottom);
            }
        }
    }
}
