#pragma once

namespace lockstep
{
    // CHANNEL block — always present on every audio track.
    // Holds the output-mix parameters: level, pan, two send taps, and the
    // output destination ("Out", A2 / DESIGN §27 — Master | Track N | Off).
    // Send A/B are post-fader taps into the master send buses (DESIGN §32.3).
    // Disk ids: lockstep.amp.level / .pan / .sendA / .sendB (unchanged from
    // v17), plus lockstep.amp.out (v25).
    struct TrackChannelState
    {
        static constexpr int kNumSlots = 5;

        float level = 1.0f;   // 0..2
        float pan   = 0.0f;   // -1..1
        float sendA = 0.0f;   // 0..1; default 0 (dry/off)
        float sendB = 0.0f;   // 0..1; default 0 (dry/off)
        float out   = 1.0f;   // OutputDest encoding; default 1 = Master

        float getSlot(int s) const noexcept
        {
            static constexpr float TrackChannelState::* kSlots[] = {
                &TrackChannelState::level,
                &TrackChannelState::pan,
                &TrackChannelState::sendA,
                &TrackChannelState::sendB,
                &TrackChannelState::out,
            };
            if (s >= 0 && s < kNumSlots) return this->*kSlots[s];
            return 0.0f;
        }

        void setSlot(int s, float v) noexcept
        {
            static constexpr float TrackChannelState::* kSlots[] = {
                &TrackChannelState::level,
                &TrackChannelState::pan,
                &TrackChannelState::sendA,
                &TrackChannelState::sendB,
                &TrackChannelState::out,
            };
            if (s >= 0 && s < kNumSlots) this->*kSlots[s] = v;
        }
    };
}
