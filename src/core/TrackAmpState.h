#pragma once

namespace lockstep
{
    // Post-machine AMP block parameters — one instance per PartTrack.
    // These are the base (unmodulated) values; step P-Locks layer on top.
    struct TrackAmpState
    {
        static constexpr int kNumSlots = 10;  // 8 existing + sendA (8) + sendB (9)

        float level = 0.5f;   // 0..1
        float pan = 0.0f;   // -1..1
        // stepped: 0=Envelope (amp follows the note gate), 1=Held-open (gate
        // ignored; the source plays its full length). Default Held-open so a
        // one-shot sample (e.g. a sub kick) is not cut short by the step gate —
        // the gate-following envelope chops the sample body and leaves only the
        // transient, which sounds like a high-pass. Pitched/sustained sources
        // can opt into Envelope mode.
        float gateSrc = 1.0f;
        float attack = 1.0f;   // 0..5000 ms; default 1 ms (instant-feel)
        float hold = 0.0f;   // 0..5000 ms
        float decay = 0.0f;   // 0..5000 ms
        float sustain = 1.0f;   // 0..1; default full sustain
        float release = 10.0f;  // 0..5000 ms; default 10 ms
        // AMP page 2: send levels (8.26 / DESIGN §32.3). Post-fader taps.
        float sendA = 0.0f;    // 0..1; default 0 (dry/off)
        float sendB = 0.0f;    // 0..1; default 0 (dry/off)

        float getSlot(int s) const noexcept
        {
            switch (s)
            {
                case 0:  return level;
                case 1:  return pan;
                case 2:  return gateSrc;
                case 3:  return attack;
                case 4:  return hold;
                case 5:  return decay;
                case 6:  return sustain;
                case 7:  return release;
                case 8:  return sendA;
                case 9:  return sendB;
                default: return 0.0f;
            }
        }

        void setSlot(int s, float v) noexcept
        {
            switch (s)
            {
                case 0:  level = v; break;
                case 1:  pan = v; break;
                case 2:  gateSrc = v; break;
                case 3:  attack = v; break;
                case 4:  hold = v; break;
                case 5:  decay = v; break;
                case 6:  sustain = v; break;
                case 7:  release = v; break;
                case 8:  sendA = v; break;
                case 9:  sendB = v; break;
                default: break;
            }
        }
    };
}
