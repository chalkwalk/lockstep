#pragma once

namespace lockstep
{
    // Post-machine FLTR block parameters — one instance per PartTrack.
    // These are the base (unmodulated) values; step P-Locks layer on top.
    struct TrackFltrState
    {
        static constexpr int kNumSlots = 6;

        // stepped: 0=LP, 1=HP, 2=BP, 3=Notch, 4=OFF (bit-exact passthrough, no DSP).
        // 4=OFF is the new-track default (DESIGN §14). Values 0-3 are unchanged for
        // v17 compatibility — stored floats in old saves resolve to the correct mode.
        float mode = 4.0f;
        float slope = 1.0f;  // stepped: 0=12dB, 1=24dB; default 24dB
        float cutoff = 1.0f;  // 0..1 -> 20Hz..20kHz (log)
        float resonance = 0.0f;  // 0..1
        float drive = 0.0f;  // 0..1
        float envToCutoff = 0.0f;  // -1..1

        float getSlot(int s) const noexcept
        {
            static constexpr float TrackFltrState::* kSlots[] = {
                &TrackFltrState::mode,
                &TrackFltrState::slope,
                &TrackFltrState::cutoff,
                &TrackFltrState::resonance,
                &TrackFltrState::drive,
                &TrackFltrState::envToCutoff,
            };
            if (s >= 0 && s < kNumSlots) return this->*kSlots[s];
            return 0.0f;
        }

        void setSlot(int s, float v) noexcept
        {
            static constexpr float TrackFltrState::* kSlots[] = {
                &TrackFltrState::mode,
                &TrackFltrState::slope,
                &TrackFltrState::cutoff,
                &TrackFltrState::resonance,
                &TrackFltrState::drive,
                &TrackFltrState::envToCutoff,
            };
            if (s >= 0 && s < kNumSlots) this->*kSlots[s] = v;
        }
    };
}
