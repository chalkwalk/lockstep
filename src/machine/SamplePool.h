#pragma once

#include "TransientDetector.h"
#include "SampleId.h"
#include "../core/Scale.h"   // kAeolian (key brightness default)
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lockstep
{
    // hashXX32 is a real xxHash32 (state/Hash.cpp), not a stub — two long-lived
    // comments claimed otherwise until 2026-07-14. Two things about it are
    // load-bearing:
    //
    //   Scope. For decoded entries it fingerprints **channel 0 only** (cheap, and
    //   the pool never needs to distinguish files by their right channel alone).
    //   The bounded cost: two files whose first channel is byte-identical collide
    //   — e.g. a mono file and a stereo widening of it. Stream entries hash the
    //   first 1 MB of *file bytes* instead (they must never decode); that is a
    //   change-detector, not a content fingerprint, and the two schemes are not
    //   comparable across origins. Entries are matched within an origin.
    //
    //   Stability. The value is **persisted identity** — sample refs (`sh`) and
    //   insert-slot IR refs (v31) are stored by hash and resolved against pool
    //   hashes recomputed at load. Changing what the hash covers therefore
    //   silently orphans every saved reference: it needs a serializer bump and a
    //   migration, not a one-line edit.
    struct SampleRef
    {
        std::string path;
        std::uint32_t hashXX32 = 0;
    };

    // Where a pool entry came from — drives the pool browser's grouping (W3a).
    // File entries are disk-backed; volatile entries start Empty and become
    // Record / Loop when a capture writes into them (a Clear reverts to Empty).
    // Stream entries (Item 6) are disk-backed like File but reference-only: they
    // carry a path + light hash and NO decoded PCM (StreamMachine streams from
    // disk). ensurePcm() decodes one on demand when a Flex sampler picks it.
    // Append-only — the value is serialised, so never renumber. (File=0.)
    enum class SampleOrigin : std::uint8_t { File = 0, Empty, Record, Loop, Stream };

    // Stable identity for a pool entry, decoupled from its array position (9.18).
    // A sample reference (track base sample_id, P-Lock override, Stream track) is
    // stored as a SampleId so it survives pool reorder / reload without pointing at
    // the wrong entry (the flat-index rot fixed here). Two identity domains:
    //   - Persistent (File / Stream) — backed by a disk file. key = content hash
    //     (SampleRef::hashXX32). Stable across reorder AND file move: reloading the
    //     file re-matches the hash and auto-relinks. Serialised.
    //   - Volatile (Record / Loop / Empty REC slot) — captured live, no file, not
    //     serialised. key = a session-local monotonic id assigned at slot creation,
    //     stable for the life of the session across pool reorder.
    // None = an unassigned / cleared reference (resolves to nothing).
    // SampleId now lives in machine/SampleId.h (extracted 9.24 S15 so JUCE-free
    // core types can carry one); it is re-exported here via that include.

    struct Sample
    {
        SampleRef ref;
        SampleOrigin origin = SampleOrigin::File;
        juce::AudioBuffer<float> pcm;
        double sampleRate = 0.0;
        bool missing = false;  // true when the file could not be found on load

        // Volatile (RAM-only) entries hold captured audio written by recorder
        // trigs (DESIGN §28). They have no file backing (empty ref.path), are
        // badged "REC", and are not serialised with the project. The pcm buffer
        // is pre-sized to a capacity in prepareVolatile(); a recorder shrinks it
        // to the captured length via setSize(avoidReallocating) so playback reads
        // exactly the captured region with no sampler changes.
        bool isVolatile = false;
        // Session-local monotonic id for a volatile entry (9.18), assigned at
        // addVolatile(). 0 for non-volatile entries. This is the stable key a
        // Volatile-domain SampleId resolves against, so an in-session reference to
        // a REC/LOOP slot survives a pool reorder. Not serialised (volatiles are
        // RAM-only); a reload assigns fresh ids to the re-seeded slots.
        std::uint32_t volatileId = 0;
        // Allocated capacity (samples) of a volatile pcm buffer, set by
        // prepareVolatile(). getNumSamples() drops to the captured length after a
        // shrink, so a writer reads the safe maximum from here instead.
        int volatileCapacity = 0;
        // Intrinsic musical length of the captured audio, in bars, stamped by the
        // recorder/looper at capture close (= capturedSamples / samplesPerBar at the
        // capture tempo; fractional for free-length loops). 0 = unknown (no tempo
        // tracking). A tempo-tracking Player reads this to stretch the buffer to the
        // project tempo. Volatile-only; not serialised.
        double sourceBars = 0.0;

        // Take-group linkage (§40.7). When a deck take is promoted, its N
        // sub-track WAVs and their downmix are written as separate File entries all
        // stamped the same non-zero takeGroupId, so a picker can present them as one
        // take. 0 = not part of a group. `takeMember` labels the entry within the
        // group: 0 = the downmix, 1..N = sub-track N. Serialised additively.
        std::uint32_t takeGroupId = 0;
        int takeMember = 0;

        // Auto-detected tempo (BPM) of a file-loaded loop, estimated from the
        // RMS envelope at load (message thread; see dsp/TempoEstimate.h). 0 =
        // unknown / not rhythmic / too long to be a loop. A tempo-tracking
        // Player uses this to derive sourceBars when a disk loop carries no
        // explicit musical length. Fused with filename/ACID hints at load, and
        // serialised (4.9): a hash-keyed cache lets a reload skip re-analysis.
        double detectedBpm = 0.0;

        // Auto-detected key (4.9; dsp/KeyEstimate.h), fused with filename/ACID
        // hints. keyRoot is a pitch class 0..11, or -1 when unknown (silence /
        // noise / too long / one-shot with no key hint). keyBrightness is a
        // Scale.h Brightness, meaningful only when keyRoot >= 0. tuningCents is
        // the estimated deviation from A440 equal temperament.
        int    keyRoot = -1;
        int    keyBrightness = kAeolian;
        double tuningCents = 0.0;
        // Detected one-shot (9.23): stamped from the ACID / filename hint at load.
        // A one-shot has no meaningful loop tempo; autoFit uses the effective value
        // to seed a plain trig (no loop, no bar-sizing) instead of a loop.
        bool   detectedOneShot = false;
        // True once analysis (or cache adoption) has run for this entry. Lets
        // displayHint distinguish "analysed, no rhythm" (a one-shot) from "not
        // analysed yet", and drives what the serializer writes.
        bool   analysed = false;

        // ── User overrides (9.23) ────────────────────────────────────────────
        // Correct a wrong detection, or stamp metadata the analyser missed. Each
        // is "unset" until the user edits it (Props editor, S6); the effective
        // value is override-else-detected. Serialised per entry (v30), written
        // only when set. Volatile entries keep these in RAM only.
        double userBpm = 0.0;           // 0 = unset
        int    userKeyRoot = -1;        // -1 = unset
        int    userKeyBrightness = kAeolian;
        double userTuningCents = 0.0;
        bool   hasUserTuning = false;   // userTuningCents is meaningful only when set
        int    userOneShot = -1;        // -1 unset / 0 loop / 1 one-shot

        // Cached per-block analysis for transient detection (message thread only).
        // Populated by SamplePool::load(); empty for missing entries.
        BlockAnalysis analysis;

        // ── First-onset hint (A1) ────────────────────────────────────────────
        // Where the audio actually starts, memoised by SamplePool::firstOnset().
        // A loop exported with a sliver of silence in front of its downbeat must
        // still fire on the 1; the players consume this as a normalised `start`.
        // Both are 0 when the material starts on the 1 (or no onset was found) —
        // that is the common case and the safe default. Derived data: recomputed
        // on load, never serialised.
        double onsetNorm = 0.0;      // 0..1 fraction of the source length
        double onsetSeconds = 0.0;   // the same position, in seconds
        bool   onsetComputed = false;
    };

    // Holds decoded PCM for every sample loaded into the session.
    // load() must be called on the message thread only.
    // get() is safe to call from the audio thread for already-loaded samples.
    class SamplePool
    {
    public:
        SamplePool();
        ~SamplePool();

        // Cached, hash-keyed analysis for a pool entry (4.9). Serialised
        // per-entry; on load a match against the freshly decoded PCM hash lets
        // load() adopt these values and skip re-analysis. A mismatch (the file
        // changed on disk) is ignored and the entry is re-analysed.
        struct CachedAnalysis
        {
            std::uint32_t hashXX32 = 0;
            double bpm = 0.0;
            int    keyRoot = -1;
            int    keyBrightness = kAeolian;
            double tuningCents = 0.0;
            bool   oneShot = false;   // detected one-shot (v30)
        };

        // Decode the file at path and append it to the pool.
        // Returns the index of the new entry, or -1 on failure.
        // Message-thread only. When `cached` is non-null and its hash matches
        // the decoded PCM, the cached analysis is adopted and detection is
        // skipped; otherwise the entry is analysed fresh.
        int load(const juce::String& path, const CachedAnalysis* cached = nullptr);

        // Stamp cached analysis onto an already-present entry (used by the
        // serializer for a MISSING file, so its cache survives a session where
        // the file could not be found). Message-thread only.
        void adoptCachedAnalysis(int index, const CachedAnalysis& ca);

        // Where the audio actually begins (A1). `norm` is a 0..1 fraction of the
        // source length, `seconds` the same position in time. {0, 0} means the
        // material starts on the 1, no onset was found inside the search window,
        // or the entry cannot be inspected — all of which callers treat alike.
        //
        // PCM entries read the block analysis load() already computed. PCM-less
        // Stream entries decode a short head window from disk, so a streamed loop
        // gets the same treatment as a RAM one. Computed once per entry and
        // memoised on it. Message-thread only.
        struct Onset { double norm = 0.0; double seconds = 0.0; };
        Onset firstOnset(int index);

        // Append a placeholder entry for a file that could not be found.
        // Preserves the pool index so P-Lock references remain valid.
        // Message-thread only.
        int addMissing(const SampleRef& ref);

        // Item 6: append a disk-streamed reference (origin = Stream) — path plus a
        // light hash of the file's first bytes, with NO decoded PCM (a StreamMachine
        // streams it from disk; a full song must never enter RAM/state wholesale).
        // Dedupes against an existing Stream entry with the same path. A missing file
        // still produces an entry (missing = true) so the index/ref survive. Returns
        // the pool index. Message-thread only.
        int addStreamRef(const juce::String& path);

        // Item 6: ensure the entry at index has decoded PCM, decoding it on demand
        // (used when a Flex sampler picks a Stream-origin, PCM-less entry). No-op if
        // PCM is already present. Refuses missing/pathless entries and files longer
        // than an internal guard (returns -1; the entry stays PCM-less). Returns the
        // index on success. Message-thread only.
        int ensurePcm(int index);

        // Replace a missing (or any) entry in-place with the decoded file at newPath.
        // Does not shift indices; call when the sequencer is stopped to avoid races.
        // Returns false if the file cannot be read.
        // Message-thread only.
        bool relink(int index, const juce::String& newPath);

        // Append an empty volatile (RAM-only) entry; returns its pool index.
        // The pcm buffer is zero-length until prepareVolatile() sizes it.
        // Message-thread only. (DESIGN §28.)
        int addVolatile();

        // Resize every volatile entry's pcm buffer to a capacity of maxSamples
        // (the recorder later shrinks to the captured length without reallocating).
        // Call from prepareToPlay(); message/prepare thread only.
        void prepareVolatile(double sampleRate, int numChannels, int maxSamples);

        // A5: open a volatile slot for writing `lengthSamples` of audio. Sets the
        // used length and zeroes exactly that region — never reallocating, because
        // prepareVolatile() already reserved the capacity. Returns the writable
        // buffer, or nullptr for a non-volatile / out-of-range slot or a length past
        // the capacity.
        //
        // This is the ONLY way a volatile buffer becomes readable. A prepared slot
        // reports length 0 and its pages are uncommitted; the recorder declares how
        // much of it it is about to fill, and nothing may read past that.
        // `numChannels` is the capture's actual width: 2 for a stereo loop/record,
        // 8 for a four-sub-track deck (§40.3). 0 (the default) inherits the width
        // prepareVolatile was prepared with, so existing captures are unchanged.
        // Only these channels are cleared and reported; prepareVolatile allocated
        // the deck maximum, so this never reallocates on the audio thread.
        juce::AudioBuffer<float>* beginVolatileCapture(int index, int lengthSamples,
                                                       int numChannels = 0);

        // A5: how much of a volatile slot has actually been recorded, in samples.
        // 0 = nothing yet. This is the hard read limit — a volatile buffer is
        // allocated without zero-filling (so untouched pages are never committed),
        // and past the used length the memory is *uninitialised*, not silent.
        // Playback, promotion, waveform display and metering all stop here.
        [[nodiscard]] int volatileUsedLength(int index) const;

        bool isVolatileIndex(int index) const;

        // Absolute pool index of the nth volatile (REC) entry, or -1 if there is
        // no nth one. The reserved REC slots are addressed by ordinal (0-based),
        // so this stays correct as file removals shift absolute indices around
        // them — the single source of truth for "where REC slot n lives".
        int nthVolatileIndex(int n) const;

        // Audio-thread-safe mutable handle to a pre-sized volatile buffer, for a
        // recorder/looper to write into. Returns nullptr for non-volatile or
        // out-of-range indices. The capacity is fixed by prepareVolatile(); a
        // writer may shrink via setSize(..., avoidReallocating=true) but must not
        // grow past the capacity.
        juce::AudioBuffer<float>* mutableVolatilePcm(int index);

        // Allocated capacity (samples) of the volatile buffer at index, or 0 if it
        // is not a prepared volatile entry. A writer must not grow the buffer past
        // this on the audio thread (would reallocate).
        int volatileCapacity(int index) const;

        // Stamp / read the captured musical length (bars) of a volatile entry. The
        // recorder/looper write this at capture close (audio thread); a tempo-
        // tracking Player reads it. Out-of-range / non-volatile reads return 0.
        void setSourceBars(int index, double bars);
        // Stamp a pool entry as a member of a take-group (§40.7).
        void setTakeGroup(int index, std::uint32_t groupId, int member);
        // A fresh, session-unique take-group id (monotonic; not serialised — the id
        // is a link, re-derived on load from the stamped values).
        std::uint32_t nextTakeGroupId() noexcept { return ++takeGroupSeq_; }
        double sourceBars(int index) const;

        // W3a: tag a volatile entry with the capture kind that wrote it (Record /
        // Loop), or Empty when cleared. Audio thread writes at capture close; the
        // pool browser reads it (message thread) to group and label REC/LOOP rows.
        // Out-of-range / non-volatile calls are ignored / return File.
        void setVolatileOrigin(int index, SampleOrigin o);
        SampleOrigin origin(int index) const;

        // Auto-detected loop tempo (BPM) of the file-loaded entry at index, or 0
        // if unknown / not yet detected. Recomputed at load()/relink(); see
        // dsp/TempoEstimate.h. A tempo-tracking Player reads this as a fallback
        // when sourceBars is absent.
        double detectedBpm(int index) const;

        // Detected key of the entry at index (4.9). keyRoot is a pitch class
        // 0..11, or -1 when unknown; keyBrightness is meaningful only when
        // keyRoot >= 0; tuningCents is the deviation from A440. Out-of-range
        // reads return the unknown defaults.
        int    keyRoot(int index) const;
        int    keyBrightness(int index) const;
        double tuningCents(int index) const;

        // ── Effective (override-else-detected) metadata (9.23) ────────────────
        // Every consumer that tracks tempo / key / tuning / one-shot reads these,
        // so a user correction takes effect everywhere. Out-of-range reads return
        // the unknown defaults.
        double effectiveBpm(int index) const;
        int    effectiveKeyRoot(int index) const;
        int    effectiveKeyBrightness(int index) const;
        double effectiveTuningCents(int index) const;
        bool   effectiveOneShot(int index) const;

        // Single-owner setters for the user overrides (message thread). Pass the
        // "unset" sentinel to clear one field (bpm 0, keyRoot -1, oneShot -1);
        // setUserTuningCents(has=false) clears tuning. clearUserOverrides drops all.
        void setUserBpm(int index, double bpm);
        void setUserKey(int index, int keyRoot, int keyBrightness);
        void setUserTuningCents(int index, double cents, bool has);
        void setUserOneShot(int index, int state);   // -1 unset / 0 loop / 1 one-shot
        void clearUserOverrides(int index);

        int size() const { return static_cast<int>(samples_.size()); }
        bool isMissing(int index) const;

        // C3: re-evaluate on-disk existence for every File/Stream (path-backed,
        // non-volatile) entry and update its `missing` flag, so a sample deleted or
        // moved *while the app is running* becomes visible (the pool browser flags
        // it MISSING and enables Relink). Returns true if any flag changed.
        // Does NOT touch decoded PCM: a File entry keeps playing from RAM even after
        // its file vanishes (right for a live set — no mid-performance dropout), and
        // freeing PCM here would race the audio thread. Message-thread only; cheap
        // (a stat per path-backed entry) — call on natural edges (pool-manager open).
        bool rescanMissing();
        const Sample* get(int index) const;

        // ── Stable-identity resolution (9.18) ──────────────────────────────────
        // idOf(index): the SampleId that names the entry at `index` (Persistent by
        //   hash for File/Stream, Volatile by session id for REC/LOOP slots).
        //   Out-of-range / None-origin → {None,0}.
        // indexOf(id): the current array position of the entry matching `id`, or -1
        //   if no entry matches (a persistent hash not present = a missing sample;
        //   a stale volatile id = the slot is gone). O(pool size); the pool is small.
        // resolve(id): the entry matching `id`, or nullptr. Callers route sample
        //   references through these so identity is never array position.
        SampleId      idOf(int index) const;
        int           indexOf(SampleId id) const;
        const Sample* resolve(SampleId id) const;

        // W3a display model — the single source of truth for how a pool entry is
        // named/hinted, shared by the pool browser (SamplePoolOverlay) and every
        // in-machine sample picker (SampleMachine/SliceMachine/StretchMachine via
        // sampleShortName). Volatile captures have no file name, so they are
        // labelled "Record N" / "Loop N" by ordinal within their origin group;
        // never-captured empties read "REC N (empty)" by volatile ordinal (still a
        // pickable slot, bug 14); file entries show the filename stem.
        // Out-of-range → "(none)".
        //   displayHint: captured bars / detected bpm / parent dir / "MISSING".
        juce::String displayName(int index) const;
        juce::String displayHint(int index) const;

        // C4: 1-based ordinal of the entry WITHIN its origin group (FILE 1..n,
        // STREAM 1..n, RECORD 1..n, LOOP 1..n; empty REC slots counted among the
        // volatiles, matching displayName's "REC N"). Unlike the raw array index,
        // this does not jump when the reserved volatile REC slots re-seed at the
        // pool front on reload, so the number a picker shows is stable. Single source
        // for both the in-machine picker and the pool browser. Out-of-range → 0.
        int groupOrdinal(int index) const;

        // Remove the entry at index, shifting higher entries down.
        // Callers must remap all references before calling. Message-thread only.
        bool remove(int index);

        // Swap two entries. Callers must remap all references before calling.
        bool swap(int a, int b);

    private:
        // Length-gated tempo estimate for a freshly decoded entry (uses its
        // cached analysis). Returns 0 for long-form / non-rhythmic material.
        static double detectBpmFor(const Sample& s);

        // Run tempo + key detection on a freshly decoded entry and fuse with the
        // parsed filename/metadata hints, writing detectedBpm/keyRoot/
        // keyBrightness/tuningCents and setting analysed = true. Message thread.
        static void analyseNewPcm(Sample& s, const struct SampleHints& hints);

        juce::AudioFormatManager formatManager_;
        std::vector<std::unique_ptr<Sample>> samples_;
        std::uint32_t takeGroupSeq_ = 0;   // §40.7 take-group id source (session-local)
        // The natural capture width prepareVolatile was prepared with (the default
        // a capture inherits). Slots are allocated wider — kMaxDeckChannels — for a
        // four-sub-track deck, but a stereo-prepared bank captures stereo (§40.3).
        int volatilePrepChannels_ = 2;
        // Monotonic source for volatile session-local ids (9.18). Starts at 1 so
        // 0 stays reserved as "unset" on non-volatile entries.
        std::uint32_t nextVolatileId_ = 1;
    };
}
