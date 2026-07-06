#pragma once

#include <memory>
#include <string>
#include "IMachine.h"

namespace lockstep
{
    // Per-track audio insert effect. Mirrors IMachine's param contract (ParamSpec,
    // ParamFrame, sectionIndex=kFxSecIdx) but processes audio without MIDI.
    // Two insert slots per track, applied post-AMP in the signal chain (DESIGN §32).
    class IEffect
    {
    public:
        virtual ~IEffect() = default;

        virtual void prepare(double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;
        virtual void process(juce::AudioBuffer<float>& buffer,
                             int numSamples,
                             const ParamFrame& params) = 0;

        // Called once per block with the host BPM. Default no-op; override for
        // tempo-synced effects (e.g. HQ Delay).
        virtual void setTimeInfo(double /*bpm*/) {}

        // 9.24 S16: hand an IR-driven effect (ConvolutionEffect) its impulse
        // response. Called on the message thread by the processor when a slot's
        // pool IR ref resolves (on load or via the pick-IR gesture). Default no-op;
        // effects that don't use an IR ignore it. Empty buffer = clear the IR.
        virtual void setImpulseResponse(const juce::AudioBuffer<float>& /*ir*/,
                                        double /*irSampleRate*/) {}

        virtual int numParams() const = 0;
        virtual ParamSpec paramSpec(int index) const = 0;
        virtual const std::string& effectId() const = 0;
        virtual juce::String badge() const = 0;
    };

    // Placement-aware quality tier. Some effects (e.g. Reverb, Delay, Saturation)
    // present two faces from a single catalogue entry: a lean LQ face on track
    // inserts and a richer, oversampled HQ face on the master bus / sends. The
    // tier is *structural* — it is derived from where the slot lives, never
    // stored — so every save/load and placement site must pass the tier that
    // matches its slot (Track for track inserts, Master for master inserts/sends).
    // Param schemas may differ between tiers; effects are never moved across
    // track<->master, so a given slot's tier (and thus its param set) is fixed.
    enum class EffectTier
    {
        Track,   // lean LQ face (~4 params, no oversampling)
        Master   // rich HQ face (~8 params, oversampled)
    };

    // Create an effect by stable string ID. Returns nullptr for unknown IDs.
    // `tier` selects the LQ/HQ face for tier-aware effects; non-tiered effects
    // ignore it. Legacy HQ-only ids (verbhq/delayhq) resolve to their HQ face
    // regardless of tier.
    std::unique_ptr<IEffect> makeEffectForId(const std::string& id,
                                             EffectTier tier = EffectTier::Track);

    // Normalise a stored effect id: maps deprecated ids to their canonical
    // catalogue id (e.g. the old master-only "lockstep.verbhq.v1" ->
    // "lockstep.reverb.v1", which auto-upgrades to HQ on a master slot).
    // Returns the id unchanged if it has no alias.
    std::string canonicalEffectId(const std::string& id);
}
