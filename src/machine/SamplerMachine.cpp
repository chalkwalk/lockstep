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

        SamplePlayer::Spec spec;
        spec.sampleIndex   = static_cast<int>(p(kSlotSampleId));
        spec.positionStart = 0.0;
        spec.windowEnd     = 0.0;  // 0 = full sample
        spec.rate          = std::pow(2.0, semitones / 12.0);
        spec.level         = p(kSlotLevel);
        spec.attackSamples  = msToSamples(p(kSlotAttack),  sampleRate_);
        spec.holdSamples    = msToSamples(p(kSlotHold),    sampleRate_);
        spec.decaySamples   = msToSamples(p(kSlotDecay),   sampleRate_);
        spec.sustainLevel   = p(kSlotSustain);
        spec.releaseSamples = msToSamples(p(kSlotRelease), sampleRate_);
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
        switch (index)
        {
        case kSlotSampleId: return { "sample_id", "Sample",   0.0f,    63.0f,   0.0f, true,  U::None,      1, R::None    };
        case kSlotPitch:    return { "pitch",      "Pitch",  -24.0f,   24.0f,   0.0f, false, U::Semitones, 1, R::Pitch   };
        case kSlotLevel:    return { "level",      "Level",   0.0f,     1.0f,   1.0f, false, U::Percent,   3, R::Level   };
        case kSlotAttack:   return { "attack",     "Attack",  0.0f,  5000.0f,   2.0f, false, U::Ms,        3, R::Attack  };
        case kSlotHold:     return { "hold",       "Hold",    0.0f,  2000.0f,   0.0f, false, U::Ms,        3, R::Hold    };
        case kSlotDecay:    return { "decay",      "Decay",   0.0f,  5000.0f, 500.0f, false, U::Ms,        3, R::Decay   };
        case kSlotSustain:  return { "sustain",    "Sustain", 0.0f,     1.0f,   0.5f, false, U::Percent,   3, R::Sustain };
        case kSlotRelease:  return { "release",    "Release", 0.0f,  5000.0f, 200.0f, false, U::Ms,        3, R::Release };
        default:            return {};
        }
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
