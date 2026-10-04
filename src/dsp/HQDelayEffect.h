#pragma once

#include "../machine/IEffect.h"
#include "../deckcore/Interpolation.h"
#include <cmath>
#include <vector>
#include <numbers>

namespace lockstep
{
    // HQ Delay — tempo-synced ping-pong with color filter in feedback path.
    // Uses setTimeInfo(bpm) to update tempo. Mix defaults to 1.0.
    // masterOnly = true.
    //
    // Delay length is a fractional (sub-sample) target read with 4-point Hermite
    // interpolation (9.24 S8). The read head slews toward the target with a
    // one-pole "tape bend" (small tempo/division nudges glide, doppler-style);
    // a jump larger than ~50 ms (a division switch) instead does a short
    // equal-power-ish crossfade between the old and new taps rather than sweeping
    // the whole distance, which would be an audible pitch dive.
    class HQDelayEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        // The division lattice, in beats (1.0 = a quarter note), ascending:
        // 1/16, 1/8T, 1/8, 1/8., 1/4, 1/4., 1/2. Delay time is a *continuous*
        // beat-fraction that a bare encoder turn snaps to one of these, and a
        // Func-turn sweeps freely between (A4). Tempo-relative either way — the
        // absolute/character delay is the plain DelayEffect, in milliseconds.
        static constexpr float kDivBeats[] = {
            0.25f, 1.0f/3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f
        };
        static constexpr const char* const kDivLabels[] = {
            "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2"
        };
        static constexpr int kNumDivs = 7;
        static constexpr float kMinBeats = kDivBeats[0];
        static constexpr float kMaxBeats = kDivBeats[kNumDivs - 1];

