#pragma once

namespace lockstep
{
    // ENVELOPE block — present only for machines where !hasInternalAmp().
    // Holds the AHDSR amplitude-envelope parameters driven by the note gate.
    // Disk ids: lockstep.amp.gateSrc / .attack / .hold / .decay / .sustain / .release.
    struct TrackEnvState
    {
        static constexpr int kNumSlots = 6;

        // stepped: 0=Envelope (follows note gate), 1=Held-open (always on).
        // Default Held-open so a one-shot sample is not cut short by the step gate.
        float gateSrc = 1.0f;
        float attack  = 1.0f;    // 0..5000 ms; default 1 ms (instant-feel)
        float hold    = 0.0f;    // 0..5000 ms
        float decay   = 0.0f;    // 0..5000 ms
        float sustain = 1.0f;    // 0..1; default full sustain
        float release = 10.0f;   // 0..5000 ms; default 10 ms

        float getSlot(int s) const noexcept
        {
            static constexpr float TrackEnvState::* kSlots[] = {
                &TrackEnvState::gateSrc,
                &TrackEnvState::attack,
                &TrackEnvState::hold,
                &TrackEnvState::decay,
                &TrackEnvState::sustain,
                &TrackEnvState::release,
            };
            if (s >= 0 && s < kNumSlots) return this->*kSlots[s];
            return 0.0f;
        }

        void setSlot(int s, float v) noexcept
        {
            static constexpr float TrackEnvState::* kSlots[] = {
                &TrackEnvState::gateSrc,
                &TrackEnvState::attack,
                &TrackEnvState::hold,
                &TrackEnvState::decay,
                &TrackEnvState::sustain,
                &TrackEnvState::release,
            };
            if (s >= 0 && s < kNumSlots) this->*kSlots[s] = v;
        }
    };
}
