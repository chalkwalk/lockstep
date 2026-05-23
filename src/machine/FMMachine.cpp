#include "FMMachine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lockstep
{
  FMMachine::FMMachine()  = default;
  FMMachine::~FMMachine() = default;

  void FMMachine::prepare(double sampleRate, int maxBlockSize)
  {
    sampleRate_ = sampleRate;
    choke_.prepare(sampleRate_, 1.5f);
    juce::ignoreUnused(maxBlockSize);
  }

  void FMMachine::reset()
  {
    voice_             = FMVoice{};
    hasPendingTrigger_ = false;
    choke_.prepare(sampleRate_, 1.5f);
  }

  // ---------------------------------------------------------------------------

  void FMMachine::startVoice(int midiNote, const ParamFrame& params)
  {
    const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

    const float macroAttack  = p(kSlotMacroAttack);
    const float macroRelease = p(kSlotMacroRelease);
    const float macroSustain = p(kSlotMacroSustain);

    const double midiFreq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

    voice_.active      = true;
    voice_.midiNote    = midiNote;
    voice_.outputLevel = p(kSlotOutputLevel);

    for (int dst = 0; dst < kNumOps; ++dst)
      for (int src = 0; src < kNumOps; ++src)
        voice_.modMatrix[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)] =
          p(kSlotModBase + dst * kNumOps + src);

    constexpr int ratioSlots[kNumOps] = { kSlotRatio1, kSlotRatio2, kSlotRatio3, kSlotRatio4 };
    constexpr int fineSlots [kNumOps] = { kSlotFine1,  kSlotFine2,  kSlotFine3,  kSlotFine4  };
    constexpr int mixSlots  [kNumOps] = { kSlotMix1,   kSlotMix2,   kSlotMix3,   kSlotMix4   };
    constexpr int atkSlots  [kNumOps] = { kSlotOp1Attack,  kSlotOp2Attack,  kSlotOp3Attack,  kSlotOp4Attack  };
    constexpr int decSlots  [kNumOps] = { kSlotOp1Decay,   kSlotOp2Decay,   kSlotOp3Decay,   kSlotOp4Decay   };
    constexpr int susSlots  [kNumOps] = { kSlotOp1Sustain, kSlotOp2Sustain, kSlotOp3Sustain, kSlotOp4Sustain };
    constexpr int relSlots  [kNumOps] = { kSlotOp1Release, kSlotOp2Release, kSlotOp3Release, kSlotOp4Release };

    for (int i = 0; i < kNumOps; ++i)
    {
      auto& op = voice_.ops[static_cast<std::size_t>(i)];

      const int   ratioIdx   = std::clamp(static_cast<int>(std::round(p(ratioSlots[i]))), 0, kNumRatios - 1);
      const float ratio      = kRatioTable[static_cast<std::size_t>(ratioIdx)];
      const double fineCents = static_cast<double>(p(fineSlots[i]));
      const double freq      = midiFreq * static_cast<double>(ratio)
                               * std::pow(2.0, fineCents / 1200.0);

      op.phase      = 0.0;
      op.phaseInc   = freq / sampleRate_;
      op.output     = 0.0f;
      op.prevOutput = 0.0f;
      op.mixerLevel = p(mixSlots[i]);

      op.sustainLevel   = std::clamp(p(susSlots[i]) * macroSustain, 0.0f, 1.0f);
      op.attackSamples  = msToSamples(p(atkSlots[i]) * macroAttack,  sampleRate_);
      op.decaySamples   = msToSamples(p(decSlots[i]) * macroRelease, sampleRate_);
      op.releaseSamples = msToSamples(p(relSlots[i]) * macroRelease, sampleRate_);
      op.envLevel       = 0.0f;
      op.releaseStartLevel = 0.0f;

      if (op.attackSamples <= 0)
      {
        op.envLevel = 1.0f;
        if (op.decaySamples > 0)
        {
          op.stage          = Stage::Decay;
          op.stageRemaining = op.decaySamples;
        }
        else
        {
          op.stage          = Stage::Sustain;
          op.stageRemaining = std::numeric_limits<int>::max();
        }
      }
      else
      {
        op.stage          = Stage::Attack;
        op.stageRemaining = op.attackSamples;
      }
    }
  }

  void FMMachine::releaseVoice()
  {
    for (auto& op : voice_.ops)
    {
      if (op.stage == Stage::Attack || op.stage == Stage::Decay || op.stage == Stage::Sustain)
      {
        op.releaseStartLevel = op.envLevel;
        op.stage             = Stage::Release;
        op.stageRemaining    = (op.releaseSamples > 0) ? op.releaseSamples : 1;
      }
    }
  }

  float FMMachine::advanceEnv(Operator& op)
  {
    const float level = op.envLevel;

    switch (op.stage)
    {
    case Stage::Attack:
      op.envLevel += 1.0f / static_cast<float>(op.attackSamples);
      if (--op.stageRemaining <= 0)
      {
        op.envLevel = 1.0f;
        if (op.decaySamples > 0)
        {
          op.stage          = Stage::Decay;
          op.stageRemaining = op.decaySamples;
        }
        else
        {
          op.stage          = Stage::Sustain;
          op.stageRemaining = std::numeric_limits<int>::max();
        }
      }
      break;

    case Stage::Decay:
      op.envLevel -= (1.0f - op.sustainLevel) / static_cast<float>(op.decaySamples);
      op.envLevel  = std::max(op.envLevel, op.sustainLevel);
      if (--op.stageRemaining <= 0)
      {
        op.envLevel = op.sustainLevel;
        if (op.sustainLevel <= 0.0f)
          op.stage = Stage::Idle;
        else
        {
          op.stage          = Stage::Sustain;
          op.stageRemaining = std::numeric_limits<int>::max();
        }
      }
      break;

    case Stage::Sustain:
      break;

    case Stage::Release:
      if (op.releaseSamples > 0)
        op.envLevel -= op.releaseStartLevel / static_cast<float>(op.releaseSamples);
      op.envLevel = std::max(op.envLevel, 0.0f);
      if (--op.stageRemaining <= 0)
      {
        op.envLevel = 0.0f;
        op.stage    = Stage::Idle;
      }
      break;

    case Stage::Idle:
      break;
    }

    return level;
  }

  // ---------------------------------------------------------------------------

  void FMMachine::process(const juce::MidiBuffer& events,
                          const ParamFrame& params,
                          juce::AudioBuffer<float>& buffer)
  {
    int triggerAt   = -1;
    int triggerNote = 60;
    int releaseAt   = -1;
    for (const auto& meta : events)
    {
      const auto msg = meta.getMessage();
      if (msg.isNoteOn())
      {
        triggerAt   = meta.samplePosition;
        triggerNote = msg.getNoteNumber();
      }
      else if (msg.isNoteOff() && releaseAt < 0)
        releaseAt = meta.samplePosition;
    }

    const int numBlockSamples = buffer.getNumSamples();
    if (triggerAt >= 0)
      triggerAt = std::clamp(triggerAt, 0, numBlockSamples - 1);
    if (releaseAt >= 0)
      releaseAt = std::clamp(releaseAt, 0, numBlockSamples - 1);

    if (!voice_.active && !choke_.isFading() && !hasPendingTrigger_ && triggerAt < 0)
      return;

    const int numOut = buffer.getNumChannels();

    for (int i = 0; i < numBlockSamples; ++i)
    {
      if (releaseAt >= 0 && i == releaseAt)
      {
        if (voice_.active)
          releaseVoice();
        releaseAt = -1;
      }

      if (triggerAt >= 0 && i == triggerAt)
      {
        if (voice_.active)
        {
          pendingNote_       = triggerNote;
          pendingParams_     = params;
          hasPendingTrigger_ = true;
          if (!choke_.isFading())
            choke_.trigger();
        }
        else
        {
          startVoice(triggerNote, params);
        }
        triggerAt = -1;
      }

      const float chokeGain = choke_.isFading() ? choke_.nextGain() : 1.0f;
      if (!choke_.isFading() && hasPendingTrigger_)
      {
        hasPendingTrigger_ = false;
        startVoice(pendingNote_, pendingParams_);
      }

      if (!voice_.active)
      {
        if (triggerAt < 0)
          break;
        continue;
      }

      // Deactivate voice once all operators have finished
      bool anyActive = false;
      for (const auto& op : voice_.ops)
        if (op.stage != Stage::Idle)
          anyActive = true;
      if (!anyActive)
      {
        voice_.active = false;
        break;
      }

      // Modulation sums from previous outputs (1-sample delay avoids algebraic loop)
      float modSum[kNumOps] = {};
      for (int dst = 0; dst < kNumOps; ++dst)
        for (int src = 0; src < kNumOps; ++src)
          modSum[dst] += voice_.modMatrix[static_cast<std::size_t>(src)]
                                         [static_cast<std::size_t>(dst)]
                         * voice_.ops[static_cast<std::size_t>(src)].prevOutput
                         * kModScale;

      // Advance envelopes and compute new operator outputs
      for (int j = 0; j < kNumOps; ++j)
      {
        auto& op = voice_.ops[static_cast<std::size_t>(j)];
        const float env   = advanceEnv(op);
        const float angle = static_cast<float>(op.phase * (2.0 * 3.14159265358979323846))
                            + modSum[j];
        op.output = std::sin(angle) * env;
        op.phase += op.phaseInc;
        if (op.phase >= 1.0) op.phase -= 1.0;
      }

      // Mix outputs to audio
      float sample = 0.0f;
      for (int j = 0; j < kNumOps; ++j)
      {
        const auto& op = voice_.ops[static_cast<std::size_t>(j)];
        sample += op.output * op.mixerLevel;
      }
      sample *= voice_.outputLevel * chokeGain;

      // Store outputs for next sample's modulation
      for (auto& op : voice_.ops)
        op.prevOutput = op.output;

      for (int ch = 0; ch < numOut; ++ch)
        buffer.addSample(ch, i, sample);
    }
  }

  bool FMMachine::isVoiceActive() const
  {
    return voice_.active || choke_.isFading() || hasPendingTrigger_;
  }

  // ---------------------------------------------------------------------------
  // Schema

  ParamSpec FMMachine::paramSpec(int index) const
  {
    using U = ParamSpec::Unit;
    using R = ParamSpec::Role;

    switch (index)
    {
    // SRC — page 1: coarse ratios
    case kSlotRatio1: return { "fm_ratio_1", "Op1 Ratio", 0.0f, 17.0f, 1.0f, true,  U::None,    1, R::Pitch };
    case kSlotRatio2: return { "fm_ratio_2", "Op2 Ratio", 0.0f, 17.0f, 1.0f, true,  U::None,    1, R::None  };
    case kSlotRatio3: return { "fm_ratio_3", "Op3 Ratio", 0.0f, 17.0f, 1.0f, true,  U::None,    1, R::None  };
    case kSlotRatio4: return { "fm_ratio_4", "Op4 Ratio", 0.0f, 17.0f, 1.0f, true,  U::None,    1, R::None  };
    // SRC — page 2: fine tune (cents)
    case kSlotFine1:  return { "fm_fine_1", "Op1 Fine", -100.0f, 100.0f, 0.0f, false, U::None,  1, R::None };
    case kSlotFine2:  return { "fm_fine_2", "Op2 Fine", -100.0f, 100.0f, 0.0f, false, U::None,  1, R::None };
    case kSlotFine3:  return { "fm_fine_3", "Op3 Fine", -100.0f, 100.0f, 0.0f, false, U::None,  1, R::None };
    case kSlotFine4:  return { "fm_fine_4", "Op4 Fine", -100.0f, 100.0f, 0.0f, false, U::None,  1, R::None };
    // SRC — page 3: mixer levels (output to audio sum)
    case kSlotMix1:   return { "fm_mix_1", "Op1 Mix", 0.0f, 1.0f, 1.0f, false, U::Percent, 1, R::Level };
    case kSlotMix2:   return { "fm_mix_2", "Op2 Mix", 0.0f, 1.0f, 0.0f, false, U::Percent, 1, R::None  };
    case kSlotMix3:   return { "fm_mix_3", "Op3 Mix", 0.0f, 1.0f, 0.0f, false, U::Percent, 1, R::None  };
    case kSlotMix4:   return { "fm_mix_4", "Op4 Mix", 0.0f, 1.0f, 0.0f, false, U::Percent, 1, R::None  };
    // AMP — page 1: macros + output level
    case kSlotMacroAttack:  return { "fm_macro_atk", "Macro Atk", 0.0f, 3.0f, 1.0f, false, U::None,    3, R::Attack  };
    case kSlotMacroRelease: return { "fm_macro_rel", "Macro Rel", 0.0f, 3.0f, 1.0f, false, U::None,    3, R::Release };
    case kSlotMacroSustain: return { "fm_macro_sus", "Macro Sus", 0.0f, 2.0f, 1.0f, false, U::None,    3, R::Sustain };
    case kSlotOutputLevel:  return { "fm_level",     "Level",     0.0f, 1.0f, 1.0f, false, U::Percent, 3, R::Level   };
    // AMP — pages 2-5: per-operator ADSR
    case kSlotOp1Attack:  return { "fm_atk_1", "Op1 Atk", 1.0f, 5000.0f,  10.0f, false, U::Ms,      3, R::None };
    case kSlotOp1Decay:   return { "fm_dec_1", "Op1 Dec", 1.0f, 5000.0f, 500.0f, false, U::Ms,      3, R::None };
    case kSlotOp1Sustain: return { "fm_sus_1", "Op1 Sus", 0.0f,    1.0f,   0.0f, false, U::Percent, 3, R::None };
    case kSlotOp1Release: return { "fm_rel_1", "Op1 Rel", 1.0f, 5000.0f, 500.0f, false, U::Ms,      3, R::None };
    case kSlotOp2Attack:  return { "fm_atk_2", "Op2 Atk", 1.0f, 5000.0f,  10.0f, false, U::Ms,      3, R::None };
    case kSlotOp2Decay:   return { "fm_dec_2", "Op2 Dec", 1.0f, 5000.0f, 200.0f, false, U::Ms,      3, R::None };
    case kSlotOp2Sustain: return { "fm_sus_2", "Op2 Sus", 0.0f,    1.0f,   0.0f, false, U::Percent, 3, R::None };
    case kSlotOp2Release: return { "fm_rel_2", "Op2 Rel", 1.0f, 5000.0f, 500.0f, false, U::Ms,      3, R::None };
    case kSlotOp3Attack:  return { "fm_atk_3", "Op3 Atk", 1.0f, 5000.0f,  10.0f, false, U::Ms,      3, R::None };
    case kSlotOp3Decay:   return { "fm_dec_3", "Op3 Dec", 1.0f, 5000.0f, 200.0f, false, U::Ms,      3, R::None };
    case kSlotOp3Sustain: return { "fm_sus_3", "Op3 Sus", 0.0f,    1.0f,   0.0f, false, U::Percent, 3, R::None };
    case kSlotOp3Release: return { "fm_rel_3", "Op3 Rel", 1.0f, 5000.0f, 500.0f, false, U::Ms,      3, R::None };
    case kSlotOp4Attack:  return { "fm_atk_4", "Op4 Atk", 1.0f, 5000.0f,  10.0f, false, U::Ms,      3, R::None };
    case kSlotOp4Decay:   return { "fm_dec_4", "Op4 Dec", 1.0f, 5000.0f, 200.0f, false, U::Ms,      3, R::None };
    case kSlotOp4Sustain: return { "fm_sus_4", "Op4 Sus", 0.0f,    1.0f,   0.0f, false, U::Percent, 3, R::None };
    case kSlotOp4Release: return { "fm_rel_4", "Op4 Rel", 1.0f, 5000.0f, 500.0f, false, U::Ms,      3, R::None };
    default: break;
    }

    // MOD section (extension of SRC, section index 6)
    // Slot layout by destination: kSlotModBase + dst*4 + src
    if (index >= kSlotModBase && index < kNumSlots)
    {
      const int offset = index - kSlotModBase;
      const int dst    = offset / kNumOps;
      const int src    = offset % kNumOps;

      // Stable string ids: fm_mod_s{src+1}_d{dst+1}
      static constexpr const char* kIds[kNumOps][kNumOps] = {
        { "fm_mod_s1_d1", "fm_mod_s1_d2", "fm_mod_s1_d3", "fm_mod_s1_d4" },
        { "fm_mod_s2_d1", "fm_mod_s2_d2", "fm_mod_s2_d3", "fm_mod_s2_d4" },
        { "fm_mod_s3_d1", "fm_mod_s3_d2", "fm_mod_s3_d3", "fm_mod_s3_d4" },
        { "fm_mod_s4_d1", "fm_mod_s4_d2", "fm_mod_s4_d3", "fm_mod_s4_d4" },
      };
      // Labels: "{src+1}->{dst+1}"
      static constexpr const char* kLabels[kNumOps][kNumOps] = {
        { "1->1", "1->2", "1->3", "1->4" },
        { "2->1", "2->2", "2->3", "2->4" },
        { "3->1", "3->2", "3->3", "3->4" },
        { "4->1", "4->2", "4->3", "4->4" },
      };
      return {
        kIds   [static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)],
        kLabels[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)],
        -1.0f, 1.0f, 0.0f, false, U::None, 6, R::None
      };
    }

    return {};
  }

  SectionInfo FMMachine::section(int index) const
  {
    switch (index)
    {
    case 1: return { "SRC" };
    case 3: return { "AMP" };
    case 6: return { "MOD", -1, 0, /*parentCanonical=*/1 };
    default: return {};
    }
  }
}
