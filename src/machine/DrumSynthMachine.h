#pragma once

#include "IMachine.h"
#include <cmath>
#include <cstdint>

namespace lockstep
{
  class DrumSynthMachine : public IMachine
  {
  public:
    DrumSynthMachine();
    ~DrumSynthMachine() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(const juce::MidiBuffer& events,
                 const ParamFrame& params,
                 juce::AudioBuffer<float>& buffer) override;

    [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
    [[nodiscard]] const char* badge()     const noexcept override { return "DS"; }
    static constexpr const char* kMachineId = "lockstep.drum.v1";

    int         numParams()          const override { return kNumSlots; }
    ParamSpec   paramSpec(int index) const override;
    int         numSections()        const override { return kNumSections; }
    SectionInfo section(int index)   const override;

    bool isVoiceActive()     const override;
    bool hasInternalFilter() const override { return false; }
    bool hasInternalAmp()    const override { return true; }

    Polyphony currentVoices(const ParamFrame& /*baseParams*/) const override
    {
      return Polyphony::V1;
    }

    // -----------------------------------------------------------------------
    // Public types (needed by file-scope helpers in .cpp)

    enum class DrumType : int { Kick = 0, Snare = 1, Hat = 2, Tom = 3,
                                Clap = 4, Cowbell = 5, Cymbal = 6, Rimshot = 7 };
    enum class AmpPhase  { Idle, Attack, Hold, Decay };
    enum class NoisePhase { Idle, Decay };

    struct DrumVoice
    {
      // Oscillator
      double phase        { 0.0 };
      float  pitchHz      { 0.f };
      float  targetHz     { 0.f };
      float  sweepTimer   { 0.f };
      float  sweepSamples { 1.f };

      // Amp envelope (AHD — drums don't sustain)
      AmpPhase ampPhase        { AmpPhase::Idle };
      float    ampLevel        { 0.f };
      float    ampTimer        { 0.f };
      float    ampAttackSamples  { 0.f };
      float    ampHoldSamples    { 0.f };
      float    ampDecaySamples   { 1.f };

      // Noise envelope (snare: independent from amp envelope)
      NoisePhase noisePhase        { NoisePhase::Idle };
      float      noiseLevel        { 0.f };
      float      noiseDecaySamples { 1.f };

      // Click / snap transient (~3 ms fast decay burst)
      float clickLevel        { 0.f };
      float clickDecaySamples { 1.f };

      // TPT SVF state (hat highpass, snare noise bandpass)
      float svfLow  { 0.f };
      float svfBand { 0.f };
      float svfG    { 0.f };
      float svfK    { 0.f };

      // For hat: note-off triggers fast decay instead of ignoring gate
      bool gateOpen { false };

      // Stored drum type (set on note-on; used per-sample in process)
      DrumType currentType { DrumType::Kick };

      // Square oscillator phases (COWBELL: [0..1], CYMBAL: [0..5])
      double sqPhases[6] { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

      // PRNG (xorshift32)
      uint32_t noiseSeed { 2166136261u };

      float velocity { 1.f };
      bool  active   { false };
    };

  private:
    // -----------------------------------------------------------------------
    // Slot index constants

    // Section 1 — SRC (8 slots, 2 pages)
    static constexpr int kSlotType       = 0;  // KICK / SNARE / HAT / TOM
    static constexpr int kSlotTune       = 1;  // base pitch offset (semitones)
    static constexpr int kSlotSweep      = 2;  // pitch sweep range above tune (semitones)
    static constexpr int kSlotSweepDecay = 3;  // pitch sweep time (ms)
    static constexpr int kSlotPunch      = 4;  // click / transient level
    static constexpr int kSlotTone       = 5;  // kick: waveshaper; snare: noise filter; hat: resonance
    static constexpr int kSlotBody       = 6;  // snare: body/noise balance (1 = all body)
    static constexpr int kSlotSnap       = 7;  // snare: snap transient level

    // Section 3 — AMP (5 slots, 1 page)
    static constexpr int kSlotAttack     = 8;
    static constexpr int kSlotHold       = 9;
    static constexpr int kSlotDecay      = 10;
    static constexpr int kSlotNoiseDecay = 11;  // snare: independent noise envelope decay
    static constexpr int kSlotLevel      = 12;
    static constexpr int kSlotRetrig     = 13;  // 0=LEGATO 1=RETRIG 2=FREE
    static constexpr int kSlotVelSens    = 14;  // 0=off, 1=full velocity sensitivity

    static constexpr int kNumSlots    = 15;
    static constexpr int kNumSections = 4;  // 0=TRIG, 1=SRC, 2=FILTER, 3=AMP

    // -----------------------------------------------------------------------
    // Helpers

    static float xorNoise(uint32_t& s) noexcept
    {
      s ^= s << 13u;
      s ^= s >> 17u;
      s ^= s << 5u;
      return static_cast<float>(static_cast<int32_t>(s)) * (1.f / 2147483648.f);
    }

    static float noteHz(int midiNote, float tuneSemitones) noexcept
    {
      return 440.f * std::pow(2.f, (static_cast<float>(midiNote) - 69.f + tuneSemitones) / 12.f);
    }

    static float msToSamples(float ms, double sr) noexcept
    {
      return static_cast<float>(ms * 0.001 * sr);
    }

    void noteOn(int midiNote, float velocity, const ParamFrame& params);
    void noteOff();

    double    sampleRate_ { 44100.0 };
    DrumVoice voice_;
  };
}
