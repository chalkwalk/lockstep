#pragma once

namespace lockstep
{
    // Scaffolds the 1-2ms micro-fade applied when a track retriggers while
    // its previous voice is still ringing out. Real implementation lands
    // alongside the sampler voice in M1.
    class VoiceChoke
    {
    public:
        VoiceChoke();
        ~VoiceChoke();

        void prepare(double sampleRate, float fadeMs);
        void trigger();
        bool  isFading() const { return fadeRemaining_ > 0; }
        float nextGain();  // per-sample: returns gain [1→0], advances counter

    private:
        int fadeSamples_ = 0;
        int fadeRemaining_ = 0;
    };
}
