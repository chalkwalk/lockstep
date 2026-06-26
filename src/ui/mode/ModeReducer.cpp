#include "ModeReducer.h"

#include <array>

namespace lockstep
{
    // =========================================================================
    // Relabel helpers — dynamic primary text for internal section keys.
    // =========================================================================

    static const char* densitySubPageLabel(const UiState& ui) noexcept
    {
        using SP = UiState::DensitySubPage;
        switch (ui.densitySubPage)
        {
            case SP::Amount:     return "MUSIC";
            case SP::Musicality: return "SELECT";
            case SP::Selection:  return "AMOUNT";
        }
        return "AMOUNT";
    }

    static const char* velSubPageLabel(const UiState& ui) noexcept
    {
        using VP = UiState::VelSubPage;
        switch (ui.velSubPage)
        {
            case VP::Depth:  return "CENTER";
            case VP::Center: return "MODE";
            case VP::Mode:   return "BLEND";
            case VP::Blend:  return "DEPTH";
        }
        return "DEPTH";
    }

    // The signatures band's TRIG key shows the DESTINATION of the next press —
    // on the TIME page it reads "KEY", on the KEY page it reads "TIME" — so the
    // affordance tells you where pressing again goes (the MZ header shows where
    // you currently are).
    static const char* timeLabel(const UiState& ui) noexcept
    {
        return ui.sigPage == UiState::SigPage::Key ? "TIME" : "KEY";
    }

    // =========================================================================
    // Overlay descriptor table.
    //
    // Each entry fully specifies exit/consume/relabel policy via aggregate init.
    // Adding a field to OverlayDescriptor makes every entry fail to compile
    // until it supplies the new value — "forgot to wire X" becomes structurally
    // impossible.
    // =========================================================================
    // NOLINTNEXTLINE(cert-err58-cpp)
    static const std::array<OverlayDescriptor, 4> kOverlays = {{
        // ── Density ─────────────────────────────────────────────────────────
        // MOD (index 4) cycles sub-pages; Song is "own" (master write path).
        {
            .id                      = Overlay::Density,
            .internalSection         = 4,
            .internalSectionConsumed = true,
            .internalSectionRelabelFn = densitySubPageLabel,
            .exitOnSectionPressOther  = true,
            .trackScopeForeign  = true,
            .phraseScopeForeign = true,
            .sceneScopeForeign  = true,
            .morphScopeForeign  = true,
            .muteScopeForeign   = true,
            .fillScopeForeign   = true,
            .songScopeForeign   = false,  // Song = master write target, not exit
            .exitOnDoubleTapFunc = true,
        },
        // ── Vel ─────────────────────────────────────────────────────────────
        // AMP (index 3) cycles sub-pages; Song is "own".
        {
            .id                      = Overlay::Vel,
            .internalSection         = 3,
            .internalSectionConsumed = true,
            .internalSectionRelabelFn = velSubPageLabel,
            .exitOnSectionPressOther  = true,
            .trackScopeForeign  = true,
            .phraseScopeForeign = true,
            .sceneScopeForeign  = true,
            .morphScopeForeign  = true,
            .muteScopeForeign   = true,
            .fillScopeForeign   = true,
            .songScopeForeign   = false,  // Song = "own"
            .exitOnDoubleTapFunc = true,
        },
        // ── Time ────────────────────────────────────────────────────────────
        // TRIG (index 0) is the entry chord; Song + Scene retarget the scope.
        {
            .id                      = Overlay::Time,
            .internalSection         = 0,
            .internalSectionConsumed = true,   // re-press TRIG cycles TIME <-> KEY
            .internalSectionRelabelFn = timeLabel,
            .exitOnSectionPressOther  = true,
            .trackScopeForeign  = true,
            .phraseScopeForeign = true,
            .sceneScopeForeign  = false,  // Scene retargets TIME scope (timeScopeFor)
            .morphScopeForeign  = true,
            .muteScopeForeign   = true,
            .fillScopeForeign   = true,
            .songScopeForeign   = false,  // Song retargets TIME scope (Func+Song=Set, Song=Song)
            .exitOnDoubleTapFunc = true,
        },
        // ── Euclid ──────────────────────────────────────────────────────────
        // Only Func double-tap exits; entry/stash restore is editor-owned.
        {
            .id                      = Overlay::Euclid,
            .internalSection         = -1,
            .internalSectionConsumed = false,
            .internalSectionRelabelFn = nullptr,
            .exitOnSectionPressOther  = false,  // euclid uses the step grid, not sections
            .trackScopeForeign  = false,
            .phraseScopeForeign = false,
            .sceneScopeForeign  = false,
            .morphScopeForeign  = false,
            .muteScopeForeign   = false,
            .fillScopeForeign   = false,
            .songScopeForeign   = false,
            .exitOnDoubleTapFunc = true,
        },
    }};

    // =========================================================================
    // Internal helpers
    // =========================================================================

    static const OverlayDescriptor* findDescriptor(Overlay ov) noexcept
    {
        for (const auto& d : kOverlays)
        {
            if (d.id == ov) return &d;
        }
        return nullptr;
    }

