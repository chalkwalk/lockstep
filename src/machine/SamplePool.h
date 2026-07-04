#pragma once

#include "TransientDetector.h"
#include "../core/Scale.h"   // kAeolian (key brightness default)
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lockstep
{
    struct SampleRef
    {
        std::string path;
        std::uint32_t hashXX32 = 0;
    };

    // Where a pool entry came from — drives the pool browser's grouping (W3a).
    // File entries are disk-backed; volatile entries start Empty and become
    // Record / Loop when a capture writes into them (a Clear reverts to Empty).
    enum class SampleOrigin : std::uint8_t { File = 0, Empty, Record, Loop };

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
        // True once analysis (or cache adoption) has run for this entry. Lets
        // displayHint distinguish "analysed, no rhythm" (a one-shot) from "not
        // analysed yet", and drives what the serializer writes.
        bool   analysed = false;

        // Cached per-block analysis for transient detection (message thread only).
        // Populated by SamplePool::load(); empty for missing entries.
        BlockAnalysis analysis;
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

        // Append a placeholder entry for a file that could not be found.
        // Preserves the pool index so P-Lock references remain valid.
        // Message-thread only.
        int addMissing(const SampleRef& ref);

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

        int size() const { return static_cast<int>(samples_.size()); }
        bool isMissing(int index) const;
        const Sample* get(int index) const;

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
    };
}
