#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace lockstep
{
    // Real-time WSOLA time-stretch + resample voice for the StretchMachine (C1).
    //
    // Independent time and pitch: given a fixed source buffer, it plays back with a
    // time ratio (output duration / input duration) and a pitch ratio (output
    // frequency / input frequency) that are decoupled. Pitch is realised by reading
    // the source at the pitch rate (a fractional step), and duration by WSOLA
    // overlap-add in that pitch-shifted "P-space": stretch factor S = time × pitch,
    // so the net is duration = time, pitch = pitch.
    //
    // WSOLA: each synthesis hop overlap-adds a Hann-windowed analysis frame, whose
    // start is nudged within a search radius to maximise correlation with the
    // existing overlap tail — minimising phase discontinuity (the quality win over
    // plain OLA). The offset is computed once from channel 0 and applied to all
    // channels so the stereo image stays coherent.
    //
    // Not real-time-allocating once prepared. The source buffer is borrowed (not
    // owned) and must outlive playback.
    class TimeStretch
    {
    public:
        void prepare(double sampleRate, int maxChannels);
        void reset();

        // Start playback of `src` from `startSample`, at the given ratios.
        // timeRatio  > 1 = longer (slower), pitchRatio > 1 = higher.
        void start(const juce::AudioBuffer<float>* src, double startSample,
                   double timeRatio, double pitchRatio);

        // Update ratios mid-flight (e.g. tempo glide). Cheap.
        void setRatios(double timeRatio, double pitchRatio) noexcept;

        // Pull `n` output samples into out[startSample..], overwriting. Channels
        // beyond the source's are filled by replicating channel 0 (mono → stereo).
        // Stops (isActive()→false) when the source is exhausted; remaining samples
        // are zero-filled.
        void process(juce::AudioBuffer<float>& out, int startSample, int n);

        [[nodiscard]] bool isActive() const noexcept { return active_; }

    private:
        void produceHop();
        [[nodiscard]] float srcSample(int ch, double pIndex) const;
        [[nodiscard]] int bestOffset() const;

        static constexpr int kFrame = 1024;     // analysis/synthesis window W
        static constexpr int kHop = kFrame / 2; // synthesis hop Hs (50% overlap)
        static constexpr int kSearch = 200;     // ± WSOLA search radius (P samples)

        double sampleRate_ = 44100.0;
        int maxCh_ = 2;

        const juce::AudioBuffer<float>* src_ = nullptr;
        double srcStart_ = 0.0;     // source sample for P-index 0
        double pitchRatio_ = 1.0;   // source samples per P sample
        double stretchS_ = 1.0;     // WSOLA factor in P-space (time × pitch)
        double pPos_ = 0.0;         // analysis position in P-space
        double pLen_ = 0.0;         // source length in P-space
        bool active_ = false;

        std::vector<float> win_;                 // Hann window, kFrame
        juce::AudioBuffer<float> acc_;           // OLA accumulator (maxCh × kFrame)
        juce::AudioBuffer<float> hop_;           // last emitted hop (maxCh × kHop)
        int hopAvail_ = 0;                       // unread samples left in hop_
    };
}