    static bool isScopeForeign(const OverlayDescriptor& d, ControllerButton btn) noexcept
    {
        using CB = ControllerButton;
        switch (btn)
        {
            case CB::TrackScope:  return d.trackScopeForeign;
            case CB::PhraseScope: return d.phraseScopeForeign;
            case CB::SceneScope:  return d.sceneScopeForeign;
            case CB::MorphScope:  return d.morphScopeForeign;
            case CB::MuteScope:   return d.muteScopeForeign;
            case CB::FillScope:   return d.fillScopeForeign;
            case CB::SongScope:   return d.songScopeForeign;
            default:              return false;
        }
    }

    static void cycleSubPage(UiState& ui, Overlay active, const ScopeCtx& ctx) noexcept
    {
        switch (active)
        {
            case Overlay::Density:
            {
                using SP = UiState::DensitySubPage;
                if (ui.densitySubPage == SP::Amount)
                    ui.densitySubPage = SP::Musicality;
                else if (ui.densitySubPage == SP::Musicality)
                    ui.densitySubPage = SP::Selection;
                else
                    ui.densitySubPage = SP::Amount;
                break;
            }
            case Overlay::Vel:
            {
                using VP = UiState::VelSubPage;
                if (!ctx.velAnyEnabled) { ui.velSubPage = VP::Mode; break; }
                switch (ui.velSubPage)
                {
                    case VP::Depth:  ui.velSubPage = VP::Center; break;
                    case VP::Center: ui.velSubPage = VP::Mode;   break;
                    case VP::Mode:   ui.velSubPage = VP::Blend;  break;
                    case VP::Blend:  ui.velSubPage = VP::Depth;  break;
                }
                break;
            }
            case Overlay::Time:
                // The signatures band: re-press TRIG cycles TIME <-> KEY.
                ui.sigPage = (ui.sigPage == UiState::SigPage::Time)
                                 ? UiState::SigPage::Key
                                 : UiState::SigPage::Time;
                break;
            default:
                break;
        }
    }

    // =========================================================================
    // Public API
    // =========================================================================

    Overlay activeOverlay(const UiState& ui) noexcept
    {
        if (ui.euclidHeld) { return Overlay::Euclid; }
        return ui.overlay;
    }

    void escapeOverlay(UiState& ui, Overlay ov) noexcept
    {
        switch (ov)
        {
            case Overlay::Density:
                if (ui.overlay == Overlay::Density)
                {
                    ui.overlay = Overlay::None;
                    ui.densityBank = 0;
                    ui.densitySubPage = UiState::DensitySubPage::Amount;
                }
                break;
            case Overlay::Vel:
                if (ui.overlay == Overlay::Vel)
                {
                    ui.overlay = Overlay::None;
                    ui.velBank = 0;
                    ui.velSubPage = UiState::VelSubPage::Depth;
                }
                break;
            case Overlay::Time:
                if (ui.overlay == Overlay::Time)
                {
                    ui.overlay = Overlay::None;
                    ui.sigPage = UiState::SigPage::Time;
                    ui.swingDismissed = true;
                }
                break;
            case Overlay::Euclid:
                ui.resetEuclid();
                break;
            case Overlay::None:
                break;
        }
    }

    OverlayResult handleOverlayEvent(UiState& ui,
                                     const ModeEvent& ev,
                                     const ScopeCtx& ctx) noexcept
    {
        const Overlay active = activeOverlay(ui);
        if (active == Overlay::None)
            return OverlayResult::NotConsumed;

        const OverlayDescriptor* desc = findDescriptor(active);
        if (desc == nullptr)
            return OverlayResult::NotConsumed;

        switch (ev.kind)
        {
            case ModeEventKind::SectionPress:
            {
                if (desc->internalSection == ev.index)
                {
                    if (desc->internalSectionConsumed)
                    {
                        cycleSubPage(ui, active, ctx);
                        return OverlayResult::Consumed;
                    }
                    return OverlayResult::NotConsumed;
                }
                if (desc->exitOnSectionPressOther)
                {
                    escapeOverlay(ui, active);
                    return OverlayResult::Exited;
                }
                return OverlayResult::NotConsumed;
            }

            case ModeEventKind::ScopePress:
            {
                if (isScopeForeign(*desc, ev.button))
                {
                    escapeOverlay(ui, active);
                    return OverlayResult::Exited;
                }
                return OverlayResult::NotConsumed;
            }

            case ModeEventKind::DoubleTapFunc:
            {
                if (desc->exitOnDoubleTapFunc)
                {
                    escapeOverlay(ui, active);
                    return OverlayResult::Exited;
                }
                return OverlayResult::NotConsumed;
            }
        }

        return OverlayResult::NotConsumed;
    }

    const char* overlayInternalSectionLabel(const UiState& ui, int sectionIdx) noexcept
    {
        const Overlay active = activeOverlay(ui);
        if (active == Overlay::None)
            return nullptr;

        const OverlayDescriptor* desc = findDescriptor(active);
        if (desc == nullptr || desc->internalSection != sectionIdx)
            return nullptr;

        if (desc->internalSectionRelabelFn == nullptr)
            return nullptr;

        return desc->internalSectionRelabelFn(ui);
    }
}
