#include "FMMachine.h"
#include "MachineParamTable.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lockstep
{
    FMMachine::FMMachine() = default;
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
        monoGate_.reset();
    }

    IMachine::Polyphony FMMachine::currentVoices(const ParamFrame& baseParams) const
    {
        if (static_cast<int>(baseParams.size()) > kSlotVoiceMode && baseParams[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f)
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
            if (voices_[static_cast<std::size_t>(i)].age < voices_[static_cast<std::size_t>(oldest)].age)
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
                if (best < 0 || v.age > bestAge)
                {
                    best = i;
                    bestAge = v.age;
                }
            }
        }
        return best;
    }

  // ---------------------------------------------------------------------------
  // Per-operator slot index bundle — single source for all operator-indexed lookups.
    struct FmOpSlotMap
    {
        int ratio, fine, mix, atk, dec, sus, rel;
    };
    static constexpr std::array<FmOpSlotMap, 4> kOpSlots = { {
        { FMMachine::kSlotRatio1, FMMachine::kSlotFine1, FMMachine::kSlotMix1,
          FMMachine::kSlotOp1Attack, FMMachine::kSlotOp1Decay, FMMachine::kSlotOp1Sustain, FMMachine::kSlotOp1Release },
        { FMMachine::kSlotRatio2, FMMachine::kSlotFine2, FMMachine::kSlotMix2,
          FMMachine::kSlotOp2Attack, FMMachine::kSlotOp2Decay, FMMachine::kSlotOp2Sustain, FMMachine::kSlotOp2Release },
        { FMMachine::kSlotRatio3, FMMachine::kSlotFine3, FMMachine::kSlotMix3,
          FMMachine::kSlotOp3Attack, FMMachine::kSlotOp3Decay, FMMachine::kSlotOp3Sustain, FMMachine::kSlotOp3Release },
        { FMMachine::kSlotRatio4, FMMachine::kSlotFine4, FMMachine::kSlotMix4,
          FMMachine::kSlotOp4Attack, FMMachine::kSlotOp4Decay, FMMachine::kSlotOp4Sustain, FMMachine::kSlotOp4Release },
    } };
  // ---------------------------------------------------------------------------

    void FMMachine::startVoice(int voiceIdx, int midiNote, const ParamFrame& params, float velocity)
    {
        auto& voice = voices_[static_cast<std::size_t>(voiceIdx)];

        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };

        const float macroAttack = p(kSlotMacroAttack);
        const float macroRelease = p(kSlotMacroRelease);
        const float macroSustain = p(kSlotMacroSustain);

        const double midiFreq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

        voice.active = true;
        voice.midiNote = midiNote;
        voice.age = ++voiceCounter_;
        voice.outputLevel = p(kSlotOutputLevel);
        voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
        voice.os.reset();  // clear stale decimator tail from a prior note

        for (int dst = 0; dst < kNumOps; ++dst)
            for (int src = 0; src < kNumOps; ++src)
                voice.modMatrix[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)] =
                    p(kSlotModBase + dst * kNumOps + src);

        for (int i = 0; i < kNumOps; ++i)
        {
            auto& op = voice.ops[static_cast<std::size_t>(i)];

            const auto& ops = kOpSlots[static_cast<std::size_t>(i)];
            const int ratioIdx = std::clamp(static_cast<int>(std::round(p(ops.ratio))), 0, kNumRatios - 1);
            const float ratio = kRatioTable[static_cast<std::size_t>(ratioIdx)];
            const double fineCents = static_cast<double>(p(ops.fine));
            const double freq = midiFreq * static_cast<double>(ratio) * std::pow(2.0, fineCents / 1200.0);

            op.phase = 0.0;
            op.phaseInc = freq / sampleRate_;
            op.output = 0.0f;
            op.prevOutput = 0.0f;
            op.prevPrevOutput = 0.0f;
            op.mixerLevel = p(ops.mix);

            op.sustainLevel = std::clamp(p(ops.sus) * macroSustain, 0.0f, 1.0f);
            op.attackSamples = msToSamples(p(ops.atk) * macroAttack, sampleRate_);
            op.decaySamples = msToSamples(p(ops.dec) * macroRelease, sampleRate_);
            op.releaseSamples = msToSamples(p(ops.rel) * macroRelease, sampleRate_);
            op.decayMul = envMul(op.decaySamples);
            op.releaseMul = envMul(op.releaseSamples);
            op.envLevel = 0.0f;
            op.releaseStartLevel = 0.0f;

            if (op.attackSamples <= 0)
            {
                op.envLevel = 1.0f;
                if (op.decaySamples > 0)
                {
                    op.stage = Stage::Decay;
                    op.stageRemaining = op.decaySamples;
                }
                else
                {
                    op.stage = Stage::Sustain;
                    op.stageRemaining = std::numeric_limits<int>::max();
                }
            }
            else
            {
                op.stage = Stage::Attack;
                op.stageRemaining = op.attackSamples;
            }
        }
    }

    bool FMMachine::voiceIsSilent(const FMVoice& v) noexcept
    {
        constexpr float kSilenceFloor = 1e-4f;  // matches dsp::Envelope's idle threshold

        // Only the CARRIERS decide audibility: the voice's output is the sum of
        // op.output * op.mixerLevel, so a modulator with a live envelope and no
        // mixer level contributes nothing you can hear (and modulating a dead
        // carrier is still silence). Judging "is this voice sounding?" on all
        // four operators would call a spent voice alive whenever any unrouted
        // operator happened to still be decaying -- which is the default patch.
        for (const auto& op : v.ops)
        {
            if (op.mixerLevel <= 0.0f || op.stage == Stage::Idle)
                continue;
            if (op.stage == Stage::Attack)
                return false;                     // rising
            if (op.envLevel > kSilenceFloor)
                return false;                     // still sounding
        }
        return true;
    }

    void FMMachine::legatoUpdateVoice(int midiNote, const ParamFrame& params, float velocity)
    {
        auto& voice = voices_[0];
        if (!voice.active || voiceIsSilent(voice))
        {
            startVoice(0, midiNote, params, velocity);
            return;
        }

        const auto p = [&](int s) { return params[static_cast<std::size_t>(s)]; };
        const double midiFreq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);

        voice.midiNote = midiNote;
        voice.age = ++voiceCounter_;
        voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
        voice.outputLevel = p(kSlotOutputLevel);

        const float macroSustain = p(kSlotMacroSustain);
        const float macroRelease = p(kSlotMacroRelease);

        for (int dst = 0; dst < kNumOps; ++dst)
            for (int src = 0; src < kNumOps; ++src)
                voice.modMatrix[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)] =
                    p(kSlotModBase + dst * kNumOps + src);

        for (int i = 0; i < kNumOps; ++i)
        {
            auto& op = voice.ops[static_cast<std::size_t>(i)];
            const auto& opSlot = kOpSlots[static_cast<std::size_t>(i)];
            const int ratioIdx = std::clamp(static_cast<int>(std::round(p(opSlot.ratio))), 0, kNumRatios - 1);
            const float ratio = kRatioTable[static_cast<std::size_t>(ratioIdx)];
            const double fineCents = static_cast<double>(p(opSlot.fine));
            op.phaseInc = midiFreq * static_cast<double>(ratio) * std::pow(2.0, fineCents / 1200.0) / sampleRate_;
            // Update timbral params so P-Lock changes take effect on legato steps.
            // Envelope stage/progress intentionally unchanged (no re-attack).
            op.mixerLevel = p(opSlot.mix);
            op.sustainLevel = std::clamp(p(opSlot.sus) * macroSustain, 0.0f, 1.0f);
            op.releaseSamples = msToSamples(p(opSlot.rel) * macroRelease, sampleRate_);
            op.releaseMul = envMul(op.releaseSamples);
        }
    }

    void FMMachine::releaseAllVoices()
    {
        // Transport stop: send every active voice's operators into their release
        // stage so held notes ring out and decay instead of freezing mid-note.
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices_[static_cast<std::size_t>(i)].active)
                releaseVoice(i);
    }

    void FMMachine::releaseVoice(int voiceIdx)
    {
        auto& voice = voices_[static_cast<std::size_t>(voiceIdx)];
        for (auto& op : voice.ops)
        {
            if (op.stage == Stage::Attack || op.stage == Stage::Decay || op.stage == Stage::Sustain)
            {
                op.releaseStartLevel = op.envLevel;
                op.stage = Stage::Release;
                op.stageRemaining = (op.releaseSamples > 0) ? op.releaseSamples : 1;
            }
        }
    }

    // Exponential decay/release multiplier for a stage of `samples` steps.
    // exp(-kEnvCurve/samples): the level covers ~1-e^-kEnvCurve of the distance
    // to its target over the stage, giving a natural fast-then-slow contour.
    float FMMachine::envMul(int samples)
    {
        return (samples > 0)
                   ? std::exp(-FMMachine::kEnvCurve / static_cast<float>(samples))
                   : 0.0f;
    }

    float FMMachine::advanceEnv(Operator& op)
    {
        const float level = op.envLevel;

        switch (op.stage)
        {
            case Stage::Attack:
                // Attack stays a linear ramp (click-free from the current level).
                op.envLevel += 1.0f / static_cast<float>(op.attackSamples);
                if (--op.stageRemaining <= 0)
                {
                    op.envLevel = 1.0f;
                    if (op.decaySamples > 0)
                    {
                        op.stage = Stage::Decay;
                        op.stageRemaining = op.decaySamples;
                    }
                    else
                    {
                        op.stage = Stage::Sustain;
                        op.stageRemaining = std::numeric_limits<int>::max();
                    }
                }
                break;

            case Stage::Decay:
                // Exponential approach to the sustain level.
                op.envLevel = op.sustainLevel + (op.envLevel - op.sustainLevel) * op.decayMul;
                op.envLevel = std::max(op.envLevel, op.sustainLevel);
                if (--op.stageRemaining <= 0)
                {
                    op.envLevel = op.sustainLevel;
                    op.stage = Stage::Sustain;
                    op.stageRemaining = std::numeric_limits<int>::max();
                }
                break;

            case Stage::Sustain:
                break;

            case Stage::Release:
                // Exponential decay toward zero.
                op.envLevel *= op.releaseMul;
                op.envLevel = std::max(op.envLevel, 0.0f);
                if (--op.stageRemaining <= 0)
                {
                    op.envLevel = 0.0f;
                    op.stage = Stage::Idle;
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

        const int numBlockSamples = buffer.getNumSamples();

        const bool polyMode = (params.size() > static_cast<std::size_t>(kSlotVoiceMode) && params[static_cast<std::size_t>(kSlotVoiceMode)] >= 0.5f);

    // Early-out if nothing is happening on any voice and no events arrived.
        bool anyVoiceState = false;
        for (const auto& v : voices_)
            if (v.active || v.choke.isFading() || v.hasPendingTrigger)
            {
                anyVoiceState = true;
                break;
            }
        if (!anyVoiceState && noteEvents.isEmpty())
            return;

        const int numOut = buffer.getNumChannels();
        int eventIdx = 0;

        // Per-op velocity sensitivity is constant across the block.
        std::array<float, kNumOps> velSens{};
        for (int j = 0; j < kNumOps; ++j)
        {
            const int vsSlot = kSlotOp1VelSens + j;
            velSens[static_cast<std::size_t>(j)] =
                (params.size() > static_cast<std::size_t>(vsSlot))
                    ? params[static_cast<std::size_t>(vsSlot)] : 0.0f;
        }

        // Per-sample one-pole coefficient for the polyphony-compensation smoother
        // (~15 ms time constant). Constant across the block.
        const float voiceNormSmooth = (sampleRate_ > 0.0)
            ? static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sampleRate_)))
            : 1.0f;

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
                        using OA = dsp::MonoGate::OffAction;
                        const OA action = monoGate_.noteOff(ev.note);
                        if (action == OA::Release)
                        {
                            if (voices_[0].active) releaseVoice(0);
                        }
                        else if (action == OA::SlideTo)
                        {
                            legatoUpdateVoice(monoGate_.topHeldNote(), params, voices_[0].velocity);
                        }
            // Ignore: background key released, nothing to do.
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
                            voice.pendingNote = ev.note;
                            voice.pendingVelocity = ev.vel;
                            voice.pendingParams = params;
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
            // Retrig mode (legacy FREE value 2 clamped to RETRIG).
                        const int retrigMode = std::clamp(
                            (params.size() > static_cast<std::size_t>(kSlotRetrig))
                                ? static_cast<int>(std::round(params[static_cast<std::size_t>(kSlotRetrig)]))
                                : 0,
                            0, 1);

                        using OA = dsp::MonoGate::OnAction;
                        const OA action = monoGate_.noteOn(ev.note);

                        auto& voice = voices_[0];

            // Helper: ghost-fade current voice then start new.
                        auto doRetrigStart = [&]() {
                            if (voice.active || voice.choke.isFading())
                            {
                                voices_[1] = voice;
                                voices_[1].isGhost = true;
                                voices_[1].hasPendingTrigger = false;
                                voices_[1].choke.prepare(sampleRate_, 1.5f);
                                voices_[1].choke.trigger();
                                startVoice(0, ev.note, params, ev.vel);
                                voices_[0].isGhost = false;
                                voices_[0].hasPendingTrigger = false;
                                voices_[0].choke.reset();
                            }
                            else
                            {
                                startVoice(0, ev.note, params, ev.vel);
                            }
                        };

                        if (action == OA::FirstTrigger)
                        {
              // New phrase (held set was empty): always retrigger, both modes.
                            doRetrigStart();
                        }
                        else if (retrigMode == 0)  // LEGATO OverlapTrigger
                        {
                            legatoUpdateVoice(ev.note, params, ev.vel);
                        }
                        else  // RETRIG OverlapTrigger
                        {
                            doRetrigStart();
                        }
                    }
                }
                ++eventIdx;
            }

      // Per-voice sample mixing.
            float mixed = 0.0f;
            int activeVoices = 0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                auto& voice = voices_[static_cast<std::size_t>(vi)];

                const float chokeGain = voice.choke.isFading() ? voice.choke.nextGain() : 1.0f;

        // Ghost slots (Mono retrig): deactivate once the choke fade ends.
                if (voice.isGhost && !voice.choke.isFading())
                {
                    voice.active = false;
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
                    if (op.stage != Stage::Idle)
                    {
                        anyActive = true;
                        break;
                    }
                if (!anyActive)
                {
                    voice.active = false;
                    continue;
                }
                ++activeVoices;

        // Carrier-mixer normalization: when the operator mixer levels sum past
        // unity, scale the voice's output back so stacking carriers / turning up
        // the operators does not blow up the level (proportional above 1.0; below
        // it is unchanged). Per-voice so each voice honours its own trigger-time
        // mixer params.
                float vMixSum = 0.0f;
                for (const auto& op : voice.ops)
                    vMixSum += op.mixerLevel;
                const float vMixNorm = (vMixSum > 1.0f) ? (1.0f / vMixSum) : 1.0f;

        // Envelopes advance once per output sample (base rate); the operator
        // core runs at 2x to anti-alias high-index FM, then decimates.
                std::array<float, kNumOps> env{};
                for (int j = 0; j < kNumOps; ++j)
                    env[static_cast<std::size_t>(j)] = advanceEnv(voice.ops[static_cast<std::size_t>(j)]);

                std::array<float, 2> sub{};
                for (int s = 0; s < 2; ++s)
                {
            // Modulation sums from previous (sub-)sample outputs. Off-diagonal
            // is cross-modulation; the diagonal (self-feedback) uses the
            // averaged last two outputs to suppress the noisy limit cycle.
                    std::array<float, kNumOps> modSum{};
                    for (int dst = 0; dst < kNumOps; ++dst)
                        for (int src = 0; src < kNumOps; ++src)
                        {
                            const float m = voice.modMatrix[static_cast<std::size_t>(src)]
                                                           [static_cast<std::size_t>(dst)];
                            if (m == 0.0f) continue;
                            const auto& so = voice.ops[static_cast<std::size_t>(src)];
                            if (src == dst)
                                modSum[static_cast<std::size_t>(dst)] +=
                                    m * 0.5f * (so.prevOutput + so.prevPrevOutput) * kSelfFeedbackScale;
                            else
                                modSum[static_cast<std::size_t>(dst)] += m * so.prevOutput * kModScale;
                        }

                    for (int j = 0; j < kNumOps; ++j)
                    {
                        auto& op = voice.ops[static_cast<std::size_t>(j)];
                        const float angle = static_cast<float>(op.phase * (2.0 * 3.14159265358979323846))
                                            + modSum[static_cast<std::size_t>(j)];
                        float out = std::sin(angle) * env[static_cast<std::size_t>(j)];
                        // Per-op velocity scaling (affects FM depth via prevOutput, not just bus).
                        const float vs = velSens[static_cast<std::size_t>(j)];
                        out *= 1.0f - vs + vs * voice.velocity;
                        op.output = out;
                        // Half-rate phase step: two sub-steps total one base sample.
                        op.phase += 0.5 * op.phaseInc;
                        op.phase -= std::floor(op.phase);
                    }

                    float smp = 0.0f;
                    for (int j = 0; j < kNumOps; ++j)
                    {
                        auto& op = voice.ops[static_cast<std::size_t>(j)];
                        smp += op.output * op.mixerLevel;
                        op.prevPrevOutput = op.prevOutput;
                        op.prevOutput = op.output;
                    }
                    sub[static_cast<std::size_t>(s)] = smp * vMixNorm;
                }

                const float sample = voice.os.decimate(sub[0], sub[1])
                                     * voice.outputLevel * chokeGain;
                mixed += sample;
            }

            // Polyphony compensation: a chord of N voices is scaled by ~1/sqrt(N)
            // so it thickens without N-times the level (1 vs 4 voices stay close).
            // Smoothed toward the target so a voice starting / stopping does not
            // step the bus gain.
            const float vnTarget = (activeVoices > 1)
                                       ? (1.0f / std::sqrt(static_cast<float>(activeVoices)))
                                       : 1.0f;
            voiceNorm_ += (vnTarget - voiceNorm_) * voiceNormSmooth;
            mixed *= voiceNorm_;

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

    namespace fm_u
    {
        static constexpr uint8_t None = 0, Ms = 1, Pct = 3;
    }
    namespace fm_r
    {
        static constexpr uint8_t None = 0, Pitch = 1, Level = 3, Attack = 8, Sustain = 11, Release = 12;
    }

    namespace
    {
        static constexpr const char* kFMRetrigLabels[] = { "LEGATO", "RETRIG", nullptr };
        static constexpr const char* kFMVoiceModeLabels[] = { "MONO", "POLY", nullptr };
        // Ratio table labels — one entry per FMMachine::kRatioTable value.
        static constexpr const char* kFMRatioLabels[] = {
            "0.5", "1", "1.5", "2", "3", "4", "5", "6",
            "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", nullptr
        };
    }

  // { id, label, min, max, def, skew, stepped, unit, role, variant, section, zcSnap, labels }
    static constexpr ParamRow kFMParams[] = {
    // --- SRC page 1: coarse ratios (section 1) ---
        { "fm_ratio_1", "Op1 Ratio", 0.f, 17.f, 1.f, 1.f, 1, fm_u::None, fm_r::Pitch, 0, 1, 0, kFMRatioLabels }, //  0
        { "fm_ratio_2", "Op2 Ratio", 0.f, 17.f, 1.f, 1.f, 1, fm_u::None, fm_r::None, 0, 1, 0, kFMRatioLabels }, //  1
        { "fm_ratio_3", "Op3 Ratio", 0.f, 17.f, 1.f, 1.f, 1, fm_u::None, fm_r::None, 0, 1, 0, kFMRatioLabels }, //  2
        { "fm_ratio_4", "Op4 Ratio", 0.f, 17.f, 1.f, 1.f, 1, fm_u::None, fm_r::None, 0, 1, 0, kFMRatioLabels }, //  3
    // --- SRC page 2: fine tune (cents) ---
        { "fm_fine_1", "Op1 Fine", -100.f, 100.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 1, 0, nullptr }, //  4
        { "fm_fine_2", "Op2 Fine", -100.f, 100.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 1, 0, nullptr }, //  5
        { "fm_fine_3", "Op3 Fine", -100.f, 100.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 1, 0, nullptr }, //  6
        { "fm_fine_4", "Op4 Fine", -100.f, 100.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 1, 0, nullptr }, //  7
    // --- SRC page 3: mixer levels ---
        { "fm_mix_1", "Op1 Mix", 0.f, 1.f, 1.f, 1.f, 0, fm_u::Pct, fm_r::Level, 0, 1, 0, nullptr }, //  8
        { "fm_mix_2", "Op2 Mix", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 1, 0, nullptr }, //  9
        { "fm_mix_3", "Op3 Mix", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 1, 0, nullptr }, // 10
        { "fm_mix_4", "Op4 Mix", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 1, 0, nullptr }, // 11
    // --- AMP page 1: macros + output level (section 3) ---
        { "fm_macro_atk", "Macro Atk", 0.f, 3.f, 1.f, 1.f, 0, fm_u::None, fm_r::Attack, 0, 3, 0, nullptr }, // 12
        { "fm_macro_rel", "Macro Rel", 0.f, 3.f, 1.f, 1.f, 0, fm_u::None, fm_r::Release, 0, 3, 0, nullptr }, // 13
        { "fm_macro_sus", "Macro Sus", 0.f, 2.f, 1.f, 1.f, 0, fm_u::None, fm_r::Sustain, 0, 3, 0, nullptr }, // 14
        { "fm_level", "Level", 0.f, 1.f, 0.5f, 1.f, 0, fm_u::Pct, fm_r::Level, 0, 3, 0, nullptr }, // 15
    // --- AMP pages 2-5: per-operator ADSR (skew 0.3 = exponential) ---
        { "fm_atk_1", "Op1 Atk", 0.f, 5000.f, 10.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 16
        { "fm_dec_1", "Op1 Dec", 1.f, 10000.f, 500.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 17
        { "fm_sus_1", "Op1 Sus", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 18
        { "fm_rel_1", "Op1 Rel", 1.f, 10000.f, 500.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 19
        { "fm_atk_2", "Op2 Atk", 0.f, 5000.f, 10.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 20
        { "fm_dec_2", "Op2 Dec", 1.f, 10000.f, 200.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 21
        { "fm_sus_2", "Op2 Sus", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 22
        { "fm_rel_2", "Op2 Rel", 1.f, 10000.f, 500.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 23
        { "fm_atk_3", "Op3 Atk", 0.f, 5000.f, 10.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 24
        { "fm_dec_3", "Op3 Dec", 1.f, 10000.f, 200.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 25
        { "fm_sus_3", "Op3 Sus", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 26
        { "fm_rel_3", "Op3 Rel", 1.f, 10000.f, 500.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 27
        { "fm_atk_4", "Op4 Atk", 0.f, 5000.f, 10.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 28
        { "fm_dec_4", "Op4 Dec", 1.f, 10000.f, 200.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 29
        { "fm_sus_4", "Op4 Sus", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 30
        { "fm_rel_4", "Op4 Rel", 1.f, 10000.f, 500.f, 0.3f, 0, fm_u::Ms, fm_r::None, 0, 3, 0, nullptr }, // 31
    // --- AMP: per-operator velocity sensitivity ---
        { "fm_vs_1", "Op1 VelSns", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 32
        { "fm_vs_2", "Op2 VelSns", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 33
        { "fm_vs_3", "Op3 VelSns", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 34
        { "fm_vs_4", "Op4 VelSns", 0.f, 1.f, 0.f, 1.f, 0, fm_u::Pct, fm_r::None, 0, 3, 0, nullptr }, // 35
        { "fm_retrig", "Retrig", 0.f, 1.f, 0.f, 1.f, 1, fm_u::None, fm_r::None, 0, 3, 0, kFMRetrigLabels }, // 36
    // --- MOD matrix (section 6): slot = 37 + dst*4 + src ---
        { "fm_mod_s1_d1", "1->1", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 37
        { "fm_mod_s2_d1", "2->1", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 38
        { "fm_mod_s3_d1", "3->1", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 39
        { "fm_mod_s4_d1", "4->1", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 40
        { "fm_mod_s1_d2", "1->2", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 41
        { "fm_mod_s2_d2", "2->2", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 42
        { "fm_mod_s3_d2", "3->2", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 43
        { "fm_mod_s4_d2", "4->2", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 44
        { "fm_mod_s1_d3", "1->3", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 45
        { "fm_mod_s2_d3", "2->3", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 46
        { "fm_mod_s3_d3", "3->3", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 47
        { "fm_mod_s4_d3", "4->3", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 48
        { "fm_mod_s1_d4", "1->4", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 49
        { "fm_mod_s2_d4", "2->4", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 50
        { "fm_mod_s3_d4", "3->4", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 51
        { "fm_mod_s4_d4", "4->4", -1.f, 1.f, 0.f, 1.f, 0, fm_u::None, fm_r::None, 0, 6, 0, nullptr }, // 52
    // --- VOICE (section 7) ---
        { "fm_voice_mode", "Voice", 0.f, 1.f, 1.f, 1.f, 1, fm_u::None, fm_r::None, 0, 7, 0, kFMVoiceModeLabels }, // 53
    };
    static_assert(std::size(kFMParams) == FMMachine::kNumSlots,
                  "kFMParams row count must equal kNumSlots");

    ParamSpec FMMachine::paramSpec(int index) const
    {
        if (index < 0 || index >= kNumSlots) return {};
        return toParamSpec(kFMParams[static_cast<std::size_t>(index)]);
    }

    SectionInfo FMMachine::section(int index) const
    {
        switch (index)
        {
            case 1:  return { "SRC" };
            case 3:  return { "AMP" };
            case 6:  return { "MOD", -1, 0, /*parentCanonical=*/1 };
            case 7:  return { "VOICE", -1, 0, /*parentCanonical=*/1 };
            default: return {};
        }
    }
}
