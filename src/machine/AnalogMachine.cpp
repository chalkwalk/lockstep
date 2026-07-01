#include "AnalogMachine.h"
#include "MachineParamTable.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lockstep
{
    static constexpr double kTwoPi = 6.283185307179586476925;
    static constexpr float kPiF = 3.14159265358979323846f;

    // =========================================================================
    // Construction

    AnalogMachine::AnalogMachine() = default;
    AnalogMachine::~AnalogMachine() = default;

    void AnalogMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        reset();
        // Re-prepare envelope objects after reset() zeroed their sr_ fields.
        ampEnv_.prepare(sampleRate);
        filterEnv_.prepare(sampleRate);
        for (auto& sv : subVoices_)
            sv.ar.prepare(sampleRate);
    }

    void AnalogMachine::reset()
    {
        for (auto& sv : subVoices_) sv = SubVoice{};
        voiceCounter_ = 0;
        paraChordNoteIdx_ = 0;
        ampEnv_.reset();
        filterEnv_.reset();
        svf1_.reset();
        svf2_.reset();
        lfoPhase_ = 0.0;
        lfoOut_ = 0.0f;
        noiseState_ = 0.0f;
        monoGhostGain_ = 0.0f;
        monoGhostFade_ = 0;
        monoGate_.reset();
        prevParaMode_ = false;
        lfoRandCurr_ = 0.0f;
        lfoRandNext_ = 0.0f;
        lfoRandPhase_ = 0.0;
        dcX1_ = 0.0f;
        dcY1_ = 0.0f;
    }

    // =========================================================================
    // Schema

    // Unit / Role as uint8 (avoids -Wsign-conversion casts in every row).
    // These must stay in sync with the ParamSpec enums in IMachine.h.
    namespace va_u
    { // unit
        static constexpr uint8_t None = 0, Ms = 1, Semi = 2, Pct = 3;
    }
    namespace va_r
    { // role
        static constexpr uint8_t None = 0, Pitch = 1, Level = 3, Pan = 4,
                                 Cut = 5, Res = 6, Drive = 7,
                                 Atk = 8, Dcy = 10, Sus = 11, Rel = 12,
                                 LfoDep = 13, LfoRat = 14, LfoShp = 15;
    }

    namespace
    { // NULL-terminated value-label arrays (static lifetime)
        static constexpr const char* kVAOscWaveLabels[] = { "SAW", "TRI", "SQR", "SIN", nullptr };
        static constexpr const char* kVAOsc2WaveLabels[] = { "SAW", "TRI", "SQR", "SIN", "OFF", nullptr };
        static constexpr const char* kVAVoiceModeLabels[] = { "MONO", "PARA", nullptr };
        static constexpr const char* kVAFilterTypeLabels[] = { "LP24", "LP12", "HP", "BP", nullptr };
        static constexpr const char* kVALfoShapeLabels[] = { "SIN", "TRI", "SAW", "SQR", "S&H", "RND", nullptr };
        static constexpr const char* kVALfoTargetLabels[] = { "CUT", "PITCH", "PW", "AMP", nullptr };
        static constexpr const char* kVALfoSyncLabels[] = { "FREE", "SYNC", nullptr };
        static constexpr const char* kVARetrigLabels[] = { "LEGATO", "RETRIG", nullptr };
    }

    // { id, label, min, max, def, skew, stepped, unit, role, variant, section, zcSnap, labels }
    static constexpr ParamRow kVAParams[] = {
        // --- SRC (section 1, 12 slots) ---
        { "va_osc1_coarse", "Osc1 Coarse", -24.f, 24.f, 0.f, 1.f, 1, va_u::Semi, va_r::Pitch, 0, 1, 0, nullptr }, //  0
        { "va_osc1_fine", "Osc1 Fine", -50.f, 50.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  1
        { "va_osc1_wave", "Osc1 Wave", 0.f, 3.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 1, 0, kVAOscWaveLabels }, //  2
        { "va_osc1_pw", "Osc1 PW", 0.f, 1.f, 0.5f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  3
        { "va_osc2_coarse", "Osc2 Coarse", -24.f, 24.f, 0.f, 1.f, 1, va_u::Semi, va_r::Pitch, 0, 1, 0, nullptr }, //  4
        { "va_osc2_fine", "Osc2 Fine", -50.f, 50.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  5
        { "va_osc2_wave", "Osc2 Wave", 0.f, 4.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 1, 0, kVAOsc2WaveLabels }, //  6
        { "va_osc2_pw", "Osc2 PW", 0.f, 1.f, 0.5f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  7
        { "va_sub", "Sub", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  8
        { "va_noise", "Noise", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, //  9
        { "va_porta", "Portamento", 0.f, 500.f, 0.f, 1.f, 0, va_u::Ms, va_r::None, 0, 1, 0, nullptr }, // 10
        { "va_voice_mode", "Voice Mode", 0.f, 1.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 1, 0, kVAVoiceModeLabels }, // 11
        // --- FLTR (section 2, 9 slots) ---
        { "va_cutoff", "Cutoff", 0.f, 1.f, 1.f, 1.f, 0, va_u::None, va_r::Cut, 0, 2, 0, nullptr }, // 12
        { "va_res", "Resonance", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::Res, 0, 2, 0, nullptr }, // 13
        { "va_filter_type", "Filter", 0.f, 3.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 2, 0, kVAFilterTypeLabels },// 14
        { "va_drive", "Drive", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::Drive, 0, 2, 0, nullptr }, // 15
        { "va_fenv_depth", "Env Depth", -1.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 2, 0, nullptr }, // 16
        { "va_fenv_a", "F Atk", 0.f, 5000.f, 1.f, 0.3f, 0, va_u::Ms, va_r::None, 0, 2, 0, nullptr }, // 17
        { "va_fenv_d", "F Dec", 1.f, 10000.f, 100.f, 0.3f, 0, va_u::Ms, va_r::None, 0, 2, 0, nullptr }, // 18
        { "va_fenv_s", "F Sus", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::None, 0, 2, 0, nullptr }, // 19
        { "va_fenv_r", "F Rel", 1.f, 10000.f, 100.f, 0.3f, 0, va_u::Ms, va_r::None, 0, 2, 0, nullptr }, // 20
        // --- AMP (section 3, 8 slots) ---
        { "va_amp_a", "Attack", 0.f, 5000.f, 1.f, 0.3f, 0, va_u::Ms, va_r::Atk, 0, 3, 0, nullptr }, // 21
        { "va_amp_d", "Decay", 1.f, 10000.f, 100.f, 0.3f, 0, va_u::Ms, va_r::Dcy, 0, 3, 0, nullptr }, // 22
        { "va_amp_s", "Sustain", 0.f, 1.f, 0.8f, 1.f, 0, va_u::None, va_r::Sus, 0, 3, 0, nullptr }, // 23
        { "va_amp_r", "Release", 1.f, 10000.f, 500.f, 0.3f, 0, va_u::Ms, va_r::Rel, 0, 3, 0, nullptr }, // 24
        { "va_level", "Level", 0.f, 1.f, 0.5f, 1.f, 0, va_u::None, va_r::Level, 0, 3, 0, nullptr }, // 25
        { "va_pan", "Pan", -1.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::Pan, 0, 3, 0, nullptr }, // 26
        { "va_retrig", "Retrig", 0.f, 1.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 3, 0, kVARetrigLabels }, // 27
        { "va_vel_sens", "Vel Sens", 0.f, 1.f, 0.f, 1.f, 0, va_u::Pct, va_r::None, 0, 3, 0, nullptr }, // 28
        // --- LFO (section 4, 5 slots) ---
        { "va_lfo_rate", "LFO Rate", 0.01f, 40.f, 3.f, 1.f, 0, va_u::None, va_r::LfoRat, 0, 4, 0, nullptr }, // 29
        { "va_lfo_depth", "LFO Depth", 0.f, 1.f, 0.f, 1.f, 0, va_u::None, va_r::LfoDep, 0, 4, 0, nullptr }, // 30
        { "va_lfo_shape", "LFO Shape", 0.f, 5.f, 0.f, 1.f, 1, va_u::None, va_r::LfoShp, 0, 4, 0, kVALfoShapeLabels }, // 31
        { "va_lfo_target", "LFO Target", 0.f, 3.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 4, 0, kVALfoTargetLabels }, // 32
        { "va_lfo_sync", "LFO Sync", 0.f, 1.f, 0.f, 1.f, 1, va_u::None, va_r::None, 0, 4, 0, kVALfoSyncLabels }, // 33
        // --- SRC continued (section 1) ---
        { "va_osc_mix", "Osc Mix", 0.f, 1.f, 0.5f, 1.f, 0, va_u::None, va_r::None, 0, 1, 0, nullptr }, // 34
        // --- MOD continued (section 4): vintage character macro ---
        { "va_age", "Age", 0.f, 1.f, 0.2f, 1.f, 0, va_u::None, va_r::None, 0, 4, 0, nullptr }, // 35
        // --- FILTER continued (section 2): cutoff key-tracking amount ---
        { "va_keytrack", "Key Trk", 0.f, 1.f, 1.f, 1.f, 0, va_u::Pct, va_r::None, 0, 2, 0, nullptr }, // 36
    };
    static_assert(std::size(kVAParams) == AnalogMachine::kNumSlots,
                  "kVAParams row count must equal kNumSlots");

    ParamSpec AnalogMachine::paramSpec(int index) const
    {
        if (index < 0 || index >= kNumSlots) return {};
        return toParamSpec(kVAParams[static_cast<std::size_t>(index)]);
    }

    SectionInfo AnalogMachine::section(int index) const
    {
        switch (index)
        {
            case 1:  return { "SRC" };
            case 2:  return { "FLTR" };
            case 3:  return { "AMP" };
            case 4:  return { "LFO" };
            default: return {};
        }
    }

    // =========================================================================
    // currentVoices

    IMachine::Polyphony AnalogMachine::currentVoices(const ParamFrame& baseParams) const
    {
        if (static_cast<int>(baseParams.size()) > kSlotVoiceMode && baseParams[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f)
            return Polyphony::V4;
        return Polyphony::V1;
    }

    // =========================================================================
    // isVoiceActive

    bool AnalogMachine::isVoiceActive() const
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

    float AnalogMachine::polyBlep(double t, double dt) noexcept
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

    float AnalogMachine::oscillatorSample(SubVoice& sv, int osc1Wave, float osc1PW,
                                      int osc2Wave, float osc2PW,
                                      float subLevel,
                                      double osc2FreqRatio,
                                      bool paraMode,
                                      float osc1Gain, float osc2Gain) noexcept
    {
        // In para mode: sv.oscType 0 → render osc1+sub only; sv.oscType 1 → render osc2+sub only.
        // In mono mode (paraMode=false): render both osc1 and osc2 weighted by osc1Gain/osc2Gain.
        const bool renderOsc1 = !paraMode || sv.oscType == 0;
        const bool renderOsc2 = !paraMode || sv.oscType == 1;

        const double osc1Inc = sv.currentFreq / sampleRate_;
        const double osc2Inc = sv.currentFreq * osc2FreqRatio / sampleRate_;
        const double subInc = sv.currentFreq * 0.5 / sampleRate_;

        float out = 0.0f;

        // Osc 1.  Phases always advance to stay coherent across mode switches.
        // Labels: 0=SAW 1=TRI 2=SQR 3=SIN.
        {
            const double ph = sv.osc1Phase;
            const double inc = osc1Inc;
            if (renderOsc1)
            {
                float s = 0.0f;
                switch (osc1Wave)
                {
                    case 0: // SAW
                        s = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, inc);
                        break;
                    case 1: // TRI
                        s = static_cast<float>(4.0 * std::abs(ph - 0.5) - 1.0);
                        break;
                    case 2: // SQR (pulse with PW control)
                    {
                        const auto pw = static_cast<double>(std::clamp(osc1PW, 0.05f, 0.95f));
                        s = ph < pw ? 1.0f : -1.0f;
                        s -= polyBlep(ph, inc);
                        s += polyBlep(std::fmod(ph - pw + 1.0, 1.0), inc);
                        break;
                    }
                    case 3: // SIN
                        s = static_cast<float>(std::sin(kTwoPi * ph));
                        break;
                    default: break;
                }
                out += s * osc1Gain;
            }
            sv.osc1Phase += inc;
            if (sv.osc1Phase >= 1.0) sv.osc1Phase -= 1.0;
        }

        // Osc 2.  Labels: 0=SAW 1=TRI 2=SQR 3=SIN 4=OFF.
        // OFF (4) silences osc2 in mono; in para mode it falls back to SAW so the
        // osc2-type sub-voice still produces output.
        const bool osc2Off = (osc2Wave == 4);
        const bool osc2Active = paraMode ? renderOsc2 : (renderOsc2 && !osc2Off);
        if (osc2Active)
        {
            const double ph = sv.osc2Phase;
            const double inc = osc2Inc;
            // Para + OFF → use SAW so the osc2 sub-voice still sounds.
            const int wave = (paraMode && osc2Off) ? 0 : osc2Wave;
            float s = 0.0f;
            switch (wave)
            {
                case 0: // SAW
                    s = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, inc);
                    break;
                case 1: // TRI
                    s = static_cast<float>(4.0 * std::abs(ph - 0.5) - 1.0);
                    break;
                case 2: // SQR (pulse with PW control)
                {
                    const auto pw = static_cast<double>(std::clamp(osc2PW, 0.05f, 0.95f));
                    s = ph < pw ? 1.0f : -1.0f;
                    s -= polyBlep(ph, inc);
                    s += polyBlep(std::fmod(ph - pw + 1.0, 1.0), inc);
                    break;
                }
                case 3: // SIN
                    s = static_cast<float>(std::sin(kTwoPi * ph));
                    break;
                default: break;
            }
            out += s * osc2Gain;
            sv.osc2Phase += inc;
            if (sv.osc2Phase >= 1.0) sv.osc2Phase -= 1.0;
        }
        else
        {
            // Advance osc2 phase even when silent to avoid a click on unmute.
            sv.osc2Phase += osc2Inc;
            if (sv.osc2Phase >= 1.0) sv.osc2Phase -= 1.0;
        }

        // Sub (one octave below osc1; scaled by osc1Gain so it follows the mix).
        if (subLevel > 0.0f)
        {
            const double ph = sv.subPhase;
            const float subSample = static_cast<float>(2.0 * ph - 1.0) + polyBlep(ph, subInc);
            out += subSample * subLevel * osc1Gain;
            sv.subPhase += subInc;
            if (sv.subPhase >= 1.0) sv.subPhase -= 1.0;
        }

        return out;
    }

    float AnalogMachine::filterSample(float in, float f, float q, int filterType) noexcept
    {
        auto runSVF = [](SVFState& s, float x, float fc, float res) -> std::tuple<float, float, float> {
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

    void AnalogMachine::triggerEnvs(const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
        ampEnv_.setADSR(p(kSlotAmpA), p(kSlotAmpD), p(kSlotAmpS), p(kSlotAmpR));
        ampEnv_.gateOn();
        filterEnv_.setADSR(p(kSlotFEnvA), p(kSlotFEnvD), p(kSlotFEnvS), p(kSlotFEnvR));
        filterEnv_.gateOn();
    }

    void AnalogMachine::releaseEnvs()
    {
        ampEnv_.gateOff();
        filterEnv_.gateOff();
    }

    // =========================================================================
    // Mono voice

    void AnalogMachine::startMonoVoice(int midiNote, const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        auto& sv = subVoices_[0];
        const double targetHz = midiNoteToHz(midiNote);
        const float portaMs = p(kSlotPorta);

        // Reset phases only when coming from a fully idle voice; if the voice is
        // still in Release, preserve phases so the re-attack is click-free.
        if (!sv.active)
        {
            sv.osc1Phase = 0.0;
            sv.osc2Phase = 0.0;
            sv.subPhase = 0.0;
        }
        if (!sv.active || portaMs <= 0.0f)
            sv.currentFreq = targetHz;

        sv.targetFreq = targetHz;
        sv.active = true;
        sv.midiNote = midiNote;
        filterTrackNote_ = midiNote;  // cutoff key-tracking reference (most recent note)

        svf1_.reset();
        svf2_.reset();

        triggerEnvs(params);

        if (p(kSlotLfoSync) >= 0.5f)
            lfoPhase_ = 0.0;
    }

    void AnalogMachine::legatoMonoVoice(int midiNote, const ParamFrame& params)
    {
        auto& sv = subVoices_[0];
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
        const double targetHz = midiNoteToHz(midiNote);
        if (p(kSlotPorta) <= 0.0f)
            sv.currentFreq = targetHz;
        sv.targetFreq = targetHz;
        sv.midiNote = midiNote;
        filterTrackNote_ = midiNote;  // cutoff key-tracking reference (most recent note)
        // Envelope continues; oscillator phases and SVF state unchanged.
    }

    void AnalogMachine::releaseMonoVoice()
    {
        releaseEnvs();
    }

    // =========================================================================
    // Para voice

    int AnalogMachine::allocSubVoice()
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
            if (subVoices_[static_cast<std::size_t>(i)].age < subVoices_[static_cast<std::size_t>(oldest)].age)
                oldest = i;
        }
        return oldest;
    }

    void AnalogMachine::startParaVoice(int midiNote, const ParamFrame& params)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const int idx = allocSubVoice();
        auto& sv = subVoices_[static_cast<std::size_t>(idx)];

        const double targetHz = midiNoteToHz(midiNote);
        const float portaMs = p(kSlotPorta);

        if (portaMs <= 0.0f || !sv.active)
            sv.currentFreq = targetHz;
        sv.targetFreq = targetHz;
        sv.active = true;
        sv.oscType = paraChordNoteIdx_ % 2;
        sv.midiNote = midiNote;
        filterTrackNote_ = midiNote;  // cutoff key-tracking reference (most recent note)
        sv.age = ++voiceCounter_;
        sv.osc1Phase = 0.0;
        sv.osc2Phase = 0.0;
        sv.subPhase = 0.0;
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
            for (auto& s : subVoices_)
            {
                s.keepForRelease = false;
                s.ar.reset();
            }
            // Re-gate the released sv so its AR still starts correctly.
            sv.ar.setADSR(kParaArAttackMs, 0.0f, 1.0f, kParaArReleaseMs);
            sv.ar.gateOn();

            triggerEnvs(params);
        }

        if (p(kSlotLfoSync) >= 0.5f && !anyOtherActive)
            lfoPhase_ = 0.0;
    }

    void AnalogMachine::releaseParaVoice(int midiNote)
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

    void AnalogMachine::process(const juce::MidiBuffer& events,
                            const ParamFrame& params,
                            juce::AudioBuffer<float>& buffer)
    {
        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const bool paraMode = (p(kSlotVoiceMode) >= 0.5f);
        const int numSamples = buffer.getNumSamples();
        const int numOut = buffer.getNumChannels();

        // ---- Mode-switch cleanup ------------------------------------------
        // When Mono↔Para flips, stale voice state from the previous mode causes
        // silence (Para→Mono: old sub-voices keep playing at full gain, overdriving
        // output) or missing envelopes (Mono→Para: voice-0 still active, so the
        // first startParaVoice() skips the master env trigger). Hard-flush here.
        if (paraMode != prevParaMode_)
        {
            monoGate_.reset();
            for (auto& sv : subVoices_)
            {
                sv.active = false;
                sv.keepForRelease = false;
                sv.ar.reset();
            }
            paraChordNoteIdx_ = 0;
            monoGhostGain_ = 0.0f;
            monoGhostFade_ = 0;
            ampEnv_.reset();
            filterEnv_.reset();
            prevParaMode_ = paraMode;
        }

        // ---- Scan events -----------------------------------------------
        struct NoteEvent
        {
            int samplePos;
            int note;
            float vel;
            bool on;
        };
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
        const float lfoRate = p(kSlotLfoRate);
        const float lfoDepth = p(kSlotLfoDepth);
        const int lfoShape = static_cast<int>(p(kSlotLfoShape));
        {
            const double lfoInc = static_cast<double>(lfoRate) / sampleRate_ * static_cast<double>(numSamples);
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
                case 5: {
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

        const int lfoTarget = static_cast<int>(p(kSlotLfoTarget));
        const float lfoCutoffMod = (lfoTarget == 0) ? lfoOut_ * 0.5f : 0.0f;
        const float lfoPitchMod = (lfoTarget == 1) ? lfoOut_ : 0.0f;
        const float lfoPWMod = (lfoTarget == 2) ? lfoOut_ * 0.2f : 0.0f;
        const float lfoAmpMod = (lfoTarget == 3) ? lfoOut_ * 0.5f : 0.0f;

        // ---- "Age" vintage drift ------------------------------------------
        // Four free-running sub-Hz drifters give analog wander on osc pitch,
        // filter cutoff and pulse width; Age also adds a touch of glue
        // saturation (driveGain below). Advanced once per block.
        const float age = std::clamp(p(kSlotAge), 0.0f, 1.0f);
        static constexpr std::array<double, 4> kDriftRatesHz{ 0.11, 0.17, 0.07, 0.23 };
        std::array<float, 4> driftVal{};
        for (int d = 0; d < 4; ++d)
        {
            driftPhase_[static_cast<std::size_t>(d)] +=
                kDriftRatesHz[static_cast<std::size_t>(d)] / sampleRate_
                * static_cast<double>(numSamples);
            driftPhase_[static_cast<std::size_t>(d)] -=
                std::floor(driftPhase_[static_cast<std::size_t>(d)]);
            driftVal[static_cast<std::size_t>(d)] = static_cast<float>(
                std::sin(kTwoPi * driftPhase_[static_cast<std::size_t>(d)]));
        }
        const float ageDetune1 = age * driftVal[0] * 7.0f;    // cents (osc1)
        const float ageDetune2 = age * driftVal[1] * 7.0f;    // cents (osc2, independent)
        const float ageCutoff  = age * driftVal[2] * 0.03f;   // normalised cutoff
        const float agePW      = age * driftVal[3] * 0.04f;   // pulse width

        // ---- Params -------------------------------------------------------
        const float cutoffParam = std::clamp(p(kSlotCutoff) + lfoCutoffMod + ageCutoff,
                                             0.0f, 1.0f);
        // Cutoff key-tracking: shift the (shared) filter cutoff by the most recent
        // note's distance from C3. (note-60)/120 ≈ one octave of cutoff travel per
        // octave of pitch at full amount, matching the 20*900^c cutoff map. Default
        // amount is 1.0; only audible once the base cutoff is below maximum.
        const float keytrackOffset = p(kSlotKeytrack)
            * (static_cast<float>(filterTrackNote_) - 60.0f) / 120.0f;
        const int filterType = static_cast<int>(p(kSlotFilterType));
        // Always-on gentle glue saturation (baseline 1.4) so default patches
        // have analog character without the level knob switching drive on from
        // nothing; user Drive and Age add harmonics on top.
        const float driveGain = 1.4f + 4.0f * p(kSlotDrive) + age * 1.5f;
        const float fEnvDepth = p(kSlotFEnvDepth);
        const float subLevel = p(kSlotSub);
        const float noiseLevel = p(kSlotNoise);
        const float portaMs = p(kSlotPorta);
        const int osc1Wave = static_cast<int>(p(kSlotOsc1Wave));
        const float osc1PW = std::clamp(p(kSlotOsc1PW) + lfoPWMod + agePW, 0.05f, 0.95f);
        const int osc2Wave = static_cast<int>(p(kSlotOsc2Wave));
        const float osc2PW = std::clamp(p(kSlotOsc2PW) + lfoPWMod - agePW, 0.05f, 0.95f);
        const float velSens = p(kSlotVelSens);
        const float velGain = 1.0f - velSens + velSens * voiceVelocity_;
        const float outputLevel = p(kSlotLevel) * (1.0f + lfoAmpMod) * velGain;
        (void) p(kSlotPan);  // pan is owned by the track CHANNEL block; inert at machine level

        const float osc2CoarseST = p(kSlotOsc2Coarse);
        const float osc2FineCent = p(kSlotOsc2Fine);
        const double osc2FreqRatio = std::pow(2.0, static_cast<double>(osc2CoarseST) / 12.0 + (static_cast<double>(osc2FineCent) + static_cast<double>(ageDetune2)) / 1200.0);

        // Osc mix: constant-power crossfade between osc1 (0) and osc2 (1).
        // Default 0.5 gives equal loudness; 0.0 = osc1 only, 1.0 = osc2 only.
        const float oscMixAngle = std::clamp(p(kSlotOscMix), 0.0f, 1.0f) * kPiF * 0.5f;
        const float osc1Gain = std::cos(oscMixAngle);
        const float osc2Gain = std::sin(oscMixAngle);

        const float osc1CoarseST = p(kSlotOsc1Coarse);
        const float osc1FineCent = p(kSlotOsc1Fine);
        const double osc1FreqMul = std::pow(2.0, static_cast<double>(osc1CoarseST) / 12.0 + (static_cast<double>(osc1FineCent) + static_cast<double>(ageDetune1)) / 1200.0 + static_cast<double>(lfoPitchMod) / 12.0);

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
            while (eventIdx < noteEvents.size() && noteEvents[eventIdx].samplePos <= i)
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
            const float effectiveCutoff = std::clamp(
                cutoffParam + fEnvLevel * fEnvDepth + keytrackOffset, 0.0f, 1.0f);
            const float cutoffHz = 20.0f * std::pow(900.0f, effectiveCutoff);
            const float svfF = std::clamp(
                2.0f * std::sin(kPiF * cutoffHz / static_cast<float>(sampleRate_)),
                0.001f, 0.99f);

            // ---- Sum oscillators ----
            float oscSum = 0.0f;
            float voiceWeight = 0.0f;  // smooth count of sounding voices (para comp)
            for (auto& sv : subVoices_)
            {
                if (!sv.active && !sv.ar.isActive() && !sv.keepForRelease) continue;

                // Portamento.
                if (portaCoeff > 0.0)
                    sv.currentFreq = portaCoeff * sv.currentFreq + (1.0 - portaCoeff) * sv.targetFreq;
                else
                    sv.currentFreq = sv.targetFreq;

                const double savedFreq = sv.currentFreq;
                sv.currentFreq *= osc1FreqMul;

                const float svSample = oscillatorSample(sv, osc1Wave, osc1PW,
                                                        osc2Wave, osc2PW,
                                                        subLevel, osc2FreqRatio,
                                                        paraMode,
                                                        osc1Gain, osc2Gain);
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
                voiceWeight += voiceGain;
            }

            // ---- Paraphonic loudness compensation ----
            // Voices sum linearly, so a 4-note chord would be ~4x as loud and
            // slam the drive stage. Normalising by 1/sqrt(voiceWeight) keeps a
            // chord thicker than one note (~2x for 4) without the abrasive 4x.
            // voiceWeight ramps smoothly with the per-voice AR gains, so this is
            // click-free; in Mono it is ~1 and leaves the level unchanged.
            if (voiceWeight > 1.0f)
                oscSum /= std::sqrt(voiceWeight);

            // ---- Shared noise ----
            if (noiseLevel > 0.0f)
            {
                noiseState_ = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                oscSum += noiseState_ * noiseLevel;
            }

            // ---- Mono RETRIG ghost-gain crossfade ----
            if (!paraMode && monoGhostFade_ > 0)
            {
                monoGhostGain_ -= monoGhostGain_ / static_cast<float>(monoGhostFade_);
                --monoGhostFade_;
                if (monoGhostFade_ <= 0) monoGhostGain_ = 0.0f;
            }

            // Apply envelope + level BEFORE drive so the level knob sets headroom,
            // not just the volume of already-saturated signal.
            const float combinedGain = aEnvLevel + (paraMode ? 0.0f : monoGhostGain_);
            const float preDrive = oscSum * combinedGain * outputLevel;

            // ---- Drive (always-on glue) ----
            // tanh(driveGain*x)/tanh(driveGain) normalises DC gain to 1.0 so the
            // drive/Age knobs add harmonics without boosting level. driveGain is
            // baselined at 1.4 (see above) so there is gentle glue even at
            // Drive=Age=0 — only audible as signals approach full scale.
            const float shaped = std::tanh(driveGain * preDrive) / std::tanh(driveGain);

            // ---- Filter ----
            float filtered = filterSample(shaped, svfF, svfQ, filterType);

            // ---- DC blocker (kills note-on thump from filter / envelope transients) ----
            const float blocked = filtered - dcX1_ + 0.999f * dcY1_;
            dcX1_ = filtered;
            dcY1_ = blocked;

            // ---- Output — write dual-mono; pan is owned by the track CHANNEL block ----
            // Loudness calibration (C2): at default params a single note measured
            // ~2.4 peak — ~5x the drum/FM reference (~0.5) and into the master
            // clipper. kOutputTrim brings the VA in line so every machine sits at a
            // sensible level with internal level ~0.5 / track 1.0. Applied post-drive
            // so the glue character is unchanged — only the level is tamed.
            constexpr float kOutputTrim = 0.21f;
            for (int ch = 0; ch < numOut; ++ch)
                buffer.addSample(ch, i, blocked * kOutputTrim);
        }
    }

} // namespace lockstep
