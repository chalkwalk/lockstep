#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SvfFilter.h"
#include "../core/TrackFltrState.h"

namespace lockstep
{
    // Audio-thread FLTR DSP state — one instance per track in the processor.
    // Owns two SVF stages (for 12/24 dB), an Env->Cutoff one-pole follower,
    // and all per-channel filter state for a stereo-capable buffer.
    struct TrackFltrDsp
    {
        static constexpr int kMaxChans = 2;

        SvfFilter stage1_[kMaxChans] {};
        SvfFilter stage2_[kMaxChans] {};
        float envLevel_        = 0.0f;
        float envTarget_       = 0.0f;
        float envAttackCoeff_  = 0.0f;  // one-pole coefficient, ~1 ms
        float envReleaseCoeff_ = 0.0f;  // one-pole coefficient, ~200 ms
        float sampleRate_      = 44100.0f;

        void prepare(double sampleRate) noexcept
        {
            sampleRate_      = static_cast<float>(sampleRate);
            const float sr   = sampleRate_;
            envAttackCoeff_  = std::exp(-1.0f / (0.001f * sr));
            envReleaseCoeff_ = std::exp(-1.0f / (0.200f * sr));
            reset();
        }

        void reset() noexcept
        {
            for (auto& f : stage1_) f.reset();
            for (auto& f : stage2_) f.reset();
            envLevel_ = envTarget_ = 0.0f;
        }

        // Process `numSamples` samples of `buf` in-place.
        // Scans `midi` for note-on/off to drive the Env->Cutoff follower.
        void processBlock(juce::AudioBuffer<float>& buf,
                          const juce::MidiBuffer& midi,
                          const TrackFltrState& fltr,
                          int numSamples) noexcept
        {
            // Update Env->Cutoff follower from MIDI events (block-level granularity)
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn())  envTarget_ = 1.0f;
                if (msg.isNoteOff()) envTarget_ = 0.0f;
            }
            const float eCoeff = (envTarget_ > envLevel_) ? envAttackCoeff_ : envReleaseCoeff_;
            envLevel_ = eCoeff * envLevel_ + (1.0f - eCoeff) * envTarget_;

            // Resolve effective cutoff with envelope modulation, clamp to 0..1
            const float envMod    = fltr.envToCutoff * envLevel_;
            const float effCutoff = juce::jlimit(0.0f, 1.0f, fltr.cutoff + envMod);

            // Log-scale frequency mapping: 20Hz at 0, 20kHz at 1
            const float freq    = 20.0f * std::pow(1000.0f, effCutoff);
            const float freqLim = juce::jlimit(20.0f, sampleRate_ * 0.49f, freq);
            // g = tan(pi * f / fs) — the normalised frequency for the SVF
            const float g = std::tan(3.141592653589793f * freqLim / sampleRate_);

            // Resonance 0..1 -> k from 2.0 (Butterworth Q=0.5) to 0.1 (very resonant)
            const float k = 2.0f - (1.9f * juce::jlimit(0.0f, 1.0f, fltr.resonance));

            const int  mode  = static_cast<int>(std::round(fltr.mode)) & 3;
            const bool is24  = (fltr.slope >= 0.5f);
            const float drive = juce::jlimit(0.0f, 1.0f, fltr.drive);
            const float driveGain = 1.0f + drive * 7.0f;

            // Stage 2 uses a fixed k (no extra resonance peaking at 24dB)
            constexpr float kStage2 = 1.0f;

            const int numCh = std::min(buf.getNumChannels(), kMaxChans);
            for (int ch = 0; ch < numCh; ++ch)
            {
                stage1_[ch].setCoeffs(g, k);
                if (is24) stage2_[ch].setCoeffs(g, kStage2);

                float* data = buf.getWritePointer(ch);
                for (int n = 0; n < numSamples; ++n)
                {
                    float s = data[n];
                    if (drive > 0.0f)
                        s = std::tanh(s * driveGain) / driveGain;
                    s = stage1_[ch].process(s, mode);
                    if (is24) s = stage2_[ch].process(s, mode);
                    data[n] = s;
                }
            }
        }
    };
}
