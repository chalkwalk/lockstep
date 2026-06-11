#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace lockstep
{
    // Generates metronome click audio.
    // Beat interval and bar length are driven by the active Section's TimeSig
    // (Phase 7 / DESIGN §4.8): beatPpq = 4.0 / denominator; strong click every
    // numerator beats.  Defaults (4/4: beatPpq=1.0, numerator=4) are backward
    // compatible with callers that don't pass a time signature.
    // Call process() once per block while the sequencer is running; the output is
    // additive — it writes directly into the existing buffer samples.
    class Metronome
    {
    public:
        void prepare(double sampleRate);

        // blockStartPpq / blockEndPpq must be the ppqOffset-adjusted positions
        // (same coordinate space used by the sequencer tick loop).
        // numerator / denominator describe the current Section's core time (§4.8).
        void process(double blockStartPpq, double blockEndPpq,
                     double samplesPerPpq, juce::AudioBuffer<float>& buffer,
                     int numerator = 4, int denominator = 4);

        void reset()
        {
            amplitude_ = 0.0f;
            phase_ = 0.0;
        }

    private:
        double sampleRate_ = 44100.0;
        double phase_ = 0.0;
        float amplitude_ = 0.0f;
        float decayRate_ = 1.0f;
        float decayRateStrong_ = 1.0f;
        float decayRateWeak_ = 1.0f;
        double freqIncrement_ = 0.0;

        void trigger(bool strong);
    };
}
