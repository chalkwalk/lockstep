#include "InspectorModel.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../PluginProcessor.h"
#include "../command/KeyBindings.h"
#include "../command/VerbCommands.h"
#include "../core/MusicalGate.h"
#include "mode/ModalState.h"
#include "MetaBand.h"

namespace lockstep
{
    // ── KEY region ────────────────────────────────────────────────────────────
    // Describes the last-touched / focused key with its gesture set.
    // Now derived entirely from the grammar (SSOT) rather than KeyAffordances.

    static juce::String buildKeyRegion(ControllerButton btn, int index) noexcept
    {
        if (btn == ControllerButton::None)
            return u8"--";
        if (btn == ControllerButton::Step)
        {
            if (index < 0) return u8"--";
            // Steps share one gesture set; narrate it here rather than repeating a
            // label on all 16 cells (the grid carries only a subtle hold-hint glyph).
            return "step " + juce::String(index + 1)
                 + "  tap=TRIG  hold=P-LOCK  dbl=LATCH";
        }

        const auto tap  = resolveBinding(btn, index, kModNone, SurfaceLayer::Base, Gesture::Tap);
        const auto hold = resolveBinding(btn, index, kModNone, SurfaceLayer::Base, Gesture::Hold);
        const auto dbl  = resolveBinding(btn, index, kModNone, SurfaceLayer::Base, Gesture::DoubleTap);
        const auto trip = resolveBinding(btn, index, kModNone, SurfaceLayer::Base, Gesture::TripleTap);
        const Gesture prom = promotedGesture(btn, index, kModNone, SurfaceLayer::Base);

        const auto& promRow = (prom == Gesture::Hold) ? hold : tap;
        if (promRow.action == ActionId::None) return u8"--";

        const juce::String name(promRow.primary);
        if (name.isEmpty()) return u8"--";

        // Append gesture summary: the primary name is already shown, so list only
        // the OTHER gestures where they carry a distinct action.
        juce::String gestures;
        if (prom == Gesture::Hold)
        {
            if (tap.action != ActionId::None && tap.action != hold.action)
                gestures += juce::String(u8" tap=") + juce::String(tap.primary);
        }
        else
        {
            if (hold.action != ActionId::None && hold.action != tap.action)
                gestures += juce::String(u8" hold=") + juce::String(hold.primary);
        }
        if (dbl.action != ActionId::None)
            gestures += juce::String(u8" dbl=") + juce::String(dbl.primary);
        if (trip.action != ActionId::None)
            gestures += juce::String(u8" triple=") + juce::String(trip.primary);

        return name + gestures;
    }

    // ── HELD region ───────────────────────────────────────────────────────────

    static juce::String buildHeldRegion(const UiState& ui,
                                        const LockstepProcessor& proc) noexcept
    {
        // Phrase-length authoring (Func + Phrase, or Func + Morph for all tracks):
        // the grid re-skins to in-run / boundary / out-run and a step press sets the
        // length. Checked before the generic Func case so the hint is specific.
        if (ui.funcHeld && !ui.machineScopeHeld && (ui.phraseScopeHeld || ui.morphHeld))
            return ui.morphHeld
                ? juce::String(u8"LENGTH (all tracks) — tap a step to set length")
                : juce::String(u8"LENGTH — tap a step to set phrase length");

        // 9.29: the two compound scopes name the rungs the cluster has no key for.
        // Checked before the bare Func/Track/Song cases — the compound is the more
        // specific statement, and it is the operand the verbs will act on.
        if (ui.funcHeld && ui.machineScopeHeld)
            return u8"MACHINE — the sound: sections page the machine";
        if (ui.funcHeld && ui.songHeld)
            return u8"SET — global / master bus";

        // Modifier held → scope description. (u8 literals: the em-dash is non-ASCII,
        // so juce::String must bind to the char8_t* overload — a plain char* literal
        // trips the JUCE ASCII assertion in juce_String.cpp.)
        if (ui.funcHeld)
            return u8"FUNC — secondary functions active";
        if (ui.trackHeld)
            return u8"TRACK — verbs write to track  dbl=LATCH";
        if (ui.phraseScopeHeld)
            return u8"PHRASE — phrase select  dbl=LATCH";
        if (ui.sceneHeld)
            return u8"SCENE — scene launch/commit  dbl=LATCH";
        if (ui.morphHeld)
            return u8"MORPH — A/B pole edit  dbl=LATCH";
        if (ui.songHeld)
            return u8"SONG — master/global scope  dbl=LATCH";
        if (ui.muteHeld)
            return ui.funcHeld ? juce::String(u8"MUTE (SCENE) — pattern mute view")
                               : juce::String(u8"MUTE — global mute view");
        if (ui.fillHeld)
            return u8"FILL — fill conditions active  dbl=LATCH";

        // Idle: show active track + machine summary.
        const int t = ui.activeTrack;
        if (t >= 0 && t < static_cast<int>(kNumTracks))
        {
            return "track " + juce::String(t + 1) + "  " + proc.getMachineId(t);
        }
        return "track --";
    }

