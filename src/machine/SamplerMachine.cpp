#include "SamplerMachine.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    SamplerMachine::SamplerMachine(SamplePool& pool)
        : SamplePlayingMachineBase(pool) {}

    SamplerMachine::~SamplerMachine() = default;

    // -------------------------------------------------------------------------

    SamplePlayer::Spec SamplerMachine::buildSpec(int midiNote,
                                                  const ParamFrame& params) const
    {
        const auto p = [&](int s) {
            return params[static_cast<std::size_t>(s)];
        };

        const double pitchOffset = static_cast<double>(p(kSlotPitch));
        const double semitones   = static_cast<double>(midiNote - 60) + pitchOffset;

        const int sampleIdx = static_cast<int>(p(kSlotSampleId));
        currentSampleIndex_ = sampleIdx;  // keep base updated for detectTransientSlices
        const Sample* sample = pool_.get(sampleIdx);
        const double numSrcSamples = (sample != nullptr)
            ? static_cast<double>(sample->pcm.getNumSamples())
            : 0.0;

        const double startNorm  = static_cast<double>(p(kSlotStart));
        const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
        const double winStart   = startNorm * numSrcSamples;
        const double winEnd     = std::min((startNorm + lengthNorm) * numSrcSamples,
                                           numSrcSamples);

        // Loop slots — relative to the playback window.
        const int loopModeInt  = static_cast<int>(std::round(p(kSlotLoopMode)));
        const auto loopMode    = static_cast<SamplePlayer::LoopMode>(
            std::clamp(loopModeInt, 0,
                       static_cast<int>(SamplePlayer::LoopMode::All)));

        const double loopStartNorm = static_cast<double>(p(kSlotLoopStart));
        const double loopLenNorm   = static_cast<double>(p(kSlotLoopLen));

        // Compute absolute loop bounds based on mode.
        double absLoopStart = 0.0;
        double absLoopEnd   = 0.0;
        const double windowLen = winEnd - winStart;

        switch (loopMode)
        {
        case SamplePlayer::LoopMode::Off:
            break;
        case SamplePlayer::LoopMode::Sust:
            // User-set loop_start and loop_length apply (free).
            absLoopStart = winStart + loopStartNorm * windowLen;
            absLoopEnd   = absLoopStart + loopLenNorm * windowLen;
            break;
        case SamplePlayer::LoopMode::SustAndRel:
            // loop_start user-set; loop_length auto = window_end − loop_start.
            absLoopStart = winStart + loopStartNorm * windowLen;
            absLoopEnd   = winEnd;
            break;
        case SamplePlayer::LoopMode::All:
            // Both auto: loop = full playback window.
            absLoopStart = winStart;
            absLoopEnd   = winEnd;
            break;
        }
        absLoopEnd = std::min(absLoopEnd, winEnd);

        SamplePlayer::Spec spec;
        spec.sampleIndex    = sampleIdx;
        spec.positionStart  = winStart;
        spec.windowStart    = winStart;
        spec.windowEnd      = winEnd;
        spec.rate           = std::pow(2.0, semitones / 12.0);
        spec.level          = p(kSlotLevel);
        spec.attackSamples  = msToSamples(p(kSlotAttack),  sampleRate_);
        spec.holdSamples    = msToSamples(p(kSlotHold),    sampleRate_);
        spec.decaySamples   = msToSamples(p(kSlotDecay),   sampleRate_);
        spec.sustainLevel   = p(kSlotSustain);
        spec.releaseSamples = msToSamples(p(kSlotRelease), sampleRate_);
        spec.loopStart      = absLoopStart;
        spec.loopEnd        = absLoopEnd;
        spec.loopMode       = loopMode;
        return spec;
    }

    void SamplerMachine::startVoiceAtSlice(int sliceIndex, const ParamFrame& params)
    {
        if (sliceIndex < 0 || sliceIndex >= numSlices_)
        {
            voices_[0].player.trigger(buildSpec(60, params));
            return;
        }

        const int sampleIdx = static_cast<int>(
            params[static_cast<std::size_t>(kSlotSampleId)]);
        const Sample* sample = pool_.get(sampleIdx);

        const float normPos = slicePositions_[static_cast<std::size_t>(sliceIndex)];
        const double startPos = (sample != nullptr)
            ? static_cast<double>(normPos)
              * static_cast<double>(sample->pcm.getNumSamples())
            : 0.0;

        auto spec          = buildSpec(60, params);
        spec.positionStart = startPos;
        voices_[0].player.trigger(spec);
    }

    void SamplerMachine::triggerVoice(int midiNote, const ParamFrame& params)
    {
        auto& vs = voices_[0];

        if (vs.player.isActive())
        {
            vs.pendingNote   = midiNote;
            vs.pendingParams = params;
            vs.hasPending    = true;
            if (!vs.choke.isFading())
                vs.choke.trigger();
            return;
        }

        vs.midiNote = midiNote;
        vs.age      = ++voiceCounter_;

        if (numSlices_ > 0 && midiNote >= 0 && midiNote < numSlices_)
            startVoiceAtSlice(midiNote, params);
        else
            vs.player.trigger(buildSpec(midiNote, params));
    }

    // -------------------------------------------------------------------------

    void SamplerMachine::process(const juce::MidiBuffer& events,
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

        if (!vs.player.isActive() && !vs.choke.isFading()
            && !vs.hasPending && triggerAt < 0)
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
                triggerVoice(triggerNote, params);
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
                const ParamFrame pp = vs.pendingParams;

                vs.midiNote = pn;
                vs.age      = ++voiceCounter_;

                if (numSlices_ > 0 && pn >= 0 && pn < numSlices_)
                    startVoiceAtSlice(pn, pp);
                else
                    vs.player.trigger(buildSpec(pn, pp));

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

    ParamSpec SamplerMachine::paramSpec(int index) const
    {
        using U = ParamSpec::Unit;
        using R = ParamSpec::Role;
        ParamSpec ps;
        switch (index)
        {
        // Canonical section 1 "SRC"
        case kSlotSampleId:
            ps = { "sample_id", "Sample",  0.0f,   63.0f,  0.0f, true,  U::None,      1, R::None  };
            break;
        case kSlotPitch:
            ps = { "pitch",     "Pitch",  -24.0f,  24.0f,  0.0f, false, U::Semitones, 1, R::Pitch };
            break;
        case kSlotStart:
            ps = { "samp_start",  "Start",  0.0f, 1.0f, 0.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLength:
            ps = { "samp_length", "Length", 0.0f, 1.0f, 1.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLoopMode:
        {
            static constexpr const char* kLoopLabels[] = { "OFF", "SUS", "S+R", "ALL" };
            ps = { "samp_loop_mode", "Loop",    0.0f, 3.0f, 0.0f, true, U::None, 1, R::None };
            ps.valueLabels = kLoopLabels;
            break;
        }
        case kSlotLoopStart:
            ps = { "samp_loop_start", "LpStart", 0.0f, 1.0f, 0.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLoopLen:
            ps = { "samp_loop_len", "LpLen",   0.0f, 1.0f, 1.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        // Canonical section 3 "AMP"
        case kSlotLevel:    ps = { "level",   "Level",   0.0f,    1.0f,   1.0f, false, U::Percent,   3, R::Level   }; break;
        case kSlotAttack:   ps = { "attack",  "Attack",  0.0f, 5000.0f,   2.0f, false, U::Ms,        3, R::Attack  }; break;
        case kSlotHold:     ps = { "hold",    "Hold",    0.0f, 2000.0f,   0.0f, false, U::Ms,        3, R::Hold    }; break;
        case kSlotDecay:    ps = { "decay",   "Decay",   0.0f, 5000.0f, 500.0f, false, U::Ms,        3, R::Decay   }; break;
        case kSlotSustain:  ps = { "sustain", "Sustain", 0.0f,    1.0f,   0.5f, false, U::Percent,   3, R::Sustain }; break;
        case kSlotRelease:  ps = { "release", "Release", 0.0f, 5000.0f, 200.0f, false, U::Ms,        3, R::Release }; break;
        default:            break;
        }
        return ps;
    }

    SectionInfo SamplerMachine::section(int index) const
    {
        switch (index)
        {
        case 1: return { "SRC" };
        case 3: return { "AMP" };
        default: return {};
        }
    }
}
