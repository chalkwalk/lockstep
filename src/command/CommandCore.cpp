#include "CommandCore.h"
#include "VerbCommands.h"
#include "StatusText.h"

namespace lockstep
{
    bool CommandCore::handleDown(const ControllerEvent& ev,
                                 CommandContext& ctx,
                                 CommandEffects& fx)
    {
        using CB = ControllerButton;
        using TA = CommandEffects::TransportAction;

        // ── Pending-confirm intercept ─────────────────────────────────────────
        // Sticky: releasing the arming chord never cancels. Any NEW press other
        // than Func cancels (= "No", status "Cancelled", event swallowed).
        // Yes = P with Func up; No = P with Func held. Func itself is exempt.
        if (ctx.uiState.confirm.pending())
        {
            if (ev.button == CB::Func)
                return false;  // Func never cancels — user may need it to reach No

            if (ev.button == CB::VerbNo)
            {
                const bool funcDown = ctx.uiState.funcHeld;
                if (!funcDown)
                    fx.executeConfirm(ctx.uiState.confirm.kind, ctx.uiState.confirm.target);
                else
                    fx.status(status::cancelled());
                ctx.uiState.confirm.reset();
                fx.requestRepaint();
                return true;
            }

            // Any other new press = No (cancel; event swallowed).
            ctx.uiState.confirm.reset();
            fx.status(status::cancelled());
            fx.requestRepaint();
            return true;
        }

        // ── Delete picker intercept ───────────────────────────────────────────
        // Sticky: releasing the arming chord doesn't exit. Func exempt.
        // Step or SelectTrack: validate, transition to named confirm.
        // Any other key: cancel.
        if (ctx.uiState.deletePicker.active())
        {
            if (ev.button == CB::Func)
                return false;

            const DeleteScope scope = ctx.uiState.deletePicker.scope;
            if (ev.button == CB::Step || ev.button == CB::SelectTrack)
            {
                const int idx = ev.index;
                if (idx >= 0)
                {
                    ConfirmKind kind = ConfirmKind::None;
                    juce::String entityName;
                    if (scope == DeleteScope::Track && ev.button == CB::SelectTrack)
                    {
                        kind = ConfirmKind::DeleteTrack;
                        entityName = "TRACK";
                    }
                    else if (scope == DeleteScope::Phrase && ev.button == CB::Step)
                    {
                        kind = ConfirmKind::DeletePhrase;
                        entityName = "PHRASE";
                    }
                    else if (scope == DeleteScope::Scene && ev.button == CB::Step)
                    {
                        kind = ConfirmKind::DeleteScene;
                        entityName = "SCENE";
                    }

                    if (kind != ConfirmKind::None)
                    {
                        ctx.uiState.deletePicker.reset();
                        ctx.uiState.confirm = { kind, idx };
                        fx.status(status::confirmDeleteNamed(entityName, idx + 1));
                        fx.requestRepaint();
                        return true;
                    }
                }
            }

            // Foreign key or out-of-range: cancel picker.
            ctx.uiState.deletePicker.reset();
            fx.status(status::cancelled());
            fx.requestRepaint();
            return true;
        }

        switch (ev.button)
        {
            case CB::PlayStop:
                fx.transport(TA::Play);
                return true;
            case CB::MetronomeToggle:
                fx.transport(TA::Metronome);
                return true;
            case CB::VerbPanic:
                fx.transport(TA::Panic);
                return true;
            case CB::TapTempo:
                fx.transport(TA::TapTempo);
                return true;

            case CB::VerbDelete: {
                using PS = EditMode::PrimaryScope;
                const PS scope = ctx.editMode.primaryScope();
                if (scope == PS::Morph)
                {
                    fx.morphErase(ctx.uiState.activeTrack);
                    fx.status(status::morphErased());
                    fx.requestRepaint();
                    return true;
                }
                juce::String entityName;
                DeleteScope delScope = DeleteScope::None;
                if (scope == PS::Track)
                {
                    delScope = DeleteScope::Track;
                    entityName = "TRACK";
                }
                else if (scope == PS::Phrase)
                {
                    delScope = DeleteScope::Phrase;
                    entityName = "PHRASE";
                }
                else if (scope == PS::Scene)
                {
                    delScope = DeleteScope::Scene;
                    entityName = "SCENE";
                }
                else return false;  // no picker for this scope (e.g. Song)

                ctx.uiState.deletePicker.scope = delScope;
                fx.status(status::deleteWhich(entityName));
                fx.requestRepaint();
                return true;
            }

            default:
                return false;
        }
    }

