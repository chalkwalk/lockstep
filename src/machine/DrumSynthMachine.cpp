#include "DrumSynthMachine.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lockstep
{
  DrumSynthMachine::DrumSynthMachine() = default;
  DrumSynthMachine::~DrumSynthMachine() = default;

  // ---------------------------------------------------------------------------
  // Lifecycle

  void DrumSynthMachine::prepare(double sampleRate, int /*maxBlockSize*/)
  {
    sampleRate_ = sampleRate;
    reset();
  }

  void DrumSynthMachine::reset()
  {
    voice_ = DrumVoice{};
  }

  // ---------------------------------------------------------------------------
  // Schema

  ParamSpec DrumSynthMachine::paramSpec(int index) const
  {
    using U = ParamSpec::Unit;
    using R = ParamSpec::Role;
    using V = ParamSpec::Variant;

    static constexpr const char* kTypeLabels[] = { "KICK", "SNARE", "HAT", "TOM" };

    switch (index)
    {
      // --- SRC section (index 1) ---
      case kSlotType:
      {
        ParamSpec p { "drum_type", "Type", 0.f, 3.f, 0.f, true, U::None, 1, R::None, V::Primary };
        p.valueLabels = std::span<const char* const>{ kTypeLabels, 4 };
        return p;
      }
      case kSlotTune:
        return { "drum_tune",        "Tune",    -24.f,   24.f,   0.f, false, U::Semitones, 1, R::Pitch };
      case kSlotSweep:
        return { "drum_sweep",       "Sweep",     0.f,   48.f,  24.f, false, U::Semitones, 1, R::None  };
      case kSlotSweepDecay:
        return { "drum_sweep_decay", "Swp Dec",   1.f,  500.f,  60.f, false, U::Ms,        1, R::None  };
      case kSlotPunch:
        return { "drum_punch",       "Punch",     0.f,    1.f,  0.5f, false, U::None,      1, R::None  };
      case kSlotTone:
        return { "drum_tone",        "Tone",      0.f,    1.f,  0.3f, false, U::None,      1, R::Drive };
      case kSlotBody:
        return { "drum_body",        "Body",      0.f,    1.f,  0.5f, false, U::None,      1, R::None  };
      case kSlotSnap:
        return { "drum_snap",        "Snap",      0.f,    1.f,  0.5f, false, U::None,      1, R::None  };

      // --- AMP section (index 3) ---
      case kSlotAttack:
        return { "drum_attack",      "Attack",    0.f,   50.f,   2.f, false, U::Ms,        3, R::Attack };
      case kSlotHold:
        return { "drum_hold",        "Hold",      0.f,  200.f,   0.f, false, U::Ms,        3, R::Hold   };
      case kSlotDecay:
        { ParamSpec p { "drum_decay", "Decay", 1.f, 5000.f, 500.f, false, U::Ms, 3, R::Decay }; p.skew = 0.3f; return p; }
      case kSlotNoiseDecay:
        return { "drum_noise_decay", "Nz Dec",    1.f, 2000.f, 200.f, false, U::Ms,        3, R::None   };
      case kSlotLevel:
        return { "drum_level",       "Level",     0.f,    1.f, 0.85f, false, U::Percent,   3, R::Level   };
      case kSlotVelSens:
        return { "drum_vel_sens",    "Vel Sens",  0.f,    1.f,  0.0f, false, U::Percent,   3, R::None    };
      case kSlotRetrig:
      {
        static constexpr const char* kRetrigLabels[] = { "LEGATO", "RETRIG", "FREE" };
        ParamSpec p { "drum_retrig", "Retrig", 0.f, 2.f, 0.f, true, U::None, 3, R::None };
        p.valueLabels = std::span<const char* const>{ kRetrigLabels, 3 };
        return p;
      }

      default: return {};
    }
  }

  SectionInfo DrumSynthMachine::section(int index) const
  {
    switch (index)
    {
      case 1: return { "SRC" };
      case 3: return { "AMP" };
      default: return {};
    }
  }

  // ---------------------------------------------------------------------------
  // Voice active

  bool DrumSynthMachine::isVoiceActive() const
  {
    return voice_.active;
  }

  // ---------------------------------------------------------------------------
  // File-scope DSP helpers

  namespace {

    float paramAt(const ParamFrame& p, int slot)
    {
      return p[static_cast<std::size_t>(slot)];
    }

    // Advance AHD amp envelope one sample; returns current level.
    float advanceAmp(DrumSynthMachine::DrumVoice& v)
    {
      using P = DrumSynthMachine::AmpPhase;
      switch (v.ampPhase)
      {
        case P::Attack:
          if (v.ampAttackSamples > 0.f)
            v.ampLevel += 1.f / v.ampAttackSamples;
          else
            v.ampLevel = 1.f;
          v.ampTimer += 1.f;
          if (v.ampLevel >= 1.f || v.ampTimer >= v.ampAttackSamples)
          {
            v.ampLevel = 1.f;
            v.ampTimer = 0.f;
            v.ampPhase = (v.ampHoldSamples > 0.f) ? P::Hold : P::Decay;
          }
          return v.ampLevel;

        case P::Hold:
          v.ampTimer += 1.f;
          if (v.ampTimer >= v.ampHoldSamples)
          {
            v.ampTimer = 0.f;
            v.ampPhase = P::Decay;
          }
          return 1.f;

        case P::Decay:
          if (v.ampDecaySamples > 0.f)
            v.ampLevel -= 1.f / v.ampDecaySamples;
          v.ampLevel = std::max(0.f, v.ampLevel);
          if (v.ampLevel <= 0.f)
          {
            v.ampPhase = P::Idle;
            v.active   = false;
          }
          return v.ampLevel;

        case P::Idle:
          return 0.f;
      }
      return 0.f;
    }

    // Advance noise decay envelope; returns current level.
    float advanceNoise(DrumSynthMachine::DrumVoice& v)
    {
      using P = DrumSynthMachine::NoisePhase;
      if (v.noisePhase != P::Decay) return 0.f;
      if (v.noiseDecaySamples > 0.f)
        v.noiseLevel -= 1.f / v.noiseDecaySamples;
      v.noiseLevel = std::max(0.f, v.noiseLevel);
      if (v.noiseLevel <= 0.f)
        v.noisePhase = P::Idle;
      return v.noiseLevel;
    }

    // Advance click/snap transient; returns current level before decay.
    float advanceClick(DrumSynthMachine::DrumVoice& v)
    {
      if (v.clickLevel <= 0.f) return 0.f;
      const float out = v.clickLevel;
      v.clickLevel -= 1.f / v.clickDecaySamples;
      v.clickLevel = std::max(0.f, v.clickLevel);
      return out;
    }

    // TPT SVF — returns bandpass output.
    float svfBand(DrumSynthMachine::DrumVoice& v, float x)
    {
      const float hp  = (x - v.svfK * v.svfBand - v.svfLow) / (1.f + v.svfG * (v.svfG + v.svfK));
      const float bp  = v.svfG * hp + v.svfBand;
      const float lp  = v.svfG * bp + v.svfLow;
      v.svfBand = 2.f * bp - v.svfBand;
      v.svfLow  = 2.f * lp - v.svfLow;
      return bp;
    }

    // TPT SVF — returns highpass output.
    float svfHigh(DrumSynthMachine::DrumVoice& v, float x)
    {
      const float hp  = (x - v.svfK * v.svfBand - v.svfLow) / (1.f + v.svfG * (v.svfG + v.svfK));
      const float bp  = v.svfG * hp + v.svfBand;
      const float lp  = v.svfG * bp + v.svfLow;
      v.svfBand = 2.f * bp - v.svfBand;
      v.svfLow  = 2.f * lp - v.svfLow;
      return hp;
    }

  } // namespace

  // ---------------------------------------------------------------------------
  // Note handling

  void DrumSynthMachine::noteOn(int midiNote, float vel, const ParamFrame& params)
  {
    const float sr = static_cast<float>(sampleRate_);
    auto& v = voice_;

    v.velocity = vel;
    v.active   = true;
    v.gateOpen = true;

    // Reset SVF state on each new note
    v.svfLow  = 0.f;
    v.svfBand = 0.f;

    const float tune       = paramAt(params, kSlotTune);
    const float sweep      = paramAt(params, kSlotSweep);
    const float sweepDecMs = paramAt(params, kSlotSweepDecay);
    const float punch      = paramAt(params, kSlotPunch);
    const float tone       = paramAt(params, kSlotTone);
    const float snap       = paramAt(params, kSlotSnap);
    const float attackMs   = paramAt(params, kSlotAttack);
    const float holdMs     = paramAt(params, kSlotHold);
    const float decayMs    = paramAt(params, kSlotDecay);
    const float noiseDecMs = paramAt(params, kSlotNoiseDecay);

    // Amp envelope
    v.ampAttackSamples = msToSamples(attackMs, sampleRate_);
    v.ampHoldSamples   = msToSamples(holdMs,   sampleRate_);
    v.ampDecaySamples  = msToSamples(decayMs,  sampleRate_);
    v.ampLevel         = 0.f;
    v.ampTimer         = 0.f;
    if (attackMs > 0.f)
      v.ampPhase = AmpPhase::Attack;
    else if (holdMs > 0.f)
      v.ampPhase = AmpPhase::Hold;
    else
      v.ampPhase = AmpPhase::Decay;

    // Noise envelope
    v.noiseDecaySamples = msToSamples(noiseDecMs, sampleRate_);
    v.noiseLevel        = 1.f;
    v.noisePhase        = NoisePhase::Decay;

    // Click transient (~3 ms)
    static constexpr float kClickMs = 3.f;
    v.clickDecaySamples = std::max(1.f, msToSamples(kClickMs, sampleRate_));
    v.clickLevel        = punch;

    // Snap transient for snare (uses the click slot with snap param level)
    // Handled per-type below.

    v.phase = 0.0;

    v.currentType = static_cast<DrumType>(
        std::clamp(static_cast<int>(std::lround(paramAt(params, kSlotType))), 0, 3));

    switch (v.currentType)
    {
      case DrumType::Kick:
      case DrumType::Tom:
      {
        v.targetHz    = noteHz(midiNote, tune);
        v.pitchHz     = noteHz(midiNote, tune + sweep);
        v.sweepSamples = std::max(1.f, msToSamples(sweepDecMs, sampleRate_));
        v.sweepTimer   = 0.f;
        v.svfG = 0.f;
        v.svfK = 0.f;
        break;
      }
      case DrumType::Snare:
      {
        v.targetHz     = noteHz(midiNote, tune);
        v.pitchHz      = v.targetHz;
        v.sweepTimer   = v.sweepSamples;  // no pitch sweep
        // Bandpass SVF for noise colouring: tone maps 500–8000 Hz
        const float noiseCutHz = 500.f + tone * 7500.f;
        v.svfG = std::tan(std::numbers::pi_v<float> * noiseCutHz / sr);
        v.svfK = 1.5f;
        // Use snap param for the click transient instead of punch
        v.clickLevel = snap;
        break;
      }
      case DrumType::Hat:
      {
        v.pitchHz = 0.f;
        // Highpass SVF: tune (−24..+24 semitones) maps 4–18 kHz
        const float normalised = (tune + 24.f) / 48.f;
        const float cutHz      = 4000.f + normalised * 14000.f;
        v.svfG = std::tan(std::numbers::pi_v<float>
                          * std::min(cutHz / sr, 0.499f));
        v.svfK = 1.f + tone * 3.f;
        // Hat has no click transient
        v.clickLevel = 0.f;
        break;
      }
    }
  }

  void DrumSynthMachine::noteOff()
  {
    voice_.gateOpen = false;
    // Hat: close the gate → very fast decay (1 ms)
    if (voice_.currentType == DrumType::Hat && voice_.active
        && voice_.ampPhase != AmpPhase::Idle)
    {
      static constexpr float kHatCloseMs = 1.f;
      voice_.ampDecaySamples = msToSamples(kHatCloseMs, sampleRate_);
      voice_.ampPhase        = AmpPhase::Decay;
    }
  }

  // ---------------------------------------------------------------------------
  // Process

  void DrumSynthMachine::process(const juce::MidiBuffer& events,
                                  const ParamFrame& params,
                                  juce::AudioBuffer<float>& buffer)
  {
    struct NoteEvent { int pos; int note; float vel; bool on; };
    juce::Array<NoteEvent> noteEvents;
    noteEvents.ensureStorageAllocated(4);

    for (const auto& meta : events)
    {
      const auto msg = meta.getMessage();
      if (msg.isNoteOn())
        noteEvents.add({ meta.samplePosition, msg.getNoteNumber(),
                         static_cast<float>(msg.getVelocity()) / 127.f, true });
      else if (msg.isNoteOff())
        noteEvents.add({ meta.samplePosition, msg.getNoteNumber(), 0.f, false });
    }

    if (!voice_.active && noteEvents.isEmpty())
      return;

    const int   numSamples = buffer.getNumSamples();
    const int   numOut     = buffer.getNumChannels();
    const float level      = paramAt(params, kSlotLevel);
    const float body       = paramAt(params, kSlotBody);
    const float velSens    = paramAt(params, kSlotVelSens);
    // velGain: lerp between full level (sens=0) and velocity-scaled level (sens=1)
    const float velGain    = 1.0f - velSens + velSens * voice_.velocity;
    const double twoPi     = 2.0 * std::numbers::pi;

    int evIdx = 0;

    for (int i = 0; i < numSamples; ++i)
    {
      // Dispatch note events at the correct sample position
      while (evIdx < noteEvents.size() && noteEvents[evIdx].pos <= i)
      {
        const auto& ev = noteEvents[evIdx];
        if (ev.on)
        {
          const int retrigMode = (params.size() > static_cast<std::size_t>(kSlotRetrig))
              ? static_cast<int>(std::round(paramAt(params, kSlotRetrig)))
              : 0;
          if (retrigMode == 2 && voice_.active)
          {
            // FREE: ignore note-on while voice is sounding
          }
          else
          {
            noteOn(ev.note, ev.vel, params);
          }
        }
        else
        {
          noteOff();
        }
        ++evIdx;
      }

      if (!voice_.active)
        continue;

      // -----------------------------------------------------------------------
      // Per-sample synthesis

      float out = 0.f;
      auto& v   = voice_;

      switch (v.currentType)
      {
        // -------------------------------------------------------------------
        case DrumType::Kick:
        case DrumType::Tom:
        {
          // Exponential pitch sweep
          float instHz;
          if (v.sweepTimer < v.sweepSamples)
          {
            const float t = v.sweepTimer / v.sweepSamples;
            // Exponential interpolation: high → low
            const float logRatio = (v.pitchHz > 0.f && v.targetHz > 0.f)
                                   ? std::log(v.pitchHz / v.targetHz) : 0.f;
            instHz = v.targetHz * std::exp(logRatio * (1.f - t));
            v.sweepTimer += 1.f;
          }
          else
          {
            instHz = v.targetHz;
          }

          v.phase += twoPi * static_cast<double>(instHz) / sampleRate_;
          if (v.phase >= twoPi) v.phase -= twoPi;

          const float osc    = static_cast<float>(std::sin(v.phase));
          const float drive  = (v.currentType == DrumType::Kick)
                               ? paramAt(params, kSlotTone)
                               : (paramAt(params, kSlotTone) * 0.25f);
          const float shaped = std::tanh(osc * (1.f + drive * 8.f));
          const float click  = xorNoise(v.noiseSeed) * advanceClick(v);
          const float amp    = advanceAmp(v);
          out = (shaped + click) * amp * velGain * level;
          break;
        }

        // -------------------------------------------------------------------
        case DrumType::Snare:
        {
          // Body sine oscillator
          v.phase += twoPi * static_cast<double>(v.targetHz) / sampleRate_;
          if (v.phase >= twoPi) v.phase -= twoPi;
          const float bodyOsc = static_cast<float>(std::sin(v.phase));

          // Noise through bandpass
          const float noise   = svfBand(v, xorNoise(v.noiseSeed)) * advanceNoise(v);

          // Snap transient
          const float snapSig = xorNoise(v.noiseSeed) * advanceClick(v);

          const float amp = advanceAmp(v);
          out = (bodyOsc * body + noise * (1.f - body) + snapSig)
                * amp * velGain * level;
          break;
        }

        // -------------------------------------------------------------------
        case DrumType::Hat:
        {
          const float filtered = svfHigh(v, xorNoise(v.noiseSeed));
          const float amp      = advanceAmp(v);
          out = filtered * amp * velGain * level;
          break;
        }
      }

      for (int ch = 0; ch < numOut; ++ch)
        buffer.addSample(ch, i, out);
    }
  }
}
