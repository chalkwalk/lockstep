#pragma once

#include "IMachine.h"
#include "VoiceChoke.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace lockstep
{
  class VAMachine : public IMachine
  {
  public:
    VAMachine();
    ~VAMachine() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(const juce::MidiBuffer& events,
                 const ParamFrame& params,
                 juce::AudioBuffer<float>& buffer) override;

    [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
    [[nodiscard]] const char* badge()     const noexcept override { return "VA"; }
    static constexpr const char* kMachineId = "lockstep.va.v1";

    int       numParams()          const override { return kNumSlots; }
    ParamSpec paramSpec(int index) const override;
    int       numSections()        const override { return kNumSections; }
    SectionInfo section(int index) const override;

    bool isVoiceActive()     const override;
    bool hasInternalFilter() const override { return true; }
    bool hasInternalAmp()    const override { return true; }

    // Voice mode is parameter-driven: Mono = V1, Para = V4 (kMaxSubVoices).
    Polyphony currentVoices(const ParamFrame& baseParams) const override;

  private:
    // -----------------------------------------------------------------------
    // Slot index constants

    // Section 1 — SRC (12 slots, 2 pages)
    static constexpr int kSlotOsc1Coarse = 0;
    static constexpr int kSlotOsc1Fine   = 1;
    static constexpr int kSlotOsc1Wave   = 2;
    static constexpr int kSlotOsc1PW     = 3;
    static constexpr int kSlotOsc2Coarse = 4;
    static constexpr int kSlotOsc2Fine   = 5;
    static constexpr int kSlotOsc2Wave   = 6;
    static constexpr int kSlotOsc2PW     = 7;
    static constexpr int kSlotSub        = 8;
    static constexpr int kSlotNoise      = 9;
    static constexpr int kSlotPorta      = 10;
    static constexpr int kSlotVoiceMode  = 11;

    // Section 2 — FLTR (9 slots, 2 pages)
    static constexpr int kSlotCutoff     = 12;
    static constexpr int kSlotRes        = 13;
    static constexpr int kSlotFilterType = 14;
    static constexpr int kSlotDrive      = 15;
    static constexpr int kSlotFEnvDepth  = 16;
    static constexpr int kSlotFEnvA      = 17;
    static constexpr int kSlotFEnvD      = 18;
    static constexpr int kSlotFEnvS      = 19;
    static constexpr int kSlotFEnvR      = 20;

    // Section 3 — AMP (6 slots, 1 page)
    static constexpr int kSlotAmpA       = 21;
    static constexpr int kSlotAmpD       = 22;
    static constexpr int kSlotAmpS       = 23;
    static constexpr int kSlotAmpR       = 24;
    static constexpr int kSlotLevel      = 25;
    static constexpr int kSlotPan        = 26;

    // Section 4 — LFO (5 slots, 1 page)
    static constexpr int kSlotLfoRate    = 27;
    static constexpr int kSlotLfoDepth   = 28;
    static constexpr int kSlotLfoShape   = 29;
    static constexpr int kSlotLfoTarget  = 30;
    static constexpr int kSlotLfoSync    = 31;

    static constexpr int kNumSlots    = 32;
    static constexpr int kNumSections = 5;  // indices 0..4
    static constexpr int kMaxSubVoices = 4;

    // -----------------------------------------------------------------------
    // DSP helpers

    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    struct SVFState
    {
      float lp = 0.0f, hp = 0.0f, bp = 0.0f;
      void reset() { lp = hp = bp = 0.0f; }
    };

    // One per-pitch sub-voice (both Mono and Para).
    // In Mono mode only subVoices_[0] is used; oscType is ignored (renders both oscs).
    // In Para mode: oscType 0 = render osc1+sub, oscType 1 = render osc2+sub.
    // Slot assignment: note_index_in_chord % 2 → oscType, so note0→osc1, note1→osc2,
    // note2→osc1, note3→osc2. Noise is shared across all voices (see VAMachine::noise_).
    struct SubVoice
    {
      bool   active      = false;
      int    oscType     = 0;      // 0 = osc1, 1 = osc2 (para mode only; ignored in mono)
      int    midiNote    = -1;
      int    age         = 0;      // allocation counter for steal-oldest
      double osc1Phase   = 0.0;
      double osc2Phase   = 0.0;
      double subPhase    = 0.0;
      double currentFreq = 0.0;   // portamento target tracking (Hz)
      double targetFreq  = 0.0;
      float  microAmp    = 1.0f;  // per-voice micro-amp for note-off fade
      int    fadeRemain  = 0;     // samples remaining in 2ms note-off fade
    };

    // Shared filter + amplitude envelope state.
    struct SharedEnv
    {
      // Filter envelope
      Stage fStage    = Stage::Idle;
      float fLevel    = 0.0f;
      float fReleaseStart = 0.0f;
      int   fRemain   = 0;
      float fDecayMul = 0.0f;
      float fRelMul   = 0.0f;
      float fSustain  = 0.0f;

      // Amp envelope
      Stage aStage    = Stage::Idle;
      float aLevel    = 0.0f;
      float aReleaseStart = 0.0f;
      int   aRemain   = 0;
      float aDecayMul = 0.0f;
      float aRelMul   = 0.0f;
      float aSustain  = 0.0f;
    };

    // PolyBLEP correction for anti-aliased oscillators.
    static float polyBlep(double t, double dt) noexcept;

    // Generate a single sample for a sub-voice oscillator stack.
    // Advances sub-voice phase accumulators.
    // In Para mode, oscType selects which oscillator to render (0=osc1+sub, 1=osc2+sub).
    // In Mono mode (oscType<0 convention: pass -1 to render all), both oscs are rendered.
    float oscillatorSample(SubVoice& sv, int osc1Wave, float osc1PW,
                           int osc2Wave, float osc2PW,
                           float subLevel,
                           double osc2FreqRatio,
                           bool paraMode) noexcept;

    // SVF: runs one pass through the two-stage cascade or single-stage.
    // filterType: 0=LP4, 1=LP2, 2=HP, 3=BP
    float filterSample(float in, float f, float q, int filterType) noexcept;

    // Advance an ADSR stage by one sample; return current level.
    static float advanceEnvLevel(Stage& stage, float& level, float& releaseStart,
                                  int& remain, float decayMul, float relMul,
                                  float sustain) noexcept;

    // Called when a new note-on arrives (both modes share this for envelope).
    void triggerEnvelopes(const ParamFrame& params);
    void releaseEnvelopes();

    // Mono-mode helpers.
    void startMonoVoice(int midiNote, const ParamFrame& params);
    void releaseMonoVoice();

    // Para-mode helpers.
    int  allocSubVoice();           // returns index of assigned sub-voice
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

    std::array<SubVoice, kMaxSubVoices> subVoices_{};
    int    voiceCounter_     = 0;   // monotonic counter for age-based stealing
    int    paraChordNoteIdx_ = 0;   // tracks which chord note is being assigned next
    SharedEnv env_{};
    SVFState  svf1_{}, svf2_{};

    // Shared noise state (single generator mixed into the pre-filter bus).
    float noiseState_ = 0.0f;

    double lfoPhase_    = 0.0;
    float  lfoOut_      = 0.0f;

    // Mono-mode pending retrigger (same choke-wait pattern as FMMachine).
    VoiceChoke choke_;
    bool  hasPendingTrigger_ = false;
    int   pendingNote_       = 60;
    ParamFrame pendingParams_{};

    // Random state for S&H LFO.
    float  lfoRandCurr_ = 0.0f;
    float  lfoRandNext_ = 0.0f;
    double lfoRandPhase_= 0.0;  // tracks phase for edge detection
  };
}