    bool CommandCore::handleUp(const ControllerEvent& ev,
                               CommandContext& ctx,
                               CommandEffects& fx)
    {
        using CB = ControllerButton;
        using T = ControllerEvent::Type;

        // Common scope-release helper: clears the held flag and fires the scope
        // event only if the modifier is not latched.  Returns false so the caller
        // can still apply per-scope unique side effects (physHeld_, updateSwingQualifier,
        // etc.).  Callers check !heldFlag after this returns to know if the release
        // took effect.
        auto releaseScope = [&](bool& latchFlag, bool& heldFlag) -> bool {
            if (latchFlag) return false;    // still latched — held flag stays
            heldFlag = false;
            ctx.editMode.onScopeEvent({ T::ButtonUp, ev.button });
            fx.requestRepaint();
            return false;  // partial: caller adds unique side effects
        };

        switch (ev.button)
        {
            case CB::TrackScope:  return releaseScope(ctx.uiState.latch.track, ctx.uiState.trackHeld);
            case CB::PhraseScope: return releaseScope(ctx.uiState.latch.phrase, ctx.uiState.phraseScopeHeld);
            case CB::SceneScope:  return releaseScope(ctx.uiState.latch.scene, ctx.uiState.sceneHeld);
            case CB::MuteScope:   return releaseScope(ctx.uiState.latch.mute, ctx.uiState.muteHeld);
            case CB::FillScope:   return releaseScope(ctx.uiState.latch.fill, ctx.uiState.fillHeld);
            case CB::MorphScope:  return releaseScope(ctx.uiState.latch.morph, ctx.uiState.morphHeld);
            case CB::SongScope:   return releaseScope(ctx.uiState.latch.song, ctx.uiState.songHeld);
            case CB::CueScope:    // not latchable: always release
                ctx.uiState.cueHeld = false;
                ctx.editMode.onScopeEvent({ T::ButtonUp, ev.button });
                fx.requestRepaint();
                return false;  // caller can still handle unique side effects
            default: return false;
        }
    }

    bool CommandCore::handleAction(ActionId action,
                                   const ControllerEvent& ev,
                                   CommandContext&,
                                   CommandEffects& fx)
    {
        using AId = ActionId;
        switch (action)
        {
            case AId::GlobalMuteToggle: fx.globalMuteToggle(ev.index); return true;
            case AId::SoloToggle:       fx.soloToggle(ev.index); return true;
            case AId::SceneMuteToggle:  fx.sceneMuteToggle(ev.index); return true;
            case AId::FluidMuteToggle:  fx.fluidMuteToggle(ev.index); return true;
            default:                    return false;
        }
    }

    bool CommandCore::handleVerb(EditMode::PrimaryScope scope,
                                 ControllerButton verb,
                                 CommandContext& ctx,
                                 CommandEffects& fx)
    {
        using PS = EditMode::PrimaryScope;
        switch (scope)
        {
            case PS::Trig:    return verbs::trig(verb, ctx, fx);
            case PS::Track:   return verbs::track(verb, ctx, fx);
            case PS::Phrase:  return verbs::phrase(verb, ctx, fx);
            case PS::Scene:   return verbs::scene(verb, ctx, fx);
            case PS::Song:    return verbs::song(verb, ctx, fx);
            case PS::None:    return verbs::noScope(verb, ctx, fx);
            case PS::Morph:   return verbs::morph(verb, ctx, fx);
            case PS::Section: return verbs::section(verb, ctx, fx);
            default:          return false;
        }
    }
}
