#include "FunctionBar.h"
#include "KeyButton.h"
#include "UITheme.h"
#include "../PluginProcessor.h"

namespace lockstep
{
    namespace
    {
        using namespace theme;

        struct QKeyDef
        {
            int         keyCode;
            const char* keyHint;
            const char* primary;
            const char* secondary;  // "" = no secondary
            KeyGroup    group;
        };

        // Q W E R  Y U I O T  (9 keys — new order: TRK | nav×3 | CPY PST CLR TAP PLY)
        // T (PlayStop) has moved from slot 4 to slot 8 (far right).
        // Y/U/I/O shift left by one.
        constexpr std::array<QKeyDef, FunctionBar::kNumKeys> kDefs = {{
            { 'Q', "Q", "TRK", "",    { kModInactive, kModActive, kModAccent } },
            { 'W', "W", "<",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'E', "E", "v",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'R', "R", ">",   "",    { kNavInactive, kNavActive, kNavAccent } },
            { 'Y', "Y", "CPY", "KEY", { kActInactive, kActActive, kActAccent } },
            { 'U', "U", "PST", "RTG", { kActInactive, kActActive, kActAccent } },
            { 'I', "I", "CLR", "POL", { kActInactive, kActActive, kActAccent } },
            { 'O', "O", "TAP", "MET", { kTapInactive, kTapActive, kTapAccent } },
            { 'T', "T", "PLY", "RST", { kTrnInactive, kTrnActive, kTrnAccent } },
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

        const bool showKeyHint = (mode_ != GridDisplayMode::Clean);

        int leftPad, cellW;
        if (mode_ == GridDisplayMode::Staggered)
        {
            const int hu = staggerHalfUnit(getWidth());
            cellW   = staggerCellW(hu);
            leftPad = staggerOffsetQ(hu);
        }
        else if (mode_ == GridDisplayMode::Clean)
        {
            cellW   = (getWidth() - kClnColGap) / kNumKeys;
            leftPad = 0;
        }
        else
        {
            cellW   = getWidth() / kNumKeys;
            leftPad = 0;
        }
        const int h = getHeight();

        const auto gridMode = uiState_.trigGridMode;

        for (int i = 0; i < kNumKeys; ++i)
        {
            const auto& def = kDefs[static_cast<std::size_t>(i)];

            int x;
            if (mode_ == GridDisplayMode::Clean && i >= 1)
                x = leftPad + cellW + kClnColGap + (i - 1) * cellW;
            else
                x = leftPad + i * cellW;

            const auto cell = juce::Rectangle<int>(x, 0, cellW, h);

            const bool isPressed    = juce::KeyPress::isKeyCurrentlyDown(def.keyCode);
            const bool isPlaying    = (def.keyCode == 'T') && processor_.clock().inPluginPlaying();
            const bool isMetActive  = (def.keyCode == 'O') && processor_.clock().isMetronomeEnabled();
            const bool isTrkHeld    = (def.keyCode == 'Q') && uiState_.trackHeld;
            const bool isModeActive = (def.keyCode == 'Y' && gridMode == TrigGridMode::Keyboard)
                                   || (def.keyCode == 'U' && gridMode == TrigGridMode::Retrig)
                                   || (def.keyCode == 'I' && gridMode == TrigGridMode::SoundPool)
                                   || isPlaying
                                   || isMetActive
                                   || isTrkHeld;

            KeyButtonState state = KeyButtonState::Normal;
            if      (isPressed)    state = KeyButtonState::Pressed;
            else if (isModeActive) state = KeyButtonState::ModeActive;
            else if (uiState_.funcHeld) state = KeyButtonState::FuncHeld;

            paintKeyButton(g, cell,
                           def.keyHint, def.primary, def.secondary,
                           def.group, state, showKeyHint);
        }
    }
}