    public:
        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sr_ = sampleRate;
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            // Tape-bend one-pole (~50 ms) and jump/crossfade thresholds, SR-scaled.
            slewCoef_ = 1.0 - std::exp(-1.0 / (0.05 * sampleRate));
            bigJumpThresh_ = 0.05 * sampleRate;
            xfadeLen_ = std::max(1, static_cast<int>(0.02 * sampleRate));
            xfadeCount_ = 0;
            const int maxBufLen = static_cast<int>(sampleRate * 2.1);  // ~2s max
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(maxBufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& z : fbkZ_) z = 0.0f;
            for (auto& z : mixZ_) z = 1.0f;
            for (auto& z : colorZ_) z = 0.0f;
            updateDelayLen();
            delayCur_ = delayTarget_;   // no initial slew glitch
            delayOld_ = delayTarget_;
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& z : fbkZ_) z = 0.0f;
            delayCur_ = delayTarget_;
            delayOld_ = delayTarget_;
            xfadeCount_ = 0;
        }

        void setTimeInfo(double bpm) override
        {
            bpm_ = bpm > 0.0 ? bpm : 120.0;
            updateDelayLen();
        }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int numCh = buffer.getNumChannels();
            if (numCh == 0 || numSamples <= 0) return;

            const float beats  = params.size() > 0
                                     ? juce::jlimit(kMinBeats, kMaxBeats, params[0])
                                     : 1.0f;
            const float fbkTgt = params.size() > 1
                                     ? juce::jlimit(0.0f, 1.1f, params[1])
                                     : 0.4f;
            const float color  = params.size() > 2
                                     ? juce::jlimit(-1.0f, 1.0f, params[2])
                                     : 0.0f;
            const float width  = params.size() > 3
                                     ? juce::jlimit(0.0f, 1.0f, params[3])
                                     : 1.0f;
            const float mixTgt = params.size() > 4
                                     ? juce::jlimit(0.0f, 1.0f, params[4])
                                     : 1.0f;

            // Any change re-derives the target; the tape-bend slew (or, past ~50 ms,
            // the crossfade) absorbs it, so a free sweep glides and a detent jump
            // does not dive in pitch.
            if (std::abs(beats - lastBeats_) > 1.0e-6f) { lastBeats_ = beats; updateDelayLen(); }

            const float colorAlpha = 1.0f - std::exp(
                -2.0f * std::numbers::pi_v<float> * (color >= 0.0f ? 3000.0f : 200.0f)
                / static_cast<float>(sr_));

            const int bufLen = static_cast<int>(buf_[0].size());
            // 4-point Hermite read of `b` at a fractional delay behind `head`.
            const auto readFrac = [&](const std::vector<float>& b, int head,
                                      double delay) {
                double readPos = static_cast<double>(head) - delay;
                while (readPos < 0.0) readPos += static_cast<double>(bufLen);
                const int i0 = static_cast<int>(readPos);
                const float fr = static_cast<float>(readPos - static_cast<double>(i0));
                const auto at = [&](int k) {
                    const int idx = ((i0 + k) % bufLen + bufLen) % bufLen;
                    return b[static_cast<std::size_t>(idx)];
                };
                return dc::hermite4(at(-1), at(0), at(1), at(2), fr);
            };

            for (int n = 0; n < numSamples; ++n)
            {
                const float inL = numCh > 0 ? buffer.getReadPointer(0)[n] : 0.0f;
                const float inR = numCh > 1 ? buffer.getReadPointer(1)[n] : inL;

                // Advance the read length: tape-bend slew for small changes, a
                // short crossfade for a big jump (division switch) so we don't
                // sweep the whole distance and pitch-dive.
                bool xfade = xfadeCount_ > 0;
                if (!xfade)
                {
                    const double diff = delayTarget_ - delayCur_;
                    if (std::abs(diff) > bigJumpThresh_)
                    {
                        delayOld_ = delayCur_;
                        delayCur_ = delayTarget_;
                        xfadeCount_ = xfadeLen_;
                        xfade = true;
                    }
                    else
                    {
                        delayCur_ += slewCoef_ * diff;
                    }
                }
                float newGain = 1.0f;
                if (xfade)
                {
                    newGain = 1.0f - static_cast<float>(xfadeCount_)
                                         / static_cast<float>(xfadeLen_);
                    --xfadeCount_;
                }

                // Read delay taps (fractional, Hermite). Ping-pong is realised in
                // the write below; the two heads advance in lockstep so the read
                // index is the same for both channels.
                const auto readTap = [&](const std::vector<float>& b) {
                    const float nw = readFrac(b, head_[0], delayCur_);
                    if (!xfade) return nw;
                    const float od = readFrac(b, head_[0], delayOld_);
                    return od * (1.0f - newGain) + nw * newGain;
                };
                float dlyL = readTap(buf_[0]);
                float dlyR = readTap(buf_[1]);

                // Color filter in feedback (tilt-like: LP if color<0, HP if color>0).
                for (int c = 0; c < 2; ++c)
                {
                    auto& cz = colorZ_[static_cast<std::size_t>(c)];
                    const float dly = (c == 0) ? dlyL : dlyR;
                    cz += colorAlpha * (dly - cz);
                    const float filtered = (color >= 0.0f) ? (dly - cz) : cz;
                    if (c == 0) dlyL = filtered; else dlyR = filtered;
                }

                // Feedback limiting (soft-clip at 1.0).
                const auto softClip = [](float x) {
                    return x / (1.0f + std::abs(x));
                };
                fbkZ_[0] += smoothCoef_ * (fbkTgt - fbkZ_[0]);
                fbkZ_[1] += smoothCoef_ * (fbkTgt - fbkZ_[1]);
                mixZ_[0] += smoothCoef_ * (mixTgt - mixZ_[0]);
                mixZ_[1] += smoothCoef_ * (mixTgt - mixZ_[1]);

                // Write: L channel gets direct input + R's delayed feedback.
                buf_[0][static_cast<std::size_t>(head_[0])] = inL + softClip(dlyR * fbkZ_[0]);
                buf_[1][static_cast<std::size_t>(head_[1])] = inR + softClip(dlyL * fbkZ_[1]);
                head_[0] = (head_[0] + 1) % bufLen;
                head_[1] = (head_[1] + 1) % bufLen;

                // Width: blend ping-pong with mono delay.
                const float mono = (dlyL + dlyR) * 0.5f;
                const float outL = mono * (1.0f - width) + dlyL * width;
                const float outR = mono * (1.0f - width) + dlyR * width;

                if (numCh > 0)
                    buffer.getWritePointer(0)[n] = inL * (1.0f - mixZ_[0]) + outL * mixZ_[0];
                if (numCh > 1)
                    buffer.getWritePointer(1)[n] = inR * (1.0f - mixZ_[1]) + outR * mixZ_[1];
            }
            for (int c = 2; c < numCh; ++c)
                buffer.copyFrom(c, 0, buffer, 0, 0, numSamples);
        }

        [[nodiscard]] int numParams() const override { return 5; }

        [[nodiscard]] ParamSpec paramSpec(int index) const override
        {
            static const std::span<const char* const> kDivSpan{ kDivLabels,
                static_cast<std::size_t>(kNumDivs) };
            ParamSpec p;
            p.sectionIndex = kFxSec;
            switch (index)
            {
                case 0:
                    p.id = "lockstep.delayhq.time";
                    p.label = "Time";
                    p.minValue = kMinBeats; p.maxValue = kMaxBeats;
                    p.defaultValue = 1.0f;   // 1/4
                    p.unit = ParamSpec::Unit::Beats;
                    p.detents = std::span<const float>(kDivBeats,
                        static_cast<std::size_t>(kNumDivs));
                    p.valueLabels = kDivSpan;   // parallel to `detents`, not indexed
                    break;
                case 1:
                    p.id = "lockstep.delayhq.feedback";
                    p.label = "Feedbk";
                    p.minValue = 0.0f; p.maxValue = 1.1f; p.defaultValue = 0.4f;
                    break;
                case 2:
                    p.id = "lockstep.delayhq.color";
                    p.label = "Color";
                    p.minValue = -1.0f; p.maxValue = 1.0f; p.defaultValue = 0.0f;
                    break;
                case 3:
                    p.id = "lockstep.delayhq.width";
                    p.label = "Width";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
                default:
                    p.id = "lockstep.delayhq.mix";
                    p.label = "Mix";
                    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = 1.0f;
                    break;
            }
            return p;
        }

        [[nodiscard]] const std::string& effectId() const override { return kId; }
        [[nodiscard]] juce::String badge() const override { return "DLH"; }

    private:
        void updateDelayLen()
        {
            const double divBeats = static_cast<double>(
                juce::jlimit(kMinBeats, kMaxBeats, lastBeats_));
            const double beatSecs = 60.0 / bpm_;
            const double maxLen = static_cast<double>(buf_[0].size());
            // Keep two samples of head-room for the Hermite neighbours.
            delayTarget_ = juce::jlimit(2.0, maxLen - 3.0,
                divBeats * beatSecs * sr_);
        }

        // HQ face of the unified "Delay" entry (auto-selected on master slots).
        static inline const std::string kId = "lockstep.delay.v1";

        double sr_ = 44100.0;
        double bpm_ = 120.0;
        float smoothCoef_ = 0.01f;

        std::vector<float> buf_[2];
        int head_[2] = { 0, 0 };
        double delayTarget_ = 22050.0;  // fractional target length (samples)
        double delayCur_ = 22050.0;     // slewed read length (tape bend)
        double delayOld_ = 22050.0;     // frozen tap during a crossfade
        double slewCoef_ = 0.0f;        // one-pole tape-bend coefficient
        double bigJumpThresh_ = 2205.0; // >~50 ms jump -> crossfade, not slew
        int xfadeLen_ = 882;            // ~20 ms crossfade
        int xfadeCount_ = 0;            // samples remaining in the crossfade
        float lastBeats_ = 1.0f;   // 1/4 (the default detent)

        float fbkZ_[2] = { 0.0f, 0.0f };
        float mixZ_[2] = { 1.0f, 1.0f };
        float colorZ_[2] = { 0.0f, 0.0f };
    };
}
