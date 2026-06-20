#include "InspectorModel.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../PluginProcessor.h"
#include "../command/KeyAffordances.h"
#include "../core/MusicalGate.h"

namespace lockstep
{
    // ── KEY region ────────────────────────────────────────────────────────────
    // Describes the last-touched / focused key with its gesture set.

    static juce::String buildKeyRegion(ControllerButton btn, int index,
                                       const KeyAffordance* aff) noexcept
    {
        if (btn == ControllerButton::None || btn == ControllerButton::Step)
        {
            if (btn == ControllerButton::Step && index >= 0)
                return "step " + juce::String(index + 1);
            return u8"--";
        }

        // Base label from affordance (tap label = natural action name).
        juce::String name;
        if (aff)
        {
            if (aff->primaryIsHold && aff->holdLabel)
                name = juce::String(aff->holdLabel);
            else if (aff->tapLabel)
                name = juce::String(aff->tapLabel);
        }
        if (name.isEmpty())
            return u8"--";

        // Append gesture summary: tap / hold / dbl-tap where distinct.
        juce::String gestures;
        if (aff)
        {
            if (aff->primaryIsHold)
            {
                if (aff->tapLabel)
                    gestures += juce::String(u8" tap=") + juce::String(aff->tapLabel);
                gestures += juce::String(u8" hold=") + name;
            }
            else
            {
                gestures += juce::String(u8" tap=") + name;
                if (aff->holdLabel)
                    gestures += juce::String(u8" hold=") + juce::String(aff->holdLabel);
            }
            if (aff->doubleTapLabel)
                gestures += juce::String(u8" dbl=") + juce::String(aff->doubleTapLabel);
        }

        return name + gestures;
    }

    // ── HELD region ───────────────────────────────────────────────────────────

    static juce::String buildHeldRegion(const UiState& ui,
                                        const LockstepProcessor& proc) noexcept
    {
        // Modifier held → scope description.
        if (ui.funcHeld)
            return "FUNC — secondary functions active";
        if (ui.trackHeld)
            return "TRACK — verbs write to track  dbl=LATCH";
        if (ui.phraseScopeHeld)
            return "PHRASE — phrase select  dbl=LATCH";
        if (ui.sceneHeld)
            return "SCENE — scene launch/commit  dbl=LATCH";
        if (ui.morphHeld)
            return "MORPH — A/B pole edit  dbl=LATCH";
        if (ui.songHeld)
            return "SONG — master/global scope  dbl=LATCH";
        if (ui.muteHeld)
            return ui.funcHeld ? "MUTE (SCENE) — pattern mute view" : "MUTE — global mute view";
        if (ui.fillHeld)
            return "FILL — fill conditions active  dbl=LATCH";

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
        // Active picker / overlay.
        if (ui.generatorHubHeld)
            return "GENERATOR HUB — pick cell  esc=release";
        if (ui.euclidHeld)
            return "EUCLID — configuring  esc=release";
        if (ui.funcTrackHeld)
            return "MACHINE PICKER — select machine  esc=release Func";
        if (ui.funcFxHeld)
            return "FX INSERT — select effect  esc=release Func";
        if (ui.masterFxPickerOpen)
            return "MASTER FX — select effect  esc=release Func+Song+FX";
        if (ui.noteEditMode)
            return "NOTE EDIT  oct " + juce::String(ui.noteEditOctave)
                   + "  nav=oct shift  esc=release Func";
        if (ui.pLockClearStep >= 0)
            return "CLEAR P-LOCK  step " + juce::String(ui.pLockClearStep + 1)
                   + "  esc=release Func";

        // Overlay from sticky field.
        switch (ui.overlay)
        {
            case Overlay::None: break;
            case Overlay::Euclid:
                return "EUCLID — generator overlay  esc=dbl-tap Func";
            case Overlay::Density:
                return "DENSITY — trig-thinning overlay  esc=dbl-tap Func";
            case Overlay::Vel:
                return "VEL STICKY — velocity band  esc=dbl-tap Func";
            case Overlay::Time:
                return "TIME — time-sig/click band  esc=dbl-tap Func";
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
        const KeyAffordance* aff = findAffordance(focusedButton);

        InspectorModel m;
        m.key     = buildKeyRegion(focusedButton, focusedIndex, aff);
        m.held    = buildHeldRegion(ui, proc);
        m.overlay = buildOverlayRegion(ui, proc);
        m.edit    = buildEditRegion(ui, ec, proc);
        return m;
    }

} // namespace lockstep
