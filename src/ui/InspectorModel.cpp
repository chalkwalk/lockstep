#include "InspectorModel.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../PluginProcessor.h"
#include "../command/KeyBindings.h"
#include "../core/MusicalGate.h"
#include "mode/ModalState.h"

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
        if (ui.funcHeld && !ui.funcTrackHeld && (ui.phraseScopeHeld || ui.morphHeld))
            return ui.morphHeld
                ? juce::String(u8"LENGTH (all tracks) — tap a step to set length")
                : juce::String(u8"LENGTH — tap a step to set phrase length");

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
            case Modal::MachinePicker:  return u8"MACHINE PICKER — select machine  esc=release Func";
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

    // ── Public builder ────────────────────────────────────────────────────────

    InspectorModel buildInspectorModel(const UiState& ui,
                                       const EditContext& ec,
                                       const LockstepProcessor& proc,
                                       ControllerButton focusedButton,
                                       int focusedIndex) noexcept
    {
        InspectorModel m;
        m.key     = buildKeyRegion(focusedButton, focusedIndex);
        m.held    = buildHeldRegion(ui, proc);
        m.overlay = buildOverlayRegion(ui, proc);
        m.edit    = buildEditRegion(ui, ec, proc);
        return m;
    }

} // namespace lockstep
