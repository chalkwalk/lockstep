#include "KeyButton.h"
#include "UITheme.h"

namespace lockstep
{
    using namespace theme;

    // Maps a SurfaceCell's button identity and state to a KeyGroup.
    // This is the single place that encodes the colour scheme for each key type.
    static KeyGroup groupForCell(const SurfaceCell& c) noexcept
    {
        const bool isMode = (c.base == CellState::ModeActive);

        // Scope glow (DESIGN §6.6): an in-scope cell renders fill + border in the
        // held scope's colour, brighter than a resting key, so the surface tells
        // the performer exactly which keys the scope rewrites. Wins over the
        // per-button scheme below; disabled cells never carry a tint.
        if (c.scopeTint != 0u && !c.disabled)
        {
            const juce::Colour tint { c.scopeTint };
            return { tint.withMultipliedBrightness(0.55f).getARGB(),  // resting in-scope fill
                     tint.getARGB(),                                  // pressed/active fill
                     tint.brighter(0.35f).getARGB() };                // border accent
        }

        switch (c.button)
        {
            // --- Number-row utility ---
            case ControllerButton::Func:
                return { kFuncInactive, kFuncActive, kFuncAccent };

            case ControllerButton::TapTempo:
                return { kTapInactive, kTapActive, kTapAccent };

            // --- Number-row modifier (Track) ---
            case ControllerButton::TrackScope:
                return isMode
                    ? KeyGroup{ kScopeTrackDim, kScopeTrack, kScopeTrack }
                    : KeyGroup{ kScopeTrackDim, kPerfActive, kPerfAccent };

            // --- Q-row modifiers (Pattern, Part) ---
            case ControllerButton::PatternScope:
                return isMode
                    ? KeyGroup{ kScopePatternDim, kScopePattern, kScopePattern }
                    : KeyGroup{ kScopePatternDim, kModActive,    kModAccent    };

            case ControllerButton::PartScope:
                return isMode
                    ? KeyGroup{ kScopePartDim, kScopePart, kScopePart }
                    : KeyGroup{ kScopePartDim, kPerfActive, kPerfAccent };

            // --- Step-row modifiers (Scene, Master, Mute, Fill) ---
            case ControllerButton::SceneScope:
                return isMode
                    ? KeyGroup{ kScopeSceneDim, kScopeScene, kScopeScene }
                    : KeyGroup{ kScopeSceneDim, kModActive,  kModAccent  };

            case ControllerButton::MasterScope:
                return isMode
                    ? KeyGroup{ kScopeMasterDim, kScopeMaster, kScopeMaster }
                    : KeyGroup{ kScopeMasterDim, kPerfActive,  kPerfAccent  };

            case ControllerButton::MuteScope:
                // When ModeActive, baseColour carries kScopePMute or kScopeMute depending on Func.
                return isMode
                    ? KeyGroup{ kScopeMuteDim, c.baseColour, c.baseColour }
                    : KeyGroup{ kScopeMuteDim, kModActive,   kModAccent   };

            case ControllerButton::FillScope:
                return isMode
                    ? KeyGroup{ kScopeFillDim, kScopeFill, kScopeFill }
                    : KeyGroup{ kScopeFillDim, kPerfActive, kPerfAccent };

            // --- Navigation ---
            case ControllerButton::NavUp:
            case ControllerButton::NavLeft:
            case ControllerButton::NavDown:
            case ControllerButton::NavRight:
                return { kNavInactive, kNavActive, kNavAccent };

            // --- Verb keys ---
            case ControllerButton::VerbYes:
            case ControllerButton::VerbNo:
                return { kActInactive, kActActive, kActAccent };

            case ControllerButton::VerbRecord:
                // OD armed: baseColour carries the amber active colour.
                if (c.baseColour == 0xFFD2821Eu)
                    return { 0xFF2E1E08u, 0xFFD2821Eu, 0xFFE0A040u };
                return { kRecInactive, kRecActive, kRecAccent };

            case ControllerButton::VerbPlay:
            case ControllerButton::VerbStop:
                return { kTrnInactive, kTrnActive, kTrnAccent };

            // --- Section keys (canonical TRIG/SRC/FILTER/AMP/MOD/FX) ---
            case ControllerButton::Section:
                // baseColour distinguishes special visual modes
                if (c.baseColour == kScopeMachine)
                    return { kSecInactive, kScopeMachine,  kScopeMachine  };
                if (c.baseColour == kScopeNoteEdit)
                    return { kSecInactive, kScopeNoteEdit, kScopeNoteEdit };
                if (c.baseColour == 0xFF404010u)            // master-active golden
                    return { kSecInactive, 0xFF404010u, 0xFFFFB432u };
                return { kSecInactive, kSecActive, kSecAccent };

            default:
                // Fallback: derive inactive from baseColour (for unknown future button types).
                return { juce::Colour(c.baseColour).withMultipliedBrightness(0.25f).getARGB(),
                         c.baseColour, c.baseColour };
        }
    }


    void paintCellKeyHint(juce::Graphics& g, juce::Rectangle<int> inner,
                          const juce::String& hint, float alpha)
    {
        if (hint.isEmpty()) return;
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour(0xFFAAC4D8u).withAlpha(alpha));
        g.drawText(hint, inner.withHeight(14).reduced(2, 0), juce::Justification::topLeft);
    }

    void paintKeyButton(juce::Graphics&       g,
                        juce::Rectangle<int>  cell,
                        const juce::String&   keyHint,
                        const juce::String&   primary,
                        const juce::String&   secondary,
                        const KeyGroup&       group,
                        KeyButtonState        state,
                        bool                  showKeyHint,
                        bool                  compoundOverlay,
                        juce::Colour          latchColour)
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
        if (showKeyHint)
            paintCellKeyHint(g, inner, keyHint, isDisabled ? 0.45f : 1.0f);

        // MHZ.1.1: secondary band grown from 12 px to 14 px.
        // Disabled keys never draw their secondary (early-out below), so reserve
        // no band for it — otherwise a reserved section's primary (e.g. FX, whose
        // funcHint "GLOBAL" never renders while disabled) floats upward off-centre.
        const bool hasSec = secondary.isNotEmpty() && !isDisabled;
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

        // MHZ.9.6: latch pip — small scope-coloured dot at bottom-left indicates virtual-hold.
        if (latchColour.getAlpha() > 0)
        {
            const int pipSz = 5;
            const auto pip = juce::Rectangle<int>(inner.getX() + 2,
                                                  inner.getBottom() - pipSz - 2,
                                                  pipSz, pipSz);
            g.setColour(latchColour);
            g.fillEllipse(pip.toFloat());
        }
    }

    void paintCell(juce::Graphics& g, juce::Rectangle<int> cell,
                   const SurfaceCell& c, bool showKeyHint)
    {
        // Derive KeyButtonState from cell flags.
        KeyButtonState st;
        if (c.pressed)
            st = KeyButtonState::Pressed;
        else if (c.disabled)
            st = KeyButtonState::Disabled;
        else if (c.base == CellState::ModeActive)
            st = KeyButtonState::ModeActive;
        else
            st = KeyButtonState::Normal;

        const KeyGroup   grp         = groupForCell(c);
        const bool       compound    = c.strip.present;
        const juce::Colour latchCol  = c.pip.present
            ? juce::Colour(c.pip.colour) : juce::Colours::transparentBlack;

        paintKeyButton(g, cell, c.keyHint, c.primary, c.funcHint,
                       grp, st, showKeyHint, compound, latchCol);
    }
}
