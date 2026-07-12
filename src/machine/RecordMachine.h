#pragma once

#include "../deckcore/Deck.h"
#include "../deckcore/Heads.h"
#include "../deckcore/Medium.h"
#include "IMachine.h"
#include "ITempoAware.h"
#include "InputSource.h"
#include "SamplePool.h"
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // RecordMachine — live resampler (Octatrack track recorder, DESIGN §29.2 /
    // §30). It captures `input_source` audio into a volatile REC buffer (§28),
    // overwriting it each time a trig fires. It synthesises nothing: the sequencer
    // fills the track buffer from the chosen source before process(), and the
    // recorder copies that input into the target buffer for `rec_length`.
    //
    // "Record trig" is contextual, not a stored step field: any trig on a
    // Record track is a capture trigger (a note-on on these tracks is meaningless
    // as a pitch). A plain trig re-captures every loop; a one-shot trig captures
    // once (the existing TrigCondition::oneShot composes — no machine work). The
    // captured buffer is immediately playable from a Sample/Slice track pointed
    // at the same pool index; freeze-to-disk (§22) is a later milestone.
    //
    // §40.1 deck engine — the 1-track linear face. Record is re-backed on the deck
    // reel (`dc::Medium`, Linear topology, one stereo sub-track): capture writes
    // through a transport-CHASING write head (`ppq × K`, §40.2), so a tempo change
    // mid-take VARISPEEDS the committed take like tape (explicitly NOT pitch-
    // preserved — that is the Loop's FreeLen fit). At constant tempo the head runs
    // at unity and the take is bit-identical to a straight copy. `rec_length` stays a
    // wall-clock cap (real engine samples); the reel length it produces stretches
    // with the tempo trajectory. On close the reel region is committed into the
    // volatile pool slot (Sample tracks play it). One level of deck-native undo
    // restores the slot's prior take.
    //
    // currentVoices() = V1 so the sequencer emits exactly one note-on per trig;
    // the recorder treats any incoming note-on as the capture-start edge.
    class RecordMachine : public IMachine, public ITempoAware
    {
    public:
        explicit RecordMachine(SamplePool& pool) : pool_(pool) {}

        // ITempoAware — bar length for the sourceBars stamp at capture close.
        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }

        static constexpr const char* kMachineId = "lockstep.record.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "REC"; }

        void prepare(double sampleRate, int /*maxBlockSize*/) override
        {
            sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
            // The reel: host-allocated stereo, sized once to the machine's max take
            // (never resized on the audio thread), bound as a LINEAR dc::Medium. The
            // write head fills it; a tempo change mid-take varispeeds it (§40.2).
            reelCap_ = std::max(1, static_cast<int>(kMaxRecSeconds * sampleRate_));
            reel_.setSize(kChannels, reelCap_, false, true, false);
            bindReel();
            reset();
        }
        void reset() override
        {
            deck_.setState(dc::DeckState::Idle);
            samplesRemaining_ = 0;
            calSpp_ = 0.0;
            targetPoolIdx_ = -1;
            writeHead_.setPosition(0.0);
            writeHead_.setRate(1.0);
            if (medium_.bound()) medium_.resetAllUsed();
        }

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // numSections() must exceed the HIGHEST sectionIndex used, not count the
        // non-empty ones: LockstepProcessor::section() gates the machine-owned
        // branch on `sectionIndex < numSections()`. Our params live at kSrcSecIdx
        // (=1), so this must be kSrcSecIdx + 1 or the SRC panel is unreachable.
        [[nodiscard]] int numSections() const override { return kSrcSecIdx + 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        // One capture trigger per trig; the note pitch/velocity are ignored.
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V1;
        }

        // Test/advisory: the last transport snapshot received this block.
        [[nodiscard]] const TransportInfo& transport() const noexcept { return transport_; }

        // Deck-native undo (§40.1): restore the pool slot's take from before the last
        // capture. One level; a second undo is a no-op (nothing older is kept).
        // Returns true if a prior take was restored. Message/quiesced thread.
        bool undo();
        [[nodiscard]] bool canUndo() const noexcept { return undoSlot_ >= 0 && undoLen_ >= 0; }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotTargetBuffer = 1;
        static constexpr int kSlotRecLength = 2;
        static constexpr int kSlotMonitor = 3;  // Off = silent tap, On = pass input through
        static constexpr int kNumSlots = 4;
        static constexpr int kChannels = 2;  // the stereo engine boundary (§40.7)

        // rec_length bounds (seconds). The default project's volatile slots are
        // sized to a fixed capacity; capture truncates at the buffer length.
        static constexpr float kMinRecSeconds = 0.1f;
        static constexpr float kMaxRecSeconds = 12.0f;
        static constexpr float kDefaultRecSeconds = 2.0f;

        // Recording iff the deck says so. The trig starts it; the take's own length
        // ends it, which is what makes Record a one-shot rather than a gesture.
        [[nodiscard]] bool capturing() const noexcept
        {
            return deck_.state() == dc::DeckState::Recording;
        }

        void startCapture(int targetSlot, float recSeconds);
        void writeInput(const juce::AudioBuffer<float>& input, int startSample, int numSamples);
        // Commit the recorded reel region [0, used) into the target pool slot, stamp
        // its musical length + Record origin, and end the take.
        void closeCapture();
        void bindReel();
        // Chase ratio r = calSpp_ / samplesPerPpq(now): 1 at constant tempo (or an
        // uncalibrated take), varispeed as the tempo deviates from the take's start.
        [[nodiscard]] double chaseRatio() const noexcept;

        SamplePool& pool_;
        double sampleRate_ = 44100.0;
        TransportInfo transport_{};  // last block transport (C2)

        // Capture state (audio-thread only). Record is the deck engine's linear,
        // one-shot face (DESIGN §40.1): its verbs arrive as recorder trigs rather
        // than console presses and its take closes itself, but "is this deck
        // recording?" has one answer across Record, Loop and Tape.
        dc::Deck deck_;
        int samplesRemaining_ = 0;    // wall-clock cap left, in ENGINE samples
        int targetPoolIdx_ = -1;      // pool slot this take commits into

        // The reel (§40.3): host-owned stereo storage, bound LINEAR. The write head
        // chases musical time, so the reel length reflects the tempo trajectory.
        juce::AudioBuffer<float> reel_;
        int reelCap_ = 0;
        dc::Medium medium_;
        dc::WriteHead writeHead_;
        double calSpp_ = 0.0;         // calibration: samples-per-ppq latched at start

        // One-level undo (§40.1): the pool slot's take from before the last capture.
        juce::AudioBuffer<float> undoBackup_;
        int undoSlot_ = -1;           // pool index the backup belongs to (-1 = none)
        int undoLen_ = -1;            // backup length in samples (0 = the slot was empty)
        double undoBars_ = 0.0;
        SampleOrigin undoOrigin_ = SampleOrigin::Empty;

        static constexpr std::array<const char* const, 2> kMonitorLabels = { "Off", "On" };
    };
}