    // 9.14 st.5 — the ARMED-VERB half of clipboard chrome. `CPY:TRK` (below) says what
    // the clipboard HOLDS; this says what the two verb keys WOULD DO if pressed right
    // now. Both halves are needed: knowing a track is on the clipboard does not tell you
    // that PLAY will stamp it here, and the answer changes with every scope you hold.
    //
    // Sourced from the scope x verb matrix (verbs::clipAffordance), NOT from the key
    // table -- for the verb family the table carries only labels while `handleVerb`
    // decides behaviour, so a hint read from the table can advertise a paste the verb
    // refuses. PASTE is shown only when the clipboard actually satisfies this scope;
    // silence is the honest answer when the key would no-op.
    static juce::String buildClipHint(const UiState& ui, const StatusInput& si) noexcept
    {
        const auto aff = verbs::clipAffordance(si.scope, ui.funcHeld, si.clipboard);
        juce::String out;
        if (aff.canCopy)  out += "  REC=COPY";
        if (aff.canPaste) out += "  PLAY=PASTE";
        return out;
    }

    // 9.30 st.2: the badges the header dashboard used to carry, folded into HELD.
    // They QUALIFY the scope, which is why they belong next to it: "TRACK" plus
    // "CPY:TRK" together say "a paste right now would stamp a track", and CK says how
    // far back the scope you are holding can be undone. Read at a glance, in the one
    // place the eye is already looking, instead of at x=420 in the header.
    static juce::String buildHeldBadges(const UiState& ui, const StatusInput& si) noexcept
    {
        juce::String out;

        // Per-track input mode — only when it is NOT the default. PLAY is the resting
        // state of every track; announcing it on all sixteen is noise, and noise is what
        // made the old dashboard invisible.
        const int t = ui.activeTrack;
        if (t >= 0 && t < static_cast<int>(kNumTracks))
        {
            switch (ui.trackInputMode[static_cast<std::size_t>(t)])
            {
                case TrackInputMode::Chromatic: out += "  CHROM"; break;
                case TrackInputMode::Levels:    out += "  LEVLS"; break;
                case TrackInputMode::Play:      break;
            }
        }

        switch (si.clipboard)
        {
            case ClipboardType::Step:    out += "  CPY:STP"; break;
            case ClipboardType::Section: out += "  CPY:SEC"; break;
            case ClipboardType::Track:   out += "  CPY:TRK"; break;
            case ClipboardType::Pattern: out += "  CPY:PHR"; break;
            case ClipboardType::Scene:   out += "  CPY:SCN"; break;
            case ClipboardType::Machine: out += "  CPY:SND"; break;
            case ClipboardType::All:     out += "  CPY:ALL"; break;
            case ClipboardType::None:    break;
        }

        if (si.checkpointDepth > 0)
            out += "  CK:" + juce::String(si.checkpointDepth);

        // 9.4 E: the held scope's pending undo levels. Distinct from CK (marks): CK is
        // what YOU saved, UNDO is the system's safety net Func+O walks. Shown only when
        // there is something to undo.
        if (si.undoDepth > 0)
            out += "  UNDO:" + juce::String(si.undoDepth);

        out += buildClipHint(ui, si);
        return out;
    }

    // ── OVERLAY region ────────────────────────────────────────────────────────

