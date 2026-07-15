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
            // A Func border decoration overrides the accent so a Func+scope cell
            // reads scope-colour fill + Func-colour border (DESIGN §6.1.1).
            const uint32_t acc = c.border.present ? c.border.colour
                                                  : tint.brighter(0.35f).getARGB();
            return { tint.withMultipliedBrightness(0.55f).getARGB(),  // resting in-scope fill
                     tint.getARGB(),                                  // pressed/active fill
                     acc };                                           // border accent
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
            {
                // baseColour distinguishes special visual modes
                if (c.baseColour == kScopeMachine)
                    return { kSecInactive, kScopeMachine, kScopeMachine };
                if (c.baseColour == kScopeNoteEdit)
                    return { kSecInactive, kScopeNoteEdit, kScopeNoteEdit };
                if (c.baseColour == 0xFF404010u)            // master-active golden
                    return { kSecInactive, 0xFF404010u, 0xFFFFB432u };
                // A Func border decoration (§6.1.1) recolours only the border,
                // leaving the neutral section fill — the Func-stack marker.
                const uint32_t acc = c.border.present ? c.border.colour : kSecAccent;
                return { kSecInactive, kSecActive, acc };
            }

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

    // Triple-tap glyph: three filled dots side-by-side (layered stop: MASTER CUT)
    static void paintTripleTapGlyph(juce::Graphics& g, int x, int y) noexcept
    {
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x),     static_cast<float>(y), 3.5f, 3.5f));
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x + 5), static_cast<float>(y), 3.5f, 3.5f));
        g.fillEllipse(juce::Rectangle<float>(static_cast<float>(x + 10),static_cast<float>(y), 3.5f, 3.5f));
    }

    // Hold glyph: hollow ring ~6px
    static void paintHoldGlyph(juce::Graphics& g, int x, int y) noexcept
    {
        g.drawEllipse(juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), 5.0f, 5.0f), 1.0f);
    }

    // Rendered width of a gesture glyph, for right-justified placement.
    // `glyphType`: 0=tap, 1=dblTap, 2=hold, 3=func (amber chip), 4=tripleTap
    static int glyphWidthFor(int glyphType) noexcept
    {
        if (glyphType == 4) return 14; // tripleTap is three dots
        return (glyphType == 1) ? 9 : 5; // dblTap is two dots; others ~5px
    }

    // Draws one gesture glyph at (x, y) — colour must already be set by caller.
    static void paintGlyph(juce::Graphics& g, int glyphType, int x, int y) noexcept
    {
        if (glyphType == 0)      paintTapGlyph(g, x, y);
        else if (glyphType == 1) paintDoubleTapGlyph(g, x, y);
        else if (glyphType == 2) paintHoldGlyph(g, x, y);
        else if (glyphType == 4) paintTripleTapGlyph(g, x, y);
        else // func chip: small filled rectangle
            g.fillRect(juce::Rectangle<float>(
                static_cast<float>(x), static_cast<float>(y), 5.0f, 4.0f));
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

        // Disabled cells keep the legacy ghosted primary/secondary look — no
        // affordance bands are drawn for an inert key.
        if (st == KeyButtonState::Disabled)
        {
            paintKeyButton(g, cell, c.keyHint, c.primary, c.funcHint,
                           grp, st, showKeyHint, compound, latchCol);
            return;
        }

        // Structure first (background, border, keyHint letter, compound strip, pip)
        // via paintKeyButton with empty primary/secondary; the fixed-uniform band
        // layout below draws the primary + affordances. Every key uses the same
        // bands so the primary locks to one vertical position across the surface.
        paintKeyButton(g, cell, c.keyHint, {}, {},
                       grp, st, showKeyHint, compound, latchCol);

        // 9.4 item G: checkpoint mark count, drawn just right of the bottom-left pip.
        // The scope key shows how deep its OWN mark stack is (DESIGN §13.6); undo is not
        // a mark and is named in the status lane instead.
        if (c.markDepth > 0)
        {
            const auto pinner = cell.reduced(1, 1);
            g.setColour(c.pip.present ? juce::Colour(c.pip.colour) : juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.drawText(juce::String(c.markDepth),
                       juce::Rectangle<int>(pinner.getX() + 8, pinner.getBottom() - 12, 16, 10),
                       juce::Justification::bottomLeft, false);
        }

        if (c.primary.isEmpty() && c.doubleTapLabel.isEmpty()
            && c.tapLabel.isEmpty() && c.holdLabel.isEmpty() && c.funcHint.isEmpty()
            && c.tripleTapLabel.isEmpty())
            return;  // nothing to render

        const auto inner = cell.reduced(1, 1);
        const bool isPressed  = (st == KeyButtonState::Pressed);
        const bool isFuncHeld = (st == KeyButtonState::FuncHeld);

        // ── Five-slot anchored stack (9.14 / DESIGN §19) ──────────────────────────
        // Fixed render order, top→bottom: double-tap, single-tap, PRIMARY (large),
        // hold, func. Every slot reserves its space even when empty, so the large
        // PRIMARY row locks to the same vertical position on every key (the §19
        // scan invariant). Compact fonts free a few px of slack, distributed as
        // equal gaps (top margin == inter-row gap == bottom margin).
        //   • action text  : left-justified, all rows share one left edge `x`,
        //                     which centres the *widest* line within the cell.
        //   • secondary glyph: right-justified at the cell edge.
        //   • PRIMARY glyph : in the left gutter, under the key-title letter.
        // A triple-tap affordance (Play: MASTER CUT) surrenders the key's single-tap
        // secondary slot so both cut depths show without shifting the PRIMARY row — the
        // §19 primary-lock is preserved (still five fixed slots). Only keys with no
        // single-tap secondary (Play's single tap IS the primary) ever set tripleTap:
        // the top slot becomes MASTER CUT (three dots), the second becomes CUT (two).
        const bool hasTriple = c.tripleTapLabel.isNotEmpty();
        const juce::String* const tripLabels[5] =
            { &c.tripleTapLabel, &c.doubleTapLabel, &c.primary, &c.holdLabel, &c.funcHint };
        const juce::String* const normLabels[5] =
            { &c.doubleTapLabel, &c.tapLabel,       &c.primary, &c.holdLabel, &c.funcHint };
        const int tripGlyphs[5] = { 4 /*tripleTap*/, 1 /*dblTap*/, -1, 2 /*hold*/, 3 /*func*/ };
        const int normGlyphs[5] = { 1 /*dblTap*/,    0 /*tap*/,    -1, 2 /*hold*/, 3 /*func*/ };
        const juce::String* const* const labels = hasTriple ? tripLabels : normLabels;
        const int* const glyphs = hasTriple ? tripGlyphs : normGlyphs;

        const float primFont = (c.primary.length() <= 6)   ? 13.0f
                               : (c.primary.length() <= 8) ? 11.0f
                                                           : 9.0f;
        constexpr float kSecFont = 7.0f;

        // Slot heights (compact). Primary is taller; the QWERTY letter is drawn
        // separately by paintCellKeyHint and may overhang the top slot's gutter.
        constexpr int kSecSlotH = 9;
        constexpr int kPrimSlotH = 14;
        const int heights[5] = { kSecSlotH, kSecSlotH, kPrimSlotH, kSecSlotH, kSecSlotH };
        const int sumH = 4 * kSecSlotH + kPrimSlotH;
        const int gap = juce::jmax(0, (inner.getHeight() - sumH) / 6);

        int slotY[5];
        int yCursor = inner.getY() + gap;
        for (int i = 0; i < 5; ++i) { slotY[i] = yCursor; yCursor += heights[i] + gap; }

        // Shared text origin `x`: centre the widest visible line within the band
        // between the left gutter (key-title + primary glyph) and the right glyph
        // column, then left-justify every line to it.
        constexpr int kGutterW   = 15;
        constexpr int kGlyphColW = 12;
        constexpr int kPadR      = 2;
        const int availLeft  = inner.getX() + kGutterW;
        const int availRight = inner.getRight() - kGlyphColW;
        float maxW = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            if (labels[i]->isEmpty()) continue;
            const float fs = (i == 2) ? primFont : kSecFont;
            maxW = juce::jmax(maxW,
                              juce::GlyphArrangement::getStringWidth(
                                  juce::Font(juce::FontOptions(fs)), *labels[i]));
        }
        const int availW = juce::jmax(0, availRight - availLeft);
        const int x = availLeft
                      + juce::jmax(0, static_cast<int>((static_cast<float>(availW) - maxW) * 0.5f));

        for (int i = 0; i < 5; ++i)
        {
            const juce::String& label = *labels[i];
            if (label.isEmpty()) continue;

            const bool isPrim = (i == 2);
            const auto slot = juce::Rectangle<int>(inner.getX(), slotY[i],
                                                   inner.getWidth(), heights[i]);

            // Text colour + font.
            if (isPrim)
            {
                const float alpha = (isFuncHeld && !isPressed) ? 0.35f : 1.0f;
                g.setColour(isPressed ? juce::Colours::white
                                      : juce::Colour(0xFFB8D0E0u).withAlpha(alpha));
                g.setFont(juce::Font(juce::FontOptions(primFont)));
            }
            else if (glyphs[i] == 3) // func
            {
                g.setColour(juce::Colour(theme::kFuncAccent).withAlpha(isFuncHeld ? 1.0f : 0.55f));
                g.setFont(juce::Font(juce::FontOptions(kSecFont)));
            }
            else
            {
                g.setColour(juce::Colour(0xFF8EA4B8u).withAlpha(0.60f));
                g.setFont(juce::Font(juce::FontOptions(kSecFont)));
            }

            const auto textArea = juce::Rectangle<int>(x, slotY[i],
                                                       juce::jmax(0, availRight - x), heights[i]);
            g.drawText(label, textArea, juce::Justification::centredLeft, false);

            // Glyph.
            const int gy = slot.getCentreY() - 2;
            if (isPrim)
            {
                // Access glyph in the left gutter (under the key title): hold ring
                // = "hold to engage", tap dot = tap key. The tap / no-tap signal.
                if (!isPressed)
                {
                    g.setColour(juce::Colour(0xFF8EA4B8u).withAlpha(0.60f));
                    const int gx = inner.getX() + 4;
                    if (c.primaryGesture == Gesture::Hold) paintHoldGlyph(g, gx, gy);
                    else                                   paintTapGlyph(g, gx, gy);
                }
            }
            else
            {
                const int gw = glyphWidthFor(glyphs[i]);
                const int gx = inner.getRight() - kPadR - gw;
                if (glyphs[i] == 3)
                    g.setColour(juce::Colour(theme::kFuncAccent).withAlpha(isFuncHeld ? 1.0f : 0.55f));
                else
                    g.setColour(juce::Colour(0xFF8EA4B8u).withAlpha(0.60f));
                paintGlyph(g, glyphs[i], gx, gy);
            }
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
