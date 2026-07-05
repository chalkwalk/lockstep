#pragma once

#include "IStretchEngine.h"
#include <memory>
#include <vector>

namespace lockstep
{
    // Bungee-backed IStretchEngine (9.23). The Bungee/Eigen headers are confined
    // to BungeeStretchEngine.cpp (Eigen compile cost + -Werror containment): the
    // stretcher lives behind a forward-declared Impl, so this header stays
    // JUCE-only. See IStretchEngine.h for the seam contract and threading rules.
    //
    // Internals: a granular pull loop (specifyGrain → analyseGrain →
    // synthesiseGrain → next) feeds a planar output FIFO that process() drains a
    // block at a time. Looping folds the source position back into
    // [loopStart, loopEnd); the overlap-add makes the seam continuous with no
    // restart/crossfade. Reverse runs the position sequence backwards.
    class BungeeStretchEngine final : public IStretchEngine
    {
    public:
        BungeeStretchEngine();
        ~BungeeStretchEngine() override;

        void prepare(double sourceRate, double outputRate,
                     int maxChannels, int maxBlock) override;
        void reset() override;
        void start(IStretchSource* src, double startPos,
                   double timeRatio, double pitchRatio) override;
        void setRatios(double timeRatio, double pitchRatio) override;
        void setLoop(juce::int64 loopStart, juce::int64 loopEnd) override;
        void setReverse(bool reverse) override;
        [[nodiscard]] double sourcePosition() const override { return position_; }
        void process(juce::AudioBuffer<float>& out, int startSample, int n) override;
        [[nodiscard]] bool isActive() const override { return active_; }
        [[nodiscard]] int latencySamples() const override { return latencySamples_; }

    private:
        struct Impl;                       // holds the Bungee stretcher (see .cpp)
        std::unique_ptr<Impl> impl_;

        // Pull the next grain from Bungee and append its output to the FIFO.
        // Honours the run-in discard and loop/reverse position sequencing.
        void produceGrain();
        // Fetch [begin, begin+n) source frames for channel `ch` into `dest`,
        // wrapping across the loop seam when a loop is active, else zero-filling.
        void fetchInput(float* dest, int ch, int begin, int n) const;

        IStretchSource* src_ = nullptr;
        double outputRate_ = 44100.0;
        double sourceRate_ = 0.0;    // <=0 ⇒ fold mode (per-source rate comp)
        int    maxCh_ = 2;
        int    maxBlock_ = 512;
        int    maxInputFrames_ = 0;  // scratch sizing, from Bungee
        int    latencySamples_ = 0;

        double timeRatio_ = 1.0;
        double pitchRatio_ = 1.0;
        bool   reverse_ = false;

        juce::int64 loopStart_ = 0;  // loopStart_ == loopEnd_ ⇒ no loop
        juce::int64 loopEnd_ = 0;

        double position_ = 0.0;      // current source frame (grain centre)
        double discardUntil_ = 0.0;  // run-in discard boundary (source frame)
        bool   discarding_ = false;
        bool   active_ = false;

        // Planar output FIFO: maxCh_ rows of fifoCap_ frames, fifoLen_ valid.
        std::vector<float> fifo_;
        int fifoCap_ = 0;
        int fifoLen_ = 0;

        // Planar read scratch: maxCh_ rows of maxInputFrames_ frames.
        std::vector<float> scratch_;
    };
}
