#include "Metronome.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    static constexpr double kStrongFreq = 1000.0;
    static constexpr double kWeakFreq = 800.0;
    static constexpr float kStrongAmp = 0.5f;
    static constexpr float kWeakAmp = 0.3f;
    static constexpr float kStrongDecayMs = 50.0f;
    static constexpr float kWeakDecayMs = 30.0f;
    static constexpr float kFloor = 0.001f;

    void Metronome::prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        amplitude_ = 0.0f;
        phase_ = 0.0;
        freqIncrement_ = 0.0;

        // rate^N = kFloor/initAmp after N = decayMs * sampleRate / 1000 samples.
        auto makeDecay = [&](float initAmp, float decayMs) {
            const float n = decayMs * 0.001f * static_cast<float>(sampleRate);
            return std::pow(kFloor / initAmp, 1.0f / n);
        };
        decayRateStrong_ = makeDecay(kStrongAmp, kStrongDecayMs);
        decayRateWeak_ = makeDecay(kWeakAmp, kWeakDecayMs);
        decayRate_ = decayRateStrong_;
    }

    void Metronome::trigger(bool strong)
    {
        phase_ = 0.0;
        amplitude_ = (strong ? kStrongAmp : kWeakAmp) * level_;
        decayRate_ = strong ? decayRateStrong_ : decayRateWeak_;
        freqIncrement_ = juce::MathConstants<double>::twoPi * (strong ? kStrongFreq : kWeakFreq) / sampleRate_;
    }

    void Metronome::process(double blockStartPpq, double blockEndPpq,
                            double samplesPerPpq, juce::AudioBuffer<float>& buffer,
                            int numerator, int denominator)
    {
        if (samplesPerPpq <= 0.0) return;
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (numSamples <= 0 || numCh <= 0) return;

        // Beat interval: one denominator-note in PPQ.  Quarter note (denom=4) = 1.0 PPQ.
        const double beatPpq = (denominator > 0)
                                   ? (4.0 / static_cast<double>(denominator))
                                   : 1.0;
        const int beatsPerBar = (numerator > 0) ? numerator : 4;

        // Pre-compute trigger points: beats at beatPpq intervals.
        // Strong beat (bar 1): every beatsPerBar beats (beat index % beatsPerBar == 0).
        struct TrigPoint
        {
            int sample;
            bool strong;
        };
        std::array<TrigPoint, 16> trigs{};
        int numTrigs = 0;

        // First candidate beat index = floor(blockStart/beat). The per-sample
        // guard below (`ppq >= blockStartPpq`) decides emission, and blockEnd is
        // exclusive (`ppq >= blockEndPpq` breaks), so a beat landing exactly on a
        // block boundary is emitted once by the block that *starts* on it. The old
        // `+ 1` skipped that boundary beat entirely — dropping the click on the
        // downbeat (PPQ 0) and making the whole click track feel a beat late
        // against on-grid trigs.
        const auto firstBeatIdx =
            static_cast<std::int64_t>(std::floor(blockStartPpq / beatPpq));
        for (auto beatIdx = firstBeatIdx;
             numTrigs < static_cast<int>(trigs.size()); ++beatIdx)
        {
            const double ppq = static_cast<double>(beatIdx) * beatPpq;
            if (ppq >= blockEndPpq) break;
            if (ppq >= blockStartPpq)
            {
                const bool strong = (beatIdx % static_cast<std::int64_t>(beatsPerBar) == 0);
                const int at = std::clamp(
                    static_cast<int>((ppq - blockStartPpq) * samplesPerPpq),
                    0, numSamples - 1);
                trigs[static_cast<std::size_t>(numTrigs++)] = { at, strong };
            }
        }

        int nextTrig = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            if (nextTrig < numTrigs && trigs[static_cast<std::size_t>(nextTrig)].sample == i)
                trigger(trigs[static_cast<std::size_t>(nextTrig++)].strong);

            float s = 0.0f;
            if (amplitude_ > kFloor)
            {
                s = static_cast<float>(std::sin(phase_)) * amplitude_;
                phase_ += freqIncrement_;
                amplitude_ *= decayRate_;
            }

            for (int ch = 0; ch < numCh; ++ch)
                buffer.addSample(ch, i, s);
        }
    }
}
