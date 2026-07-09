#pragma once

#include "IMachine.h"
#include "ITempoAware.h"
#include "SamplePool.h"
#include "../dsp/BungeeStretchEngine.h"
#include "../dsp/PcmStretchSource.h"
#include <array>

namespace lockstep
{
    // StretchMachine — the Flex analog (DESIGN §29.2): independent pitch + tempo
    // playback of a pool buffer via the WSOLA TimeStretch voice (C1/C3). Unlike the
    // rate-based SampleMachine (pitch=speed, the turntable), the Player decouples
    // them: `pitch` transposes without changing duration, and `timestretch=Tempo`
    // stretches the buffer to the project tempo using its stamped sourceBars — so a
    // captured loop stays in time as the BPM changes (what the looper's varispeed
    // self-play and the rate Sample cannot do).
    //
    // Monophonic v1 (one stretch voice), gated like the Static machine
    // (hasInternalAmp suppresses the track ENVELOPE; level/pan come from CHANNEL).
    // A short anti-click fade gates note-on/off. Polyphony + AHDSR are future work.
    class StretchMachine : public IMachine, public ITempoAware
    {
    public:
        explicit StretchMachine(SamplePool& pool) : pool_(pool) {}

        static constexpr const char* kMachineId = "lockstep.stretch.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }

        // 9.18: a Flex-style PCM player — resolves resident PCM (File + captures).
        [[nodiscard]] SampleClass sampleClass() const override { return SampleClass::Pcm; }
        [[nodiscard]] const char* badge() const noexcept override { return "STCH"; }

        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        // Transport graceful-stop release (DESIGN: layered stop model). A note-off
        // (gate end) stays a crisp ~5 ms anti-click gate; releaseAllVoices() instead
        // fades the held voice out over the adjustable `player_release` time, so a
        // single-tap graceful stop lets a long stem ring down musically instead of
        // being cut. Called on the transport falling edge for every non-MIDI track.
        void releaseAllVoices() override;

        // Track/master CUT (double/triple-tap Play): snap the voice out over the fast
        // gate with no release, so a resumed transport starts clean. Cancels any
        // in-flight graceful release so the slow ramp can't outlive the mute.
        void killAllVoices() override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // kSrcSecIdx + 1, NOT a count: section() is gated on
        // `sectionIndex < numSections()`, so this must exceed the highest section
        // index used. All slots live at kSrcSecIdx (=1); returning 1 made `1 < 1`
        // false and hid the whole SRC panel (sample-id + pitch unreachable — the
        // "no source panel" trap in CLAUDE.md). Cf. StreamMachine, which got this right.
        [[nodiscard]] int numSections() const override { return kSrcSecIdx + 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        [[nodiscard]] bool hasInternalAmp() const override { return true; }
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V1;
        }

    private:
        static constexpr int kSlotSampleId = 0;
        static constexpr int kSlotPitch = 1;        // ±24 semitones (independent)
        static constexpr int kSlotTimestretch = 2;  // 0 = Off (native), 1 = Tempo
        static constexpr int kSlotStart = 3;        // trim 0..1
        static constexpr int kSlotTune = 4;         // ±50 cents fine-tune
        static constexpr int kSlotLoop = 5;         // 0 = Off, 1 = On
        static constexpr int kSlotReverse = 6;      // 0 = Fwd, 1 = Rev
        static constexpr int kSlotTuneMode = 7;     // 0 = Auto (cancel A440 dev), 1 = Raw
        static constexpr int kSlotRelease = 8;      // graceful-stop fade time (0..1)
        static constexpr int kNumSlots = 9;

        [[nodiscard]] double timeRatioFor(int playedLen) const;
        [[nodiscard]] double pitchRatioFor(int midiNote, const ParamFrame& params) const;
        void startNote(int midiNote, const ParamFrame& params);
        // Apply the loop window from the cached note-on ingredients. Called at
        // note-on and again from process() when player_loop is toggled mid-voice
        // (the live re-latch — a sustaining one-shot voice never re-fires, so the
        // toggle would otherwise be inert).
        void applyLoop(bool loop);

        SamplePool& pool_;
        double sampleRate_ = 44100.0;
        int maxBlock_ = 512;
        BungeeStretchEngine engine_;
        PcmStretchSource source_;
        TransportInfo transport_{};

        bool playing_ = false;
        int activeNote_ = -1;
        int activeSampleId_ = -1; // pool index of the playing buffer
        int playedLen_ = 0;       // played region length (samples) at note-on
        int tsMode_ = 1;          // resolved timestretch mode for the active note
        // Loop-window ingredients cached at note-on so process() can re-apply the
        // exact window math when player_loop is toggled while the voice sustains.
        bool loopOn_ = false;     // latched loop state for the active note
        bool reverse_ = false;    // latched reverse for the active note
        int startSample_ = 0;     // trim point (samples) for the active note
        int pcmLen_ = 0;          // buffer length (samples) of the active buffer
        float gain_ = 0.0f;       // anti-click gate gain
        float fadeInc_ = 0.0f;    // per-sample gate ramp (note-on/off anti-click)
        float releaseInc_ = 0.0f; // per-sample down-ramp for a graceful stop (cached
                                  // from player_release each block; used by releaseAllVoices)
        bool  releasing_ = false; // true = fading out under a graceful stop, not a note-off

        static constexpr std::array<const char* const, 2> kTsLabels = { "Off", "Tempo" };
        static constexpr std::array<const char* const, 2> kLoopLabels = { "Off", "On" };
        static constexpr std::array<const char* const, 2> kRevLabels = { "Fwd", "Rev" };
        static constexpr std::array<const char* const, 2> kTuneModeLabels = { "Auto", "Raw" };
    };
}
