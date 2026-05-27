#pragma once

#include "IMachine.h"
#include "VoiceChoke.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace lockstep
{
  class FMMachine : public IMachine
  {
  public:
    FMMachine();
    ~FMMachine() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(const juce::MidiBuffer& events,
                 const ParamFrame& params,
                 juce::AudioBuffer<float>& buffer) override;

    [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
    [[nodiscard]] const char* badge()     const noexcept override { return "FM"; }
    static constexpr const char* kMachineId = "lockstep.fm.v1";

    int       numParams()          const override { return kNumSlots; }
    ParamSpec paramSpec(int index) const override;
    int       numSections()        const override { return kNumSections; }
    SectionInfo section(int index) const override;

    bool isVoiceActive()    const override;
    bool hasInternalAmp()   const override { return true; }

    // Mono (V1) or Poly (V4); driven by kSlotVoiceMode.
    Polyphony currentVoices(const ParamFrame& baseParams) const override;

    // DX7-inspired ratio table: 18 stepped values [0.5 … 16]
    static constexpr int kNumRatios = 18;
    static constexpr std::array<float, kNumRatios> kRatioTable = {
      0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
      7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f
    };

  private:
    // -----------------------------------------------------------------------
    // Slot index constants

    // Section 1 — SRC (12 slots, 3 pages)
    static constexpr int kSlotRatio1  = 0;
    static constexpr int kSlotRatio2  = 1;
    static constexpr int kSlotRatio3  = 2;
    static constexpr int kSlotRatio4  = 3;
    static constexpr int kSlotFine1   = 4;
    static constexpr int kSlotFine2   = 5;
    static constexpr int kSlotFine3   = 6;
    static constexpr int kSlotFine4   = 7;
    static constexpr int kSlotMix1    = 8;
    static constexpr int kSlotMix2    = 9;
    static constexpr int kSlotMix3    = 10;
    static constexpr int kSlotMix4    = 11;

    // Section 3 — AMP (20 slots, 5 pages, hasInternalAmp=true)
    static constexpr int kSlotMacroAttack   = 12;
    static constexpr int kSlotMacroRelease  = 13;
    static constexpr int kSlotMacroSustain  = 14;
    static constexpr int kSlotOutputLevel   = 15;
    static constexpr int kSlotOp1Attack     = 16;
    static constexpr int kSlotOp1Decay      = 17;
    static constexpr int kSlotOp1Sustain    = 18;
    static constexpr int kSlotOp1Release    = 19;
    static constexpr int kSlotOp2Attack     = 20;
    static constexpr int kSlotOp2Decay      = 21;
    static constexpr int kSlotOp2Sustain    = 22;
    static constexpr int kSlotOp2Release    = 23;
    static constexpr int kSlotOp3Attack     = 24;
    static constexpr int kSlotOp3Decay      = 25;
    static constexpr int kSlotOp3Sustain    = 26;
    static constexpr int kSlotOp3Release    = 27;
    static constexpr int kSlotOp4Attack     = 28;
    static constexpr int kSlotOp4Decay      = 29;
    static constexpr int kSlotOp4Sustain    = 30;
    static constexpr int kSlotOp4Release    = 31;

    // Section 6 — MOD (16 slots, 4 pages, extension of SRC)
    // Grouped by destination: slots[32+dst*4+src] = matrix[src][dst]
    static constexpr int kSlotModBase = 32;  // matrix[src][dst] = kSlotModBase + dst*4 + src

    // Section 7 — VOICE (1 slot, extension of SRC).
    // Mono / Poly switch. Lives in its own extension section so the SRC,
    // MOD, and VOICE pages stay contiguous within their own slot ranges
    // (the MZ navigation assumes section-contiguity).
    static constexpr int kSlotVoiceMode = 48;
    static constexpr int kSlotRetrig    = 49;  // 0=LEGATO 1=RETRIG 2=FREE

    static constexpr int kNumSlots    = 50;
    static constexpr int kNumSections = 8;  // indices 0..7; 6=MOD, 7=VOICE (both SRC extensions)

    static constexpr int kNumOps = 4;
    static constexpr int kMaxVoices = 4;

    // Modulation depth scaling: matrix value ±1 → ±π radians (β≈3.14 at full depth)
    static constexpr float kModScale = 3.14159265358979323846f;

    // -----------------------------------------------------------------------
    // Voice state

    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    struct Operator
    {
      double phase     = 0.0;
      double phaseInc  = 0.0;
      float  output    = 0.0f;
      float  prevOutput= 0.0f;
      Stage  stage     = Stage::Idle;
      float  envLevel  = 0.0f;
      float  releaseStartLevel = 0.0f;
      int    stageRemaining    = 0;
      int    attackSamples     = 0;
      int    decaySamples      = 0;
      float  sustainLevel      = 0.0f;
      int    releaseSamples    = 0;
      float  mixerLevel        = 0.0f;
    };

    struct FMVoice
    {
      bool    active   = false;
      int     midiNote = -1;
      std::uint64_t age = 0;  // monotonic stamp; oldest active voice is stolen first
      std::array<Operator, kNumOps> ops{};
      float   outputLevel = 1.0f;
      // matrix[src][dst]: op src modulates op dst
      std::array<std::array<float, kNumOps>, kNumOps> modMatrix{};
      // Per-voice pending re-trigger state (Mono mode only): set when a
      // note-on lands while this voice is already active so the choke
      // fade can finish before the new voice starts on the same slot.
      bool        hasPendingTrigger = false;
      int         pendingNote       = 60;
      ParamFrame  pendingParams{};
      VoiceChoke  choke{};
      bool        isGhost           = false;  // fade-only slot; deactivates when choke ends
    };

    int  allocVoice();                // returns index in voices_
    int  findVoiceByNote(int midiNote) const;
    void startVoice(int voiceIdx, int midiNote, const ParamFrame& params);
    void legatoUpdateVoice(int midiNote, const ParamFrame& params);
    void releaseVoice(int voiceIdx);
    float advanceEnv(Operator& op);

    static int msToSamples(float ms, double sampleRate)
    {
      return static_cast<int>(static_cast<double>(ms) * 0.001 * sampleRate);
    }

    double  sampleRate_ = 0.0;
    std::array<FMVoice, kMaxVoices> voices_{};
    std::uint64_t voiceCounter_ = 0;
  };
}
