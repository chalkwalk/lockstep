#include "VerbCommands.h"
#include "StatusText.h"
#include "../core/TrigCondition.h"
#include "../io/Clipboard.h"
#include <algorithm>

namespace lockstep::verbs
{
    bool trig(ControllerButton verb, CommandContext& ctx, [[maybe_unused]] CommandEffects& fx)
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
                      [](const StepClipEntry& a, const StepClipEntry& b) { return a.relOffset < b.relOffset; });
            ctx.clipboard.type = ClipboardType::Step;
            return true;
        }

        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Step && ctx.clipboard.type != ClipboardType::All)
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
            const bool funcHeld = ctx.editMode.scopeState().func;
            const int activeSlot = ec.activeSlot();
            if (ctx.editMode.sectionHeld())
            {
                // Trig + <section held> + Clear — domain-scoped clear on the held
                // step(s): wipe every override owned by the held section, leaving
                // other sections (and trig + condition) intact. SRC owns the note/
                // velocity/gate payload, so on SRC we also clear the trig override.
                // This is the home of "clear notes" — formerly Func+P. (DESIGN §13.2)
                const int at = ctx.uiState.activeTrack;
                const int secIdx = ctx.uiState.trackSection[static_cast<std::size_t>(at)];
                const int nSlots = ctx.catalog.numParams(at);
                for (int idx : ec.heldSteps())
                {
                    if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                    auto& s = trk.steps[static_cast<std::size_t>(idx)];
                    for (int sl = 0; sl < nSlots; ++sl)
                        if (ctx.catalog.paramSpec(at, sl).sectionIndex == secIdx)
                            s.overrides.clear(sl);
                    if (secIdx == IMachine::kSrcSecIdx)
                        s.trigOverride = TrigOverride{};
                }
            }
            else if (funcHeld)
            {
                // Trig + Func + Clear — clear all P-Locks on held step(s),
                // leaving trig and condition intact.
                for (int idx : ec.heldSteps())
                {
                    if (idx < 0 || idx >= kMaxStepsPerTrack) continue;
                    auto& s = trk.steps[static_cast<std::size_t>(idx)];
                    s.overrides = PLock{};
                    s.trigOverride = TrigOverride{};
                    s.fillOverrides = PLock{};
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
                    s.overrides = PLock{};
                    s.trigOverride = TrigOverride{};
                    s.fillOverrides = PLock{};
                    s.fillTrigOverride = TrigOverride{};
                    s.trig = false;
                    s.condition = TrigCondition{};
                }
            }
            ec.markParamWritten();
            return true;
        }

        return false;
    }

    bool track(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;
        const int at = ctx.uiState.activeTrack;
        auto& trk = ctx.sequence.tracks[static_cast<std::size_t>(at)];

        if (verb == CB::VerbRecord)
        {
            ctx.clipboard.clipTrack = trk;
            ctx.clipboard.type = ClipboardType::Track;
            fx.status(status::copiedTrack(at));
            fx.releaseLatch(CB::TrackScope);
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Track && ctx.clipboard.type != ClipboardType::All)
                return false;
            trk = ctx.clipboard.clipTrack;
            fx.status(status::pastedTrackWithSource(at));
            fx.releaseLatch(CB::TrackScope);
            return true;
        }
        if (verb == CB::VerbClear)
        {
            // Confirm gate — blast radius differs by whether Song is also held.
            // State captured at arm time; action fires in executeConfirm even if
            // the scope is released before the user presses CONFIRM.
            if (ctx.uiState.songHeld)
            {
                ctx.uiState.confirm = { ConfirmKind::ClearTrackAll, at };
                fx.status(status::confirmClearTrackAll(at));
            }
            else
            {
                ctx.uiState.confirm = { ConfirmKind::ClearTrack, at };
                fx.status(status::confirmClearTrack(at));
            }
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
            ctx.clipboard.type = ClipboardType::Pattern;
            fx.status(status::copiedPhrase());
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Pattern && ctx.clipboard.type != ClipboardType::All)
                return false;
            ctx.sequence = ctx.clipboard.clipSequence;
            fx.status(status::pastedPhrase());
            return true;
        }
        if (verb == CB::VerbClear)
        {
            // Arm a confirm prompt; the actual clear runs in executeConfirm.
            ctx.uiState.confirm = { ConfirmKind::ClearPhrase, -1 };
            fx.status(status::confirmClearPhrase());
            return true;
        }
        if (verb == CB::VerbDelete)
        {
            // VerbDelete reaches verbs::phrase() only as a fallback — normal flow
            // routes Phrase+Delete through CommandCore::handleDown (DeletePicker).
            ctx.arrangement.snapshot(CheckpointScope::Song, 0);
            for (auto& trk : ctx.sequence.tracks)
            {
                for (auto& s : trk.steps)
                {
                    s.trig = false;
                    s.condition = TrigCondition{};
                    s.overrides = PLock{};
                    s.trigOverride = TrigOverride{};
                    s.fillTrigState = FillTrigState::Inherit;
                    s.fillOverrides = PLock{};
                    s.fillTrigOverride = TrigOverride{};
                }
            }
            fx.status(status::clearedPhrase());
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
            fx.status(status::copiedScene());
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Scene && ctx.clipboard.type != ClipboardType::All)
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
            fx.status(status::panic());
            return true;
        }
        return false;
    }

    bool noScope(ControllerButton verb, CommandContext& ctx, CommandEffects&)
    {
        using CB = ControllerButton;
        if (verb == CB::VerbSnapshot)
        {
            ctx.arrangement.snapshot(CheckpointScope::Song, ctx.uiState.activeTrack);
            return true;
        }
        return false;
    }

    bool section(ControllerButton verb, CommandContext& ctx, [[maybe_unused]] CommandEffects& fx)
    {
        using CB = ControllerButton;
        const int at = ctx.uiState.activeTrack;
        const int secIdx = ctx.uiState.trackSection[static_cast<std::size_t>(at)];
        auto& trk = ctx.sequence.tracks[static_cast<std::size_t>(at)];
        const int trkLen = trk.length;
        const int nSlots = ctx.catalog.numParams(at);

        if (verb == CB::VerbRecord)
        {
            ctx.clipboard.sectionSlots.clear();
            ctx.clipboard.sectionTrackLength = trkLen;
            for (int sl = 0; sl < nSlots; ++sl)
            {
                if (ctx.catalog.paramSpec(at, sl).sectionIndex != secIdx) continue;
                SectionClipSlot entry;
                entry.slot = sl;
                entry.perStep.reserve(static_cast<std::size_t>(trkLen));
                for (int st = 0; st < trkLen; ++st)
                {
                    const auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                    const bool has = plock.has(sl);
                    entry.perStep.push_back({ has, has ? plock.get(sl, 0.0f) : 0.0f });
                }
                ctx.clipboard.sectionSlots.push_back(std::move(entry));
            }
            ctx.clipboard.type = ClipboardType::Section;
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Section && ctx.clipboard.type != ClipboardType::All)
                return false;
            for (const auto& entry : ctx.clipboard.sectionSlots)
            {
                const int steps = std::min(static_cast<int>(entry.perStep.size()), trkLen);
                for (int st = 0; st < steps; ++st)
                {
                    auto& plock = trk.steps[static_cast<std::size_t>(st)].overrides;
                    if (entry.perStep[static_cast<std::size_t>(st)].first)
                        plock.set(entry.slot, entry.perStep[static_cast<std::size_t>(st)].second);
                    else
                        plock.clear(entry.slot);
                }
            }
            return true;
        }
        if (verb == CB::VerbClear)
        {
            for (int sl = 0; sl < nSlots; ++sl)
            {
                if (ctx.catalog.paramSpec(at, sl).sectionIndex != secIdx) continue;
                for (int st = 0; st < trkLen; ++st)
                    trk.steps[static_cast<std::size_t>(st)].overrides.clear(sl);
            }
            // SRC owns the note/velocity/gate payload — clearing the SRC domain at
            // section scope wipes the trig overrides across the track too, mirroring
            // the held-step SRC clear in step(). (DESIGN §13.2)
            if (secIdx == IMachine::kSrcSecIdx)
                for (int st = 0; st < trkLen; ++st)
                    trk.steps[static_cast<std::size_t>(st)].trigOverride = TrigOverride{};
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
            fx.status(status::morphErased());
        }
        else
        {
            fx.morphBake(track);
            fx.status(status::morphBaked());
        }
        return true;
    }
}
