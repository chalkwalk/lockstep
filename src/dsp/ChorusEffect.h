#pragma once

#include "../machine/IEffect.h"
#include "TempoRate.h"
#include "../deckcore/Interpolation.h"
#include "OversamplingStages.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace lockstep
{
    // Multi-voice chorus — Rate (Hz) / Depth / Mix / Feedback (9.24 S9 rebuild).
    //
    // The original was a single integer-tap voice (thin, and it zipper-stepped as
    // the modulated delay quantised to whole samples). This rebuild runs THREE
    // fractional (4-point Hermite) voices per channel, each on a phase-offset LFO
    // (0, 120, 240 deg), with the right channel's LFOs rotated a further quarter
    // cycle so the wet signal decorrelates into a wide stereo image.
    //
    // The appended `chorus_fb` param (default 0) folds the summed wet back into the
    // delay line for a flanger-leaning resonance; at its default of 0 the write is
    // exactly the dry input, so old projects (which have no stored value) load with
    // the classic no-feedback character. Param ids rate/depth/mix are unchanged.
    //
    // The wet path (the whole modulated delay + feedback) runs 2x oversampled at
    // session rates <= ~48 kHz. The moving Hermite read tap is a time-varying
    // (Doppler) resampler; on near-Nyquist content it images/aliases, which the
    // dualsine torture probe exposed as a "siren". Running the delay at 2x pushes
    // that fold-back above base-Nyquist where the oversampler's decimation filter
    // removes it. Following the 9.25 R5 rate-scaled rule, the extra stage drops out
    // at >= 96 kHz (native headroom already covers it) — like Saturation/Distortion.
    // Only the wet path is oversampled; the dry is mixed at base rate, unchanged.
    class ChorusEffect final : public IEffect
    {
        static constexpr int kFxSec = 5;
        static constexpr int kVoices = 3;
        // Nominal centre delay and modulation swing (seconds). Base keeps the read
        // tap comfortably away from the write head; swing scales with Depth.
        static constexpr float kBaseSec = 0.011f;   // 11 ms centre
        static constexpr float kSwingSec = 0.008f;  // up to 8 ms of modulation

    public:
        void prepare(double sampleRate, int maxBlockSize) override
        {
            sampleRate_ = sampleRate;
            // 2x wet oversampling at <= ~48 kHz; drops to native (1x) above that,
            // per the 9.25 R5 rate-scaled rule (a modulated delay needs far less
            // headroom than a hard nonlinearity, so one stage is plenty and it can
            // vanish once the session rate already provides the octave).
            osFactor_ = (sampleRate_ < 48000.0 * 1.5) ? 2 : 1;
            osRate_ = sampleRate_ * static_cast<double>(osFactor_);
            smoothCoef_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sampleRate));
            smoothCoefOs_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * osRate_));
            const int bufLen = static_cast<int>(osRate_ * 0.05) + 1;  // 50ms max
            for (auto& b : buf_) b.assign(static_cast<std::size_t>(bufLen), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
            for (auto& z : mixZ_)   z = 0.5f;
            for (auto& z : depthZ_) z = 0.3f;
            for (auto& z : fbZ_)    z = 0.0f;

            const int maxB = std::max(1, maxBlockSize);
            for (auto& os : os_)
            {
                if (osFactor_ > 1)
                {
                    // Single half-band stage = 2x. Polyphase IIR: near-zero latency
                    // (no wet/dry misalignment in the mix), no PDC — matches Distortion.
                    os = std::make_unique<juce::dsp::Oversampling<float>>(
                        1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
                    os->initProcessing(static_cast<std::size_t>(maxB));
                }
                else
                {
                    os.reset();
                }
            }
            dryScratch_.assign(static_cast<std::size_t>(maxB), 0.0f);
        }

        void reset() override
        {
            for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : head_) h = 0;
            for (auto& ph : phase_) ph = 0.0f;
            for (auto& os : os_) if (os) os->reset();
        }

        // 9.31: tempo, broadcast per block by the processor. The modulation rate is
        // a period in beats, so the sweep tracks the song rather than the wall clock.
        void setTimeInfo(double bpm) override { bpm_ = bpm > 0.0 ? bpm : 120.0; }

        void process(juce::AudioBuffer<float>& buffer, int numSamples,
                     const ParamFrame& params) override
        {
            const int ch = buffer.getNumChannels();
            if (ch == 0 || numSamples <= 0) return;

            // 9.31: Rate is a PERIOD IN BEATS (it used to be a 0..1 knob scaled to
            // 0.1..5 Hz) -- the chorus now breathes with the tempo.
            const float periodBeats = params.size() > 0
                                   ? juce::jlimit(dsp::kMinModPeriod, dsp::kMaxModPeriod,
                                                  params[0])
                                   : 4.0f;
            const float rate = dsp::rateHzFromPeriodBeats(periodBeats, bpm_);
            const float depthTarget = params.size() > 1
                                          ? juce::jlimit(0.0f, 1.0f, params[1]) : 0.3f;
            const float mixTarget = params.size() > 2 ? params[2] : 0.5f;
            const float fbTarget = params.size() > 3
                                       ? juce::jlimit(0.0f, 0.95f, params[3]) : 0.0f;
            // LFO increment per oversampled sample (the delay runs at osRate_).
            const float phInc = static_cast<float>(
                rate * juce::MathConstants<double>::twoPi / osRate_);

            const int nOs = std::min(numSamples, static_cast<int>(dryScratch_.size()));

            for (int c = 0; c < std::min(ch, 2); ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const auto si = static_cast<std::size_t>(c);

                // Stash the base-rate dry for the final mix.
                for (int i = 0; i < nOs; ++i)
                    dryScratch_[static_cast<std::size_t>(i)] = data[i];

                // Upsample the dry into the wet work buffer (2x), or run in place
                // at native rate. `wet` is overwritten with wet-only by renderWet.
                float* wet = data;
                int wetN = nOs;
                juce::dsp::AudioBlock<float> block(&data, 1, static_cast<std::size_t>(nOs));
                if (os_[si])
                {
                    auto up = os_[si]->processSamplesUp(block);
                    wet = up.getChannelPointer(0);
                    wetN = static_cast<int>(up.getNumSamples());
                }

                renderWet(si, wet, wetN, depthTarget, fbTarget, rate, phInc, c);

                if (os_[si])
                    os_[si]->processSamplesDown(block);  // data <- base-rate wet

                // Base-rate dry/wet mix (smoothed).
                auto& mixZ = mixZ_[si];
                for (int i = 0; i < nOs; ++i)
                {
                    mixZ += smoothCoef_ * (mixTarget - mixZ);
                    data[i] = dryScratch_[static_cast<std::size_t>(i)] * (1.0f - mixZ)
                              + data[i] * mixZ;
                }
            }
            for (int c = 2; c < ch; ++c)
                buffer.copyFrom(c, 0, buffer, std::min(c, 1), 0, numSamples);
        }

        int numParams() const override { return 4; }

        ParamSpec paramSpec(int i) const override
        {
            auto mk = [](const char* id, const char* label, float def) {
                ParamSpec p;
                p.id = id;
                p.label = label;
                p.maxValue = 1.0f;
                p.defaultValue = def;
                p.sectionIndex = kFxSec;
                return p;
            };
            switch (i)
            {
                case 0: {
                    ParamSpec p;
                    p.id = "lockstep.chorus.rate";
                    p.label = "Rate";
                    p.minValue = dsp::kMinModPeriod;
                    p.maxValue = dsp::kMaxModPeriod;
                    p.defaultValue = 4.0f;   // one cycle per bar (~0.5 Hz at 120)
                    p.skew = 0.35f;
                    p.sectionIndex = kFxSec;
                    p.unit = ParamSpec::Unit::Beats;
                    p.detents = dsp::modPeriodDetents();
                    p.valueLabels = dsp::modPeriodLabels();
                    return p;
                }
                case 1: return mk("lockstep.chorus.depth", "Depth", 0.3f);
                case 2: return mk("lockstep.chorus.mix",   "Mix",   0.5f);
                // Appended S9. Default 0 = legacy no-feedback sound on old projects.
                case 3: return mk("lockstep.chorus.fb",    "Feedbk", 0.0f);
                default: return {};
            }
        }

        const std::string& effectId() const override { return kId; }
        juce::String badge() const override { return "CHR"; }

    private:
        double bpm_ = 120.0;   // 9.31 — fed per block by setTimeInfo
        // Run the three-voice modulated delay + feedback over `io[0..n)` at osRate_,
        // replacing each sample with the wet-only signal. Reads dry from io for the
        // delay-line write, so the caller must pass dry-in and gets wet-out.
        void renderWet(std::size_t si, float* io, int n, float depthTarget,
                       float fbTarget, float /*rate*/, float phInc, int c)
        {
            constexpr float twoPi = juce::MathConstants<float>::twoPi;
            // Per-voice LFO phase offsets (0, 120, 240 deg).
            constexpr std::array<float, kVoices> voiceOff = {
                0.0f, twoPi / 3.0f, 2.0f * twoPi / 3.0f };

            auto& b = buf_[si];
            auto& wr = head_[si];
            auto& ph = phase_[si];
            auto& depthZ = depthZ_[si];
            auto& fbZ = fbZ_[si];
            const int bufLen = static_cast<int>(b.size());
            // Right channel's LFOs are rotated a quarter cycle for stereo width.
            const float chanOff = (c == 1) ? (twoPi * 0.25f) : 0.0f;

            for (int i = 0; i < n; ++i)
            {
                depthZ += smoothCoefOs_ * (depthTarget - depthZ);
                fbZ    += smoothCoefOs_ * (fbTarget - fbZ);

                const float swing = kSwingSec * depthZ;
                float wetSum = 0.0f;
                for (int v = 0; v < kVoices; ++v)
                {
                    const float lfo = 0.5f
                        + 0.5f * std::sin(ph + voiceOff[static_cast<std::size_t>(v)]
                                          + chanOff);
                    const float delaySec = kBaseSec + swing * lfo;
                    const float dSamp = std::clamp(
                        delaySec * static_cast<float>(osRate_),
                        2.0f, static_cast<float>(bufLen - 3));
                    float readPos = static_cast<float>(wr) - dSamp;
                    if (readPos < 0.0f) readPos += static_cast<float>(bufLen);
                    const int i0 = static_cast<int>(readPos);
                    const float fr = readPos - static_cast<float>(i0);
                    const auto at = [&](int k) {
                        const int idx = ((i0 + k) % bufLen + bufLen) % bufLen;
                        return b[static_cast<std::size_t>(idx)];
                    };
                    wetSum += dc::hermite4(at(-1), at(0), at(1), at(2), fr);
                }
                const float wet = wetSum * (1.0f / static_cast<float>(kVoices));

                // Feedback (default 0 -> write is exactly the dry input, the classic
                // no-feedback chorus). Soft-limited for stability.
                const float fed = io[i] + fbZ * wet;
                b[static_cast<std::size_t>(wr)] = fed / (1.0f + std::abs(fed) * 0.2f);
                wr = (wr + 1) % bufLen;

                io[i] = wet;

                ph += phInc;
                if (ph > twoPi) ph -= twoPi;
            }
        }

        static inline const std::string kId = "lockstep.chorus.v1";
        double sampleRate_ = 44100.0;
        double osRate_ = 44100.0;
        int osFactor_ = 1;
        float smoothCoef_ = 0.005f;    // base-rate (mix)
        float smoothCoefOs_ = 0.005f;  // oversampled-rate (depth/feedback, in-delay)
        std::array<std::vector<float>, 2> buf_;
        std::array<int, 2> head_{};
        std::array<float, 2> phase_{};
        std::array<float, 2> mixZ_{ 0.5f, 0.5f };
        std::array<float, 2> depthZ_{ 0.3f, 0.3f };
        std::array<float, 2> fbZ_{ 0.0f, 0.0f };
        // One mono 2x oversampler per channel (null above ~48 kHz where the wet
        // path runs at native rate). dryScratch_ holds one channel's base-rate dry.
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> os_{};
        std::vector<float> dryScratch_;
    };
}
