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
    for (auto& v : voices_)
      v.choke.prepare(sampleRate_, 1.5f);
    juce::ignoreUnused(maxBlockSize);
  }

  void FMMachine::reset()
  {
    for (auto& v : voices_)
    {
      v = FMVoice{};
      v.choke.prepare(sampleRate_, 1.5f);
    }
    voiceCounter_ = 0;
  }

  IMachine::Polyphony FMMachine::currentVoices(const ParamFrame& baseParams) const
  {
    if (static_cast<int>(baseParams.size()) > kSlotVoiceMode
        && baseParams[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f)
      return Polyphony::V4;
    return Polyphony::V1;
  }

  // ---------------------------------------------------------------------------
  // Voice allocation helpers

  int FMMachine::allocVoice()
  {
    // First: idle voice with no fade pending.
    for (int i = 0; i < kMaxVoices; ++i)
    {
      const auto& v = voices_[static_cast<std::size_t>(i)];
      if (!v.active && !v.choke.isFading() && !v.hasPendingTrigger)
        return i;
    }
    // All active: steal the oldest, skipping voices already pending a retrigger.
    // This ensures each note in a chord steals a distinct voice.
    int oldest = -1;
    for (int i = 0; i < kMaxVoices; ++i)
    {
      const auto& v = voices_[static_cast<std::size_t>(i)];
      if (v.hasPendingTrigger) continue;
      if (oldest < 0 || v.age < voices_[static_cast<std::size_t>(oldest)].age)
        oldest = i;
    }
    if (oldest >= 0)
      return oldest;
    // All voices already have a pending retrigger — steal the oldest overall.
    oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
      if (voices_[static_cast<std::size_t>(i)].age
          < voices_[static_cast<std::size_t>(oldest)].age)
        oldest = i;
    return oldest;
  }

  int FMMachine::findVoiceByNote(int midiNote) const
  {
    // Newest matching voice (highest age) — handles re-triggering the same
    // pitch in Poly mode where note-offs should retire the most recent voice.
    int best = -1;
    std::uint64_t bestAge = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
      const auto& v = voices_[static_cast<std::size_t>(i)];
      if (v.active && v.midiNote == midiNote)
      {
        if (best < 0 || v.age > bestAge) { best = i; bestAge = v.age; }
      }
    }
    return best;
  }

  // ---------------------------------------------------------------------------

  void FMMachine::startVoice(int voiceIdx, int midiNote, const ParamFrame& params, float velocity)
  {
    auto& voice = voices_[static_cast<std::size_t>(voiceIdx)];

    const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

    const float macroAttack  = p(kSlotMacroAttack);
    const float macroRelease = p(kSlotMacroRelease);
    const float macroSustain = p(kSlotMacroSustain);

    const double midiFreq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

    voice.active      = true;
    voice.midiNote    = midiNote;
    voice.age         = ++voiceCounter_;
    voice.outputLevel = p(kSlotOutputLevel);
    voice.velocity    = std::clamp(velocity, 0.0f, 1.0f);

    for (int dst = 0; dst < kNumOps; ++dst)
      for (int src = 0; src < kNumOps; ++src)
        voice.modMatrix[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)] =
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
      auto& op = voice.ops[static_cast<std::size_t>(i)];

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

  void FMMachine::legatoUpdateVoice(int midiNote, const ParamFrame& params, float velocity)
  {
    auto& voice = voices_[0];
    if (!voice.active)
    {
      startVoice(0, midiNote, params, velocity);
      return;
    }

    const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
    const double midiFreq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

    constexpr int ratioSlots[kNumOps] = { kSlotRatio1, kSlotRatio2, kSlotRatio3, kSlotRatio4 };
    constexpr int fineSlots [kNumOps] = { kSlotFine1,  kSlotFine2,  kSlotFine3,  kSlotFine4  };

    voice.midiNote = midiNote;
    voice.age      = ++voiceCounter_;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);

    for (int i = 0; i < kNumOps; ++i)
    {
      auto& op = voice.ops[static_cast<std::size_t>(i)];
      const int ratioIdx = std::clamp(static_cast<int>(std::round(p(ratioSlots[i]))), 0, kNumRatios - 1);
      const float ratio  = kRatioTable[static_cast<std::size_t>(ratioIdx)];
      const double fineCents = static_cast<double>(p(fineSlots[i]));
      op.phaseInc = midiFreq * static_cast<double>(ratio)
                    * std::pow(2.0, fineCents / 1200.0) / sampleRate_;
    }
  }

  void FMMachine::releaseVoice(int voiceIdx)
  {
    auto& voice = voices_[static_cast<std::size_t>(voiceIdx)];
    for (auto& op : voice.ops)
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
    struct NoteEvent { int samplePos; int note; float vel; bool on; };
    juce::Array<NoteEvent> noteEvents;
    noteEvents.ensureStorageAllocated(8);
    for (const auto& meta : events)
    {
      const auto msg = meta.getMessage();
      if (msg.isNoteOn())
        noteEvents.add({ meta.samplePosition, msg.getNoteNumber(),
                         msg.getFloatVelocity(), true });
      else if (msg.isNoteOff())
        noteEvents.add({ meta.samplePosition, msg.getNoteNumber(), 0.0f, false });
    }

    const int numBlockSamples = buffer.getNumSamples();

    const bool polyMode = (params.size() > static_cast<std::size_t>(kSlotVoiceMode)
                           && params[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f);

    // Early-out if nothing is happening on any voice and no events arrived.
    bool anyVoiceState = false;
    for (const auto& v : voices_)
      if (v.active || v.choke.isFading() || v.hasPendingTrigger)
        { anyVoiceState = true; break; }
    if (!anyVoiceState && noteEvents.isEmpty())
      return;

    const int numOut = buffer.getNumChannels();
    int eventIdx = 0;

    for (int i = 0; i < numBlockSamples; ++i)
    {
      while (eventIdx < noteEvents.size() && noteEvents[eventIdx].samplePos <= i)
      {
        const auto& ev = noteEvents[eventIdx];
        if (!ev.on)
        {
          if (polyMode)
          {
            const int idx = findVoiceByNote(ev.note);
            if (idx >= 0) releaseVoice(idx);
          }
          else
          {
            if (voices_[0].active) releaseVoice(0);
          }
        }
        else
        {
          if (polyMode)
          {
            const int idx = allocVoice();
            auto& voice = voices_[static_cast<std::size_t>(idx)];
            if (voice.active || voice.choke.isFading())
            {
              // Stealing an active/fading voice: defer the new note until the
              // choke fade completes so we don't click.
              voice.pendingNote       = ev.note;
              voice.pendingVelocity   = ev.vel;
              voice.pendingParams     = params;
              voice.hasPendingTrigger = true;
              if (!voice.choke.isFading())
                voice.choke.trigger();
            }
            else
            {
              startVoice(idx, ev.note, params, ev.vel);
            }
          }
          else
          {
            auto& voice = voices_[0];
            const int retrigMode = (params.size() > static_cast<std::size_t>(kSlotRetrig))
                ? static_cast<int>(std::round(params[static_cast<std::size_t>(kSlotRetrig)]))
                : 0;
            if (retrigMode == 2)  // FREE: skip if voice is sounding
            {
              if (!voice.active && !voice.choke.isFading())
                startVoice(0, ev.note, params, ev.vel);
            }
            else if (retrigMode == 0)  // LEGATO: update pitch, let envelope continue
            {
              legatoUpdateVoice(ev.note, params, ev.vel);
            }
            else  // RETRIG: immediate ghost-slot crossfade
            {
              if (voice.active || voice.choke.isFading())
              {
                voices_[1]                    = voice;
                voices_[1].isGhost           = true;
                voices_[1].hasPendingTrigger = false;
                voices_[1].choke.prepare(sampleRate_, 1.5f);
                voices_[1].choke.trigger();
                startVoice(0, ev.note, params, ev.vel);
                voices_[0].isGhost           = false;
                voices_[0].hasPendingTrigger = false;
                voices_[0].choke.reset();
              }
              else
              {
                startVoice(0, ev.note, params, ev.vel);
              }
            }
          }
        }
        ++eventIdx;
      }

      // Per-voice sample mixing.
      float mixed = 0.0f;
      for (int vi = 0; vi < kMaxVoices; ++vi)
      {
        auto& voice = voices_[static_cast<std::size_t>(vi)];

        const float chokeGain = voice.choke.isFading() ? voice.choke.nextGain() : 1.0f;

        // Ghost slots (Mono retrig): deactivate once the choke fade ends.
        if (voice.isGhost && !voice.choke.isFading())
        {
          voice.active  = false;
          voice.isGhost = false;
          continue;
        }

        // Poly voice-steal pending start (not used in Mono mode).
        if (!voice.choke.isFading() && voice.hasPendingTrigger)
        {
          voice.hasPendingTrigger = false;
          startVoice(vi, voice.pendingNote, voice.pendingParams, voice.pendingVelocity);
        }

        if (!voice.active) continue;

        // Deactivate voice once all operators are idle.
        bool anyActive = false;
        for (const auto& op : voice.ops)
          if (op.stage != Stage::Idle) { anyActive = true; break; }
        if (!anyActive) { voice.active = false; continue; }

        // Modulation sums from previous outputs (1-sample delay avoids algebraic loop)
        float modSum[kNumOps] = {};
        for (int dst = 0; dst < kNumOps; ++dst)
          for (int src = 0; src < kNumOps; ++src)
            modSum[dst] += voice.modMatrix[static_cast<std::size_t>(src)]
                                          [static_cast<std::size_t>(dst)]
                           * voice.ops[static_cast<std::size_t>(src)].prevOutput
                           * kModScale;

        for (int j = 0; j < kNumOps; ++j)
        {
          auto& op = voice.ops[static_cast<std::size_t>(j)];
          const float env   = advanceEnv(op);
          const float angle = static_cast<float>(op.phase * (2.0 * 3.14159265358979323846))
                              + modSum[j];
          op.output = std::sin(angle) * env;
          op.phase += op.phaseInc;
          if (op.phase >= 1.0) op.phase -= 1.0;

          // Per-op velocity scaling (affects FM modulation depth, not just output bus)
          const int vsSlot = kSlotOp1VelSens + j;
          const float velSens = (params.size() > static_cast<std::size_t>(vsSlot))
              ? params[static_cast<std::size_t>(vsSlot)] : 0.0f;
          op.output *= 1.0f - velSens + velSens * voice.velocity;
        }

        float sample = 0.0f;
        for (int j = 0; j < kNumOps; ++j)
        {
          const auto& op = voice.ops[static_cast<std::size_t>(j)];
          sample += op.output * op.mixerLevel;
        }
        sample *= voice.outputLevel * chokeGain;

        for (auto& op : voice.ops)
          op.prevOutput = op.output;

        mixed += sample;
      }

      for (int ch = 0; ch < numOut; ++ch)
        buffer.addSample(ch, i, mixed);
    }
  }

  bool FMMachine::isVoiceActive() const
  {
    for (const auto& v : voices_)
      if (v.active || v.choke.isFading() || v.hasPendingTrigger)
        return true;
    return false;
  }

  // ---------------------------------------------------------------------------
  // Schema

  ParamSpec FMMachine::paramSpec(int index) const
  {
    using U = ParamSpec::Unit;
    using R = ParamSpec::Role;

    static constexpr const char* kVoiceModeLabels[] = { "MONO", "POLY" };

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
    // AMP — pages 2-5: per-operator ADSR (attack 0–5s, decay/release 1–10s, exponential curve)
    case kSlotOp1Attack:  { ParamSpec p { "fm_atk_1", "Op1 Atk",  0.0f, 5000.0f,  10.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp1Decay:   { ParamSpec p { "fm_dec_1", "Op1 Dec",  1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp1Sustain: return { "fm_sus_1", "Op1 Sus", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp1Release: { ParamSpec p { "fm_rel_1", "Op1 Rel",  1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp2Attack:  { ParamSpec p { "fm_atk_2", "Op2 Atk",  0.0f, 5000.0f,  10.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp2Decay:   { ParamSpec p { "fm_dec_2", "Op2 Dec",  1.0f,10000.0f, 200.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp2Sustain: return { "fm_sus_2", "Op2 Sus", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp2Release: { ParamSpec p { "fm_rel_2", "Op2 Rel",  1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp3Attack:  { ParamSpec p { "fm_atk_3", "Op3 Atk",  0.0f, 5000.0f,  10.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp3Decay:   { ParamSpec p { "fm_dec_3", "Op3 Dec",  1.0f,10000.0f, 200.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp3Sustain: return { "fm_sus_3", "Op3 Sus", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp3Release: { ParamSpec p { "fm_rel_3", "Op3 Rel",  1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp4Attack:  { ParamSpec p { "fm_atk_4", "Op4 Atk",  0.0f, 5000.0f,  10.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp4Decay:   { ParamSpec p { "fm_dec_4", "Op4 Dec",  1.0f,10000.0f, 200.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp4Sustain: return { "fm_sus_4", "Op4 Sus", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp4Release: { ParamSpec p { "fm_rel_4", "Op4 Rel",  1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::None }; p.skew = 0.3f; return p; }
    case kSlotOp1VelSens: return { "fm_vs_1", "Op1 VelSns", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp2VelSens: return { "fm_vs_2", "Op2 VelSns", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp3VelSens: return { "fm_vs_3", "Op3 VelSns", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotOp4VelSens: return { "fm_vs_4", "Op4 VelSns", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };
    case kSlotRetrig:
    {
      static constexpr const char* kRetrigLabels[] = { "LEGATO", "RETRIG", "FREE" };
      ParamSpec p { "fm_retrig", "Retrig", 0.0f, 2.0f, 0.0f, true, U::None, 3, R::None };
      p.valueLabels = std::span<const char* const>(kRetrigLabels);
      return p;
    }
    case kSlotVoiceMode:  { ParamSpec p { "fm_voice_mode", "Voice", 0.0f, 1.0f, 0.0f, true, U::None, 7, R::None }; p.valueLabels = std::span<const char* const>(kVoiceModeLabels); return p; }
    default: break;
    }

    // MOD section (extension of SRC, section index 6)
    // Slot layout by destination: kSlotModBase + dst*4 + src
    if (index >= kSlotModBase && index < kSlotModBase + kNumOps * kNumOps)
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
    case 6: return { "MOD",   -1, 0, /*parentCanonical=*/1 };
    case 7: return { "VOICE", -1, 0, /*parentCanonical=*/1 };
    default: return {};
    }
  }
}
