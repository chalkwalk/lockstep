#include "BungeeStretchEngine.h"

#include <bungee/Bungee.h>

#include <cmath>

namespace lockstep
{
    // Bungee stretcher + its per-grain request live here so the Bungee/Eigen
    // headers never leak into the JUCE-only seam header.
    struct BungeeStretchEngine::Impl
    {
        std::unique_ptr<Bungee::Stretcher<Bungee::Basic>> stretcher;
        Bungee::Request req{};
    };

    BungeeStretchEngine::BungeeStretchEngine() : impl_(std::make_unique<Impl>()) {}
    BungeeStretchEngine::~BungeeStretchEngine() = default;

    void BungeeStretchEngine::prepare(double sourceRate, double outputRate,
                                      int maxChannels, int maxBlock)
    {
        outputRate_ = outputRate > 0.0 ? outputRate : 44100.0;
        sourceRate_ = sourceRate;               // <=0 ⇒ fold mode
        maxCh_ = juce::jmax(1, maxChannels);
        maxBlock_ = juce::jmax(1, maxBlock);

        // Native-rate mode builds the engine at the source rate (Bungee converts
        // internally); fold mode builds it rate-agnostic and folds each source's
        // own rate into speed/pitch at play time.
        const int inRate = static_cast<int>(std::lround(
            sourceRate_ > 0.0 ? sourceRate_ : outputRate_));
        const int outRate = static_cast<int>(std::lround(outputRate_));

        impl_->stretcher = std::make_unique<Bungee::Stretcher<Bungee::Basic>>(
            Bungee::SampleRates{ inRate, outRate }, maxCh_, 0);

        maxInputFrames_ = juce::jmax(1, impl_->stretcher->maxInputFrameCount());

        // Output per grain is a synthesis hop, comfortably below one max input
        // chunk; size the FIFO for a full block plus one grain's worth of slack.
        fifoCap_ = maxBlock_ + maxInputFrames_ + 512;
        fifo_.assign(static_cast<size_t>(maxCh_) * static_cast<size_t>(fifoCap_), 0.0f);
        scratch_.assign(static_cast<size_t>(maxCh_) * static_cast<size_t>(maxInputFrames_), 0.0f);

        reset();
    }

    void BungeeStretchEngine::reset()
    {
        src_ = nullptr;
        active_ = false;
        discarding_ = false;
        fifoLen_ = 0;
        position_ = 0.0;
        discardUntil_ = 0.0;
        loopStart_ = loopEnd_ = 0;
        reverse_ = false;
        timeRatio_ = pitchRatio_ = 1.0;
        latencySamples_ = 0;
    }

    void BungeeStretchEngine::start(IStretchSource* src, double startPos,
                                    double timeRatio, double pitchRatio)
    {
        src_ = src;
        timeRatio_ = timeRatio > 1.0e-6 ? timeRatio : 1.0e-6;
        pitchRatio_ = pitchRatio > 1.0e-6 ? pitchRatio : 1.0e-6;
        position_ = startPos;
        discardUntil_ = startPos;
        discarding_ = true;
        fifoLen_ = 0;
        active_ = (src_ != nullptr && impl_->stretcher != nullptr);
        if (!active_)
            return;

        // Fold source rate into speed/pitch in fold mode (r == 1 in native mode).
        const double r = (sourceRate_ <= 0.0 && src_ != nullptr)
            ? src_->sampleRate() / outputRate_ : 1.0;
        const double dir = reverse_ ? -1.0 : 1.0;

        impl_->req.position = startPos;
        impl_->req.speed = dir * (1.0 / timeRatio_) * r;
        impl_->req.pitch = pitchRatio_ * r;
        impl_->req.reset = true;
        impl_->req.resampleMode = resampleMode_autoInOut;

        impl_->stretcher->preroll(impl_->req);
    }

    void BungeeStretchEngine::setRatios(double timeRatio, double pitchRatio)
    {
        timeRatio_ = timeRatio > 1.0e-6 ? timeRatio : 1.0e-6;
        pitchRatio_ = pitchRatio > 1.0e-6 ? pitchRatio : 1.0e-6;
    }

    void BungeeStretchEngine::setLoop(juce::int64 loopStart, juce::int64 loopEnd)
    {
        loopStart_ = loopStart;
        loopEnd_ = loopEnd;
    }

    void BungeeStretchEngine::setReverse(bool reverse) { reverse_ = reverse; }

    void BungeeStretchEngine::fetchInput(float* dest, int ch, int begin, int n) const
    {
        if (src_ == nullptr)
        {
            std::fill(dest, dest + n, 0.0f);
            return;
        }

        const bool looping = loopEnd_ > loopStart_;
        if (!looping)
        {
            src_->read(dest, ch, begin, n);   // zero-fills OOB
            return;
        }

        const juce::int64 span = loopEnd_ - loopStart_;
        for (int i = 0; i < n; ++i)
        {
            juce::int64 idx = static_cast<juce::int64>(begin) + i;
            juce::int64 m = ((idx - loopStart_) % span + span) % span + loopStart_;
            src_->read(dest + i, ch, m, 1);
        }
    }

