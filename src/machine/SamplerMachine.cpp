#include "SamplerMachine.h"
#include "MachineParamTable.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    SamplerMachine::SamplerMachine(SamplePool& pool)
        : SamplePlayingMachineBase(pool) {}

    SamplerMachine::~SamplerMachine() = default;

    // -------------------------------------------------------------------------

    SamplePlayer::Spec SamplerMachine::buildSpec(int midiNote, float velocity,
                                                 const ParamFrame& params) const
    {
        const auto p = [&](int s) {
            return params[static_cast<std::size_t>(s)];
        };

        const double pitchOffset = static_cast<double>(p(kSlotPitch));
        const double semitones = static_cast<double>(midiNote - 60) + pitchOffset;

        const int sampleIdx = static_cast<int>(p(kSlotSampleId));
        currentSampleIndex_ = sampleIdx;  // keep base updated for detectTransientSlices
        const Sample* sample = pool_.get(sampleIdx);
        const double numSrcSamples = (sample != nullptr)
                                         ? static_cast<double>(sample->pcm.getNumSamples())
                                         : 0.0;

        const double startNorm = static_cast<double>(p(kSlotStart));
        const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
        const double winStart = startNorm * numSrcSamples;
        const double winEnd = std::min((startNorm + lengthNorm) * numSrcSamples,
                                       numSrcSamples);

        // Loop slots — relative to the playback window.
        const int loopModeInt = static_cast<int>(std::round(p(kSlotLoopMode)));
        const auto loopMode = static_cast<SamplePlayer::LoopMode>(
            std::clamp(loopModeInt, 0,
                       static_cast<int>(SamplePlayer::LoopMode::All)));

        const double loopStartNorm = static_cast<double>(p(kSlotLoopStart));
        const double loopLenNorm = static_cast<double>(p(kSlotLoopLen));

        // Compute absolute loop bounds based on mode.
        double absLoopStart = 0.0;
        double absLoopEnd = 0.0;
        const double windowLen = winEnd - winStart;

        switch (loopMode)
        {
            case SamplePlayer::LoopMode::Off:
                break;
            case SamplePlayer::LoopMode::Sust:
            // User-set loop_start and loop_length apply (free).
                absLoopStart = winStart + loopStartNorm * windowLen;
                absLoopEnd = absLoopStart + loopLenNorm * windowLen;
                break;
            case SamplePlayer::LoopMode::SustAndRel:
            // loop_start user-set; loop_length auto = window_end − loop_start.
                absLoopStart = winStart + loopStartNorm * windowLen;
                absLoopEnd = winEnd;
                break;
            case SamplePlayer::LoopMode::All:
            // Both auto: loop = full playback window.
                absLoopStart = winStart;
                absLoopEnd = winEnd;
                break;
        }
        absLoopEnd = std::min(absLoopEnd, winEnd);

        SamplePlayer::Spec spec;
        spec.sampleIndex = sampleIdx;
        spec.positionStart = winStart;
        spec.windowStart = winStart;
        spec.windowEnd = winEnd;
        spec.rate = std::pow(2.0, semitones / 12.0);
        const float velSens = (params.size() > static_cast<std::size_t>(kSlotVelSens))
                                   ? std::clamp(p(kSlotVelSens), 0.0f, 1.0f) : 0.0f;
        spec.level = p(kSlotLevel) * (1.0f + velSens * (velocity - 1.0f));
        spec.attackSamples = msToSamples(p(kSlotAttack), sampleRate_);
        spec.holdSamples = msToSamples(p(kSlotHold), sampleRate_);
        spec.decaySamples = msToSamples(p(kSlotDecay), sampleRate_);
        spec.sustainLevel = p(kSlotSustain);
        spec.releaseSamples = msToSamples(p(kSlotRelease), sampleRate_);
        spec.loopStart = absLoopStart;
        spec.loopEnd = absLoopEnd;
        spec.loopMode = loopMode;
        return spec;
    }

    void SamplerMachine::startVoiceAtSlice(int sliceIndex, float velocity,
                                             const ParamFrame& params)
    {
        if (sliceIndex < 0 || sliceIndex >= numSlices_)
        {
            voices_[0].player.trigger(buildSpec(60, velocity, params));
            return;
        }

        const int sampleIdx = static_cast<int>(
            params[static_cast<std::size_t>(kSlotSampleId)]);
        const Sample* sample = pool_.get(sampleIdx);

        const float normPos = slicePositions_[static_cast<std::size_t>(sliceIndex)];
        const double startPos = (sample != nullptr)
                                    ? static_cast<double>(normPos) * static_cast<double>(sample->pcm.getNumSamples())
                                    : 0.0;

        auto spec = buildSpec(60, velocity, params);
        spec.positionStart = startPos;
        voices_[0].player.trigger(spec);
    }

    void SamplerMachine::triggerVoice(int midiNote, float velocity, const ParamFrame& params)
    {
        auto& vs = voices_[0];

        const int retrigMode = (params.size() > static_cast<std::size_t>(kSlotRetrig))
                                   ? static_cast<int>(std::round(params[static_cast<std::size_t>(kSlotRetrig)]))
                                   : 0;

        if (retrigMode >= 2 && vs.player.isActive())
            return;  // legacy FREE: skip while voice is sounding

        if (vs.player.isActive())
        {
            vs.pendingNote = midiNote;
            vs.pendingVelocity = velocity;
            vs.pendingParams = params;
            vs.hasPending = true;
            if (!vs.choke.isFading())
                vs.choke.trigger();
            return;
        }

        vs.midiNote = midiNote;
        vs.age = ++voiceCounter_;

        if (numSlices_ > 0 && midiNote >= 0 && midiNote < numSlices_)
            startVoiceAtSlice(midiNote, velocity, params);
        else
            vs.player.trigger(buildSpec(midiNote, velocity, params));
    }

    // -------------------------------------------------------------------------

    void SamplerMachine::process(const juce::MidiBuffer& events,
                                 const ParamFrame& params,
                                 juce::AudioBuffer<float>& buffer)
    {
        int triggerAt = -1;
        int triggerNote = 60;
        float triggerVelocity = 1.0f;
        int releaseAt = -1;

        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                triggerAt = meta.samplePosition;
                triggerNote = msg.getNoteNumber();
                triggerVelocity = msg.getFloatVelocity();
            }
            else if (msg.isNoteOff() && releaseAt < 0)
            {
                releaseAt = meta.samplePosition;
            }
        }

        const int numBlockSamples = buffer.getNumSamples();
        if (triggerAt >= 0)
            triggerAt = std::clamp(triggerAt, 0, numBlockSamples - 1);
        if (releaseAt >= 0)
            releaseAt = std::clamp(releaseAt, 0, numBlockSamples - 1);

        auto& vs = voices_[0];

        if (!vs.player.isActive() && !vs.choke.isFading() && !vs.hasPending && triggerAt < 0)
            return;

        const Sample* sample = vs.player.isActive()
                                   ? pool_.get(vs.player.sampleIndex)
                                   : nullptr;
        const int numOut = buffer.getNumChannels();

        for (int i = 0; i < numBlockSamples; ++i)
        {
            if (releaseAt >= 0 && i == releaseAt)
            {
                vs.player.release();
                releaseAt = -1;
            }

            if (triggerAt >= 0 && i == triggerAt)
            {
                triggerVoice(triggerNote, triggerVelocity, params);
                if (!vs.choke.isFading())
                {
                    sample = pool_.get(vs.player.sampleIndex);
                    if (sample == nullptr)
                    {
                        vs.player = SamplePlayer{};
                        break;
                    }
                }
                triggerAt = -1;
            }

            const float chokeGain = vs.choke.isFading() ? vs.choke.nextGain() : 1.0f;

            if (!vs.choke.isFading() && vs.hasPending)
            {
                vs.hasPending = false;
                const int pn = vs.pendingNote;
                const float pv = vs.pendingVelocity;
                const ParamFrame pp = vs.pendingParams;

                vs.midiNote = pn;
                vs.age = ++voiceCounter_;

                if (numSlices_ > 0 && pn >= 0 && pn < numSlices_)
                    startVoiceAtSlice(pn, pv, pp);
                else
                    vs.player.trigger(buildSpec(pn, pv, pp));

                sample = pool_.get(vs.player.sampleIndex);
                if (sample == nullptr)
                {
                    vs.player = SamplePlayer{};
                    break;
                }
            }

            if (!vs.player.isActive())
            {
                if (triggerAt < 0)
                    break;
                continue;
            }

            if (sample == nullptr)
                continue;

            const float out = vs.player.step(sample->pcm) * chokeGain;
            for (int ch = 0; ch < numOut; ++ch)
                buffer.addSample(ch, i, out);
        }
    }

    // -------------------------------------------------------------------------

    namespace sa_u
    {
        static constexpr uint8_t None = 0, Ms = 1, Semi = 2, Pct = 3;
    }
    namespace sa_r
    {
        static constexpr uint8_t None = 0, Pitch = 1, Level = 3,
                                 Atk = 8, Hold = 9, Dcy = 10, Sus = 11, Rel = 12;
    }

    namespace
    {
        static constexpr const char* kSALoopLabels[] = { "OFF", "SUS", "SUS+REL", "ALL", nullptr };
        static constexpr const char* kSARetrigLabels[] = { "LEGATO", "RETRIG", nullptr };
    }

    // { id, label, min, max, def, skew, stepped, unit, role, variant, section, zcSnap, labels }
    static constexpr ParamRow kSAParams[] = {
        // --- SRC (section 1) ---
        { "sample_id", "Sample", 0.f, 63.f, 0.f, 1.f, 1, sa_u::None, sa_r::None, 0, 1, 0, nullptr }, //  0
        { "pitch", "Pitch", -24.f, 24.f, 0.f, 1.f, 0, sa_u::Semi, sa_r::Pitch, 0, 1, 0, nullptr }, //  1
        { "samp_start", "Start", 0.f, 1.f, 0.f, 1.f, 0, sa_u::None, sa_r::None, 0, 1, 1, nullptr }, //  2  zcSnap=1
        { "samp_length", "Length", 0.f, 1.f, 1.f, 1.f, 0, sa_u::None, sa_r::None, 0, 1, 1, nullptr }, //  3  zcSnap=1
        { "samp_loop_mode", "Loop", 0.f, 3.f, 0.f, 1.f, 1, sa_u::None, sa_r::None, 0, 1, 0, kSALoopLabels }, //  4
        { "samp_loop_start", "LpStart", 0.f, 1.f, 0.f, 1.f, 0, sa_u::None, sa_r::None, 0, 1, 1, nullptr }, //  5  zcSnap=1
        { "samp_loop_len", "LpLen", 0.f, 1.f, 1.f, 1.f, 0, sa_u::None, sa_r::None, 0, 1, 1, nullptr }, //  6  zcSnap=1
        // --- AMP (section 3) ---
        { "level", "Level", 0.f, 1.f, 0.5f, 1.f, 0, sa_u::Pct, sa_r::Level, 0, 3, 0, nullptr }, //  7
        { "attack", "Attack", 0.f, 5000.f, 2.f, 0.3f, 0, sa_u::Ms, sa_r::Atk, 0, 3, 0, nullptr }, //  8
        { "hold", "Hold", 0.f, 2000.f, 0.f, 0.5f, 0, sa_u::Ms, sa_r::Hold, 0, 3, 0, nullptr }, //  9
        { "decay", "Decay", 1.f, 10000.f, 500.f, 0.3f, 0, sa_u::Ms, sa_r::Dcy, 0, 3, 0, nullptr }, // 10
        { "sustain", "Sustain", 0.f, 1.f, 0.5f, 1.f, 0, sa_u::Pct, sa_r::Sus, 0, 3, 0, nullptr }, // 11
        { "release", "Release", 1.f, 10000.f, 200.f, 0.3f, 0, sa_u::Ms, sa_r::Rel, 0, 3, 0, nullptr }, // 12
        { "samp_retrig", "Retrig", 0.f, 1.f, 0.f, 1.f, 1, sa_u::None, sa_r::None, 0, 3, 0, kSARetrigLabels }, // 13
        { "samp_velsens", "Vel>Amp", 0.f, 1.f, 0.f, 1.f, 0, sa_u::Pct, sa_r::None, 0, 3, 0, nullptr }, // 14
    };
    static_assert(std::size(kSAParams) == SamplerMachine::kNumSlots,
                  "kSAParams row count must equal kNumSlots");

    namespace
    {
        // §6.10 contextLabel: LpStart/LpLen annotations based on loop mode.
        // kSlotLoopMode = 4 (hardcoded to avoid private-member access).
        int saLoopMode(const ParamFrame& f)
        {
            if (static_cast<int>(f.size()) <= 4) return 0;
            return std::clamp(static_cast<int>(std::round(f[4])), 0, 3);
        }

        juce::String lpStartLabel(const ParamFrame& f)
        {
            switch (saLoopMode(f))
            {
                case 0: return "LpStart";          // Off: slot exists but unused
                case 1: return "LpStart";          // Sust: user-set start
                case 2: return "LpStart";          // SustRel: user-set start
                case 3: return "LpStart (auto)";   // All: auto = window start
                default: return "LpStart";
            }
        }

        juce::String lpLenLabel(const ParamFrame& f)
        {
            switch (saLoopMode(f))
            {
                case 0: return "LpLen";            // Off: unused
                case 1: return "LpLen";            // Sust: user-set length
                case 2: return "LpLen (auto)";     // SustRel: auto = to window end
                case 3: return "LpLen (auto)";     // All: auto = full window
                default: return "LpLen";
            }
        }
    }

    ParamSpec SamplerMachine::paramSpec(int index) const
    {
        if (index < 0 || index >= kNumSlots) return {};
        auto spec = toParamSpec(kSAParams[static_cast<std::size_t>(index)]);
        if (index == kSlotLoopStart) spec.contextLabel = lpStartLabel;
        if (index == kSlotLoopLen)   spec.contextLabel = lpLenLabel;
        return spec;
    }

    SectionInfo SamplerMachine::section(int index) const
    {
        switch (index)
        {
            case 1:  return { "SRC" };
            case 3:  return { "AMP" };
            default: return {};
        }
    }
}
