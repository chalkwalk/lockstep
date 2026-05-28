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
        reset();
        // Re-prepare envelope objects after reset() zeroed their sr_ fields.
        ampEnv_.prepare(sampleRate);
        filterEnv_.prepare(sampleRate);
        for (auto& sv : subVoices_)
            sv.ar.prepare(sampleRate);
    }

    void VAMachine::reset()
    {
        for (auto& sv : subVoices_) sv = SubVoice{};
        voiceCounter_     = 0;
        paraChordNoteIdx_ = 0;
        ampEnv_.reset();
        filterEnv_.reset();
        svf1_.reset();
        svf2_.reset();
        lfoPhase_      = 0.0;
        lfoOut_        = 0.0f;
        noiseState_    = 0.0f;
        monoGhostGain_ = 0.0f;
        monoGhostFade_ = 0;
        monoGate_.reset();
        lfoRandCurr_   = 0.0f;
        lfoRandNext_   = 0.0f;
        lfoRandPhase_  = 0.0;
    }

    // =========================================================================
    // Schema

    ParamSpec VAMachine::paramSpec(int index) const
    {
        using U = ParamSpec::Unit;
        using R = ParamSpec::Role;

        static constexpr const char* kVoiceModeLabels[]  = { "MONO", "PARA" };
        static constexpr const char* kFilterTypeLabels[]  = { "LP24", "LP12", "HP", "BP" };
        static constexpr const char* kOscWaveLabels[]     = { "SAW", "TRI", "SQR", "SIN" };
        static constexpr const char* kOsc2WaveLabels[]    = { "SAW", "TRI", "SQR", "SIN", "OFF" };
        static constexpr const char* kLfoShapeLabels[]    = { "SIN", "TRI", "SAW", "SQR", "S&H", "RND" };
        static constexpr const char* kLfoTargetLabels[]   = { "CUT", "PITCH", "PW", "AMP" };
        static constexpr const char* kLfoSyncLabels[]     = { "FREE", "SYNC" };

        switch (index)
        {
        // --- SRC (section 1) ---
        case kSlotOsc1Coarse: return { "va_osc1_coarse", "Osc1 Coarse", -24.0f, 24.0f,  0.0f, true,  U::Semitones, 1, R::Pitch };
        case kSlotOsc1Fine:   return { "va_osc1_fine",   "Osc1 Fine",   -50.0f, 50.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotOsc1Wave:   { ParamSpec p { "va_osc1_wave",   "Osc1 Wave",     0.0f,  3.0f,  0.0f, true,  U::None,      1, R::None  }; p.valueLabels = std::span<const char* const>(kOscWaveLabels); return p; }
        case kSlotOsc1PW:     return { "va_osc1_pw",     "Osc1 PW",       0.0f,  1.0f,  0.5f, false, U::None,      1, R::None  };
        case kSlotOsc2Coarse: return { "va_osc2_coarse", "Osc2 Coarse", -24.0f, 24.0f,  0.0f, true,  U::Semitones, 1, R::Pitch };
        case kSlotOsc2Fine:   return { "va_osc2_fine",   "Osc2 Fine",   -50.0f, 50.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotOsc2Wave:   { ParamSpec p { "va_osc2_wave",   "Osc2 Wave",     0.0f,  4.0f,  0.0f, true,  U::None,      1, R::None  }; p.valueLabels = std::span<const char* const>(kOsc2WaveLabels); return p; }
        case kSlotOsc2PW:     return { "va_osc2_pw",     "Osc2 PW",       0.0f,  1.0f,  0.5f, false, U::None,      1, R::None  };
        case kSlotSub:        return { "va_sub",          "Sub",           0.0f,  1.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotNoise:      return { "va_noise",        "Noise",         0.0f,  1.0f,  0.0f, false, U::None,      1, R::None  };
        case kSlotPorta:      return { "va_porta",        "Portamento",    0.0f,500.0f,  0.0f, false, U::Ms,        1, R::None  };
        case kSlotVoiceMode:  { ParamSpec p { "va_voice_mode",   "Voice Mode",    0.0f,  1.0f,  0.0f, true,  U::None,      1, R::None  }; p.valueLabels = std::span<const char* const>(kVoiceModeLabels);  return p; }

        // --- FLTR (section 2) ---
        case kSlotCutoff:     return { "va_cutoff",      "Cutoff",        0.0f,  1.0f,  1.0f, false, U::None,      2, R::Cutoff    };
        case kSlotRes:        return { "va_res",          "Resonance",     0.0f,  1.0f,  0.0f, false, U::None,      2, R::Resonance };
        case kSlotFilterType: { ParamSpec p { "va_filter_type",  "Filter",        0.0f,  3.0f,  0.0f, true,  U::None,      2, R::None  }; p.valueLabels = std::span<const char* const>(kFilterTypeLabels); return p; }
        case kSlotDrive:      return { "va_drive",        "Drive",         0.0f,  1.0f,  0.0f, false, U::None,      2, R::Drive     };
        case kSlotFEnvDepth:  return { "va_fenv_depth",   "Env Depth",    -1.0f,  1.0f,  0.0f, false, U::None,      2, R::None      };
        case kSlotFEnvA:      { ParamSpec p { "va_fenv_a", "F Atk",  0.0f, 5000.0f,   1.0f, false, U::Ms, 2, R::None }; p.skew = 0.3f; return p; }
        case kSlotFEnvD:      { ParamSpec p { "va_fenv_d", "F Dec",  1.0f,10000.0f, 100.0f, false, U::Ms, 2, R::None }; p.skew = 0.3f; return p; }
        case kSlotFEnvS:      return { "va_fenv_s",       "F Sus",         0.0f,  1.0f,  0.0f, false, U::None,      2, R::None      };
        case kSlotFEnvR:      { ParamSpec p { "va_fenv_r", "F Rel",  1.0f,10000.0f, 100.0f, false, U::Ms, 2, R::None }; p.skew = 0.3f; return p; }

        // --- AMP (section 3) ---
        case kSlotAmpA:       { ParamSpec p { "va_amp_a", "Attack",  0.0f, 5000.0f,   1.0f, false, U::Ms, 3, R::Attack  }; p.skew = 0.3f; return p; }
        case kSlotAmpD:       { ParamSpec p { "va_amp_d", "Decay",   1.0f,10000.0f, 100.0f, false, U::Ms, 3, R::Decay   }; p.skew = 0.3f; return p; }
        case kSlotAmpS:       return { "va_amp_s",        "Sustain",       0.0f,  1.0f,  0.8f,  false, U::None,     3, R::Sustain };
        case kSlotAmpR:       { ParamSpec p { "va_amp_r", "Release", 1.0f,10000.0f, 500.0f, false, U::Ms, 3, R::Release }; p.skew = 0.3f; return p; }
        case kSlotLevel:      return { "va_level",        "Level",         0.0f,  1.0f,  0.8f,  false, U::None,     3, R::Level   };
        case kSlotPan:        return { "va_pan",          "Pan",          -1.0f,  1.0f,  0.0f,  false, U::None,     3, R::Pan     };
        case kSlotRetrig:
        {
          static constexpr const char* kRetrigLabels[] = { "LEGATO", "RETRIG" };
          ParamSpec p { "va_retrig", "Retrig", 0.0f, 1.0f, 0.0f, true, U::None, 3, R::None };
          p.valueLabels = std::span<const char* const>(kRetrigLabels);
          return p;
        }
        case kSlotVelSens:
          return { "va_vel_sens", "Vel Sens", 0.0f, 1.0f, 0.0f, false, U::Percent, 3, R::None };

        // --- LFO (section 4) ---
        case kSlotLfoRate:    return { "va_lfo_rate",     "LFO Rate",    0.01f, 40.0f,  3.0f, false, U::None,      4, R::LfoRate  };
        case kSlotLfoDepth:   return { "va_lfo_depth",    "LFO Depth",   0.0f,   1.0f,  0.0f, false, U::None,      4, R::LfoDepth };
        case kSlotLfoShape:   { ParamSpec p { "va_lfo_shape",    "LFO Shape",   0.0f,   5.0f,  0.0f, true,  U::None,      4, R::LfoShape }; p.valueLabels = std::span<const char* const>(kLfoShapeLabels);  return p; }
        case kSlotLfoTarget:  { ParamSpec p { "va_lfo_target",   "LFO Target",  0.0f,   3.0f,  0.0f, true,  U::None,      4, R::None     }; p.valueLabels = std::span<const char* const>(kLfoTargetLabels); return p; }
        case kSlotLfoSync:    { ParamSpec p { "va_lfo_sync",     "LFO Sync",    0.0f,   1.0f,  0.0f, true,  U::None,      4, R::None     }; p.valueLabels = std::span<const char* const>(kLfoSyncLabels);   return p; }
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
    // currentVoices

    IMachine::Polyphony VAMachine::currentVoices(const ParamFrame& baseParams) const
    {
        if (static_cast<int>(baseParams.size()) > kSlotVoiceMode
            && baseParams[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f)
            return Polyphony::V4;
        return Polyphony::V1;
    }

    // =========================================================================
    // isVoiceActive

    bool VAMachine::isVoiceActive() const
    {
        if (monoGhostFade_ > 0) return true;
        for (const auto& sv : subVoices_)
        {
            if (sv.active || sv.ar.isActive() || sv.keepForRelease) return true;
        }
        return ampEnv_.isActive();
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
                                      float subLevel,
                                      double osc2FreqRatio,
                                      bool paraMode) noexcept
    {
        // In para mode: sv.oscType 0 → render osc1+sub only; sv.oscType 1 → render osc2+sub only.
        // In mono mode (paraMode=false): render both osc1 and osc2.
        const bool renderOsc1 = !paraMode || sv.oscType == 0;
        const bool renderOsc2 = !paraMode || sv.oscType == 1;

        const double osc1Inc = sv.currentFreq / sampleRate_;
        const double osc2Inc = sv.currentFreq * osc2FreqRatio / sampleRate_;
        const double subInc  = sv.currentFreq * 0.5 / sampleRate_;

        float out = 0.0f;

        // Osc 1 (advance phase even when not rendering to stay in sync for mode switches)
        {
            const double ph = sv.osc1Phase;
            const double inc = osc1Inc;
            if (renderOsc1)
            {
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
            }
            sv.osc1Phase += inc;
            if (sv.osc1Phase >= 1.0) sv.osc1Phase -= 1.0;
        }

        // Osc 2 (0 = Off in mono mode; in para mode it's the osc2-type voice's primary osc)
        const bool osc2Active = paraMode ? (renderOsc2) : (osc2Wave > 0);
        if (osc2Active)
        {
            const double ph = sv.osc2Phase;
            const double inc = osc2Inc;
            const int wave   = paraMode ? std::max(1, osc2Wave) : osc2Wave;  // para: use saw if OFF
            float s = 0.0f;
            switch (wave)
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
        else
        {
            // Advance osc2 phase even when silent to avoid a jump on unmute.
            sv.osc2Phase += osc2Inc;
            if (sv.osc2Phase >= 1.0) sv.osc2Phase -= 1.0;
        }

        // Sub (one octave below the primary osc)
        if (subLevel > 0.0f)
        {
            const double ph = sv.subPhase;
            const float subSample = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, subInc);
            out += subSample * subLevel;
            sv.subPhase += subInc;
            if (sv.subPhase >= 1.0) sv.subPhase -= 1.0;
        }

        return out;
    }

    float VAMachine::filterSample(float in, float f, float q, int filterType) noexcept
    {
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

    // =========================================================================
    // Envelope helpers

    void VAMachine::triggerEnvs(const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
        ampEnv_.setADSR(p(kSlotAmpA), p(kSlotAmpD), p(kSlotAmpS), p(kSlotAmpR));
        ampEnv_.gateOn();
        filterEnv_.setADSR(p(kSlotFEnvA), p(kSlotFEnvD), p(kSlotFEnvS), p(kSlotFEnvR));
        filterEnv_.gateOn();
    }

    void VAMachine::releaseEnvs()
    {
        ampEnv_.gateOff();
        filterEnv_.gateOff();
    }

    // =========================================================================
    // Mono voice

    void VAMachine::startMonoVoice(int midiNote, const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        auto& sv = subVoices_[0];
        const double targetHz = midiNoteToHz(midiNote);
        const float portaMs   = p(kSlotPorta);

        // Reset phases only when coming from a fully idle voice; if the voice is
        // still in Release, preserve phases so the re-attack is click-free.
        if (!sv.active)
        {
            sv.osc1Phase   = 0.0;
            sv.osc2Phase   = 0.0;
            sv.subPhase    = 0.0;
        }
        if (!sv.active || portaMs <= 0.0f)
            sv.currentFreq = targetHz;

        sv.targetFreq = targetHz;
        sv.active     = true;
        sv.midiNote   = midiNote;

        svf1_.reset();
        svf2_.reset();

        triggerEnvs(params);

        if (p(kSlotLfoSync) >= 0.5f)
            lfoPhase_ = 0.0;
    }

    void VAMachine::legatoMonoVoice(int midiNote, const ParamFrame& params)
    {
        auto& sv = subVoices_[0];
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
        const double targetHz = midiNoteToHz(midiNote);
        if (p(kSlotPorta) <= 0.0f)
            sv.currentFreq = targetHz;
        sv.targetFreq = targetHz;
        sv.midiNote   = midiNote;
        // Envelope continues; oscillator phases and SVF state unchanged.
    }

    void VAMachine::releaseMonoVoice()
    {
        releaseEnvs();
    }

    // =========================================================================
    // Para voice

    int VAMachine::allocSubVoice()
    {
        // Prefer the slot matching the current chord-note index.
        const int preferred = paraChordNoteIdx_ % kMaxSubVoices;
        const auto& pv = subVoices_[static_cast<std::size_t>(preferred)];
        if (!pv.active && !pv.ar.isActive() && !pv.keepForRelease)
            return preferred;

        // Fall back to any fully idle voice.
        for (int i = 0; i < kMaxSubVoices; ++i)
        {
            const auto& sv = subVoices_[static_cast<std::size_t>(i)];
            if (!sv.active && !sv.ar.isActive() && !sv.keepForRelease)
                return i;
        }

        // All active: steal oldest (lowest age counter).
        int oldest = 0;
        for (int i = 1; i < kMaxSubVoices; ++i)
        {
            if (subVoices_[static_cast<std::size_t>(i)].age
                < subVoices_[static_cast<std::size_t>(oldest)].age)
                oldest = i;
        }
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
        sv.oscType     = paraChordNoteIdx_ % 2;
        sv.midiNote    = midiNote;
        sv.age         = ++voiceCounter_;
        sv.osc1Phase   = 0.0;
        sv.osc2Phase   = 0.0;
        sv.subPhase    = 0.0;
        sv.keepForRelease = false;

        ++paraChordNoteIdx_;

        // Per-voice AR: gives Microfreak-style per-note articulation.
        sv.ar.setADSR(kParaArAttackMs, 0.0f, 1.0f, kParaArReleaseMs);
        sv.ar.gateOn();

        // Trigger master envelopes only when this is the first held voice
        // (transition from all-voices-off to first-voice-on).
        bool anyOtherActive = false;
        for (int i = 0; i < kMaxSubVoices; ++i)
        {
            if (i != idx && subVoices_[static_cast<std::size_t>(i)].active)
            {
                anyOtherActive = true;
                break;
            }
        }

        if (!anyOtherActive)
        {
            paraChordNoteIdx_ = 1;
            svf1_.reset();
            svf2_.reset();
            // Clear any lingering keepForRelease flags from previous chord.
            for (auto& s : subVoices_) { s.keepForRelease = false; s.ar.reset(); }
            // Re-gate the released sv so its AR still starts correctly.
            sv.ar.setADSR(kParaArAttackMs, 0.0f, 1.0f, kParaArReleaseMs);
            sv.ar.gateOn();

            triggerEnvs(params);
        }

        if (p(kSlotLfoSync) >= 0.5f && !anyOtherActive)
            lfoPhase_ = 0.0;
    }

    void VAMachine::releaseParaVoice(int midiNote)
    {
        for (auto& sv : subVoices_)
        {
            if (sv.active && sv.midiNote == midiNote)
                sv.active = false;
        }

        const bool anyActive = std::any_of(subVoices_.begin(), subVoices_.end(),
                                            [](const SubVoice& s) { return s.active; });
        if (!anyActive)
        {
            // Last key released: master envs enter Release.
            // Mark voices that were sounding so they keep contributing through
            // the release stage (master amp env governs; per-voice AR is bypassed).
            releaseEnvs();
            for (auto& sv : subVoices_)
            {
                if (sv.ar.isActive())
                    sv.keepForRelease = true;
            }
        }
        else
        {
            // Partial chord release: let the per-voice AR decay for articulation.
            for (auto& sv : subVoices_)
            {
                if (!sv.active && !sv.keepForRelease && sv.ar.isActive())
                    sv.ar.gateOff();
            }
        }
    }

    // =========================================================================
    // process()

    void VAMachine::process(const juce::MidiBuffer& events,
                             const ParamFrame& params,
                             juce::AudioBuffer<float>& buffer)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const bool paraMode  = (p(kSlotVoiceMode) >= 0.5f);
        const int numSamples = buffer.getNumSamples();
        const int numOut     = buffer.getNumChannels();

        // ---- Scan events -----------------------------------------------
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
            case 2: raw = static_cast<float>(2.0 * lfoPhase_ - 1.0); break;
            case 3: raw = static_cast<float>(1.0 - 2.0 * lfoPhase_); break;
            case 4: raw = (lfoPhase_ < 0.5) ? 1.0f : -1.0f; break;
            case 5:
            {
                if (lfoPhase_ < prevPhase)
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

        const int   lfoTarget    = static_cast<int>(p(kSlotLfoTarget));
        const float lfoCutoffMod = (lfoTarget == 0) ? lfoOut_ * 0.5f : 0.0f;
        const float lfoPitchMod  = (lfoTarget == 1) ? lfoOut_         : 0.0f;
        const float lfoPWMod     = (lfoTarget == 2) ? lfoOut_ * 0.2f  : 0.0f;
        const float lfoAmpMod    = (lfoTarget == 3) ? lfoOut_ * 0.5f  : 0.0f;

        // ---- Params -------------------------------------------------------
        const float cutoffParam  = std::clamp(p(kSlotCutoff) + lfoCutoffMod, 0.0f, 1.0f);
        const int   filterType   = static_cast<int>(p(kSlotFilterType));
        const float driveGain    = 1.0f + 4.0f * p(kSlotDrive);
        const float fEnvDepth    = p(kSlotFEnvDepth);
        const float subLevel     = p(kSlotSub);
        const float noiseLevel   = p(kSlotNoise);
        const float portaMs      = p(kSlotPorta);
        const int   osc1Wave     = static_cast<int>(p(kSlotOsc1Wave));
        const float osc1PW       = std::clamp(p(kSlotOsc1PW) + lfoPWMod, 0.05f, 0.95f);
        const int   osc2Wave     = static_cast<int>(p(kSlotOsc2Wave));
        const float osc2PW       = std::clamp(p(kSlotOsc2PW) + lfoPWMod, 0.05f, 0.95f);
        const float velSens      = p(kSlotVelSens);
        const float velGain      = 1.0f - velSens + velSens * voiceVelocity_;
        const float outputLevel  = p(kSlotLevel) * (1.0f + lfoAmpMod) * velGain;
        const float pan          = std::clamp(p(kSlotPan), -1.0f, 1.0f);

        const float osc2CoarseST  = p(kSlotOsc2Coarse);
        const float osc2FineCent  = p(kSlotOsc2Fine);
        const double osc2FreqRatio = std::pow(2.0, static_cast<double>(osc2CoarseST) / 12.0
                                               + static_cast<double>(osc2FineCent) / 1200.0);

        const float osc1CoarseST  = p(kSlotOsc1Coarse);
        const float osc1FineCent  = p(kSlotOsc1Fine);
        const double osc1FreqMul  = std::pow(2.0, static_cast<double>(osc1CoarseST) / 12.0
                                              + static_cast<double>(osc1FineCent) / 1200.0
                                              + static_cast<double>(lfoPitchMod)   / 12.0);

        const double portaCoeff = (portaMs > 0.0f)
            ? std::exp(-1.0 / (static_cast<double>(portaMs) * 0.001 * sampleRate_))
            : 0.0;

        const float svfQ = std::max(0.01f, (1.0f - p(kSlotRes)) * 1.4f);

        // Retrig mode (legacy FREE value clamped to RETRIG).
        const int retrigMode = std::clamp(
            (params.size() > static_cast<std::size_t>(kSlotRetrig))
                ? static_cast<int>(std::round(p(kSlotRetrig)))
                : 0,
            0, 1);

        // ---- Per-sample synthesis loop ------------------------------------
        int eventIdx = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- Dispatch note events ----
            while (eventIdx < noteEvents.size()
                   && noteEvents[eventIdx].samplePos <= i)
            {
                const auto& ev = noteEvents[eventIdx];
                if (ev.on)
                {
                    voiceVelocity_ = ev.vel;
                    if (paraMode)
                    {
                        startParaVoice(ev.note, params);
                    }
                    else
                    {
                        using OA = dsp::MonoGate::OnAction;
                        const OA action = monoGate_.noteOn(ev.note);

                        if (action == OA::FirstTrigger)
                        {
                            // New phrase (held set was empty): always retrigger.
                            startMonoVoice(ev.note, params);
                        }
                        else if (retrigMode == 0)  // LEGATO OverlapTrigger
                        {
                            legatoMonoVoice(ev.note, params);
                        }
                        else  // RETRIG OverlapTrigger
                        {
                            if (subVoices_[0].active || monoGhostFade_ > 0)
                            {
                                // Ghost-fade the old amp level while the new
                                // envelope attacks from 0.
                                monoGhostGain_ = ampEnv_.currentLevel();
                                monoGhostFade_ = msToSamples(1.5f, sampleRate_);
                                legatoMonoVoice(ev.note, params);
                                ampEnv_.setADSR(p(kSlotAmpA), p(kSlotAmpD),
                                                p(kSlotAmpS), p(kSlotAmpR));
                                filterEnv_.setADSR(p(kSlotFEnvA), p(kSlotFEnvD),
                                                   p(kSlotFEnvS), p(kSlotFEnvR));
                                ampEnv_.hardRetrigger();
                                filterEnv_.hardRetrigger();
                                if (p(kSlotLfoSync) >= 0.5f) lfoPhase_ = 0.0;
                            }
                            else
                            {
                                startMonoVoice(ev.note, params);
                            }
                        }
                    }
                }
                else
                {
                    if (paraMode)
                    {
                        releaseParaVoice(ev.note);
                    }
                    else
                    {
                        using OA = dsp::MonoGate::OffAction;
                        const OA action = monoGate_.noteOff(ev.note);
                        if (action == OA::Release)
                        {
                            releaseMonoVoice();
                        }
                        else if (action == OA::SlideTo)
                        {
                            legatoMonoVoice(monoGate_.topHeldNote(), params);
                        }
                        // Ignore: background key released, no change.
                    }
                }
                ++eventIdx;
            }

            // ---- Advance filter and amp envelopes ----
            const float fEnvLevel = filterEnv_.tick();
            const float aEnvLevel = ampEnv_.tick();

            // Mono: deactivate voice once amp env finishes so the next
            // FirstTrigger starts with a clean phase reset.
            if (!paraMode && !ampEnv_.isActive())
                subVoices_[0].active = false;

            // Para: once master amp env finishes, free the keepForRelease voices.
            if (paraMode && !ampEnv_.isActive())
            {
                for (auto& sv : subVoices_)
                {
                    if (sv.keepForRelease)
                    {
                        sv.keepForRelease = false;
                        sv.ar.reset();
                    }
                }
            }

            if (!ampEnv_.isActive() && monoGhostFade_ <= 0)
            {
                bool anyFade = false;
                for (const auto& sv : subVoices_)
                {
                    if (sv.ar.isActive() || sv.keepForRelease)
                    {
                        anyFade = true;
                        break;
                    }
                }
                if (!anyFade) continue;
            }

            // ---- Effective cutoff ----
            const float effectiveCutoff = std::clamp(cutoffParam + fEnvLevel * fEnvDepth,
                                                      0.0f, 1.0f);
            const float cutoffHz = 20.0f * std::pow(900.0f, effectiveCutoff);
            const float svfF = std::clamp(
                2.0f * std::sin(kPiF * cutoffHz / static_cast<float>(sampleRate_)),
                0.001f, 0.99f);

            // ---- Sum oscillators ----
            float oscSum = 0.0f;
            for (auto& sv : subVoices_)
            {
                if (!sv.active && !sv.ar.isActive() && !sv.keepForRelease) continue;

                // Portamento.
                if (portaCoeff > 0.0)
                    sv.currentFreq = portaCoeff * sv.currentFreq
                                     + (1.0 - portaCoeff) * sv.targetFreq;
                else
                    sv.currentFreq = sv.targetFreq;

                const double savedFreq = sv.currentFreq;
                sv.currentFreq *= osc1FreqMul;

                const float svSample = oscillatorSample(sv, osc1Wave, osc1PW,
                                                         osc2Wave, osc2PW,
                                                         subLevel, osc2FreqRatio,
                                                         paraMode);
                sv.currentFreq = savedFreq;

                // In mono mode the master ampEnv controls volume (combinedGain
                // below); per-voice AR is para-only. In para keepForRelease the
                // master amp release also governs, so AR is bypassed there too.
                float voiceGain;
                if (!paraMode || sv.keepForRelease)
                    voiceGain = 1.0f;
                else
                    voiceGain = sv.ar.tick();

                oscSum += svSample * voiceGain;
            }

            // ---- Shared noise ----
            if (noiseLevel > 0.0f)
            {
                noiseState_ = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                oscSum += noiseState_ * noiseLevel;
            }

            // ---- Drive ----
            const float driven = std::tanh(driveGain * oscSum);

            // ---- Filter ----
            float filtered = filterSample(driven, svfF, svfQ, filterType);

            // ---- Mono RETRIG ghost-gain crossfade ----
            if (!paraMode && monoGhostFade_ > 0)
            {
                monoGhostGain_ -= monoGhostGain_ / static_cast<float>(monoGhostFade_);
                --monoGhostFade_;
                if (monoGhostFade_ <= 0) monoGhostGain_ = 0.0f;
            }
            const float combinedGain = aEnvLevel + (paraMode ? 0.0f : monoGhostGain_);
            const float finalGain    = combinedGain * outputLevel;

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
