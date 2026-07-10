#pragma once

#include "ChannelView.h"
#include "Medium.h"
#include "Resampler.h"

#include <cmath>

namespace dc
{
    // The heads (DESIGN §40.10).
    //
    // A head is an object *over* a medium, not a field *of* one. It owns a signed
    // fractional position and a rate — medium samples advanced per engine sample —
    // and nothing else. Rate is a full signed axis: 1 is play, 0.5 is half speed,
    // 0 is a stalled head, negative is reverse, and scrub sweeps continuously
    // through zero. There is no "normal speed" special case anywhere below.
    //
    // Many heads may share one medium. A read head may also be **slaved** to
    // another head at a fixed offset instead of running its own position, which
    // is the entirety of what a tape delay's taps are: one write head, N read
    // heads trailing it. Lockstep's decks use one of each; the partner app's echo
    // mode uses several (docs/partner-app-concept.md).
    //
    // Feedback is *caller-side*. The heads know the signal law; the host knows the
    // topology. Step the heads per sample and a delay's regeneration path closes
    // with no latency; step them per block and it closes at block granularity.

    // The shared kernel bank. Building it allocates, so it is built once, off the
    // audio thread, and shared by every head in the process.
    inline const Resampler& sharedKernels()
    {
        static const Resampler r;
        return r;
    }

    class Head
    {
    public:
        virtual ~Head() = default;

        [[nodiscard]] double position() const noexcept { return pos_; }
        void setPosition(double p) noexcept { pos_ = p; }

        [[nodiscard]] double rate() const noexcept { return rate_; }
        void setRate(double r) noexcept { rate_ = r; }

        // Advance by one engine sample. Circular media keep the position inside
        // [0, capacity) so it cannot drift out of double precision over a long
        // performance; linear media let it run off the reel, where every access
        // resolves to nothing.
        virtual void step(const Medium& m) noexcept
        {
            pos_ += rate_;
            if (m.topology() == Topology::Circular && m.capacity() > 0)
            {
                const auto cap = static_cast<double>(m.capacity());
                pos_ = std::fmod(pos_, cap);
                if (pos_ < 0.0) pos_ += cap;
            }
        }

    protected:
        double pos_ = 0.0;
        double rate_ = 1.0;
    };

    // Bandlimited fractional read. The cutoff tracks the rate (reading faster than
    // unity scales the source spectrum up, so the kernel drops to Nyquist/rate
    // before anything folds); direction is irrelevant because the kernel is
    // symmetric and only |rate| picks the bucket; rate 0 holds a sample.
    //
    // The tap loop goes through Medium::read rather than a raw pointer, so wrap
    // (circular) / silence-off-the-end (linear) / the high-water rule all hold
    // for free, and an i16 medium reads the same as an f32 one.
    class ReadHead : public Head
    {
    public:
        // Slave this head to another at `offsetSamples` BEHIND it — a delay tap.
        // The tap's position is derived, never advanced; step() on it is a no-op.
        // Passing nullptr unslaves it at its current derived position.
        void follow(const Head* leader, double offsetSamples) noexcept
        {
            if (leader == nullptr && leader_ != nullptr)
                pos_ = effectivePosition();
            leader_ = leader;
            offset_ = offsetSamples;
        }

        [[nodiscard]] bool slaved() const noexcept { return leader_ != nullptr; }
        [[nodiscard]] double offset() const noexcept { return offset_; }
        void setOffset(double offsetSamples) noexcept { offset_ = offsetSamples; }

        // Where this head actually sits. A slaved head trails its leader; the
        // leader's rate is the tap's rate too, which is why a varispeed sweep
        // stretches the echo instead of merely retuning it.
        [[nodiscard]] double effectivePosition() const noexcept
        {
            return leader_ != nullptr ? leader_->position() - offset_ : pos_;
        }

        [[nodiscard]] double effectiveRate() const noexcept
        {
            return leader_ != nullptr ? leader_->rate() : rate_;
        }

        void step(const Medium& m) noexcept override
        {
            if (leader_ != nullptr) return;  // derived from the leader
            Head::step(m);
        }

        // One frame from `sub` into `out[0..numChans)`. Channels beyond the
        // sub-track's are left untouched.
        void readFrame(const Medium& m, int sub, float* out, int numChans) const noexcept
        {
            const double p = effectivePosition();
            const double baseF = std::floor(p);
            const auto base = static_cast<std::int64_t>(baseF);
            const auto k = sharedKernels().kernelFor(effectiveRate(), p - baseF);

            const int chans = std::min(numChans, m.channels());
            for (int ch = 0; ch < chans; ++ch)
            {
                float acc = 0.0f;
                for (int i = 0; i < Resampler::kTaps; ++i)
                {
                    const std::int64_t idx = base - (Resampler::kHalf - 1) + i;
                    acc += m.read(sub, ch, idx) * k.taps[static_cast<std::size_t>(i)];
                }
                out[ch] = acc;
            }
        }

    private:
        const Head* leader_ = nullptr;
        double offset_ = 0.0;
    };

    // Bandlimited scatter-add: the transpose of the read. One input sample is
    // distributed across the kernel at the head's fractional position, so a
    // varispeed write band-limits itself instead of quantising to the nearest
    // medium sample.
    //
    // The deposit is scaled by |rate| — kernel density on the medium is 1/rate,
    // so this one factor makes the write amplitude-invariant (flux does not
    // depend on transport speed), makes a stalled head write *nothing* rather
    // than pile unbounded energy on one spot, and lets a scrub through zero fade
    // out and back in at the turnaround.
    //
    // Add-only: the caller owns erasure (EraseHead) and decay. Overlapping
    // windows therefore never multiply existing content.
    class WriteHead : public Head
    {
    public:
        void writeFrame(Medium& m, int sub, const float* in, int numChans) noexcept
        {
            if (! m.bound() || m.capacity() <= 0) return;

            const double baseF = std::floor(pos_);
            const auto base = static_cast<std::int64_t>(baseF);
            const auto k = sharedKernels().kernelFor(rate_, pos_ - baseF);
            if (k.writeGain <= 0.0f) return;  // a stalled head deposits nothing

            commit(m, sub, base);

            const int chans = std::min(numChans, m.channels());
            for (int ch = 0; ch < chans; ++ch)
            {
                const float v = in[ch] * k.writeGain;
                for (int i = 0; i < Resampler::kTaps; ++i)
                {
                    const std::int64_t idx = base - (Resampler::kHalf - 1) + i;
                    m.add(sub, ch, idx, v * k.taps[static_cast<std::size_t>(i)]);
                }
            }
        }

    private:
        // Turn the storage the kernel is about to touch from garbage into silence.
        // A loop is addressable everywhere the moment it is written to at all, so
        // a circular medium commits whole; a reel commits the span under the head.
        static void commit(Medium& m, int sub, std::int64_t base) noexcept
        {
            if (m.topology() == Topology::Circular)
            {
                m.ensureCommitted(sub, m.capacity());
                return;
            }
            const std::int64_t end = base + Resampler::kHalf + 1;
            if (end > 0)
                m.ensureCommitted(sub, static_cast<int>(std::min<std::int64_t>(end, m.capacity())));
        }
    };
}
