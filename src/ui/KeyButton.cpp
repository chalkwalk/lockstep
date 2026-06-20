#include "KeyButton.h"
#include "UITheme.h"
#include "CellAppearance.h"

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
            const juce::Colour tint{ c.scopeTint };
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

            // --- Structural scope modifiers: one hue in every state, no generic borrow ---
            case ControllerButton::TrackScope:
                return { kScopeTrackDim, kScopeTrack, kScopeTrackAcc };

            case ControllerButton::PhraseScope:
                return { kScopePhraseDim, kScopePhrase, kScopePhraseAcc };

            case ControllerButton::SceneScope:
                return { kScopeSceneDim, kScopeScene, kScopeSceneAcc };

            case ControllerButton::MorphScope:
                return { kScopeMorphDim, kScopeMorph, kScopeMorphAcc };

            case ControllerButton::SongScope:
                return { kScopeSongDim, kScopeSong, kScopeSongAcc };

            case ControllerButton::MuteScope:
                // ModeActive: baseColour carries kScopePMute (Func+Mute) or kScopeMute.
                return isMode
                           ? KeyGroup{ kScopeMuteDim, c.baseColour, c.baseColour }
                           : KeyGroup{ kScopeMuteDim, kScopeMute, kScopeMuteAcc };

            case ControllerButton::FillScope:
                return { kScopeFillDim, kScopeFill, kScopeFillAcc };

            // --- Navigation ---
            case ControllerButton::NavUp:
            case ControllerButton::NavLeft:
            case ControllerButton::NavDown:
            case ControllerButton::NavRight:
                return { kNavInactive, kNavActive, kNavAccent };

            // --- Verb keys: neutral slate at rest; conventional colour on-active ---
            case ControllerButton::VerbSnapshot:
                // Y = Snapshot. ModeActive not currently used; resting is always slate.
                return { kVerbInactive, kVerbSnapActive, kVerbSnapAccent };

            case ControllerButton::VerbConfirm:
                // P = Confirm. Neutral slate resting.
                return { kVerbInactive, kVerbActive, kVerbAccent };

            case ControllerButton::VerbRecord:
                // OD armed: baseColour carries the amber sentinel.
                if (c.baseColour == kVerbODActive)
                    return { 0xFF2E1E08u, kVerbODActive, kVerbODAccent };
                // Armed (ModeActive) → red; otherwise neutral slate.
                return isMode
                           ? KeyGroup{ kVerbInactive, kVerbRecActive, kVerbRecAccent }
                           : KeyGroup{ kVerbInactive, kVerbActive, kVerbAccent };

            case ControllerButton::VerbPlay:
                // Playing (ModeActive) → green; otherwise neutral slate.
                return isMode
                           ? KeyGroup{ kVerbInactive, kVerbPlayActive, kVerbPlayAccent }
                           : KeyGroup{ kVerbInactive, kVerbActive, kVerbAccent };

            case ControllerButton::VerbStopLegacy:
                // Legacy — no key emits this anymore. Show as neutral.
                return { kVerbInactive, kVerbActive, kVerbAccent };

            case ControllerButton::VerbClear:
                // O = Clear. ModeActive would indicate a pending-confirm prompt.
                return isMode
                           ? KeyGroup{ kVerbInactive, kVerbClearActive, kVerbClearAccent }
                           : KeyGroup{ kVerbInactive, kVerbActive, kVerbAccent };

            case ControllerButton::VerbDelete:
            case ControllerButton::VerbPanic:
                return { kVerbInactive, kVerbClearActive, kVerbClearAccent };

            // --- Section keys (canonical TRIG/SRC/FILTER/AMP/MOD/FX) ---
            case ControllerButton::Section:
                // baseColour distinguishes special visual modes
                if (c.baseColour == kScopeMachine)
                    return { kSecInactive, kScopeMachine, kScopeMachine };
                if (c.baseColour == kScopeNoteEdit)
                    return { kSecInactive, kScopeNoteEdit, kScopeNoteEdit };
                if (c.baseColour == 0xFF404010u)            // master-active golden
                    return { kSecInactive, 0xFF404010u, 0xFFFFB432u };
                return { kSecInactive, kSecActive, kSecAccent };

            case ControllerButton::Step: {
                // Step-grid cells: table drives base fill/accent; baseColour
                // wins when the model has computed a specific override (e.g. scope
                // tint blended by SurfaceModel — already handled above). Use the
                // table fill dimmed for the inactive face, full for active.
                const auto ap = appearanceOf(c.base);
                const uint32_t fill = (ap.screenFill != 0u) ? ap.screenFill : c.baseColour;
                const uint32_t accent = (ap.screenAccent != 0u) ? ap.screenAccent : fill;
                return { juce::Colour(fill).withMultipliedBrightness(0.55f).getARGB(),
                         fill, accent };
            }

            default:
                // Fallback: derive inactive from baseColour (for unknown future button types).
                return { juce::Colour(c.baseColour).withMultipliedBrightness(0.25f).getARGB(),
                         c.baseColour, c.baseColour };
        }
    }


    // Single source of truth for a key cell's state and resolved background fill.
    // Both the on-screen paint path (paintCell/paintKeyButton) and external
    // surfaces (cellFillColour, used by the Push) funnel through these, so the
    // fill colour cannot diverge between renderers.
    static KeyButtonState stateOf(const SurfaceCell& c) noexcept
    {
        if (c.pressed) return KeyButtonState::Pressed;
        if (c.disabled) return KeyButtonState::Disabled;
        if (c.base == CellState::ModeActive || c.base == CellState::MorphPoleActive) return KeyButtonState::ModeActive;
        return KeyButtonState::Normal;
    }

    static juce::Colour resolveFill(const KeyGroup& g, KeyButtonState st) noexcept
    {
        const bool active = (st == KeyButtonState::Pressed || st == KeyButtonState::ModeActive);
        juce::Colour bg = active ? juce::Colour(g.active) : juce::Colour(g.inactive);
        if (st == KeyButtonState::Disabled)
            bg = bg.withAlpha(0.2f);
        return bg;
    }

    void paintCellKeyHint(juce::Graphics& g, juce::Rectangle<int> inner,
                          const juce::String& hint, float alpha)
    {
        if (hint.isEmpty()) return;
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour(0xFFAAC4D8u).withAlpha(alpha));
        g.drawText(hint, inner.withHeight(14).reduced(2, 0), juce::Justification::topLeft);
    }

    void paintKeyButton(juce::Graphics& g,
                        juce::Rectangle<int> cell,
                        const juce::String& keyHint,
                        const juce::String& primary,
                        const juce::String& secondary,
                        const KeyGroup& group,
                        KeyButtonState state,
                        bool showKeyHint,
                        bool compoundOverlay,
                        juce::Colour latchColour)
    {
        // MHZ.1.1: reduced inner margin (was 2,2) to use more of the cell area.
        const auto inner = cell.reduced(1, 1);

        const bool isPressed = (state == KeyButtonState::Pressed);
        const bool isModeActive = (state == KeyButtonState::ModeActive);
        const bool isFuncHeld = (state == KeyButtonState::FuncHeld);
        const bool isDisabled = (state == KeyButtonState::Disabled);

        // Background — shared resolver (same one external surfaces use).
        g.setColour(resolveFill(group, state));
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
        const int secH = hasSec ? 14 : 0;
        const auto primArea = inner.withTrimmedBottom(secH);
        const auto secArea = inner.withTrimmedTop(inner.getHeight() - secH).reduced(2, 0);

        // Primary label — ghost-dim when Disabled; dims to 0.35 when FuncHeld.
        // MHZ.1.1: primary font grown from 10pt to 15pt.
        // MHZ.1.2: labels up to 6 chars fit at 15pt; over-6 falls back to
        //          auto-fit (juce truncation disabled, JUCE clips).
        if (primary.isNotEmpty())
        {
            const float alpha = isDisabled                   ? 0.28f
                                : (isFuncHeld && !isPressed) ? 0.35f
                                                             : 1.0f;
            const juce::Colour primCol = isPressed
                                             ? juce::Colours::white
                                             : juce::Colour(0xFFB8D0E0u).withAlpha(alpha);

            // Choose font size: 15pt for ≤ 6 chars, auto-shrink for longer labels.
            const float fontSize = (primary.length() <= 6)   ? 15.0f
                                   : (primary.length() <= 8) ? 11.0f
                                                             : 9.0f;
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
            g.setColour(juce::Colour(kAmberStrip));
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

    // ── Tiny gesture glyph helpers ────────────────────────────────────────────
    // Drawn with a simple vector shape; must be ASCII-safe (no Unicode literals).

    // Tap glyph: one filled dot ~4px
    static void paintTapGlyph(juce::Graphics& g, int x, int y) noexcept
    {
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), 4.0f, 4.0f));
    }

    // Double-tap glyph: two filled dots side-by-side
    static void paintDoubleTapGlyph(juce::Graphics& g, int x, int y) noexcept
    {
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x),     static_cast<float>(y), 3.5f, 3.5f));
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x + 5), static_cast<float>(y), 3.5f, 3.5f));
    }

    // Hold glyph: hollow ring ~6px
    static void paintHoldGlyph(juce::Graphics& g, int x, int y) noexcept
    {
        g.drawEllipse(juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), 5.0f, 5.0f), 1.0f);
    }

    // ── 5-slot cell renderer (9.12 / DESIGN §19) ─────────────────────────────
    // Draws a gesture-affordance slot: glyph + label in small hint text.
    // `glyphType`: 0=tap, 1=dblTap, 2=hold, 3=func (amber chip)
    static void paintAffordanceSlot(juce::Graphics& g, juce::Rectangle<int> area,
                                    const juce::String& label, int glyphType,
                                    float alpha) noexcept
    {
        if (label.isEmpty()) return;
        const juce::Colour slotCol = (glyphType == 3)
            ? juce::Colour(theme::kFuncAccent).withAlpha(alpha)
            : juce::Colour(0xFF8EA4B8u).withAlpha(alpha);
        g.setColour(slotCol);
        g.setFont(juce::Font(juce::FontOptions(8.0f)));

        const int glyphW = (glyphType == 1) ? 12 : 8; // dblTap glyph wider
        const int textX = area.getX() + glyphW + 2;
        const auto textArea = area.withX(textX).withWidth(area.getWidth() - glyphW - 2);
        g.drawText(label, textArea, juce::Justification::centredLeft, true);

        const int glyphY = area.getCentreY() - 2;
        if (glyphType == 0)      paintTapGlyph(g, area.getX(), glyphY);
        else if (glyphType == 1) paintDoubleTapGlyph(g, area.getX(), glyphY);
        else if (glyphType == 2) paintHoldGlyph(g, area.getX(), glyphY);
        else {
            // func chip: small filled amber rectangle
            g.fillRect(juce::Rectangle<float>(
                static_cast<float>(area.getX()), static_cast<float>(glyphY),
                5.0f, 4.0f));
        }
    }

    void paintCell(juce::Graphics& g, juce::Rectangle<int> cell,
                   const SurfaceCell& c, bool showKeyHint)
    {
        const KeyButtonState st = stateOf(c);
        const KeyGroup grp = groupForCell(c);
        const bool compound = c.strip.present;
        const juce::Colour latchCol = c.pip.present
                                          ? juce::Colour(c.pip.colour)
                                          : juce::Colours::transparentBlack;

        const bool hasDblTap  = c.doubleTapLabel.isNotEmpty();
        const bool hasTapSlot = (c.primaryGesture != Gesture::Tap)  && c.tapLabel.isNotEmpty();
        const bool hasHoldSlot= (c.primaryGesture != Gesture::Hold) && c.holdLabel.isNotEmpty();
        const bool hasFuncSlot= c.funcHint.isNotEmpty();
        const bool hasAnySlot = hasDblTap || hasTapSlot || hasHoldSlot || hasFuncSlot;

        if (!hasAnySlot)
        {
            // Fast path: no affordances — render exactly as before.
            paintKeyButton(g, cell, c.keyHint, c.primary, c.funcHint,
                           grp, st, showKeyHint, compound, latchCol);
            return;
        }

        // Affordance path: paint structure (background, border, keyHint, strips, pip)
        // via paintKeyButton with empty primary/secondary so it handles structure only;
        // then render primary + all 5 slots with fixed-reserved layout.
        paintKeyButton(g, cell, c.keyHint, {}, {},
                       grp, st, showKeyHint, compound, latchCol);

        if (st == KeyButtonState::Disabled) return;

        const auto inner = cell.reduced(1, 1);
        const bool isPressed  = (st == KeyButtonState::Pressed);
        const bool isFuncHeld = (st == KeyButtonState::FuncHeld);

        // Fixed-reserved heights (9.12): ALL slots always allocated.
        // Primary locks to the same centre band across all keys in this family.
        static constexpr int kSlotH = 11;
        static constexpr int kFuncH = 14;

        const int keyHintH   = showKeyHint ? 14 : 0;
        const int overheadTop = keyHintH + kSlotH + kSlotH; // dbl + tap (always reserved)
        const int overheadBot = kSlotH + kFuncH;            // hold + func (always reserved)
        const int primY = inner.getY() + overheadTop;
        const int primH = juce::jmax(10, inner.getHeight() - overheadTop - overheadBot);
        const auto primArea = juce::Rectangle<int>(inner.getX(), primY, inner.getWidth(), primH);

        // Primary label
        if (c.primary.isNotEmpty())
        {
            const float alpha = isFuncHeld && !isPressed ? 0.35f : 1.0f;
            const juce::Colour primCol = isPressed
                                             ? juce::Colours::white
                                             : juce::Colour(0xFFB8D0E0u).withAlpha(alpha);
            const float fontSize = (c.primary.length() <= 6)   ? 15.0f
                                   : (c.primary.length() <= 8) ? 11.0f
                                                                : 9.0f;
            g.setFont(juce::Font(juce::FontOptions(fontSize)));
            g.setColour(primCol);
            g.drawText(c.primary, primArea, juce::Justification::centred, false);

            // Access glyph beside primary (indicates which gesture owns this slot).
            if (!isPressed)
            {
                g.setColour(juce::Colour(0xFF8EA4B8u).withAlpha(0.30f));
                const int gx = primArea.getRight() - 8;
                const int gy = primArea.getCentreY() - 2;
                if (c.primaryGesture == Gesture::Hold) paintHoldGlyph(g, gx, gy);
                else                                   paintTapGlyph(g, gx, gy);
            }
        }

        const float slotAlpha = 0.60f;

        // Double-tap slot: top (below keyHint). Blank space reserved even when empty.
        {
            const int y = inner.getY() + keyHintH;
            const auto area = juce::Rectangle<int>(inner.getX() + 2, y, inner.getWidth() - 4, kSlotH);
            if (hasDblTap) paintAffordanceSlot(g, area, c.doubleTapLabel, 1, slotAlpha);
        }

        // Tap slot: below dbl, above primary. Blank when tap is the primary gesture.
        {
            const int y = inner.getY() + keyHintH + kSlotH;
            const auto area = juce::Rectangle<int>(inner.getX() + 2, y, inner.getWidth() - 4, kSlotH);
            if (hasTapSlot) paintAffordanceSlot(g, area, c.tapLabel, 0, slotAlpha);
        }

        // Hold slot: below primary. Blank when hold is the primary gesture.
        {
            const int y = primY + primH;
            const auto area = juce::Rectangle<int>(inner.getX() + 2, y, inner.getWidth() - 4, kSlotH);
            if (hasHoldSlot) paintAffordanceSlot(g, area, c.holdLabel, 2, slotAlpha);
        }

        // Func slot: bottom strip. Amber glyph + funcHint text.
        {
            const int y = primY + primH + kSlotH;
            const auto area = juce::Rectangle<int>(inner.getX() + 2, y, inner.getWidth() - 4, kFuncH);
            const float alpha = isFuncHeld ? 1.0f : 0.5f;
            if (hasFuncSlot) paintAffordanceSlot(g, area, c.funcHint, 3, alpha);
        }
    }

    uint32_t cellFillColour(const SurfaceCell& c) noexcept
    {
        // Same resolver the on-screen paintCell/paintKeyButton use — guaranteed
        // identical fill for the on-screen key and the controller's LED.
        return resolveFill(groupForCell(c), stateOf(c)).getARGB();
    }

    void paintGridCellFill(juce::Graphics& g, juce::Rectangle<int> cell,
                           const SurfaceCell& c) noexcept
    {
        g.setColour(juce::Colour(c.baseColour));
        g.fillRoundedRectangle(cell.toFloat(), 4.0f);
    }

    void paintGridCellText(juce::Graphics& g, juce::Rectangle<int> cell,
                           const SurfaceCell& c, float textAlpha) noexcept
    {
        // Press feedback (standardized).
        if (c.pressed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.5f);
        }

        // Primary text from model, normalized to 9 pt.
        if (c.primary.isNotEmpty())
        {
            const float alpha = (textAlpha >= 0.0f) ? textAlpha
                : ((c.base == CellState::MachineCurrent
                 || c.base == CellState::EffectLoaded
                 || c.base == CellState::SelectorCurrent
                 || c.base == CellState::MorphPoleActive) ? 0.90f : 0.65f);
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(c.primary, cell.reduced(2), juce::Justification::centred, true);
        }
    }
}
