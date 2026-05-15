#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace lockstep
{
    // Generates metronome click audio (4/4 assumed).
    // Beats fall at every integer PPQ value; beat 1 of the bar is any multiple of 4.
    // Call process() once per block while the sequencer is running; the output is
    // additive — it writes directly into the existing buffer samples.
    class Metronome
    {
    public:
        void prepare(double sampleRate);

        // blockStartPpq / blockEndPpq must be the ppqOffset-adjusted positions
        // (same coordinate space used by the sequencer tick loop).
        void process(double blockStartPpq, double blockEndPpq,
                     double samplesPerPpq, juce::AudioBuffer<float>& buffer);

        void reset() { amplitude_ = 0.0f; phase_ = 0.0; }

    private:
        double sampleRate_       = 44100.0;
        double phase_            = 0.0;
        float  amplitude_        = 0.0f;
        float  decayRate_        = 1.0f;
        float  decayRateStrong_  = 1.0f;
        float  decayRateWeak_    = 1.0f;
        double freqIncrement_    = 0.0;

        void trigger(bool strong);
    };
}