    void BungeeStretchEngine::produceGrain()
    {
        if (!active_ || impl_->stretcher == nullptr || src_ == nullptr)
            return;

        auto& stretcher = *impl_->stretcher;
        auto& req = impl_->req;

        // Refresh speed/pitch each grain (setRatios / reverse can change them).
        const double r = (sourceRate_ <= 0.0) ? src_->sampleRate() / outputRate_ : 1.0;
        const double dir = reverse_ ? -1.0 : 1.0;
        req.speed = dir * (1.0 / timeRatio_) * r;
        req.pitch = pitchRatio_ * r;

        const Bungee::InputChunk chunk = stretcher.specifyGrain(req);
        const int n = juce::jmax(0, chunk.end - chunk.begin);
        const int clampedN = juce::jmin(n, maxInputFrames_);

        for (int ch = 0; ch < maxCh_; ++ch)
            fetchInput(scratch_.data() + static_cast<size_t>(ch) * static_cast<size_t>(maxInputFrames_),
                       ch, chunk.begin, clampedN);

        const bool looping = loopEnd_ > loopStart_;
        const int srcLen = static_cast<int>(src_->length());
        const int muteHead = looping ? 0 : juce::jmax(0, -chunk.begin);
        const int muteTail = looping ? 0 : juce::jmax(0, chunk.end - srcLen);

        stretcher.analyseGrain(scratch_.data(), maxInputFrames_, muteHead, muteTail);

        Bungee::OutputChunk out{};
        stretcher.synthesiseGrain(out);

        // Proportional run-in discard so the first emitted frame is startPos
        // (mirrors Bungee's CommandLine::write preroll trimming).
        float* data = out.data;
        int fc = out.frameCount;
        if (discarding_)
        {
            const double pB = out.request[Bungee::OutputChunk::begin]->position;
            const double pE = out.request[Bungee::OutputChunk::end]->position;
            if (!std::isnan(pB) && pB != pE)
            {
                const double preIn = reverse_ ? (pB - discardUntil_)
                                              : (discardUntil_ - pB);
                const int nPre = juce::jmax(0, static_cast<int>(
                    std::lround(preIn * fc / std::abs(pE - pB))));
                if (fc > nPre) { data += nPre; fc -= nPre; discarding_ = false; }
                else           { fc = 0; }
            }
        }

        // Append fc frames to the planar FIFO.
        fc = juce::jmin(fc, fifoCap_ - fifoLen_);
        for (int ch = 0; ch < maxCh_; ++ch)
        {
            const float* srcRow = data + static_cast<std::ptrdiff_t>(ch) * out.channelStride;
            float* dstRow = fifo_.data() + static_cast<size_t>(ch) * static_cast<size_t>(fifoCap_)
                            + static_cast<size_t>(fifoLen_);
            for (int i = 0; i < fc; ++i)
                dstRow[i] = srcRow[i];
        }
        fifoLen_ += fc;

        // Advance the position sequence. Bungee infers grain speed from the
        // delta between successive request positions, so the position MUST stay
        // monotonic — folding it back at the loop seam would read as a huge seek
        // and glitch. Instead we let it run monotonically and wrap only the input
        // fetch (fetchInput maps indices into the loop window); a clean loop's
        // source audio is continuous across the wrap, so overlap-add is seamless.
        stretcher.next(req);

        if (looping)
        {
            // Report a folded position for phase anchoring (Stage 4).
            const double span = static_cast<double>(loopEnd_ - loopStart_);
            const double lo = static_cast<double>(loopStart_);
            double p = req.position;
            while (p >= static_cast<double>(loopEnd_)) p -= span;
            while (p < lo)                             p += span;
            position_ = p;
        }
        else
        {
            position_ = req.position;
            // Non-loop end: stop once the last produced grain has covered the
            // final sample (the vocoder tail is masked by the machine gate).
            if (!reverse_ && position_ >= static_cast<double>(srcLen)) active_ = false;
            if (reverse_ && position_ < 0.0)                           active_ = false;
        }
    }

    void BungeeStretchEngine::process(juce::AudioBuffer<float>& out, int startSample, int n)
    {
        // Fill the FIFO to at least n frames (bounded: each grain emits a hop).
        int guard = n + maxInputFrames_ + 8;
        while (fifoLen_ < n && active_ && guard-- > 0)
            produceGrain();

        const int have = juce::jmin(n, fifoLen_);
        const int nch = out.getNumChannels();
        for (int ch = 0; ch < nch; ++ch)
        {
            const int srcRow = juce::jmin(ch, maxCh_ - 1);
            const float* fifoRow = fifo_.data() + static_cast<size_t>(srcRow) * static_cast<size_t>(fifoCap_);
            float* dst = out.getWritePointer(ch, startSample);
            for (int i = 0; i < have; ++i) dst[i] = fifoRow[i];
            for (int i = have; i < n; ++i) dst[i] = 0.0f;
        }

        // Shift the consumed frames out of the FIFO.
        if (have > 0)
        {
            for (int ch = 0; ch < maxCh_; ++ch)
            {
                float* row = fifo_.data() + static_cast<size_t>(ch) * static_cast<size_t>(fifoCap_);
                std::move(row + have, row + fifoLen_, row);
            }
            fifoLen_ -= have;
        }
    }
}
