#pragma once

namespace lockstep
{
    // Post-machine AMP block parameters — one instance per PartTrack.
    // These are the base (unmodulated) values; step P-Locks layer on top.
    struct TrackAmpState
    {
        static constexpr int kNumSlots = 8;

        float level    = 1.0f;   // 0..1
        float pan      = 0.0f;   // -1..1
        float gateSrc  = 0.0f;   // stepped: 0=Envelope, 1=Held-open
        float attack   = 1.0f;   // 0..5000 ms; default 1 ms (instant-feel)
        float hold     = 0.0f;   // 0..5000 ms
        float decay    = 0.0f;   // 0..5000 ms
        float sustain  = 1.0f;   // 0..1; default full sustain
        float release  = 10.0f;  // 0..5000 ms; default 10 ms

        float getSlot(int s) const noexcept
        {
            switch (s)
            {
            case 0: return level;
            case 1: return pan;
            case 2: return gateSrc;
            case 3: return attack;
            case 4: return hold;
            case 5: return decay;
            case 6: return sustain;
            case 7: return release;
            default: return 0.0f;
            }
        }

        void setSlot(int s, float v) noexcept
        {
            switch (s)
            {
            case 0: level   = v; break;
            case 1: pan     = v; break;
            case 2: gateSrc = v; break;
            case 3: attack  = v; break;
            case 4: hold    = v; break;
            case 5: decay   = v; break;
            case 6: sustain = v; break;
            case 7: release = v; break;
            default: break;
            }
        }
    };
}
