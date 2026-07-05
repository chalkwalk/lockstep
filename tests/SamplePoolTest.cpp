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

        // 9.18: SampleId identity is decoupled from array position -------------
        {
            const double sr = 44100.0;
            auto bufA = poolTones({ 0, 4, 7 }, { 2.0, 1.0, 1.0 }, sr, 1.0);
            auto bufB = poolTones({ 2, 5, 9 }, { 2.0, 1.0, 1.0 }, sr, 1.0);
            juce::File wavA = writeTempWav(bufA, sr, "lockstep_id_a");
            juce::File wavB = writeTempWav(bufB, sr, "lockstep_id_b");

            SamplePool pool;
            const int a = pool.load(wavA.getFullPathName());   // idx 0
            const int b = pool.load(wavB.getFullPathName());   // idx 1
            const int v0 = pool.addVolatile();                 // idx 2
            const int v1 = pool.addVolatile();                 // idx 3
            CHECK(a == 0 && b == 1 && v0 == 2 && v1 == 3, "layout: A B v0 v1");

            const SampleId idA  = pool.idOf(a);
            const SampleId idB  = pool.idOf(b);
            const SampleId idV0 = pool.idOf(v0);
            const SampleId idV1 = pool.idOf(v1);

            CHECK(idA.domain == SampleId::Domain::Persistent, "file id is Persistent");
            CHECK(idV0.domain == SampleId::Domain::Volatile, "volatile id is Volatile");
            CHECK(idA != idB, "distinct files have distinct ids");
            CHECK(idV0 != idV1, "distinct volatiles have distinct ids");

            // Round-trip: idOf then indexOf returns the same array position.
            CHECK(pool.indexOf(idA) == a && pool.indexOf(idB) == b,
                  "indexOf round-trips file ids");
            CHECK(pool.indexOf(idV0) == v0 && pool.indexOf(idV1) == v1,
                  "indexOf round-trips volatile ids");

            // Identity survives a reorder: remove A (idx 0) shifts everything down.
            CHECK(pool.remove(a), "remove entry 0");
            CHECK(pool.indexOf(idB) == 0, "B's id now resolves to shifted index 0");
            CHECK(pool.indexOf(idV0) == 1 && pool.indexOf(idV1) == 2,
                  "volatile ids survive the shift");
            CHECK(pool.resolve(idB) != nullptr
                  && pool.resolve(idB)->ref.hashXX32 == idB.key,
                  "resolve(idB) returns B by hash");

            // A vanished entry: its id resolves to nothing (not a wrong entry).
            CHECK(pool.indexOf(idA) == -1 && pool.resolve(idA) == nullptr,
                  "removed entry's id resolves to nothing, never a neighbour");

            // Identity survives a swap.
            CHECK(pool.swap(0, 1), "swap B and v0");
            CHECK(pool.indexOf(idB) == 1 && pool.indexOf(idV0) == 0,
                  "ids track entries across a swap");

            wavA.deleteFile();
            wavB.deleteFile();
        }

        // 9.18: File and Stream of the same file are distinct ids -------------
        {
            const double sr = 44100.0;
            auto buf = poolTones({ 0, 4, 7 }, { 2.0, 1.0, 1.0 }, sr, 1.0);
            juce::File wav = writeTempWav(buf, sr, "lockstep_id_dual");

            SamplePool pool;
            const int f = pool.load(wav.getFullPathName());        // File (PCM hash)
            const int s = pool.addStreamRef(wav.getFullPathName()); // Stream (byte hash)
            CHECK(f == 0 && s == 1, "same file loads as two distinct entries");
            const SampleId idF = pool.idOf(f);
            const SampleId idS = pool.idOf(s);
            CHECK(idF.domain == SampleId::Domain::Persistent
                  && idS.domain == SampleId::Domain::Persistent,
                  "both are Persistent-domain");
            CHECK(idF != idS, "File (PCM hash) and Stream (byte hash) are distinct ids");
            CHECK(pool.indexOf(idF) == f && pool.indexOf(idS) == s,
                  "each resolves to its own entry");

            wav.deleteFile();
        }

        // 9.18: an unbacked / None reference resolves to nothing --------------
        {
            SamplePool pool;
            const int m = pool.addMissing(SampleRef{});  // no path, hash 0
            CHECK(m == 0, "empty missing entry appended");
            CHECK(!pool.idOf(m).valid(), "a hash-0 entry has no persistent identity");
            CHECK(pool.indexOf(SampleId{}) == -1, "None id resolves to nothing");
            CHECK(pool.resolve(SampleId{}) == nullptr, "resolve(None) is nullptr");
        }

        // C4: per-group ordinals (FILE 1..n / STREAM 1..n / REC 1..n) are stable
        // regardless of where the volatile REC slots sit — reload re-seeds them at
        // the pool front, which used to shift the raw indices a picker showed.
        {
            const double sr = 44100.0;
            auto b1 = poolTones({ 0, 4, 7 }, { 2.0, 1.0, 1.0 }, sr, 0.4);
            auto b2 = poolTones({ 2, 5, 9 }, { 2.0, 1.0, 1.0 }, sr, 0.4);
            juce::File w1 = writeTempWav(b1, sr, "lockstep_ord_1");
            juce::File w2 = writeTempWav(b2, sr, "lockstep_ord_2");

            // Arrangement A: files first, then volatiles appended.
            SamplePool a;
            const int a1 = a.load(w1.getFullPathName());
            const int a2 = a.load(w2.getFullPathName());
            const int as = a.addStreamRef("/nonexistent/lockstep_ord_stream.wav");
            a.addVolatile();
            a.addVolatile();
            CHECK(a.groupOrdinal(a1) == 1 && a.groupOrdinal(a2) == 2,
                  "ordinal: FILE entries number 1,2");
            CHECK(a.groupOrdinal(as) == 1, "ordinal: the STREAM entry numbers 1");

            // Arrangement B: volatiles seeded at the FRONT (the reload layout), then
            // the same files/stream. Group ordinals must match A exactly.
            SamplePool b;
            b.addVolatile();
            b.addVolatile();
            const int b1i = b.load(w1.getFullPathName());
            const int b2i = b.load(w2.getFullPathName());
            const int bs = b.addStreamRef("/nonexistent/lockstep_ord_stream.wav");
            CHECK(b.groupOrdinal(b1i) == 1 && b.groupOrdinal(b2i) == 2,
                  "ordinal: FILE numbering unshifted by front-seeded volatiles");
            CHECK(b.groupOrdinal(bs) == 1,
                  "ordinal: STREAM numbering unshifted by front-seeded volatiles");
            // The raw indices differ (b's files sit at 2,3 not 0,1) — proving the
            // ordinal is decoupled from array position.
            CHECK(b1i != a1, "ordinal: raw indices did shift (front-seed)");

            w1.deleteFile();
            w2.deleteFile();
        }

        // C3: a file present at load that disappears at runtime becomes missing on
        // rescan; a volatile capture is never touched; a reappeared file clears.
        {
            const double sr = 44100.0;
            auto buf = poolTones({ 0, 4, 7 }, { 2.0, 1.0, 1.0 }, sr, 0.5);
            juce::File wav = writeTempWav(buf, sr, "lockstep_rescan");

            SamplePool pool;
            const int idx = pool.load(wav.getFullPathName());
            const int vol = pool.addVolatile();
            CHECK(idx >= 0 && !pool.isMissing(idx), "rescan: file loads present");

            // No change while the file exists.
            CHECK(!pool.rescanMissing(), "rescan: no change while file present");
            CHECK(!pool.isMissing(idx), "rescan: still present");

            // Delete on disk → rescan flips it missing (and reports a change).
            wav.deleteFile();
            CHECK(pool.rescanMissing(), "rescan: reports a change after deletion");
            CHECK(pool.isMissing(idx), "rescan: deleted file now flags missing");
            CHECK(!pool.isMissing(vol), "rescan: volatile capture never missing");
            // The decoded PCM stays in RAM (a live set keeps playing) — only the
            // surfacing flag changed.
            CHECK(pool.get(idx) != nullptr && pool.get(idx)->pcm.getNumSamples() > 0,
                  "rescan: decoded PCM is retained after the file vanishes");

            // Restore the file → rescan clears the flag again.
            juce::File wav2 = writeTempWav(buf, sr, "lockstep_rescan");
            CHECK(pool.rescanMissing(), "rescan: reports a change after restore");
            CHECK(!pool.isMissing(idx), "rescan: restored file clears missing");
            wav2.deleteFile();
        }

        // 9.23: user overrides — effective = override-else-detected; clear restores.
        {
            SamplePool pool;
            const int idx = pool.addVolatile();  // detected defaults: all unset
            CHECK(feq(static_cast<float>(pool.effectiveBpm(idx)), 0.0f),
                  "effective bpm defaults to detected (0)");
            CHECK(pool.effectiveKeyRoot(idx) == -1, "effective key defaults to detected (-1)");
            CHECK(!pool.effectiveOneShot(idx), "effective one-shot defaults to detected (false)");

            pool.setUserBpm(idx, 140.0);
            CHECK(feq(static_cast<float>(pool.effectiveBpm(idx)), 140.0f), "user bpm overrides");
            pool.setUserKey(idx, 3, kDorian);
            CHECK(pool.effectiveKeyRoot(idx) == 3
                  && pool.effectiveKeyBrightness(idx) == kDorian, "user key overrides");
            pool.setUserTuningCents(idx, -12.0, true);
            CHECK(feq(static_cast<float>(pool.effectiveTuningCents(idx)), -12.0f),
                  "user tuning overrides");
            pool.setUserOneShot(idx, 1);
            CHECK(pool.effectiveOneShot(idx), "user one-shot On");
            pool.setUserOneShot(idx, 0);
            CHECK(!pool.effectiveOneShot(idx), "user one-shot Off (loop) overrides detected");

            pool.clearUserOverrides(idx);
            CHECK(feq(static_cast<float>(pool.effectiveBpm(idx)), 0.0f)
                  && pool.effectiveKeyRoot(idx) == -1
                  && feq(static_cast<float>(pool.effectiveTuningCents(idx)), 0.0f)
                  && !pool.effectiveOneShot(idx),
                  "clearUserOverrides restores detected values");
        }
    }
}