    static juce::String buildOverlayRegion(const UiState& ui,
                                           const LockstepProcessor& proc) noexcept
    {
        // A6: a count-in is running. It outranks any modal narration — the performer
        // needs to know how many bars are left, and the click is already saying so.
        if (proc.preRollActive())
        {
            const auto [elapsed, total] = proc.preRollProgress();
            const juce::String head = u8"COUNT-IN — bar ";
            return head + juce::String(elapsed + 1) + " of "
                   + juce::String(total) + "  Play=abort";
        }

        // One cascade: narrate the active modal via the read SSOT (activeModal),
        // so the inspector text always agrees with what the grid/MZ shows. The
        // switch is exhaustive over Modal (no default) — adding a modal without an
        // inspector line is a -Wswitch compile error. (u8 literals: non-ASCII dash.)
        switch (activeModal(ui))
        {
            case Modal::MasterFxPicker: return u8"MASTER FX — select effect  esc=release Func+Song+FX";
            case Modal::TrackFxPicker:  return u8"FX INSERT — select effect  esc=release Func";
            case Modal::MachinePicker:  return u8"PICK MACHINE — tap a cell to load it on this track";
            case Modal::GeneratorHub:   return u8"GENERATOR HUB — pick cell  esc=release";
            case Modal::NoteEdit:
                return "NOTE EDIT  oct " + juce::String(ui.noteEditOctave)
                       + "  nav=oct shift  esc=release Func";
            case Modal::PLockClear:
                return "CLEAR P-LOCK  step " + juce::String(ui.pLockClearStep + 1)
                       + "  esc=release Func";
            case Modal::Euclid:  return u8"EUCLID — configuring  esc=dbl-tap Func";
            case Modal::Melodic: return u8"MELODY — generating  P=print  Func+P=cancel";
            case Modal::Harmony: return u8"CHORD — voice-mover  P=print  Func+P=cancel";
            case Modal::Density: return u8"DENSITY — trig-thinning overlay  esc=dbl-tap Func";
            case Modal::Vel:     return u8"VEL STICKY — velocity band  esc=dbl-tap Func";
            case Modal::Time:    return u8"TIME — time-sig/click band  esc=dbl-tap Func";
            case Modal::SampleProps: return u8"SAMPLE — pool properties  esc=dbl-tap Func";
            case Modal::None:    break;
        }

        // Idle: current scene.
        return "scene " + juce::String(proc.activeSectionIdx() + 1);
    }

    // ── EDIT region ───────────────────────────────────────────────────────────

    static juce::String buildEditRegion(const UiState& ui, const EditContext& ec,
                                        const LockstepProcessor& proc) noexcept
    {
        const int stepIdx = ec.heldStepIndex();
        if (stepIdx < 0)
            return u8"--";

        const int t = ui.activeTrack;
        if (t < 0 || t >= static_cast<int>(kNumTracks))
            return u8"--";

        // Build a compact summary of overrides on the held step.
        juce::String summary = "step " + juce::String(stepIdx + 1) + "  ";
        const auto& track = proc.sequence().tracks[static_cast<std::size_t>(t)];
        const auto& step  = track.steps[static_cast<std::size_t>(stepIdx)];

        if (step.trigOverride.hasVelocity)
            summary += "vel " + juce::String(step.trigOverride.velocity) + "  ";
        if (step.trigOverride.hasGate)
            summary += "gate " + juce::String(kMusicalGateLabels[static_cast<int>(step.trigOverride.gateValue)]) + "  ";

        const int activeSlot = ec.activeSlot();
        if (activeSlot >= 0 && step.overrides.has(activeSlot))
        {
            const auto& spec = proc.paramSpec(t, activeSlot);
            summary += juce::String(spec.label) + " " + juce::String(step.overrides.get(activeSlot, 0.0f), 2);
        }

        return summary.trimEnd();
    }

    // ── STATUS lane (9.30 §42.2) ──────────────────────────────────────────────

    juce::String confirmPromptFor(ConfirmKind kind, int target) noexcept
    {
        // Derived from the STATE, not from a message captured at arm time. The old
        // prompt was a string handed to a fading toast; if the toast expired, the text
        // was gone while the arming lived on. Here the text cannot outlive -- or
        // under-live -- the state it describes: it IS the state, rendered.
        const juce::String n = juce::String(target + 1);
        switch (kind)
        {
            case ConfirmKind::DeleteTrack:         return "DELETE TRACK " + n + "?";
            case ConfirmKind::DeletePhrase:        return "DELETE PHRASE?";
            case ConfirmKind::DeleteScene:         return "DELETE SCENE " + n + "?";
            case ConfirmKind::BakeScene:           return "BAKE live deviations into the scene?";
            case ConfirmKind::CreateScene:         return "CREATE SCENE " + n + "?";
            case ConfirmKind::CreateBaselineScene: return "CREATE BASELINE SCENE " + n + "?";
            case ConfirmKind::PasteScene:          return "PASTE OVER SCENE " + n + "?";
            case ConfirmKind::ClearTrack:          return "CLEAR TRACK " + n + " (this phrase)?";
            case ConfirmKind::ClearTrackAll:       return "CLEAR TRACK " + n + " (ALL phrases)?";
            case ConfirmKind::ClearPhrase:         return "CLEAR PHRASE (all tracks)?";
            case ConfirmKind::None:                break;
        }
        return {};
    }

