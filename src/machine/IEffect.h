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
                             int                      numSamples,
                             const ParamFrame&        params) = 0;

        virtual int      numParams()             const = 0;
        virtual ParamSpec paramSpec(int index)   const = 0;
        virtual const std::string& effectId()    const = 0;
        virtual juce::String badge()             const = 0;
    };

    // Create an effect by stable string ID. Returns nullptr for unknown IDs.
    std::unique_ptr<IEffect> makeEffectForId(const std::string& id);
}
