#pragma once

#include "BungeeStretchEngine.h"
#include "IStretchEngine.h"
#include "../machine/ChannelPolicy.h"
#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // ── Offline time-stretch render (S7) ─────────────────────────────────────
    //
    // Shared, pitch-preserved render-to-buffer used by the FreeLen background
    // bake (LoopMachine streaming fit → static PCM) and the FIT verb. Message
    // thread OR a background worker — never the audio thread (the engine
    // allocates in prepare()). Kept here so the processor and the headless tests
    // exercise the SAME render the live stream is baked from.

    // A random-access IStretchSource over a resident buffer.
    struct BufferStretchSource : IStretchSource
    {
        const juce::AudioBuffer<float>& buf;
        double rate;
        BufferStretchSource(const juce::AudioBuffer<float>& b, double r) : buf(b), rate(r) {}
        int read(float* dest, int ch, juce::int64 srcPos, int n) override
        {
            const int chans = buf.getNumChannels();
            const int useCh = std::min(ch, chans - 1);
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 sp = srcPos + i;
                dest[i] = (useCh >= 0 && sp >= 0 && sp < buf.getNumSamples())
                              ? buf.getSample(useCh, static_cast<int>(sp)) : 0.0f;
            }
            return n;
        }
        [[nodiscard]] juce::int64 length() const override { return buf.getNumSamples(); }
        [[nodiscard]] int numChannels() const override { return engineChannels(buf.getNumChannels()); }
        [[nodiscard]] double sampleRate() const override { return rate; }
    };

    // Render `src` time-stretched to exactly `outLen` frames (pitch preserved)
    // across `numChans` channels into `out`. The engine is prepared for the full
    // requested width so a deck-medium-wide source (a multi-sub-track Loop, up to
    // eight channels) is stretched channel-for-channel — matching the realtime
    // streaming engine, whose maxChannels is the same deck width. The engine's
    // onset latency is discarded so `out[k]` aligns with the stream's sample k.
    inline void renderStretchWide(const juce::AudioBuffer<float>& src, double srcRate,
                                  int outLen, double outRate, int numChans,
                                  juce::AudioBuffer<float>& out)
    {
        const int nch = std::max(1, numChans);
        out.setSize(nch, std::max(0, outLen), false, false, true);
        out.clear();
        const int srcLen = src.getNumSamples();
        if (srcLen <= 0 || outLen <= 0) return;

        constexpr int kBlock = 512;
        BungeeStretchEngine eng;
        eng.prepare(srcRate, outRate, nch, kBlock);
        BufferStretchSource ssrc{ src, srcRate };
        const double ratio = static_cast<double>(outLen) / static_cast<double>(srcLen);
        eng.start(&ssrc, 0.0, ratio, 1.0);

        // Discard the engine's onset latency, then capture outLen frames.
        const int lat = eng.latencySamples();
        juce::AudioBuffer<float> tmp(nch, kBlock);
        int produced = 0, discarded = 0;
        // Bound the loop generously so a misbehaving engine cannot spin forever.
        const int maxIters = (outLen + lat) / kBlock + 8;
        for (int it = 0; it < maxIters && produced < outLen; ++it)
        {
            tmp.clear();
            eng.process(tmp, 0, kBlock);
            for (int i = 0; i < kBlock && produced < outLen; ++i)
            {
                if (discarded < lat) { ++discarded; continue; }
                for (int c = 0; c < nch; ++c) out.setSample(c, produced, tmp.getSample(c, i));
                ++produced;
            }
        }
    }

    // Stereo convenience (the FIT verb's loaded-source render is a channel-pair).
    inline void renderStretch(const juce::AudioBuffer<float>& src, double srcRate,
                              int outLen, double outRate, juce::AudioBuffer<float>& out)
    {
        renderStretchWide(src, srcRate, outLen, outRate, 2, out);
    }
}