    // Exhaustive over MetaBand (no `default:`), so a new band that forgets to say what
    // the knobs are doing is a -Wswitch build error rather than a blank caption.
    static const char* metaBandCaption(MetaBand b) noexcept
    {
        switch (b)
        {
            case MetaBand::Mixer:            return "MIXER - bank levels";
            case MetaBand::Cond:             return "TRIG CONDITION";
            case MetaBand::Trig:             return "TRIG / NOTE defaults";
            case MetaBand::Divider:          return "TRACK DIVIDER";
            case MetaBand::PhraseLen:        return "PHRASE LENGTH";
            case MetaBand::Global:           return "MASTER / GLOBAL";
            case MetaBand::Swing:            return "SWING";
            case MetaBand::Density:          return "DENSITY";
            case MetaBand::DensityMode:      return "DENSITY - musicality";
            case MetaBand::DensitySelection: return "DENSITY - selection";
            case MetaBand::MasterFx:         return "MASTER FX";
            case MetaBand::Euclidean:        return "EUCLID";
            case MetaBand::Melodic:          return "MELODY";
            case MetaBand::Harmony:          return "CHORD";
            case MetaBand::Transport:        return "TRANSPORT globals";
            case MetaBand::Vel:              return "VELOCITY - depth";
            case MetaBand::VelCenter:        return "VELOCITY - centre";
            case MetaBand::VelMode:          return "VELOCITY - mode";
            case MetaBand::VelBlend:         return "VELOCITY - blend";
            case MetaBand::Time:             return "TIME";
            case MetaBand::Key:              return "KEY";
            case MetaBand::StepPosition:     return "STEP POSITION";
            case MetaBand::SampleProps:      return "SAMPLE properties";
            case MetaBand::None:             break;
        }
        return "";
    }

    // The branches below are in the SAME ORDER the MZ's own write dispatch takes
    // (ManipulationZone.cpp: fill -> morph -> writeParam, and inside writeParam:
    // control-all -> held step -> base). That ordering is the whole correctness
    // argument: a caption that claims to name the write target and gets the
    // precedence wrong is worse than no caption, because it is believed.
    juce::String mzWriteTarget(const UiState& ui, const EditContext& ec,
                               const LockstepProcessor& proc) noexcept
    {
        // 1. A META BAND has taken the MZ. The knobs are not writing params at all --
        //    they are that band's own controls -- so name the band, not a track.
        const auto band = resolveMetaBand(ui);
        if (band != MetaBand::None)
            return juce::String("MZ -> ") + juce::String(metaBandCaption(band));

        const int t = ui.activeTrack;
        if (t < 0 || t >= static_cast<int>(kNumTracks))
            return "MZ -> --";

        const bool held = ec.isActiveForEditing() && ec.heldTrackIndex() == t;
        const int nHeld = held ? static_cast<int>(ec.heldSteps().size()) : 0;
        const int step  = held ? ec.heldStepIndex() : -1;

        // A held-step phrase, reused by the fill and P-Lock legs.
        const auto stepPhrase = [&]() -> juce::String {
            if (nHeld > 1) return juce::String(nHeld) + " HELD STEPS";
            return "STEP " + juce::String(step + 1);
        };

        // 2. FILL. The fill layer is a SECOND set of per-step locks, live only while
        //    the Fill modifier is down -- so with Fill held the same knob authors a
        //    different lock than it did a moment ago. It also needs a step: with no
        //    step held, writeFillParam has nowhere to put the value and DROPS it. A
        //    knob that silently does nothing is exactly what this lane exists to catch.
        if (proc.fillActive())
        {
            if (step >= 0)
                return "MZ -> " + stepPhrase() + "  FILL override";
            return "MZ -> FILL  (hold a step -- writes are dropped otherwise)";
        }

        // 3. MORPH. With Morph held the knob does not write a value at all: it writes a
        //    DEVIATION into the morph layer, split across the A/B poles at the fader's
        //    position. Same knob, same page, entirely different store.
        if (ui.morphHeld)
            return "MZ -> TRACK " + juce::String(t + 1) + "  MORPH layer (A/B)";

        // 4. CONTROL-ALL fans one knob across every track whose schema has the same
        //    slot id -- unless a step is held on this track, in which case the held-step
        //    write wins (writeParam's own precedence, mirrored here).
        if (proc.controlAllActive() && !held)
            return "MZ -> ALL TRACKS  base params (CONTROL-ALL)";

        // 5. A HELD (or latched) step: writes land in the STEP's override, not the
        //    track's base. That is the Override-ELSE-Base rule, and it is the difference
        //    between "change the sound" and "change this one hit" -- from one turn of the
        //    same knob, with nothing on the knob to tell you which you just did.
        if (step >= 0)
            return "MZ -> " + stepPhrase() + "  override (P-LOCK)";

        // 6. At rest: the focused track's BASE params, and the section page the knobs are
        //    pointed at.
        juce::String out = "MZ -> TRACK " + juce::String(t + 1) + "  base params";
        const int sec = ui.trackSection[static_cast<std::size_t>(t)];
        if (sec >= 0 && sec < IMachine::kMaxSections)
            out += juce::String("  ")
                 + juce::String(IMachine::kCanonicalSectionNames[static_cast<std::size_t>(sec)]);
        return out;
    }

