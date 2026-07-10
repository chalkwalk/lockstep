#pragma once

#include "../deckcore/Deck.h"
#include "../deckcore/MarkerLane.h"
#include "../deckcore/Medium.h"
#include "IMachine.h"
#include "ITempoAware.h"
#include "InputSource.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

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

        // Console verbs (§40.5), routed like the looper's. For now: RecordCycle
        // punches in/out; PlayStop stops/resumes; Clear wipes the reel.
        void applyVerb(int verb);  // 1 RecordCycle, 2 PlayStop, 3 Clear

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
        [[nodiscard]] double positionSamples() const noexcept { return transport_.transportPhaseSamples; }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotMediumLength = 1;  // reel length, seconds
        static constexpr int kSlotMonitor = 2;       // Off | On (live-thru)
        static constexpr int kNumSlots = 3;

        static constexpr double kDefaultMediumSeconds = 300.0;  // 5 min (§40.3)
        static constexpr double kMinMediumSeconds = 1.0;
        static constexpr double kMaxMediumSeconds = 600.0;

        static constexpr std::array<const char* const, 2> kMonitorLabels = { "Off", "On" };

        void bindReel() noexcept;

        TransportInfo transport_{};
        double sampleRate_ = 44100.0;

        // The reel: host-allocated, stereo, bound as a LINEAR dc::Medium. A
        // pending length change is applied on the next message-thread setMediumSeconds.
        juce::AudioBuffer<float> reel_;
        double mediumSeconds_ = kDefaultMediumSeconds;
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
