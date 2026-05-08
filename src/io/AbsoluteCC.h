#pragma once

namespace lockstep
{
    // 0..127 absolute MIDI CC. Soft-takeover: the incoming value must cross
    // the current internal value before it starts driving the parameter, to
    // prevent zippering when a knob's physical position disagrees with the
    // patch state.
    class AbsoluteCCRouter
    {
    public:
        // Returns the new internal value (possibly unchanged if the
        // takeover threshold has not yet been crossed).
        float route(float currentValue, float incomingNormalised);

    private:
        bool crossed_ = false;
        float lastIncoming_ = -1.0f;
    };
}
