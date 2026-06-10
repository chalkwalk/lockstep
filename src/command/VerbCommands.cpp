#include "VerbCommands.h"
#include "../core/TrigCondition.h"
#include "../io/Clipboard.h"
#include <juce_core/juce_core.h>
#include <algorithm>

namespace lockstep::verbs
{
    bool trig(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;

        auto& ec = ctx.editContext;
        if (!ec.isActiveForEditing()) return false;
        const int track = ec.heldTrackIndex();
        auto& trk = ctx.sequence.tracks[static_cast<std::size_t>(track)];

        if (verb == CB::VerbRecord)
        {
            const auto& held = ec.heldSteps();
            if (held.empty()) return false;
            const int anchor = *std::min_element(held.begin(), held.end());
            ctx.clipboard.stepEntries.clear();
            for (int idx : held)
            {
                if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                ctx.clipboard.stepEntries.push_back(
                    { idx - anchor, trk.steps[static_cast<std::size_t>(idx)] });
            }
            std::sort(ctx.clipboard.stepEntries.begin(),
                      ctx.clipboard.stepEntries.end(),
                      [](const StepClipEntry& a, const StepClipEntry& b)
                      { return a.relOffset < b.relOffset; });
            ctx.clipboard.type = ClipboardType::Step;
            return true;
        }

        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Step
                && ctx.clipboard.type != ClipboardType::All)
                return false;
            const int anchor = ec.heldStepIndex();
            const int trkLen = trk.length;
            for (const auto& entry : ctx.clipboard.stepEntries)
            {
                int dst = anchor + entry.relOffset;
                dst = ((dst % trkLen) + trkLen) % trkLen;
                trk.steps[static_cast<std::size_t>(dst)] = entry.data;
            }
            return true;
        }

        if (verb == CB::VerbClear)
        {
            const bool funcHeld  = ctx.editMode.scopeState().func;
            const int  activeSlot = ec.activeSlot();
            if (funcHeld)
            {
                // Trig + Func + Clear — clear all P-Locks on held step(s),
                // leaving trig and condition intact.
                for (int idx : ec.heldSteps())
                {
                    if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                    auto& s = trk.steps[static_cast<std::size_t>(idx)];
                    s.overrides        = PLock{};
                    s.trigOverride     = TrigOverride{};
                    s.fillOverrides    = PLock{};
                    s.fillTrigOverride = TrigOverride{};
                }
            }
            else if (activeSlot >= 0)
            {
                // Trig + (active MZ slot) + Clear — clear only that slot's
                // P-Lock on all held steps.
                for (int idx : ec.heldSteps())
                {
                    if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                    trk.steps[static_cast<std::size_t>(idx)].overrides.clear(activeSlot);
                }
            }
            else
            {
                // Full clear: trig off + condition reset + all P-Locks.
                for (int idx : ec.heldSteps())
                {
                    if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                    auto& s = trk.steps[static_cast<std::size_t>(idx)];
                    s.overrides        = PLock{};
                    s.trigOverride     = TrigOverride{};
                    s.fillOverrides    = PLock{};
                    s.fillTrigOverride = TrigOverride{};
                    s.trig             = false;
                    s.condition        = TrigCondition{};
                }
            }
            ec.markParamWritten();
            return true;
        }

        if (verb == CB::VerbNo && ctx.editMode.scopeState().func)
        {
            // Trig + Func + No — clear note/velocity/gate overrides on held
            // step(s), leaving step.trig and P-Locks intact.
            for (int idx : ec.heldSteps())
            {
                if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                auto& s = trk.steps[static_cast<std::size_t>(idx)];
                s.trigOverride.noteCount   = 0;
                s.trigOverride.notes       = {};
                s.trigOverride.hasVelocity = false;
                s.trigOverride.velocity    = 100;
                s.trigOverride.hasGate     = false;
                s.trigOverride.gateValue   = MusicalGate::None;
            }
            return true;
        }

        return false;
    }

    bool track(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;
        const int at = ctx.uiState.activeTrack;
        auto& trk = ctx.sequence.tracks[static_cast<std::size_t>(at)];
        const juce::String trkName = "Track " + juce::String(at + 1);

        if (verb == CB::VerbRecord)
        {
            ctx.clipboard.clipTrack = trk;
            ctx.clipboard.type      = ClipboardType::Track;
            fx.status("Copied " + trkName);
            fx.releaseLatch(CB::TrackScope);
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Track
                && ctx.clipboard.type != ClipboardType::All)
                return false;
            trk = ctx.clipboard.clipTrack;
            fx.status("Pasted -> " + trkName);
            fx.releaseLatch(CB::TrackScope);
            return true;
        }
        if (verb == CB::VerbClear)
        {
            for (auto& s : trk.steps)
            {
                s.trig         = false;
                s.condition    = TrigCondition{};
                s.overrides    = PLock{};
                s.trigOverride = TrigOverride{};
            }
            fx.status("Cleared " + trkName);
            fx.releaseLatch(CB::TrackScope);
            return true;
        }
        return false;
    }

    bool phrase(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;

        if (verb == CB::VerbRecord)
        {
            ctx.clipboard.clipSequence = ctx.sequence;
            ctx.clipboard.type         = ClipboardType::Pattern;
            fx.status("Copied Phrase");
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Pattern
                && ctx.clipboard.type != ClipboardType::All)
                return false;
            ctx.sequence = ctx.clipboard.clipSequence;
            fx.status("Pasted Phrase");
            return true;
        }
        if (verb == CB::VerbClear || verb == CB::VerbDelete)
        {
            if (verb == CB::VerbDelete)
                ctx.arrangement.snapshot(CheckpointScope::Song, 0);

            for (auto& trk : ctx.sequence.tracks)
            {
                for (auto& s : trk.steps)
                {
                    s.trig             = false;
                    s.condition        = TrigCondition{};
                    s.overrides        = PLock{};
                    s.trigOverride     = TrigOverride{};
                    s.fillTrigState    = FillTrigState::Inherit;
                    s.fillOverrides    = PLock{};
                    s.fillTrigOverride = TrigOverride{};
                }
            }
            fx.status("Cleared Phrase");
            return true;
        }
        return false;
    }

    bool scene(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;

        // Scene verbs require Func held (bare Scene+Record = bake, handled in dispatchDown).
        if (!ctx.editMode.scopeState().func) return false;

        if (verb == CB::VerbRecord)
        {
            ctx.clipboard.scene.floor = ctx.arrangement.scene();
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                ctx.clipboard.scene.phrases[static_cast<std::size_t>(t)] =
                    ctx.arrangement.activePhrase(t);
            ctx.clipboard.type = ClipboardType::Scene;
            fx.status("Copied Scene");
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Scene
                && ctx.clipboard.type != ClipboardType::All)
                return false;

            if (ctx.editMode.scopeState().mute)
                fx.sceneFloorPaste();
            else
                fx.sceneFullPaste(ctx.arrangement.sceneIdx);
            return true;
        }
        return false;
    }

    bool song(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        (void)ctx;
        using CB = ControllerButton;
        if (verb == CB::VerbClear)
        {
            fx.transport(CommandEffects::TransportAction::Panic);
            fx.status("Panic");
            return true;
        }
        return false;
    }

    bool noScope(ControllerButton verb, CommandContext& ctx, CommandEffects&)
    {
        using CB = ControllerButton;
        if (verb == CB::VerbYes)
        {
            ctx.arrangement.snapshot(CheckpointScope::Song, ctx.uiState.activeTrack);
            return true;
        }
        return false;
    }

    bool morph(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;
        if (verb != CB::VerbClear) return false;
        const int track = ctx.uiState.activeTrack;
        if (track < 0) return false;
        if (ctx.editMode.scopeState().func)
        {
            fx.morphErase(track);
            fx.status("Morph erased");
        }
        else
        {
            fx.morphBake(track);
            fx.status("Morph baked");
        }
        return true;
    }
}
