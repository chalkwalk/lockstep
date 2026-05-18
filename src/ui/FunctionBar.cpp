#include "FunctionBar.h"
#include "../PluginProcessor.h"

namespace lockstep
{
    namespace
    {
        struct QKeyDef
        {
            int         keyCode;
            const char* keyLabel;   // physical key letter
            const char* primLabel;  // primary function
            const char* funcLabel;  // Func-layer function ("" = no secondary)
            bool        isNav;      // nav key (different base colour)
        };

        // W E R T Y U I O  (8 keys in function row of 9x4 layout)
        constexpr std::array<QKeyDef, FunctionBar::kNumKeys> kDefs = {{
            { 'W', "W", "<",   "",    true  },  // NavLeft
            { 'E', "E", "v",   "",    true  },  // NavDown
            { 'R', "R", ">",   "",    true  },  // NavRight
            { 'T', "T", "REC", "YES", false },  // RecordArm / Yes
            { 'Y', "Y", "CPY", "KEY", false },  // VerbRecord / TrigModeKeyboard
            { 'U', "U", "PST", "RTG", false },  // VerbPlay   / TrigModeRetrig
            { 'I', "I", "CLR", "POL", false },  // VerbStop   / TrigModeSoundPool
            { 'O', "O", "TAP", "NO",  false },  // TapTempo   / No
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
        const int h = getHeight();

        for (int i = 0; i < kNumKeys; ++i)
        {
            const auto& def  = kDefs[static_cast<std::size_t>(i)];
            const int   x    = leftPad + i * cellW;
            const auto  cell = juce::Rectangle<int>(x, 0, cellW, h).reduced(2, 2);

            const bool isPressed  = juce::KeyPress::isKeyCurrentlyDown(def.keyCode);
            const bool isRecArmed = (def.keyCode == 'T') && processor_.clock().isRecordArmed();

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

            if (showKeyLetters)
            {
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour::fromRGB(75, 92, 108));
                g.drawText(def.keyLabel,
                           cell.withHeight(10).reduced(2, 0),
                           juce::Justification::topLeft);
            }

            // Primary label — dimmed when Func is held (Func-layer is about to activate).
            const float primAlpha = (isPressed || !uiState_.funcHeld) ? 1.0f : 0.35f;
            const juce::Colour labelColour = isPressed
                ? juce::Colours::white
                : juce::Colour::fromRGB(160, 185, 210).withAlpha(primAlpha);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.setColour(labelColour);
            g.drawText(def.primLabel, cell, juce::Justification::centred);

            // Func-layer label — bottom; bright when Func held, dim otherwise.
            if (def.funcLabel[0] != '\0')
            {
                const float alpha = uiState_.funcHeld ? 1.0f : 0.25f;
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                g.setColour(juce::Colour::fromRGB(180, 200, 220).withAlpha(alpha));
                g.drawText(def.funcLabel,
                           cell.withTrimmedTop(cell.getHeight() - 11).reduced(2, 0),
                           juce::Justification::centredBottom);
            }
        }
    }
}
