#pragma once

#include "IMachine.h"
#include "dsp/Envelope.h"
#include "dsp/MonoGate.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace lockstep
{
    class AnalogMachine : public IMachine
    {
    public:
        AnalogMachine();
        ~AnalogMachine() override;

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;
        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "ANLG"; }
        static constexpr const char* kMachineId = "lockstep.analog.v1";

        static constexpr int kNumSlots = 39;

        int numParams() const override { return kNumSlots; }
        ParamSpec paramSpec(int index) const override;
        int numSections() const override { return kNumSections; }
        SectionInfo section(int index) const override;

        bool isVoiceActive() const override;
        bool hasInternalAmp() const override { return true; }

        Polyphony currentVoices(const ParamFrame& baseParams) const override;

    private:
    // -----------------------------------------------------------------------
    // Slot index constants.
    //
    // IMPORTANT: each section MUST be a contiguous run of slot indices. The
    // editor paginates a section as a contiguous window (firstSlot + 8*page)
    // and blanks any cell whose sectionIndex differs — so a slot tacked onto a
    // section out of index order is simply never rendered. (This is why the
    // per-osc levels / mixer drive / Key Trk were invisible when appended at
    // the end.) Keep new params inside their section's range.

    // Section 1 — SRC (15 slots, 2 pages)
    //   page 1: the two oscillators (pitch / wave / PW)
        static constexpr int kSlotOsc1Coarse = 0;
        static constexpr int kSlotOsc1Fine = 1;
        static constexpr int kSlotOsc1Wave = 2;
        static constexpr int kSlotOsc1PW = 3;
        static constexpr int kSlotOsc2Coarse = 4;
        static constexpr int kSlotOsc2Fine = 5;
        static constexpr int kSlotOsc2Wave = 6;
        static constexpr int kSlotOsc2PW = 7;
    //   page 2: the mixer — per-source levels feeding the summing-amp drive
        static constexpr int kSlotOsc1Level = 8;
        static constexpr int kSlotOsc2Level = 9;
        static constexpr int kSlotSub = 10;
        static constexpr int kSlotNoise = 11;
        static constexpr int kSlotMixerDrive = 12;  // asymmetric summing-amp sat
        static constexpr int kSlotPorta = 13;
        static constexpr int kSlotVoiceMode = 14;

    // Section 2 — FLTR (10 slots, 2 pages)
        static constexpr int kSlotCutoff = 15;
        static constexpr int kSlotRes = 16;
        static constexpr int kSlotFilterType = 17;
        static constexpr int kSlotDrive = 18;
        static constexpr int kSlotFEnvDepth = 19;
        static constexpr int kSlotFEnvA = 20;
        static constexpr int kSlotFEnvD = 21;
        static constexpr int kSlotFEnvS = 22;
        static constexpr int kSlotFEnvR = 23;
        static constexpr int kSlotKeytrack = 24;  // cutoff key-tracking amount

    // Section 3 — AMP (8 slots, 1 page)
        static constexpr int kSlotAmpA = 25;
        static constexpr int kSlotAmpD = 26;
        static constexpr int kSlotAmpS = 27;
        static constexpr int kSlotAmpR = 28;
        static constexpr int kSlotLevel = 29;
        static constexpr int kSlotPan = 30;
        static constexpr int kSlotRetrig = 31;  // 0=LEGATO 1=RETRIG
        static constexpr int kSlotVelSens = 32;

    // Section 4 — MOD/LFO (6 slots, 1 page)
        static constexpr int kSlotLfoRate = 33;
        static constexpr int kSlotLfoDepth = 34;
        static constexpr int kSlotLfoShape = 35;
        static constexpr int kSlotLfoTarget = 36;
        static constexpr int kSlotLfoSync = 37;
        static constexpr int kSlotAge = 38;  // vintage drift + glue macro

        static constexpr int kNumSections = 5;
        static constexpr int kMaxSubVoices = 4;

    // -----------------------------------------------------------------------
    // DSP helpers

        struct SVFState
        {
            float lp = 0.0f, hp = 0.0f, bp = 0.0f;
            void reset() { lp = hp = bp = 0.0f; }
        };

    // One per-pitch sub-voice (both Mono and Para).
    // In Mono mode only subVoices_[0] is used.
    // In Para mode: oscType 0 = render osc1+sub, oscType 1 = render osc2+sub.
        struct SubVoice
        {
            bool active = false;
            int oscType = 0;
            int midiNote = -1;
            int age = 0;
            double osc1Phase = 0.0;
            double osc2Phase = 0.0;
            double subPhase = 0.0;
            double currentFreq = 0.0;
            double targetFreq = 0.0;
      // Per-voice AR follower for Microfreak-style paraphony.
      // Articulates individual notes within a sustained chord; the master amp
      // env governs overall volume.
            dsp::Envelope ar{};
      // Set when all para keys are released so the voice keeps contributing
      // signal through the master amp envelope's release stage.
            bool keepForRelease = false;
        };

        static float polyBlep(double t, double dt) noexcept;

        float oscillatorSample(SubVoice& sv, int osc1Wave, float osc1PW,
                               int osc2Wave, float osc2PW,
                               float subLevel,
                               double osc2FreqRatio,
                               bool paraMode,
                               float osc1Level, float osc2Level) noexcept;

        float filterSample(float in, float f, float q, int filterType,
                           float drive) noexcept;

        void triggerEnvs(const ParamFrame& params);
        void releaseEnvs();

    // Mono-mode helpers.
        void startMonoVoice(int midiNote, const ParamFrame& params);
        void legatoMonoVoice(int midiNote, const ParamFrame& params);
        void releaseMonoVoice();

    // Para-mode helpers.
        int allocSubVoice();
        void startParaVoice(int midiNote, const ParamFrame& params);
        void releaseParaVoice(int midiNote);

        static int msToSamples(float ms, double sr) noexcept
        {
            return std::max(1, static_cast<int>(static_cast<double>(ms) * 0.001 * sr));
        }

        static double midiNoteToHz(int note) noexcept
        {
            return 440.0 * std::pow(2.0, (note - 69) / 12.0);
        }

    // -----------------------------------------------------------------------
    // State

        double sampleRate_ = 44100.0;

        float voiceVelocity_ = 1.0f;
        std::array<SubVoice, kMaxSubVoices> subVoices_{};
        int voiceCounter_ = 0;
        int paraChordNoteIdx_ = 0;

        dsp::Envelope ampEnv_{};
        dsp::Envelope filterEnv_{};

        SVFState svf1_{}, svf2_{};

        float noiseState_ = 0.0f;
        double lfoPhase_ = 0.0;
        float lfoOut_ = 0.0f;

    // "Age" vintage drift: four free-running slow oscillators at mutually
    // detuned sub-Hz rates produce analog wander on osc1/osc2 pitch, filter
    // cutoff and pulse width. Advanced once per block; scaled by the Age macro.
        std::array<double, 4> driftPhase_{ 0.0, 0.37, 0.13, 0.71 };

    // RETRIG mode ghost crossfade: old amp level fades to 0 while new
    // voice attacks from 0.  Combined gain = ampEnv_.tick() + ghostGain_.
        float monoGhostGain_ = 0.0f;
        // Reference note for filter cutoff key-tracking (the shared SVF tracks the
        // most recently played note). Updated on every note-on; defaults to C3.
        int filterTrackNote_ = 60;
        int monoGhostFade_ = 0;

        dsp::MonoGate monoGate_{};

        float lfoRandCurr_ = 0.0f;
        float lfoRandNext_ = 0.0f;
        double lfoRandPhase_ = 0.0;

    // Tracks the previous block's voice mode so we can detect a Mono↔Para
    // switch and flush stale voice state before it causes havoc.
        bool prevParaMode_ = false;

    // One-pole DC blocker on the mono pre-pan output.
        float dcX1_ = 0.0f;
        float dcY1_ = 0.0f;

    // Per-voice AR constants (Microfreak paraphony articulation).
        static constexpr float kParaArAttackMs = 8.0f;
        static constexpr float kParaArReleaseMs = 25.0f;
    };
}
