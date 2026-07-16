#include "VerbCommands.h"
#include "StatusText.h"
#include "../core/TrigCondition.h"
#include "../io/Clipboard.h"
#include <algorithm>

namespace lockstep::verbs
{
    // The scope x clipboard matrix (9.14 st.5). Exhaustive over PrimaryScope with no
    // `default:` so a new scope must state whether it copies (PRINCIPLES §20).
    ClipAffordance clipAffordance(EditMode::PrimaryScope scope,
                                  bool func,
                                  ClipboardType clip) noexcept
    {
        using PS = EditMode::PrimaryScope;
        using CT = ClipboardType;

        // Func + Track IS the Machine scope (9.29): the sound, not the track. It is
        // reached through the table (MachineCopy / MachinePaste) rather than through
        // primaryScope, and it is stricter than the rest -- an omni grab holds no
        // machine params, so `All` does NOT satisfy a machine paste.
        if (func && scope == PS::Track)
            return { true, clip == CT::Machine, CT::Machine };

        // Func alone = the omni grab (captured in the editor: it spans layers no single
        // scope owns). Its paste is the mirror: it stamps a single captured layer, and
        // refuses an `All` clip because "paste everything" has no unambiguous target --
        // the user must name a scope.
        if (scope == PS::Func)
            return { true, clip != CT::None && clip != CT::All, CT::All };

        auto native = CT::None;
        switch (scope)
        {
            case PS::Trig:    native = CT::Step;    break;
            case PS::Section: native = CT::Section; break;
            case PS::Track:   native = CT::Track;   break;
            case PS::Phrase:  native = CT::Pattern; break;

            // Scene's verbs require Func (bare Scene+Record is BAKE, which is a
            // different verb on the same key), so an unqualified Scene hold offers
            // no clipboard at all.
            case PS::Scene:   native = func ? CT::Scene : CT::None; break;

            // Song has no clipboard. Its rows in the binding table SAID copy/paste
            // until 9.14 st.5 and `verbs::song` never implemented either, so both keys
            // were silent no-ops wearing labels. The song-level grab is the omni one,
            // on Func.
            case PS::Song:
            case PS::Mute:
            case PS::Fill:
            case PS::Cue:
            case PS::Morph:
            case PS::Func:
            case PS::None:
                native = CT::None;
                break;
        }

        if (native == CT::None)
            return {};

        return { true, clip == native || clip == CT::All, native };
    }

    bool pasteAccepts(EditMode::PrimaryScope scope, bool func, ClipboardType clip) noexcept
    {
        return clipAffordance(scope, func, clip).canPaste;
    }

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
            if (!pasteAccepts(EditMode::PrimaryScope::Trig, false, ctx.clipboard.type))
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

        // 9.4 item B. The key has said SNAP under a held Track for months and done
        // nothing: the bare row supplied the label, dispatch funnelled it here, and
        // here there was no VerbSnapshot arm, so handleVerb returned false. Restore
        // was scope-aware the whole time, so the Track stack was poppable but not
        // pushable -- which is how a restore could only ever find it empty.
        if (verb == CB::VerbSnapshot)
        {
            ctx.arrangement.snapshot(CheckpointScope::Track, at);
            fx.status(status::markedTrack(
                at, ctx.arrangement.checkpointDepth(CheckpointScope::Track, at)));
            return true;
        }

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
            if (!pasteAccepts(EditMode::PrimaryScope::Track, false, ctx.clipboard.type))
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

        // 9.4 item B — the Phrase half of the same phantom (see verbs::track). The
        // checkpoint targets the track's ACTIVE phrase, which is what Arrangement's
        // Phrase stack is keyed by; the held Phrase modifier names the scope, the
        // focused track names which phrase.
        if (verb == CB::VerbSnapshot)
        {
            const int at = ctx.uiState.activeTrack;
            ctx.arrangement.snapshot(CheckpointScope::Phrase, at);
            fx.status(status::markedPhrase(
                ctx.arrangement.checkpointDepth(CheckpointScope::Phrase, at)));
            return true;
        }

