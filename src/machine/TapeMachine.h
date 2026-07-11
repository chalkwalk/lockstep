#pragma once

#include "../deckcore/Deck.h"
#include "../deckcore/EraseHead.h"
#include "../deckcore/Heads.h"
#include "../deckcore/MarkerLane.h"
#include "../deckcore/Medium.h"
#include "ConsoleMode.h"
#include "IMachine.h"
#include "ITempoAware.h"
#include "InputSource.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace lockstep
{
    // TapeMachine — the third face of the deck engine (DESIGN §40, §40.2/§40.3).
    //
    // A tape is made of POSITION. Where Record overwrites a buffer on a trig and
    // Loop is a circular sound-on-sound loop, Tape is a LINEAR medium addressed by
    // the song's own absolute position (PRINCIPLES §25.1): the playhead is the
    // transport's playhead, so playback reads whatever is on the reel at the
    // current position, recording writes the input there, and a locate winds the
    // tape — the reel follows the position because it IS the position (§40.2).
    //
    // The medium is a host-allocated fixed-length reel (§40.3), settable, and an
    // honest limit: content past the reel's end does not exist (linear topology),
    // so a transport that runs off the end simply records and plays nothing —
    // the tape ran out. Unlike Loop, Tape does not use the volatile pool (a
    // five-minute reel cannot be a twelve-second slot); it owns its reel.
    //
    // This is the first slice: record-along-timeline + playback + locate at unity
    // rate. Varispeed (medium-rate deviation via the heads), punch-as-a-layer,
    // markers, i16 depth, and the console are follow-ups.
    class TapeMachine : public IMachine, public ITempoAware
    {
    public:
        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }

        static constexpr const char* kMachineId = "lockstep.tape.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "TAPE"; }
        [[nodiscard]] bool isDeckClass() const override { return true; }  // §40.7
        // A tape is a console machine (§40.5): the step grid is its transport +
        // marker surface, always on when the track is focused.
        [[nodiscard]] ConsoleMode consoleMode() const override { return ConsoleMode::AlwaysOn; }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // Params live at kSrcSecIdx, so numSections must be kSrcSecIdx + 1 or the
        // SRC panel is unreachable (the numSections()-is-highest+1 rule).
        [[nodiscard]] int numSections() const override { return kSrcSecIdx + 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        // Message thread: (re)allocate the reel to `seconds`. Allocation policy is
        // host-side (§40.11) — this is the one place the tape's storage is sized.
        void setMediumSeconds(double seconds);
        // Message thread: switch the reel's sample depth (F32 ↔ I16, §40.10). A
        // depth change reallocates the backing and discards the take — honest: you
        // swapped the tape stock. No-op when the depth is unchanged.
        void setMediumDepth(int depth);   // 0 = F32, 1 = I16

        // Console verbs (§40.5), routed like the looper's. For now: RecordCycle
        // punches in/out; PlayStop stops/resumes; Clear wipes the reel.
        void applyVerb(int verb);  // 1 RecordCycle, 2 PlayStop, 3 Clear, 4 Undo
        [[nodiscard]] bool canUndo() const noexcept { return haveUndo_; }

        // §40.2 scrub / wind (standalone only — the processor gates on
        // transportWindable()). While the tape is Stopped, a non-zero scrub rate
        // auditions the reel under a moving head (bandlimited, so a wind is audible
        // both directions and stops at the leader). Rate is signed reel samples per
        // engine sample; the audio slews toward it so it winds rather than jumps.
        // Reel-is-truth: the processor commits the transport to the head each block,
        // so play/punch resume where the ear found the point. Message thread.
        void setScrubTargetRate(double reelRate) noexcept
        {
            scrubTarget_.store(reelRate, std::memory_order_relaxed);
        }
        // A jog nudge (encoder rock): a one-shot velocity impulse the audio decays.
        void nudgeScrub(double reelImpulse) noexcept
        {
            scrubTarget_.store(scrubTarget_.load(std::memory_order_relaxed) + reelImpulse,
                               std::memory_order_relaxed);
        }
        [[nodiscard]] bool scrubActive() const noexcept
        {
            return std::abs(scrubTarget_.load(std::memory_order_relaxed)) > 1e-6 || scrubbing_;
        }
        [[nodiscard]] double scrubHeadReelPos() const noexcept
        {
            return scrubHeadReel_.load(std::memory_order_relaxed);
        }
        [[nodiscard]] double scrubHeadPpq() const noexcept { return reelToPpq(scrubHeadReelPos()); }

        // Markers (§40.4) — dumb navigation points on the timeline. Dropped at the
        // current transport position (manually, or auto on a Scene/Song switch while
        // recording — the processor drives that). A cue returns a target position
        // for the host to LOCATE to; the marker itself fires nothing (fence #1).
        int dropMarkerHere(int labelId = 0);
        // Drop at an explicit position — the processor's auto-drop passes the
        // block's current position (the machine's own transport_ lags a block).
        int dropMarkerAt(double posSamples, int labelId = 0);
        void clearMarkers() noexcept { markers_.clear(); }
        [[nodiscard]] int markerCount() const noexcept { return markers_.count(); }
        [[nodiscard]] double markerPosition(int i) const noexcept
        {
            return (i >= 0 && i < markers_.count()) ? markers_.at(i).positionSamples : -1.0;
        }
        // Cue targets, in samples, from the current transport position; -1 if none.
        [[nodiscard]] double cueNearest() const noexcept;
        [[nodiscard]] double cueNext() const noexcept;
        [[nodiscard]] double cuePrev() const noexcept;
        [[nodiscard]] dc::MarkerLane& markerLane() noexcept { return markers_; }
        [[nodiscard]] const dc::MarkerLane& markerLane() const noexcept { return markers_; }

        // Advisory (message thread / tests).
        [[nodiscard]] dc::DeckState state() const noexcept { return deck_.state(); }
        [[nodiscard]] bool recording() const noexcept { return deck_.state() == dc::DeckState::Recording; }
        [[nodiscard]] double mediumSeconds() const noexcept { return mediumSeconds_; }
        [[nodiscard]] int recordedSamples() const noexcept { return medium_.used(0); }
        // Copy the recorded extent out for promotion (§40.8), reading through the
        // medium so it is depth-transparent (an i16 reel promotes the same as f32).
        // Message thread / non-audio use only.
        void copyReelTo(juce::AudioBuffer<float>& dst, int numFrames) const noexcept;
        [[nodiscard]] bool depthI16() const noexcept { return depthI16_; }
        [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
        // Reel position under chase-lock (§40.2): a CALIBRATED reel is addressed by
        // musical position × calibration (`ppq × K`); an uncalibrated one falls back
        // to the raw transportPhaseSamples, which is byte-identical to today (and
        // is what a reel with no tempo context — e.g. a bare unit test — uses).
        [[nodiscard]] double positionSamples() const noexcept { return reelPosAtBlockStart(); }
        // §40.2 chase-lock diagnostics (message thread / strip / tests).
        [[nodiscard]] double calibrationSamplesPerPpq() const noexcept { return calSamplesPerPpq_; }
        // Reel samples advanced per engine sample at the current tempo: 1 at the
        // calibration tempo (and on an uncalibrated reel), != 1 when the tempo
        // deviates. This is the number the strip surfaces as "×0.5" etc. Live from
        // the last transport snapshot, so it is right even between processed blocks.
        [[nodiscard]] double chaseRatio() const noexcept { return chaseRatioNow(); }
        // Reel-sample → ppq, via this reel's calibration (single owner of the
        // inverse mapping — the cue→locate path uses it, §40.2 reel-is-truth).
        [[nodiscard]] double reelToPpq(double reelPos) const noexcept
        {
            const double k = calSamplesPerPpq_ > 0.0 ? calSamplesPerPpq_ : currentSamplesPerPpq();
            return k > 0.0 ? reelPos / k : 0.0;
        }
        // Drop a marker at a musical position (ppq), converted into this reel's
        // domain by its own calibration — the processor's auto-drop passes the
        // block's ppq (the machine's transport_ lags a block, §40.4).
        int dropMarkerAtPpq(double ppq, int labelId = 0);

        [[nodiscard]] double currentSamplesPerPpq() const noexcept
        {
            return (transport_.barPpq > 0.0) ? transport_.samplesPerBar / transport_.barPpq : 0.0;
        }
        // The chase rate r = K / samplesPerPpq(current). 1 on an uncalibrated reel
        // or with no tempo context (→ the unity integer path, today's behaviour).
        [[nodiscard]] double chaseRatioNow() const noexcept
        {
            const double spp = currentSamplesPerPpq();
            return (calSamplesPerPpq_ > 0.0 && spp > 0.0) ? calSamplesPerPpq_ / spp : 1.0;
        }
        [[nodiscard]] double reelPosAtBlockStart() const noexcept
        {
            return calSamplesPerPpq_ > 0.0 ? transport_.transportPpq * calSamplesPerPpq_
                                           : transport_.transportPhaseSamples;
        }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotMediumLength = 1;  // reel length, seconds
        static constexpr int kSlotMonitor = 2;       // Off | On (live-thru)
        static constexpr int kSlotMediumDepth = 3;   // F32 | I16 (§40.10)
        static constexpr int kNumSlots = 4;

        static constexpr double kDefaultMediumSeconds = 300.0;  // 5 min (§40.3)
        static constexpr double kMinMediumSeconds = 1.0;
        static constexpr double kMaxMediumSeconds = 600.0;

        static constexpr std::array<const char* const, 2> kMonitorLabels = { "Off", "On" };
        static constexpr std::array<const char* const, 2> kDepthLabels = { "32f", "16i" };

        void bindReel() noexcept;
        // (Re)allocate the reel + undo backing for the current seconds/depth and
        // rebind the medium. One place owns the storage geometry (§40.11).
        void allocateReel();

        TransportInfo transport_{};
        double sampleRate_ = 44100.0;

        // The reel: host-allocated, stereo, bound as a LINEAR dc::Medium (§40.3).
        // Exactly ONE backing is live at a time — a 32-bit float AudioBuffer, or a
        // flat 16-bit block (2×cap, planar). Both are allocated WITHOUT zero-fill
        // (lazy commit): a reel costs address space, and only recorded samples
        // become resident — i16 must keep that property to actually halve RAM.
        int reelCap_ = 0;               // per-channel capacity of the live backing
        bool depthI16_ = false;         // false = F32 (reel_), true = I16 (reelI16_)
        juce::AudioBuffer<float> reel_;
        std::unique_ptr<std::int16_t[]> reelI16_;

        // §40.3 / fence #8: a punch is non-destructive. As recording overwrites the
        // reel, the ORIGINAL sample at each first-touched position is saved into the
        // undo backing (span-scoped: only the punched region), so Undo restores what
        // was there. One level deep, like the looper's. The undo backing mirrors the
        // reel's depth (keeping it f32 would forfeit half the i16 RAM win) and is
        // reached through undoStore_ so save/restore is depth-transparent.
        juce::AudioBuffer<float> undoReel_;
        std::unique_ptr<std::int16_t[]> undoI16_;
        std::array<dc::Store, 2> undoStore_{};
        int undoLo_ = -1;      // lowest position saved this punch (-1 = none)
        int undoHi_ = -1;      // highest position saved this punch
        bool haveUndo_ = false;
        double mediumSeconds_ = kDefaultMediumSeconds;

        // §40.2 chase-lock. The reel's calibration (samples per ppq) latches from
        // the transport tempo at the first record onto an empty reel, and Clear
        // resets it to 0. 0 = uncalibrated → chase at unity (an empty deck behaves
        // exactly as a 1× timeline). Session-local runtime state, like the reel
        // audio: promote-or-lose, never serialized; a fresh machine loses both.
        double calSamplesPerPpq_ = 0.0;

        // §40.2 scrub / wind. scrubTarget_ is the commanded rate (wind cells set a
        // steady value; a jog adds an impulse). The audio owns headPos_ + the slewed
        // scrubSmoothed_ and publishes the head to scrubHeadReel_ for the processor's
        // reel-is-truth locate.
        std::atomic<double> scrubTarget_{ 0.0 };     // reel samples / engine sample
        std::atomic<double> scrubHeadReel_{ 0.0 };   // published head (reel samples)
        double scrubSmoothed_ = 0.0;                 // slewed rate (audio thread)
        double scrubHeadPos_ = 0.0;                  // fractional reel head (audio)
        bool scrubbing_ = false;                     // rendering a wind this block
        static constexpr double kScrubEase = 0.0008; // one-pole slew per sample

        std::array<dc::Store, 2> planes_{};
        dc::Medium medium_;

        // The deck state machine (§40.1): Idle/Playing/Recording/Stopped. Tape does
        // not overdub-at-wrap or close-at-length, so it uses a subset.
        dc::Deck deck_;

        // The marker lane (§40.4). Serialized with the deck (§40.8); the audio has
        // to be promoted, but the marks are metadata that always persist.
        dc::MarkerLane markers_;
    };
}
