// SampleIdRoundTripTest -- 9.18 sample-pool identity, processor-level.
//
// Covers the save-and-promote path (a volatile capture becomes a durable File
// entry and references follow it) and an end-to-end identity round-trip through
// the real serializer (a track's sample_id survives save + reload by content
// hash, not array position).

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/machine/SampleMachine.h"
#include "../src/state/PluginState.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        // Install a SampleMachine on `track` with default base params.
        void installSampler(LockstepProcessor& proc, int track)
        {
            SampleMachine tmp{ proc.samplePool() };
            const int np = tmp.numParams();
            auto& k = proc.kit(track);
            k.machineId = SampleMachine::kMachineId;
            k.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
            for (int i = 0; i < np; ++i)
                k.baseParams[static_cast<std::size_t>(i)] = tmp.paramSpec(i).defaultValue;
            proc.reinstallMachinesFromActiveKit();
        }

        // Write a short sine capture into the volatile REC slot at `volIdx`.
        void captureInto(SamplePool& pool, int volIdx, int len)
        {
            auto* buf = pool.mutableVolatilePcm(volIdx);
            if (buf == nullptr) return;
            // Shrink the reported length to the "captured" region without realloc.
            const int ch = std::max(1, buf->getNumChannels());
            buf->setSize(ch, len, false, false, /*avoidReallocating*/ true);
            for (int c = 0; c < ch; ++c)
                for (int i = 0; i < len; ++i)
                    buf->setSample(c, i, 0.2f * std::sin(2.0f * 3.14159265f * 440.0f
                                                         * static_cast<float>(i) / 48000.0f));
            pool.setVolatileOrigin(volIdx, SampleOrigin::Record);
        }
    }

    void runSampleIdRoundTripTests()
    {
        // ── save-and-promote: volatile capture -> durable File; refs follow ──
        {
            EngineHarness h;
            auto& proc = h.processor();
            installSampler(proc, 0);

            const int vol = proc.samplePool().nthVolatileIndex(0);
            CHECK(vol >= 0, "a reserved REC slot exists");
            captureInto(proc.samplePool(), vol, 4096);

            const int slot = proc.slotForId(0, "sample_id");
            CHECK(slot >= 0, "sampler has a sample_id slot");
            // Point track 0 at the volatile capture (kit + working buffer).
            proc.kit(0).baseParams[static_cast<std::size_t>(slot)] = static_cast<float>(vol);
            proc.sequence().tracks[0].baseParams[static_cast<std::size_t>(slot)] =
                static_cast<float>(vol);

            juce::File dest = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("lockstep_promote_test.wav");
            dest.deleteFile();

            const int newIdx = proc.promoteVolatileToFile(vol, dest);
            CHECK(newIdx >= 0, "promote returns a new File pool index");
            const auto* ns = proc.samplePool().get(newIdx);
            CHECK(ns != nullptr && !ns->isVolatile, "promoted entry is a File (non-volatile)");
            CHECK(ns != nullptr && ns->pcm.getNumSamples() > 0, "promoted entry carries PCM");
            CHECK(dest.withFileExtension("wav").existsAsFile(), "a WAV was written to disk");

            // The track reference now points at the durable File, not the volatile.
            const int nowIdx = static_cast<int>(std::lround(
                proc.sequence().tracks[0].baseParams[static_cast<std::size_t>(slot)]));
            CHECK(nowIdx == newIdx, "track sample_id repointed from volatile to File");
            // The volatile slot survives the promotion.
            CHECK(proc.samplePool().isVolatileIndex(vol), "the volatile capture is left intact");

            dest.withFileExtension("wav").deleteFile();
        }

        // ── end-to-end: sample_id survives save + reload by content hash ─────
        {
            // Two files on disk so the pool has stable, hash-identified entries.
            auto tone = [](double freq) {
                const int n = 8192;
                juce::AudioBuffer<float> b(1, n);
                for (int i = 0; i < n; ++i)
                    b.setSample(0, i, 0.2f * std::sin(2.0f * 3.14159265f * static_cast<float>(freq)
                                                      * static_cast<float>(i) / 48000.0f));
                return b;
            };
            auto writeWav = [](const juce::AudioBuffer<float>& b, const juce::String& stem) {
                juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile(stem + ".wav");
                f.deleteFile();
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
                const auto opts = juce::AudioFormatWriterOptions{}
                                      .withSampleRate(48000.0)
                                      .withNumChannels(1)
                                      .withBitsPerSample(32)
                                      .withSampleFormat(
                                          juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
                auto w = fmt.createWriterFor(os, opts);
                if (w != nullptr) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
                return f;
            };
            juce::File wavA = writeWav(tone(220.0), "lockstep_rt_a");
            juce::File wavB = writeWav(tone(440.0), "lockstep_rt_b");

            std::uint32_t hashB = 0;
            juce::MemoryBlock blob;
            {
                EngineHarness h;
                auto& proc = h.processor();
                installSampler(proc, 0);
                const int a = proc.samplePool().load(wavA.getFullPathName());
                const int b = proc.samplePool().load(wavB.getFullPathName());
                CHECK(a >= 0 && b >= 0, "both files load into the pool");
                hashB = proc.samplePool().get(b)->ref.hashXX32;

                const int slot = proc.slotForId(0, "sample_id");
                proc.kit(0).baseParams[static_cast<std::size_t>(slot)] = static_cast<float>(b);
                proc.sequence().tracks[0].baseParams[static_cast<std::size_t>(slot)] =
                    static_cast<float>(b);

                proc.getStateInformation(blob);
            }

            // Reload into a fresh processor and confirm track 0 resolves to the entry
            // whose content hash matches wavB — regardless of its array position.
            {
                EngineHarness h2;
                auto& proc = h2.processor();
                proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

                const int slot = proc.slotForId(0, "sample_id");
                CHECK(slot >= 0, "reloaded sampler has a sample_id slot");
                const int idx = static_cast<int>(std::lround(
                    proc.sequence().tracks[0].baseParams[static_cast<std::size_t>(slot)]));
                const auto* s = proc.samplePool().get(idx);
                CHECK(s != nullptr, "reloaded sample_id resolves to a live pool entry");
                CHECK(s != nullptr && s->ref.hashXX32 == hashB,
                      "sample_id re-resolves to wavB by content hash after reload");
            }

            wavA.deleteFile();
            wavB.deleteFile();
        }
    }
}