        if (verb == CB::VerbRecord)
        {
            // 5.3: grab the focused track's ACTIVE phrase (single, fork-aware paste),
            // not the whole working sequence. The editor snapshots it (it must flush
            // live edits through the processor first). Type tag stays Pattern.
            fx.copyPhraseActiveSlot();
            return true;
        }
        if (verb == CB::VerbPlay)
        {
            // A single-phrase Pattern grab pastes into the active slot, fork-on-shared
            // (editor-side, async confirm). An omni `All` grab still stamps its whole
            // captured sequence layer here (the §13.2 omni paste), unchanged.
            if (ctx.clipboard.type == ClipboardType::Pattern)
            {
                fx.pastePhraseActiveSlot();
                return true;
            }
            if (!pasteAccepts(EditMode::PrimaryScope::Phrase, false, ctx.clipboard.type))
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
            ctx.arrangement.armUndo(CheckpointScope::Song, 0);
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

        // 9.4 items B+C. These two sit ABOVE the Func guard on purpose: they are the
        // scope's BARE verbs, and the guard below is what kept Scene's half of the
        // snapshot grammar out of reach. Scene's clipboard needs Func (bare Scene+Record
        // is BAKE, a different verb on the same key) -- its mark and its sync do not.
        if (verb == CB::VerbSnapshot)
        {
            ctx.arrangement.snapshot(CheckpointScope::Scene, ctx.uiState.activeTrack);
            fx.status(status::markedScene(
                ctx.arrangement.sceneIdx + 1,
                ctx.arrangement.checkpointDepth(CheckpointScope::Scene, 0)));
            return true;
        }

        // SYNC, rehomed from Scene+Y. Discard the live deviations; snap every track back
        // to the scene as stored. Clear is the right seat: Scene+Record BAKES deviations
        // into the scene, so Scene+Clear is the one that throws them away.
        if (verb == CB::VerbClear)
        {
            ctx.arrangement.resyncAllToScene();
            fx.status(status::sceneSynced(ctx.arrangement.sceneIdx + 1));
            return true;
        }

        // Scene's CLIPBOARD verbs require Func held (bare Scene+Record = bake, handled in
        // dispatchDown).
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
            // Func is guaranteed held here (guarded at the top of scene()).
            if (!pasteAccepts(EditMode::PrimaryScope::Scene, true, ctx.clipboard.type))
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

    // No scope held = the Song scope. That is the grammar's default working unit
    // (DESIGN §13.6), not a special case -- which is why a bare Y belongs here and not
    // in an editor branch of its own.
    bool noScope(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;
        if (verb == CB::VerbSnapshot)
        {
            ctx.arrangement.snapshot(CheckpointScope::Song, ctx.uiState.activeTrack);
            fx.status(status::markedSong(
                ctx.arrangement.checkpointDepth(CheckpointScope::Song, 0)));
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
            if (!pasteAccepts(EditMode::PrimaryScope::Section, false, ctx.clipboard.type))
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

    // 9.29 -- the Machine scope. Track owns identity; Machine owns the sound. These
    // verbs therefore move the machine's ID + its param set and nothing else: paste a
    // sound onto a track and the track keeps its mute, level, routing, phrase and
    // steps. That split is the whole reason the scope exists.
    bool machine(ControllerButton verb, CommandContext& ctx, CommandEffects& fx)
    {
        using CB = ControllerButton;
        const int at = ctx.uiState.activeTrack;
        if (at < 0 || at >= static_cast<int>(kNumTracks)) return false;

        if (verb == CB::VerbRecord)
        {
            const int n = ctx.catalog.numParams(at);
            ctx.clipboard.clipMachineId = ctx.catalog.machineId(at);
            ctx.clipboard.clipMachineParams.clear();
            ctx.clipboard.clipMachineParams.reserve(static_cast<std::size_t>(n));
            for (int s = 0; s < n; ++s)
                ctx.clipboard.clipMachineParams.push_back(ctx.catalog.baseParam(at, s));
            ctx.clipboard.type = ClipboardType::Machine;
            fx.status(status::copiedSound(ctx.clipboard.clipMachineId));
            return true;
        }

        if (verb == CB::VerbPlay)
        {
            if (ctx.clipboard.type != ClipboardType::Machine
                || ctx.clipboard.clipMachineId.empty())
            {
                fx.status(status::noSoundCopied());
                return true;
            }
            // Load the engine first when the target runs a different one -- assigning
            // resets the params to that machine's defaults, so it has to happen BEFORE
            // the copied values land or they would be wiped by their own paste.
            if (ctx.clipboard.clipMachineId != ctx.catalog.machineId(at))
                fx.machineAssign(at, ctx.clipboard.clipMachineId.c_str());
            fx.machineParams(at, ctx.clipboard.clipMachineParams);
            fx.status(status::pastedSound(at, ctx.clipboard.clipMachineId));
            fx.releaseLatch(CB::TrackScope);
            return true;
        }

        if (verb == CB::VerbClear)
        {
            // Init = re-assign the same machine: setTrackMachine rebuilds it and resets
            // baseParams to the schema defaults, which is exactly "init" and keeps one
            // owner for what a default IS (the ParamSpec).
            const juce::String id = ctx.catalog.machineId(at);
            fx.machineAssign(at, ctx.catalog.machineId(at));
            fx.status(status::initedMachine(at, id));
            return true;
        }
        return false;
    }
}
