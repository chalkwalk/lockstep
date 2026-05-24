#include "VAMachine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lockstep
{
    static constexpr double kTwoPi    = 6.283185307179586476925;
    static constexpr float  kPiF      = 3.14159265358979323846f;

    // =========================================================================
    // Construction

    VAMachine::VAMachine()  = default;
    VAMachine::~VAMachine() = default;

    void VAMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        choke_.prepare(sampleRate_, 1.5f);
        reset();
    }

    void VAMachine::reset()
    {
        for (auto& sv : subVoices_) sv = SubVoice{};
        voiceCounter_ = 0;
        env_  = SharedEnv{};
        svf1_.reset();
        svf2_.reset();
        lfoPhase_ = 0.0;
        lfoOut_   = 0.0f;
        choke_.prepare(sampleRate_, 1.5f);
        hasPendingTrigger_ = false;
        lfoRandCurr_ = 0.0f;
        lfoRandNext_ = 0.0f;
        lfoRandPhase_ = 0.0;
    }

    // =========================================================================
    // Schema

    ParamSpec VAMachine::paramSpec(int index) const
    {
        using U = ParamSpec::Unit;
        using R = ParamSpec::Role;

        switch (index)
        {
        // --- SRC (section 1) ---
        case kSlotOsc1Coarse: return { "va_osc1_coarse", "Osc1 Coarse", -24.0f, 24.0f,  0.0f, true,  U::Semitones, 1, R::Pitch };
        case kSlotOsc1Fine:   return { "va_osc1_fine",   "Osc1 Fine",   -50.0f, 50.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotOsc1Wave:   return { "va_osc1_wave",   "Osc1 Wave",     0.0f,  3.0f,  0.0f, true,  U::None,      1, R::None  };
        case kSlotOsc1PW:     return { "va_osc1_pw",     "Osc1 PW",       0.0f,  1.0f,  0.5f, false, U::None,      1, R::None  };
        case kSlotOsc2Coarse: return { "va_osc2_coarse", "Osc2 Coarse", -24.0f, 24.0f,  0.0f, true,  U::Semitones, 1, R::Pitch };
        case kSlotOsc2Fine:   return { "va_osc2_fine",   "Osc2 Fine",   -50.0f, 50.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotOsc2Wave:   return { "va_osc2_wave",   "Osc2 Wave",     0.0f,  4.0f,  0.0f, true,  U::None,      1, R::None  };
        case kSlotOsc2PW:     return { "va_osc2_pw",     "Osc2 PW",       0.0f,  1.0f,  0.5f, false, U::None,      1, R::None  };
        case kSlotSub:        return { "va_sub",          "Sub",           0.0f,  1.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotNoise:      return { "va_noise",        "Noise",         0.0f,  1.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotPorta:      return { "va_porta",        "Portamento",    0.0f,500.0f,  0.0f, false, U::Ms,        1, R::None  };
        case kSlotVoiceMode:  return { "va_voice_mode",   "Voice Mode",    0.0f,  1.0f,  0.0f, true,  U::None,      1, R::None  };

        // --- FLTR (section 2) ---
        case kSlotCutoff:     return { "va_cutoff",      "Cutoff",        0.0f,  1.0f,  1.0f, false, U::None,      2, R::Cutoff    };
        case kSlotRes:        return { "va_res",          "Resonance",     0.0f,  1.0f,  0.0f, false, U::None,      2, R::Resonance };
        case kSlotFilterType: return { "va_filter_type",  "Filter",        0.0f,  3.0f,  0.0f, true,  U::None,      2, R::None      };
        case kSlotDrive:      return { "va_drive",        "Drive",         0.0f,  1.0f,  0.0f, false, U::None,      2, R::Drive     };
        case kSlotFEnvDepth:  return { "va_fenv_depth",   "Env Depth",    -1.0f,  1.0f,  0.0f, false, U::None,      2, R::None      };
        case kSlotFEnvA:      return { "va_fenv_a",       "F Atk",         1.0f,5000.0f, 1.0f, false, U::Ms,        2, R::None      };
        case kSlotFEnvD:      return { "va_fenv_d",       "F Dec",         1.0f,5000.0f,100.0f,false, U::Ms,        2, R::None      };
        case kSlotFEnvS:      return { "va_fenv_s",       "F Sus",         0.0f,  1.0f,  0.0f, false, U::None,      2, R::None      };
        case kSlotFEnvR:      return { "va_fenv_r",       "F Rel",         1.0f,5000.0f,100.0f,false, U::Ms,        2, R::None      };

        // --- AMP (section 3) ---
        case kSlotAmpA:       return { "va_amp_a",        "Attack",        1.0f,5000.0f,  1.0f, false, U::Ms,       3, R::Attack  };
        case kSlotAmpD:       return { "va_amp_d",        "Decay",         1.0f,5000.0f,100.0f, false, U::Ms,       3, R::Decay   };
        case kSlotAmpS:       return { "va_amp_s",        "Sustain",       0.0f,  1.0f,  0.8f,  false, U::None,     3, R::Sustain };
        case kSlotAmpR:       return { "va_amp_r",        "Release",       1.0f,5000.0f,500.0f, false, U::Ms,       3, R::Release };
        case kSlotLevel:      return { "va_level",        "Level",         0.0f,  1.0f,  0.8f,  false, U::None,     3, R::Level   };
        case kSlotPan:        return { "va_pan",          "Pan",          -1.0f,  1.0f,  0.0f,  false, U::None,     3, R::Pan     };

        // --- LFO (section 4) ---
        case kSlotLfoRate:    return { "va_lfo_rate",     "LFO Rate",    0.01f, 40.0f,  3.0f, false, U::None,      4, R::LfoRate  };
        case kSlotLfoDepth:   return { "va_lfo_depth",    "LFO Depth",   0.0f,   1.0f,  0.0f, false, U::None,      4, R::LfoDepth };
        case kSlotLfoShape:   return { "va_lfo_shape",    "LFO Shape",   0.0f,   5.0f,  0.0f, true,  U::None,      4, R::LfoShape };
        case kSlotLfoTarget:  return { "va_lfo_target",   "LFO Target",  0.0f,   3.0f,  0.0f, true,  U::None,      4, R::None     };
        case kSlotLfoSync:    return { "va_lfo_sync",     "LFO Sync",    0.0f,   1.0f,  0.0f, true,  U::None,      4, R::None     };

        default: return {};
        }
    }

    SectionInfo VAMachine::section(int index) const
    {
        switch (index)
        {
        case 1: return { "SRC"  };
        case 2: return { "FLTR" };
        case 3: return { "AMP"  };
        case 4: return { "LFO"  };
        default: return {};
        }
    }

    // =========================================================================
    // isVoiceActive

    bool VAMachine::isVoiceActive() const
    {
        if (choke_.isFading() || hasPendingTrigger_) return true;
        for (const auto& sv : subVoices_)
            if (sv.active || sv.fadeRemain > 0) return true;
        return env_.aStage != Stage::Idle;
    }

    // =========================================================================
    // DSP helpers

    float VAMachine::polyBlep(double t, double dt) noexcept
    {
        if (t < dt)
        {
            const auto x = static_cast<float>(t / dt);
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0 - dt)
        {
            const auto x = static_cast<float>((t - 1.0) / dt);
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    float VAMachine::oscillatorSample(SubVoice& sv, int osc1Wave, float osc1PW,
                                      int osc2Wave, float osc2PW,
                                      float subLevel, float noiseLevel,
                                      double osc2FreqRatio) noexcept
    {
        const double osc1Inc = sv.currentFreq / sampleRate_;
        const double osc2Inc = sv.currentFreq * osc2FreqRatio / sampleRate_;
        const double subInc  = sv.currentFreq * 0.5 / sampleRate_;

        float out = 0.0f;

        // Osc 1
        {
            const double ph = sv.osc1Phase;
            const double inc = osc1Inc;
            float s = 0.0f;
            switch (osc1Wave)
            {
            case 0: // Saw
                s = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, inc);
                break;
            case 1: // Pulse
            {
                const auto pw = static_cast<double>(std::clamp(osc1PW, 0.05f, 0.95f));
                s = ph < pw ? 1.0f : -1.0f;
                s -= polyBlep(ph, inc);
                s += polyBlep(std::fmod(ph - pw + 1.0, 1.0), inc);
                break;
            }
            case 2: // Triangle
                s = static_cast<float>(4.0 * std::abs(ph - 0.5) - 1.0);
                break;
            case 3: // Sine
                s = static_cast<float>(std::sin(kTwoPi * ph));
                break;
            default: break;
            }
            out += s;
            sv.osc1Phase += inc;
            if (sv.osc1Phase >= 1.0) sv.osc1Phase -= 1.0;
        }

        // Osc 2 (0 = Off)
        if (osc2Wave > 0)
        {
            const double ph = sv.osc2Phase;
            const double inc = osc2Inc;
            float s = 0.0f;
            switch (osc2Wave)
            {
            case 1: // Saw
                s = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, inc);
                break;
            case 2: // Pulse
            {
                const auto pw = static_cast<double>(std::clamp(osc2PW, 0.05f, 0.95f));
                s = ph < pw ? 1.0f : -1.0f;
                s -= polyBlep(ph, inc);
                s += polyBlep(std::fmod(ph - pw + 1.0, 1.0), inc);
                break;
            }
            case 3: // Triangle
                s = static_cast<float>(4.0 * std::abs(ph - 0.5) - 1.0);
                break;
            case 4: // Sine
                s = static_cast<float>(std::sin(kTwoPi * ph));
                break;
            default: break;
            }
            out += s;
            sv.osc2Phase += inc;
            if (sv.osc2Phase >= 1.0) sv.osc2Phase -= 1.0;
        }

        // Sub (one octave below osc1, sawtooth)
        if (subLevel > 0.0f)
        {
            const double ph = sv.subPhase;
            const float subSample = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, subInc);
            out += subSample * subLevel;
            sv.subPhase += subInc;
            if (sv.subPhase >= 1.0) sv.subPhase -= 1.0;
        }

        // Noise
        if (noiseLevel > 0.0f)
        {
            const float noise = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
            out += noise * noiseLevel;
        }

        return out;
    }

    float VAMachine::filterSample(float in, float f, float q, int filterType) noexcept
    {
        // SVF update helper — inline lambda-free for performance
        auto runSVF = [](SVFState& s, float x, float fc, float res) -> std::tuple<float,float,float>
        {
            s.hp = x - res * s.bp - s.lp;
            s.bp += fc * s.hp;
            s.lp += fc * s.bp;
            return { s.lp, s.hp, s.bp };
        };

        auto [lp1, hp1, bp1] = runSVF(svf1_, in, f, q);

        switch (filterType)
        {
        case 0: // LP4: cascade
        {
            auto [lp2, hp2, bp2] = runSVF(svf2_, lp1, f, q);
            return lp2;
        }
        case 1: // LP2
            return lp1;
        case 2: // HP
            return hp1;
        case 3: // BP
            return bp1;
        default:
            return lp1;
        }
    }

    float VAMachine::advanceEnvLevel(Stage& stage, float& level, float& /*releaseStart*/,
                                      int& remain, float decayMul, float relMul,
                                      float sustain) noexcept
    {
        switch (stage)
        {
        case Stage::Idle: return 0.0f;
        case Stage::Attack:
            if (remain > 0) { level += 1.0f / static_cast<float>(remain + 1); --remain; }
            level = std::min(level, 1.0f);
            if (remain <= 0) { stage = Stage::Decay; }
            return level;
        case Stage::Decay:
            level *= decayMul;
            level = std::max(level, sustain);
            if (std::abs(level - sustain) < 0.0001f) { level = sustain; stage = Stage::Sustain; }
            return level;
        case Stage::Sustain:
            return sustain;
        case Stage::Release:
            level *= relMul;
            if (level < 0.0001f) { level = 0.0f; stage = Stage::Idle; }
            return level;
        }
        return 0.0f;
    }

    // =========================================================================
    // Envelope helpers

    void VAMachine::triggerEnvelopes(const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        // Filter envelope
        env_.fStage   = Stage::Attack;
        env_.fLevel   = 0.0f;
        env_.fSustain = p(kSlotFEnvS);
        env_.fRemain  = msToSamples(p(kSlotFEnvA), sampleRate_);
        {
            const int ds = msToSamples(p(kSlotFEnvD), sampleRate_);
            env_.fDecayMul = std::exp(-1.0f / static_cast<float>(ds));
        }
        {
            const int rs = msToSamples(p(kSlotFEnvR), sampleRate_);
            env_.fRelMul = std::exp(-1.0f / static_cast<float>(rs));
        }

        // Amp envelope
        env_.aStage   = Stage::Attack;
        env_.aLevel   = 0.0f;
        env_.aSustain = p(kSlotAmpS);
        env_.aRemain  = msToSamples(p(kSlotAmpA), sampleRate_);
        {
            const int ds = msToSamples(p(kSlotAmpD), sampleRate_);
            env_.aDecayMul = std::exp(-1.0f / static_cast<float>(ds));
        }
        {
            const int rs = msToSamples(p(kSlotAmpR), sampleRate_);
            env_.aRelMul = std::exp(-1.0f / static_cast<float>(rs));
        }
    }

    void VAMachine::releaseEnvelopes()
    {
        if (env_.fStage != Stage::Idle)
        {
            env_.fStage        = Stage::Release;
            env_.fReleaseStart = env_.fLevel;
        }
        if (env_.aStage != Stage::Idle)
        {
            env_.aStage        = Stage::Release;
            env_.aReleaseStart = env_.aLevel;
        }
    }

    // =========================================================================
    // Mono voice

    void VAMachine::startMonoVoice(int midiNote, const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        auto& sv = subVoices_[0];
        const double targetHz = midiNoteToHz(midiNote);
        const float portaMs   = p(kSlotPorta);

        if (portaMs <= 0.0f || !sv.active)
        {
            sv.currentFreq = targetHz;
            sv.osc1Phase   = 0.0;
            sv.osc2Phase   = 0.0;
            sv.subPhase    = 0.0;
        }
        sv.targetFreq  = targetHz;
        sv.active      = true;
        sv.midiNote    = midiNote;
        sv.microAmp    = 1.0f;
        sv.fadeRemain  = 0;

        svf1_.reset();
        svf2_.reset();

        triggerEnvelopes(params);

        // LFO key sync
        if (p(kSlotLfoSync) >= 0.5f)
            lfoPhase_ = 0.0;
    }

    void VAMachine::releaseMonoVoice()
    {
        // Keep subVoices_[0].active true so the oscillator keeps producing samples
        // through the amp envelope's Release stage; the voice is deactivated only
        // once the envelope finishes (see the Idle transition in the process loop).
        releaseEnvelopes();
    }

    // =========================================================================
    // Para voice

    int VAMachine::allocSubVoice()
    {
        // First: inactive voice.
        for (int i = 0; i < kMaxSubVoices; ++i)
            if (!subVoices_[static_cast<std::size_t>(i)].active
                && subVoices_[static_cast<std::size_t>(i)].fadeRemain == 0)
                return i;
        // All active: steal oldest (lowest age counter).
        int oldest = 0;
        for (int i = 1; i < kMaxSubVoices; ++i)
            if (subVoices_[static_cast<std::size_t>(i)].age
                < subVoices_[static_cast<std::size_t>(oldest)].age)
                oldest = i;
        return oldest;
    }

    void VAMachine::startParaVoice(int midiNote, const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const int idx = allocSubVoice();
        auto& sv = subVoices_[static_cast<std::size_t>(idx)];

        const double targetHz = midiNoteToHz(midiNote);
        const float portaMs   = p(kSlotPorta);

        if (portaMs <= 0.0f || !sv.active)
            sv.currentFreq = targetHz;
        sv.targetFreq  = targetHz;
        sv.active      = true;
        sv.midiNote    = midiNote;
        sv.age         = ++voiceCounter_;
        sv.microAmp    = 1.0f;
        sv.fadeRemain  = 0;
        sv.osc1Phase   = 0.0;
        sv.osc2Phase   = 0.0;
        sv.subPhase    = 0.0;

        // Trigger envelopes only if this is the first active voice.
        bool anyOtherActive = false;
        for (int i = 0; i < kMaxSubVoices; ++i)
            if (i != idx && (subVoices_[static_cast<std::size_t>(i)].active
                             || subVoices_[static_cast<std::size_t>(i)].fadeRemain > 0))
                { anyOtherActive = true; break; }

        if (!anyOtherActive)
        {
            svf1_.reset();
            svf2_.reset();
            triggerEnvelopes(params);
        }

        if (p(kSlotLfoSync) >= 0.5f && !anyOtherActive)
            lfoPhase_ = 0.0;
    }

    void VAMachine::releaseParaVoice(int midiNote)
    {
        // Start micro-fade on matching voice(s).
        const int fadeSamples = std::max(1, static_cast<int>(0.002 * sampleRate_));
        for (auto& sv : subVoices_)
        {
            if (sv.active && sv.midiNote == midiNote)
            {
                sv.active     = false;
                sv.fadeRemain = fadeSamples;
            }
        }

        // Release envelopes when all voices are done/fading.
        const bool anyActive = std::any_of(subVoices_.begin(), subVoices_.end(),
                                            [](const SubVoice& s) { return s.active; });
        if (!anyActive)
            releaseEnvelopes();
    }

    // =========================================================================
    // process()

    void VAMachine::process(const juce::MidiBuffer& events,
                             const ParamFrame& params,
                             juce::AudioBuffer<float>& buffer)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const bool paraMode = (p(kSlotVoiceMode) >= 0.5f);
        const int numSamples = buffer.getNumSamples();
        const int numOut     = buffer.getNumChannels();

        // ---- Scan events -----------------------------------------------
        // Collect all note-ons and note-offs with their sample positions.
        struct NoteEvent { int samplePos; int note; bool on; };
        juce::Array<NoteEvent> noteEvents;
        noteEvents.ensureStorageAllocated(8);
        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                noteEvents.add({ meta.samplePosition, msg.getNoteNumber(), true });
            else if (msg.isNoteOff())
                noteEvents.add({ meta.samplePosition, msg.getNoteNumber(), false });
        }

        // Early exit
        if (!isVoiceActive() && noteEvents.isEmpty()) return;

        // ---- LFO (per-block update) ------------------------------------
        const float lfoRate  = p(kSlotLfoRate);
        const float lfoDepth = p(kSlotLfoDepth);
        const int   lfoShape = static_cast<int>(p(kSlotLfoShape));
        {
            const double lfoInc = static_cast<double>(lfoRate) / sampleRate_
                                  * static_cast<double>(numSamples);
            const double prevPhase = lfoPhase_;
            lfoPhase_ += lfoInc;
            while (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;

            float raw = 0.0f;
            switch (lfoShape)
            {
            case 0: raw = static_cast<float>(std::sin(kTwoPi * lfoPhase_)); break;
            case 1: raw = static_cast<float>(4.0 * std::abs(lfoPhase_ - 0.5) - 1.0); break;
            case 2: raw = static_cast<float>(2.0 * lfoPhase_ - 1.0); break;  // Saw
            case 3: raw = static_cast<float>(1.0 - 2.0 * lfoPhase_); break;  // RSaw
            case 4: raw = (lfoPhase_ < 0.5) ? 1.0f : -1.0f; break;           // Pulse
            case 5: // Random (S&H): advance on phase reset
            {
                if (lfoPhase_ < prevPhase)  // wrap
                {
                    lfoRandCurr_ = lfoRandNext_;
                    lfoRandNext_ = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
                }
                raw = lfoRandCurr_;
                break;
            }
            default: break;
            }
            lfoOut_ = raw * lfoDepth;
        }

        const int   lfoTarget = static_cast<int>(p(kSlotLfoTarget));
        const float lfoCutoffMod = (lfoTarget == 0) ? lfoOut_ * 0.5f : 0.0f;
        const float lfoPitchMod  = (lfoTarget == 1) ? lfoOut_         : 0.0f;  // in semitones
        const float lfoPWMod     = (lfoTarget == 2) ? lfoOut_ * 0.2f  : 0.0f;
        const float lfoAmpMod    = (lfoTarget == 3) ? lfoOut_ * 0.5f  : 0.0f;

        // ---- Params -------------------------------------------------------
        const float cutoffParam   = std::clamp(p(kSlotCutoff) + lfoCutoffMod, 0.0f, 1.0f);
        const int   filterType    = static_cast<int>(p(kSlotFilterType));
        const float driveGain     = 1.0f + 4.0f * p(kSlotDrive);
        const float fEnvDepth     = p(kSlotFEnvDepth);
        const float subLevel      = p(kSlotSub);
        const float noiseLevel    = p(kSlotNoise);
        const float portaMs       = p(kSlotPorta);
        const int   osc1Wave      = static_cast<int>(p(kSlotOsc1Wave));
        const float osc1PW        = std::clamp(p(kSlotOsc1PW) + lfoPWMod, 0.05f, 0.95f);
        const int   osc2Wave      = static_cast<int>(p(kSlotOsc2Wave));
        const float osc2PW        = std::clamp(p(kSlotOsc2PW) + lfoPWMod, 0.05f, 0.95f);
        const float outputLevel   = p(kSlotLevel) * (1.0f + lfoAmpMod);
        const float pan           = std::clamp(p(kSlotPan), -1.0f, 1.0f);

        // Osc2 frequency ratio from coarse + fine params.
        const float osc2CoarseST = p(kSlotOsc2Coarse);
        const float osc2FineCent  = p(kSlotOsc2Fine);
        const double osc2FreqRatio = std::pow(2.0, static_cast<double>(osc2CoarseST) / 12.0
                                               + static_cast<double>(osc2FineCent) / 1200.0);

        // Osc1 coarse/fine pitch offset in Hz ratio.
        const float osc1CoarseST  = p(kSlotOsc1Coarse);
        const float osc1FineCent  = p(kSlotOsc1Fine);
        const double osc1FreqMul  = std::pow(2.0, static_cast<double>(osc1CoarseST) / 12.0
                                              + static_cast<double>(osc1FineCent) / 1200.0
                                              + static_cast<double>(lfoPitchMod)   / 12.0);

        // Portamento coefficient (one-pole IIR).
        const double portaCoeff = (portaMs > 0.0f)
            ? std::exp(-1.0 / (static_cast<double>(portaMs) * 0.001 * sampleRate_))
            : 0.0;

        // Resonance → SVF damping coefficient.
        // In a SVF, this is the *inverse* of the resonance Q-factor:
        //   damping=1.414 → Butterworth (no resonance peak, default)
        //   damping→0     → self-oscillation
        // So the knob must map res_param=0 → high damping and res_param=1 → low damping.
        const float svfQ = std::max(0.01f, (1.0f - p(kSlotRes)) * 1.4f);

        // ---- Per-sample synthesis loop ------------------------------------
        int eventIdx = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            // Handle events at this sample position.
            while (eventIdx < noteEvents.size()
                   && noteEvents[eventIdx].samplePos <= i)
            {
                const auto& ev = noteEvents[eventIdx];
                if (ev.on)
                {
                    if (paraMode)
                    {
                        startParaVoice(ev.note, params);
                    }
                    else
                    {
                        // Mono: if voice active, choke and defer.
                        if (subVoices_[0].active || choke_.isFading())
                        {
                            if (!choke_.isFading()) choke_.trigger();
                            hasPendingTrigger_ = true;
                            pendingNote_       = ev.note;
                            pendingParams_     = params;
                        }
                        else
                        {
                            startMonoVoice(ev.note, params);
                        }
                    }
                }
                else
                {
                    if (paraMode)
                        releaseParaVoice(ev.note);
                    else if (subVoices_[0].midiNote == ev.note)
                        releaseMonoVoice();
                }
                ++eventIdx;
            }

            // ---- Advance filter and amp envelopes ----
            const float fEnvLevel = advanceEnvLevel(env_.fStage, env_.fLevel,
                                                     env_.fReleaseStart, env_.fRemain,
                                                     env_.fDecayMul, env_.fRelMul,
                                                     env_.fSustain);
            const float aEnvLevel = advanceEnvLevel(env_.aStage, env_.aLevel,
                                                     env_.aReleaseStart, env_.aRemain,
                                                     env_.aDecayMul, env_.aRelMul,
                                                     env_.aSustain);

            // Amp envelope reached Idle (Release completed): mono voice is now
            // silent and can be reused by the next note-on without a choke.
            if (!paraMode && env_.aStage == Stage::Idle)
                subVoices_[0].active = false;

            if (env_.aStage == Stage::Idle && !hasPendingTrigger_ && !choke_.isFading())
            {
                // Check if any voice is still fading.
                bool anyFade = false;
                for (const auto& sv : subVoices_)
                    if (sv.fadeRemain > 0) { anyFade = true; break; }
                if (!anyFade) continue;  // silence — skip this sample
            }

            // ---- Effective cutoff ----
            const float effectiveCutoff = std::clamp(cutoffParam + fEnvLevel * fEnvDepth,
                                                      0.0f, 1.0f);
            // Map [0,1] → [20, 18000] Hz exponentially.
            const float cutoffHz = 20.0f * std::pow(900.0f, effectiveCutoff);
            const float svfF = std::clamp(
                2.0f * std::sin(kPiF * cutoffHz / static_cast<float>(sampleRate_)),
                0.001f, 0.99f);

            // ---- Sum oscillators from all active sub-voices ----
            float oscSum = 0.0f;
            for (auto& sv : subVoices_)
            {
                if (!sv.active && sv.fadeRemain == 0) continue;

                // Portamento: slide currentFreq toward targetFreq.
                if (portaCoeff > 0.0)
                    sv.currentFreq = portaCoeff * sv.currentFreq
                                     + (1.0 - portaCoeff) * sv.targetFreq;
                else
                    sv.currentFreq = sv.targetFreq;

                // Apply osc1 coarse/fine.
                const double savedFreq = sv.currentFreq;
                sv.currentFreq *= osc1FreqMul;

                float svSample = oscillatorSample(sv, osc1Wave, osc1PW,
                                                   osc2Wave, osc2PW,
                                                   subLevel, noiseLevel,
                                                   osc2FreqRatio);
                sv.currentFreq = savedFreq;

                // Per-voice micro-amp (note-off fade).
                if (sv.fadeRemain > 0)
                {
                    sv.microAmp *= 1.0f - (1.0f / static_cast<float>(sv.fadeRemain + 1));
                    --sv.fadeRemain;
                }
                oscSum += svSample * sv.microAmp;
            }

            // ---- Drive ----
            const float driven = std::tanh(driveGain * oscSum);

            // ---- Filter ----
            float filtered = filterSample(driven, svfF, svfQ, filterType);

            // ---- Mono choke gain ----
            const float chokeGain = (!paraMode && choke_.isFading()) ? choke_.nextGain() : 1.0f;
            // After advancing choke, start pending voice if choke just ended.
            if (!paraMode && !choke_.isFading() && hasPendingTrigger_)
            {
                startMonoVoice(pendingNote_, pendingParams_);
                hasPendingTrigger_ = false;
            }
            float finalGain = aEnvLevel * outputLevel * chokeGain;

            // ---- Output ----
            const float outSample = filtered * finalGain;
            const float gainL = (numOut >= 2) ? std::sqrt(std::max(0.0f, 1.0f - pan) * 0.5f + 0.5f) : 1.0f;
            const float gainR = (numOut >= 2) ? std::sqrt(std::max(0.0f, 1.0f + pan) * 0.5f + 0.5f) : 1.0f;

            if (numOut >= 2)
            {
                buffer.addSample(0, i, outSample * gainL);
                buffer.addSample(1, i, outSample * gainR);
            }
            else if (numOut == 1)
            {
                buffer.addSample(0, i, outSample);
            }
        }
    }

} // namespace lockstep
