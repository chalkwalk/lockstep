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
        // than Func cancels (= "Cancel", status "Cancelled", event swallowed).
        // Confirm = P with Func up; Cancel = P with Func held. Func itself is exempt.
        if (ctx.uiState.confirm.pending())
        {
            if (ev.button == CB::Func)
                return false;  // Func never cancels — user may need it to reach Cancel

            if (ev.button == CB::VerbConfirm)
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

            // Any other new press = Cancel (event swallowed).
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

            case CB::VerbDelete:
                return deleteVerb(ctx, fx);

            default:
                return false;
        }
    }

    // 9.29: the delete verb, one owner, two callers. It used to live inline in the
    // CB::VerbDelete case, which the Func layer produced by rewriting Clear. Delete is
    // now a HOLD on Clear under a scope (the Machine scope took Func+Track+Clear), so
    // it arrives as ActionId::VerbDelete from the table -- but CB::VerbDelete is still
    // a legal logical button a controller may send, and both must mean the same thing.
    bool CommandCore::deleteVerb(CommandContext& ctx, CommandEffects& fx)
    {
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
                                   CommandContext& ctx,
                                   CommandEffects& fx)
    {
        using AId = ActionId;
        using TA = CommandEffects::TransportAction;
        using CB = ControllerButton;
        switch (action)
        {
            case AId::GlobalMuteToggle: fx.globalMuteToggle(ev.index); return true;
            case AId::SoloToggle:       fx.soloToggle(ev.index); return true;
            case AId::SceneMuteToggle:  fx.sceneMuteToggle(ev.index); return true;
            case AId::FluidMuteToggle:  fx.fluidMuteToggle(ev.index); return true;
            case AId::ToggleCapture:    fx.toggleCapture(); return true;

            // ── 9.12 Stage 7b: the verb family ───────────────────────────────────
            // The table decides WHAT the key means here (Copy / Paste / ScopedClear /
            // BakeScene / MorphErase...); the held scope decides WHICH OPERAND, which
            // is what DESIGN §13 has always said ("the compound qualifies the scope;
            // it does not change what a verb means"). So every verb action delegates
            // to the one existing implementation, verbs::* via handleVerb -- routing
            // moves to the grammar, behaviour does NOT move, and there is no second
            // copy of a verb to drift from the first.
            case AId::VerbSnapshot:
            case AId::VerbRecord:
            case AId::VerbPlay:
            case AId::VerbClear:
            case AId::VerbCopy:
            case AId::VerbPaste:
            case AId::VerbScopedClear:
            case AId::VerbBakeScene:
            case AId::VerbMorphBake:
            case AId::VerbMorphErase:
                return handleVerb(ctx.editMode.primaryScope(), ev.button, ctx, fx);

            // Delete is the one verb that does NOT delegate by button: it is reached by
            // HOLDING Clear (9.29), so the button under the finger says "clear" while
            // the gesture says "delete". The action is the truth; ev.button is not.
            case AId::VerbDelete:
                return deleteVerb(ctx, fx);

            // ── 9.29: the Machine scope's verbs ──────────────────────────────────
            // The table already knows the operand (the row requires Func+Track), so
            // these do not consult primaryScope -- EditMode has no Machine value and
            // does not need one. The compound scope lives in the binding, which is
            // exactly where 9.12 says an operand belongs.
            // The picker is an overlay, so it goes through the overlay effect rather
            // than the editor poking UiState — which is what lets a controller open it
            // too (PRINCIPLES §19: one model, two surfaces).
            case AId::OpenMachinePicker:
                fx.openOverlay(CommandEffects::OverlayId::MachinePicker, 1);
                return true;

            // ── 9.12 Stage 7c: the nav family ────────────────────────────────────
            // The four nav keys carried an if-cascade each, re-deriving by hand the
            // same priority the table already encodes (Morph > Phrase > Track > Func).
            // Now the table decides and the editor supplies the effect. The +1/-1
            // convention is the whole reason the pairs cannot drift apart.
            case AId::NavTrackUp:        fx.navFocusTrack(+1); return true;
            case AId::NavTrackDown:      fx.navFocusTrack(-1); return true;
            case AId::NavPageRight:      fx.navPage(+1); return true;
            case AId::NavPageLeft:       fx.navPage(-1); return true;
            case AId::NavOctaveUp:       fx.navOctave(+1); return true;
            case AId::NavOctaveDown:     fx.navOctave(-1); return true;
            case AId::LengthDouble:      fx.trackLengthScale(+1); return true;
            case AId::LengthHalve:       fx.trackLengthScale(-1); return true;
            case AId::RotateRight:       fx.rotateSteps(+1); return true;
            case AId::RotateLeft:        fx.rotateSteps(-1); return true;
            case AId::CycleInputModeUp:     fx.cycleInputMode(+1); return true;
            case AId::CycleInputModeDown:   fx.cycleInputMode(-1); return true;
            case AId::MorphPickPoleA:    fx.morphPole(1); return true;
            case AId::MorphPickPoleB:    fx.morphPole(2); return true;

            // Transpose: bare = an octave, Func = one semitone. The QUALIFIER is read
            // here rather than split into four actions, because Func is doing what Func
            // always does — narrowing the same verb, not naming a different one.
            case AId::TransposeUp:
                fx.transposeTrack(ctx.uiState.funcHeld ? 1 : 12);
                return true;
            case AId::TransposeDown:
                fx.transposeTrack(ctx.uiState.funcHeld ? -1 : -12);
                return true;

            // ── 9.12 Stage 7e: confirm / quantize ────────────────────────────────
            // QUANT is the one verb on P that is not a confirm at all: it zeroes the
            // microOffsets of whatever the scope names (held steps, the track, or every
            // track under Phrase). The cascade lives in the effect, unchanged.
            case AId::QuantizeHeld:  fx.quantizeHeld(); return true;
            // Confirm / Cancel with nothing pending: the plain verb (a pending prompt is
            // intercepted by handleDown long before this, and never reaches an action).
            case AId::VerbConfirm:
            case AId::VerbCancel:
                return handleVerb(ctx.editMode.primaryScope(), CB::VerbConfirm, ctx, fx);

            // ── 9.12 Stage 7f: transport ─────────────────────────────────────────
            // Both already have a home in handleDown's button switch (a controller may
            // send them as buttons); wiring the ACTIONS points the QWERTY path at the
            // same effect, so the two surfaces cannot drift.
            case AId::TapTempo:        fx.transport(TA::TapTempo); return true;
            case AId::MetronomeToggle: fx.transport(TA::Metronome); return true;

            // ── 9.12 Stage 7d: the section family ────────────────────────────────
            case AId::SelectSection:      fx.selectSection(ev.index); return true;
            case AId::SelectMetaSection:  fx.selectMetaSection(ev.index); return true;
            case AId::OpenTrackFxPicker:  fx.openFxPicker(false); return true;
            case AId::OpenMasterFxPicker: fx.openFxPicker(true); return true;

            // 9.31: the MIXER is a latched meta page, not an overlay -- it survives
            // the modifier release like COND or DIVIDER, and a re-press of the same
            // chord toggles it away. selectMetaSection is the existing owner of that
            // latch, so the mixer needs no new modal state (and no new boolean).
            case AId::OpenMixer:
                fx.selectMetaBand(CommandEffects::MetaBandId::Mixer);
                return true;

            case AId::MachineCopy:  return verbs::machine(CB::VerbRecord, ctx, fx);
            case AId::MachinePaste: return verbs::machine(CB::VerbPlay, ctx, fx);
            case AId::MachineInit:  return verbs::machine(CB::VerbClear, ctx, fx);

            // ── 9.12 Stage 7a: the modifier family ───────────────────────────────
            case AId::HoldFuncScope:   fx.enterScope(CB::Func); return true;
            case AId::HoldTrackScope:  fx.enterScope(CB::TrackScope); return true;
            case AId::HoldPhraseScope: fx.enterScope(CB::PhraseScope); return true;
            case AId::HoldSceneScope:  fx.enterScope(CB::SceneScope); return true;
            case AId::HoldMorphScope:  fx.enterScope(CB::MorphScope); return true;
            case AId::HoldSongScope:   fx.enterScope(CB::SongScope); return true;
            case AId::HoldMuteScope:   fx.enterScope(CB::MuteScope); return true;
            case AId::HoldFillScope:   fx.enterScope(CB::FillScope); return true;

            // ── 9.12 Stage 6: the gesture-axis actions ───────────────────────────
            // Wired here first, still unreached from dispatch: stages 7-8 point each
            // family at handleAction and delete its imperative branch, one at a time,
            // with the golden net proving the behaviour did not move.
            case AId::LatchTrackScope:  fx.latchModifier(CB::TrackScope); return true;
            case AId::LatchPhraseScope: fx.latchModifier(CB::PhraseScope); return true;
            case AId::LatchSceneScope:  fx.latchModifier(CB::SceneScope); return true;
            case AId::LatchMorphScope:  fx.latchModifier(CB::MorphScope); return true;
            case AId::LatchSongScope:   fx.latchModifier(CB::SongScope); return true;
            case AId::LatchMuteScope:   fx.latchModifier(CB::MuteScope); return true;
            case AId::LatchFillScope:   fx.latchModifier(CB::FillScope); return true;

            case AId::FuncEscape:       fx.escapeOverlay(); return true;
            case AId::VerbRestore:      fx.restorePop(); return true;
            case AId::RestoreFloor:     fx.restoreFloor(); return true;
            case AId::VerbUndo:         fx.undo(); return true;

            case AId::RecordArmToggle:  fx.transport(TA::RecArm); return true;
            case AId::RecordArmOverdub: fx.recordArmOverdub(); return true;
            case AId::PlayStopToggle:   fx.transport(TA::Play); return true;   // same path as CB::PlayStop
            case AId::PlayStopReset:    fx.transport(TA::StopReset); return true;

            case AId::StepLatch:        fx.stepLatch(ev.index); return true;
            case AId::NavPageUnlock:    fx.navPageUnlock(); return true;
            case AId::OpenGeneratorHub: fx.openGeneratorHub(); return true;
            case AId::OpenRetrigPicker: fx.setTrigGridMode(TrigGridMode::Retrig); return true;
            case AId::OpenSoundPool:    fx.setTrigGridMode(TrigGridMode::SoundPool); return true;

            // ── 9.12 Stage 8: the actions handleAction DECLINES, on purpose ───────
            // The switch is exhaustive over ActionId with NO `default:`, so -Wswitch
            // (-Werror) makes a new action a BUILD failure rather than a key that
            // silently does nothing when pressed. That compile-time guarantee is what
            // replaced the kNotYetMigrated set: a list of debt you must remember to
            // shrink is exactly the kind of hand-synced invariant this phase exists to
            // delete (PRINCIPLES §20 -- make it structural, not remembered).
            //
            // Declining is a real answer, and each of these says something different:

            // Confirm / Cancel with nothing pending. A PENDING prompt never reaches an
            // action -- handleDown intercepts VerbConfirm far upstream and executes or
            // cancels it. With nothing pending they delegate to the scope x verb matrix,
            // which declines them in every scope. Handled above; listed here only so the
            // reader does not go looking for a missing case.
            //   (AId::VerbConfirm / AId::VerbCancel are handled in the verb block.)

            // Display-only rows: the key's FRAME advertises the gesture; the behaviour is
            // owned elsewhere, so there is deliberately nothing to dispatch.
            case AId::TransportTrackCut:   // Play double-tap  — the transport owns it
            case AId::TransportMasterCut:  // Play triple-tap  — ditto
            case AId::HoldSceneMuteView:   // Scene+Mute is a VIEW, derived from held state
            case AId::HoldMachineScope:    // Func+Track = Machine (9.29): a compound scope,
            case AId::FocusGlobal:         // Func+Song  = Set     (9.29): entered by the
                                           //   modifier's BARE row (7a), not by this row
            case AId::HoldCueScope:        // Func+3     = Cue      (6.4): entered imperatively
                                           //   in the editor (enterCueScope); display row only
                return false;

            // No action. Resolving to None means "this key is inert here", which is a
            // grammar answer (a reserved combination), not a hole.
            case AId::None:
                return false;

            // The sentinel is not an action.
            case AId::Count:
                return false;
        }
        return false;   // unreachable — the switch above is exhaustive over ActionId
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
