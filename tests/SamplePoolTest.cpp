// SamplePoolTest -- volatile (RAM-only) REC buffer support (Part A, DESIGN §28).
//
// Covers the pool-level capability the RecordMachine (6.2) builds on:
//   - addVolatile() appends an entry flagged isVolatile, no file backing.
//   - prepareVolatile() sizes volatile buffers to a capacity; a writer can then
//     shrink the reported length with avoidReallocating (so a Sample reading the
//     entry plays exactly the captured region) without reallocating.
//   - mutableVolatilePcm() returns a writable handle for volatile entries only.

#include "TestHarness.h"
#include "../src/machine/SamplePool.h"
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace lockstep
{
    namespace
    {
        // Render scale-tone partials (octave 4, pc 0 == C4) into a mono buffer.
        juce::AudioBuffer<float> poolTones(const std::vector<int>& pcs,
                                           const std::vector<double>& w,
                                           double sr, double seconds)
        {
            const int n = static_cast<int>(sr * seconds);
            juce::AudioBuffer<float> buf(1, n);
            buf.clear();
            float* d = buf.getWritePointer(0);
            for (std::size_t k = 0; k < pcs.size(); ++k)
            {
                const int midi = 60 + pcs[k];
                const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
                const double amp = 0.2 * w[k];
                for (int i = 0; i < n; ++i)
                    d[i] += static_cast<float>(amp * std::sin(2.0 * juce::MathConstants<double>::pi
                                                              * hz * i / sr));
            }
            return buf;
        }

        // Write a buffer to a fresh temp WAV; returns the file (caller deletes).
        juce::File writeTempWav(const juce::AudioBuffer<float>& buf, double sr,
                                const juce::String& stem)
        {
            juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile(stem + ".wav");
            f.deleteFile();
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
            if (os == nullptr) return f;
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate(sr)
                                     .withNumChannels(buf.getNumChannels())
                                     .withBitsPerSample(32)
                                     .withSampleFormat(
                                         juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            auto w = fmt.createWriterFor(os, options);   // moves the stream on success
            if (w != nullptr)
                w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
            return f;
        }
    }

    void runSamplePoolTests()
    {
        // addVolatile + flags ------------------------------------------------
        {
            SamplePool pool;
            const int v0 = pool.addVolatile();
            const int v1 = pool.addVolatile();
            CHECK(v0 == 0 && v1 == 1, "volatile entries append in order");
            CHECK(pool.isVolatileIndex(v0) && pool.isVolatileIndex(v1),
                  "isVolatileIndex true for volatile entries");
            CHECK(!pool.isVolatileIndex(-1) && !pool.isVolatileIndex(99),
                  "isVolatileIndex false out of range");

            const Sample* s = pool.get(v0);
            CHECK(s != nullptr && s->isVolatile, "Sample flagged volatile");
            CHECK(s != nullptr && s->ref.path.empty() && s->ref.hashXX32 == 0,
                  "volatile entry has no file backing");

            // W3a: origin tagging drives the pool browser's RECORD/LOOP grouping.
            CHECK(pool.origin(v0) == SampleOrigin::Empty,
                  "fresh volatile slot starts Empty (hidden in the browser)");
            pool.setVolatileOrigin(v0, SampleOrigin::Record);
            pool.setVolatileOrigin(v1, SampleOrigin::Loop);
            CHECK(pool.origin(v0) == SampleOrigin::Record, "origin set to Record");
            CHECK(pool.origin(v1) == SampleOrigin::Loop, "origin set to Loop");
            pool.setVolatileOrigin(v0, SampleOrigin::Empty);  // a Clear reverts it
            CHECK(pool.origin(v0) == SampleOrigin::Empty, "Clear reverts origin to Empty");
        }

        // prepareVolatile sizes to capacity; mutable handle works -------------
        {
            SamplePool pool;
            const int v = pool.addVolatile();
            const int cap = 4096;
            pool.prepareVolatile(48000.0, 2, cap);

            const Sample* s = pool.get(v);
            CHECK(s != nullptr && s->pcm.getNumChannels() == 2, "prepared to 2 ch");
            CHECK(s != nullptr && s->pcm.getNumSamples() == cap, "prepared to capacity");
            CHECK(s != nullptr && s->sampleRate == 48000.0, "prepared sample rate set");

            auto* buf = pool.mutableVolatilePcm(v);
            CHECK(buf != nullptr, "mutable handle for volatile entry");

            // Shrink to a captured length without reallocating, then verify the
            // data pointer is unchanged (capacity preserved) and the reported
            // length now matches the capture.
            const float* before = buf->getReadPointer(0);
            buf->setSize(2, 1000, false, false, /*avoidReallocating*/ true);
            CHECK(buf->getNumSamples() == 1000, "shrunk to captured length");
            CHECK(buf->getReadPointer(0) == before, "no reallocation on shrink");
        }

        // nthVolatileIndex addresses REC slots by ordinal, robust to shifts ---
        {
            SamplePool pool;
            // A non-volatile placeholder at index 0, then two REC slots above it.
            const int file0 = pool.addMissing(SampleRef{});
            const int r0 = pool.addVolatile();
            const int r1 = pool.addVolatile();
            CHECK(file0 == 0 && r0 == 1 && r1 == 2, "layout: file then two REC");
            CHECK(pool.nthVolatileIndex(0) == 1 && pool.nthVolatileIndex(1) == 2,
                  "nthVolatileIndex maps ordinals to absolute indices");
            CHECK(pool.nthVolatileIndex(2) == -1, "no third REC slot");

            // Removing the file below shifts the REC entries down; ordinals hold.
            pool.remove(file0);
            CHECK(pool.nthVolatileIndex(0) == 0 && pool.nthVolatileIndex(1) == 1,
                  "ordinals survive a file removal that shifts absolute indices");
        }

        // mutableVolatilePcm refuses non-volatile / out of range -------------
        {
            SamplePool pool;
            const int v = pool.addVolatile();
            CHECK(pool.mutableVolatilePcm(v) != nullptr, "volatile handle ok");
            CHECK(pool.mutableVolatilePcm(-1) == nullptr, "no handle out of range");
            CHECK(pool.mutableVolatilePcm(99) == nullptr, "no handle out of range hi");
        }

        // displayName/displayHint — shared model for browser + pickers (W3a) --
        {
            SamplePool pool;
            // Two volatile slots: one Record, one Loop; plus an untouched Empty.
            const int rec = pool.addVolatile();
            const int loop = pool.addVolatile();
            const int empty = pool.addVolatile();
            pool.setVolatileOrigin(rec, SampleOrigin::Record);
            pool.setVolatileOrigin(loop, SampleOrigin::Loop);

            // The picker used to render these blank (filename stem of an empty
            // path); the shared model synthesises a name from origin + ordinal.
            CHECK(pool.displayName(rec) == "Record 1",
                  "first Record volatile names as Record 1");
            CHECK(pool.displayName(loop) == "Loop 1",
                  "first Loop volatile names as Loop 1");
            CHECK(pool.displayName(rec).isNotEmpty() && pool.displayName(loop).isNotEmpty(),
                  "volatile display names are never blank");
            // bug 14: an un-captured volatile slot is no longer a nameless "(empty)"
            // row — it reads as a real, pickable slot named by its volatile ordinal
            // (rec, loop, empty are volatile 1/2/3) with an "(empty)" badge.
            CHECK(pool.displayName(empty) == "REC 3 (empty)",
                  "un-captured volatile slot reads REC N (empty) by volatile ordinal");
            CHECK(pool.displayName(empty).isNotEmpty(),
                  "empty volatile display name is never blank");
            // Clearing a captured slot reverts it to the ordinal + (empty) badge.
            pool.setVolatileOrigin(rec, SampleOrigin::Empty);
            CHECK(pool.displayName(rec) == "REC 1 (empty)",
                  "cleared volatile reverts to REC N (empty)");
            pool.setVolatileOrigin(rec, SampleOrigin::Record);
            CHECK(pool.displayName(-1) == "(none)" && pool.displayName(99) == "(none)",
                  "out-of-range display name is (none)");

            // Bars hint takes priority over a bpm estimate for volatiles.
            pool.setSourceBars(loop, 4.0);
            CHECK(pool.displayHint(loop) == "4.00 bars",
                  "volatile hint shows captured bars when known");
        }

        // sourceBars stamp/read on volatile entries --------------------------
        {
            SamplePool pool;
            const int file0 = pool.addMissing(SampleRef{});  // non-volatile
            const int v = pool.addVolatile();
            CHECK(feq(static_cast<float>(pool.sourceBars(v)), 0.0f),
                  "sourceBars defaults to 0 (unknown)");

            pool.setSourceBars(v, 3.5);  // a free-length loop: 3.5 bars
            CHECK(feq(static_cast<float>(pool.sourceBars(v)), 3.5f),
                  "sourceBars round-trips on a volatile entry");

            // Non-volatile and out-of-range writes/reads are no-ops returning 0.
            pool.setSourceBars(file0, 2.0);
            CHECK(feq(static_cast<float>(pool.sourceBars(file0)), 0.0f),
                  "sourceBars is volatile-only (non-volatile stays 0)");
            CHECK(feq(static_cast<float>(pool.sourceBars(99)), 0.0f),
                  "sourceBars out of range returns 0");
        }

        // 4.9: key detection at load + one-shot hint --------------------------
        {
            // A sustained C-major chord/scale -> detected key C, hint shows Cmaj.
            const double sr = 44100.0;
            auto buf = poolTones({ 0, 2, 4, 5, 7, 9, 11 },
                                 { 3.0, 1.0, 2.0, 1.0, 2.0, 1.0, 1.0 }, sr, 3.0);
            juce::File wav = writeTempWav(buf, sr, "lockstep_pooltest_cmaj");

            SamplePool pool;
            const int idx = pool.load(wav.getFullPathName());
            CHECK(idx >= 0, "tonal WAV loads");
            CHECK(pool.keyRoot(idx) == 0, "C-major WAV detects root C (0)");
            CHECK(pool.displayHint(idx).contains("maj"),
                  "tonal hint carries the major key label");

            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->analysed, "loaded file entry is marked analysed");
            wav.deleteFile();
        }
        {
            // A short noise burst: no rhythm, no key -> analysed one-shot.
            const double sr = 44100.0;
            const int n = static_cast<int>(sr * 0.4);
            juce::AudioBuffer<float> nb(1, n);
            std::mt19937 rng(99);
            std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
            for (int i = 0; i < n; ++i)
                nb.getWritePointer(0)[i] = dist(rng);
            juce::File wav = writeTempWav(nb, sr, "lockstep_pooltest_oneshot");

            SamplePool pool;
            const int idx = pool.load(wav.getFullPathName());
            CHECK(idx >= 0, "one-shot WAV loads");
            CHECK(pool.keyRoot(idx) == -1, "noise one-shot has no key");
            CHECK(feq(static_cast<float>(pool.detectedBpm(idx)), 0.0f),
                  "noise one-shot has no tempo");
            CHECK(pool.displayHint(idx).endsWith("one-shot"),
                  "analysed rhythm-less entry reads one-shot");
            wav.deleteFile();
        }

        // 4.9: cache adoption on hash match; re-analysis on mismatch ----------
        {
            const double sr = 44100.0;
            auto buf = poolTones({ 0, 2, 4, 5, 7, 9, 11 },
                                 { 3.0, 1.0, 2.0, 1.0, 2.0, 1.0, 1.0 }, sr, 3.0);
            juce::File wav = writeTempWav(buf, sr, "lockstep_pooltest_cache");

            // First load computes the real hash + a real (root-0) analysis.
            std::uint32_t hash = 0;
            {
                SamplePool pool;
                const int idx = pool.load(wav.getFullPathName());
                const Sample* s = pool.get(idx);
                CHECK(s != nullptr, "cache probe load ok");
                hash = s->ref.hashXX32;
            }

            // A deliberately wrong-but-valid cache with the MATCHING hash must be
            // adopted verbatim (proves detection is skipped).
            {
                SamplePool pool;
                SamplePool::CachedAnalysis ca;
                ca.hashXX32 = hash;
                ca.bpm = 77.0;
                ca.keyRoot = 3;   // != detected 0
                ca.keyBrightness = kDorian;
                ca.tuningCents = 5.0;
                const int idx = pool.load(wav.getFullPathName(), &ca);
                CHECK(feq(static_cast<float>(pool.detectedBpm(idx)), 77.0f),
                      "matching-hash cache is adopted (bpm 77 survives)");
                CHECK(pool.keyRoot(idx) == 3, "matching-hash cache adopts key root");
            }

            // A mismatching hash must be ignored -> fresh analysis (root 0 back).
            {
                SamplePool pool;
                SamplePool::CachedAnalysis ca;
                ca.hashXX32 = hash ^ 0x1u;   // wrong hash
                ca.bpm = 77.0;
                ca.keyRoot = 3;
                const int idx = pool.load(wav.getFullPathName(), &ca);
                CHECK(pool.keyRoot(idx) == 0, "hash mismatch re-analyses (root 0)");
                CHECK(!feq(static_cast<float>(pool.detectedBpm(idx)), 77.0f),
                      "hash mismatch discards poisoned bpm");
            }
            wav.deleteFile();
        }

        // Item 6: addStreamRef — path + light hash, NO PCM; dedupe; ensurePcm ---
        {
            const double sr = 44100.0;
            auto buf = poolTones({ 0, 4, 7 }, { 2.0, 1.0, 1.0 }, sr, 1.0);
            juce::File wav = writeTempWav(buf, sr, "lockstep_pooltest_stream");

            SamplePool pool;
            const int idx = pool.addStreamRef(wav.getFullPathName());
            CHECK(idx == 0, "addStreamRef appends an entry");
            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->origin == SampleOrigin::Stream,
                  "stream entry is Stream-origin");
            CHECK(s != nullptr && s->pcm.getNumSamples() == 0,
                  "stream entry carries NO decoded PCM");
            CHECK(s != nullptr && s->ref.hashXX32 != 0,
                  "stream entry hashes the file's first bytes");
            CHECK(!pool.isVolatileIndex(idx), "stream entry is not volatile");
            CHECK(pool.displayName(idx) == wav.getFileNameWithoutExtension(),
                  "stream entry displays the filename stem");

            // Dedupe: the same path returns the same index, no second entry.
            const int again = pool.addStreamRef(wav.getFullPathName());
            CHECK(again == idx && pool.size() == 1, "addStreamRef dedupes by path");

            // ensurePcm decodes on demand (a Flex sampler picking a Stream entry).
            const int ep = pool.ensurePcm(idx);
            CHECK(ep == idx, "ensurePcm returns the index on success");
            const Sample* sd = pool.get(idx);
            CHECK(sd != nullptr && sd->pcm.getNumSamples() > 0,
                  "ensurePcm decodes the PCM on demand");
            // Idempotent — a second call is a no-op that keeps the buffer.
            const int ep2 = pool.ensurePcm(idx);
            CHECK(ep2 == idx, "ensurePcm is idempotent");

            wav.deleteFile();
        }

        // Item 6: a missing stream file still yields an entry (index/ref survive).
        {
            SamplePool pool;
            const int idx = pool.addStreamRef("/nonexistent/lockstep_missing_stream.wav");
            CHECK(idx == 0, "addStreamRef of a missing file still appends");
            const Sample* s = pool.get(idx);
            CHECK(s != nullptr && s->origin == SampleOrigin::Stream,
                  "missing stream entry keeps Stream origin");
            CHECK(pool.isMissing(idx), "missing stream file flags missing");
            CHECK(pool.ensurePcm(idx) == -1, "ensurePcm refuses a missing entry");
        }
    }
}
