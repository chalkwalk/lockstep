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
            switch (s)
            {
                case 0:  return mode;
                case 1:  return slope;
                case 2:  return cutoff;
                case 3:  return resonance;
                case 4:  return drive;
                case 5:  return envToCutoff;
                default: return 0.0f;
            }
        }

        void setSlot(int s, float v) noexcept
        {
            switch (s)
            {
                case 0:  mode = v; break;
                case 1:  slope = v; break;
                case 2:  cutoff = v; break;
                case 3:  resonance = v; break;
                case 4:  drive = v; break;
                case 5:  envToCutoff = v; break;
                default: break;
            }
        }
    };
}