    // Precedence IS the taxonomy: State outranks Alert outranks Event. A fading toast
    // must never be able to hide something that is armed and waiting for the next key.
    static void buildStatusLane(InspectorModel& m, const UiState& ui,
                                const EditContext& ec, const LockstepProcessor& proc,
                                const StatusInput& si) noexcept
    {
        // 1. STATE — an armed confirm. Rendered as the pop-over (§42.3).
        if (ui.confirm.pending())
        {
            m.statusKind = StatusKind::Confirm;
            m.confirmPrompt = confirmPromptFor(ui.confirm.kind, ui.confirm.target);
            m.confirmActions = u8"[P] CONFIRM        [any other key] CANCEL";
            m.status = m.confirmPrompt;   // the lane text, for controller displays
            return;
        }

        // 2. STATE — the deletion picker is armed and waiting for a target.
        if (ui.deletePicker.active())
        {
            m.statusKind = StatusKind::State;
            switch (ui.deletePicker.scope)
            {
                case DeleteScope::Track:  m.status = u8"DELETE WHICH TRACK? — tap a track"; break;
                case DeleteScope::Phrase: m.status = u8"DELETE WHICH PHRASE? — tap a step"; break;
                case DeleteScope::Scene:  m.status = u8"DELETE WHICH SCENE? — tap a step"; break;
                case DeleteScope::None:   break;
            }
            return;
        }

        // 3. EVENT — a toast, while it lives. Below state, above nothing.
        if (si.toast.isNotEmpty() && si.toastAgeMs <= si.toastDurationMs)
        {
            m.statusKind = StatusKind::Event;
            m.status = si.toast;
            m.statusAlpha = juce::jlimit(0.0f, 1.0f,
                                         1.0f - static_cast<float>(si.toastAgeMs)
                                                    / static_cast<float>(si.toastDurationMs));
            return;
        }

        // 4. ALERT — persistent until the condition clears. Outlives every toast, so it
        //    is checked after them only because a toast is the more recent news; it
        //    reappears the moment the toast expires (it never actually went away).
        if (si.missingSamples > 0)
        {
            m.statusKind = StatusKind::Alert;
            m.status = juce::String(si.missingSamples)
                       + (si.missingSamples == 1 ? " sample missing" : " samples missing")
                       + " - Manage to relink";
            return;
        }

        // At rest the lane is NOT blank: it captions the MZ below it. A strip of dead
        // pixels directly above the knobs was the one place on the surface with room to
        // say the thing the knobs cannot say about themselves — which layer they write to.
        m.statusKind = StatusKind::Idle;
        m.status = mzWriteTarget(ui, ec, proc);
    }

    // ── Public builder ────────────────────────────────────────────────────────

    InspectorModel buildInspectorModel(const UiState& ui,
                                       const EditContext& ec,
                                       const LockstepProcessor& proc,
                                       ControllerButton focusedButton,
                                       int focusedIndex,
                                       const StatusInput& si) noexcept
    {
        InspectorModel m;
        m.key     = buildKeyRegion(focusedButton, focusedIndex);
        m.held    = buildHeldRegion(ui, proc) + buildHeldBadges(ui, si);
        m.overlay = buildOverlayRegion(ui, proc);
        m.edit    = buildEditRegion(ui, ec, proc);
        buildStatusLane(m, ui, ec, proc, si);
        return m;
    }

} // namespace lockstep
