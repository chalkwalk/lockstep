#include "SlicerMachine.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    SlicerMachine::SlicerMachine(SamplePool& pool)
        : SamplePlayingMachineBase(pool) {}

    SlicerMachine::~SlicerMachine() = default;

    // -------------------------------------------------------------------------

    SlicerMachine::Polyphony SlicerMachine::currentVoices(const ParamFrame& params) const
    {
        if (params.size() > static_cast<std::size_t>(kSlotVoiceMode))
        {
            const int vm = static_cast<int>(std::round(params[static_cast<std::size_t>(kSlotVoiceMode)]));
            if (vm >= 1)
                return Polyphony::V4;
        }
        return Polyphony::V1;
    }

    // -------------------------------------------------------------------------

    SamplePlayer::Spec SlicerMachine::buildSpec(int midiNote,
                                                 const ParamFrame& params) const
    {
        const auto p = [&](int s) {
            return params[static_cast<std::size_t>(s)];
        };

        const int sampleIdx = static_cast<int>(p(kSlotSampleId));
        currentSampleIndex_ = sampleIdx;
        const Sample* sample = pool_.get(sampleIdx);
        const double numSrcSamples = (sample != nullptr)
            ? static_cast<double>(sample->pcm.getNumSamples())
            : 0.0;

        const int    modeInt   = static_cast<int>(std::round(p(kSlotMode)));
        const bool   isSlice   = (modeInt == 0);
        const double rateParam = static_cast<double>(p(kSlotRate));
        const double pitchSemis = static_cast<double>(midiNote - 60)
                                + static_cast<double>(p(kSlotPitch));

        // Determine the playback window.  In SLICE mode the note selects a slice
        // and start/length are relative to that slice.  In SCRUB mode they are
        // relative to the full sample.
        double winStart = 0.0;
        double winEnd   = numSrcSamples;

        if (isSlice && numSlices_ > 0)
        {
            const int sliceIdx = std::clamp(midiNote, 0, numSlices_ - 1);
            const double sliceStart = static_cast<double>(
                slicePositions_[static_cast<std::size_t>(sliceIdx)]) * numSrcSamples;
            const double sliceEnd   = (sliceIdx + 1 < numSlices_)
                ? static_cast<double>(slicePositions_[static_cast<std::size_t>(sliceIdx + 1)]) * numSrcSamples
                : numSrcSamples;
            const double sliceLen   = sliceEnd - sliceStart;

            const double startNorm  = static_cast<double>(p(kSlotStart));
            const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
            winStart = sliceStart + startNorm  * sliceLen;
            winEnd   = std::min(sliceStart + (startNorm + lengthNorm) * sliceLen, sliceEnd);
        }
        else
        {
            const double startNorm  = static_cast<double>(p(kSlotStart));
            const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
            winStart = startNorm * numSrcSamples;
            winEnd   = std::min((startNorm + lengthNorm) * numSrcSamples, numSrcSamples);
        }

        // Loop region.
        const int loopModeInt = static_cast<int>(std::round(p(kSlotLoopMode)));
        const auto loopMode   = static_cast<SamplePlayer::LoopMode>(
            std::clamp(loopModeInt, 0,
                       static_cast<int>(SamplePlayer::LoopMode::All)));

        const double loopStartNorm = static_cast<double>(p(kSlotLoopStart));
        const double loopLenNorm   = static_cast<double>(p(kSlotLoopLen));
        const double windowLen     = winEnd - winStart;

        double absLoopStart = 0.0;
        double absLoopEnd   = 0.0;

        switch (loopMode)
        {
        case SamplePlayer::LoopMode::Off:
            break;
        case SamplePlayer::LoopMode::Sust:
            absLoopStart = winStart + loopStartNorm * windowLen;
            absLoopEnd   = absLoopStart + loopLenNorm * windowLen;
            break;
        case SamplePlayer::LoopMode::SustAndRel:
            absLoopStart = winStart + loopStartNorm * windowLen;
            absLoopEnd   = winEnd;
            break;
        case SamplePlayer::LoopMode::All:
            absLoopStart = winStart;
            absLoopEnd   = winEnd;
            break;
        }
        absLoopEnd = std::min(absLoopEnd, winEnd);

        const float fadeSamples = static_cast<float>(
            msToSamples(p(kSlotFade), sampleRate_));

        SamplePlayer::Spec spec;
        spec.sampleIndex    = sampleIdx;
        spec.positionStart  = winStart;
        spec.windowEnd      = winEnd;
        spec.rate           = rateParam * std::pow(2.0, pitchSemis / 12.0);
        spec.level          = 1.0f;
        spec.attackSamples  = static_cast<int>(fadeSamples);
        spec.holdSamples    = 0;
        spec.decaySamples   = 0;
        spec.sustainLevel   = 1.0f;
        spec.releaseSamples = static_cast<int>(fadeSamples);
        spec.loopStart      = absLoopStart;
        spec.loopEnd        = absLoopEnd;
        spec.loopMode       = loopMode;
        return spec;
    }

    void SlicerMachine::triggerVoice(int midiNote, const ParamFrame& params)
    {
        const int vi = allocVoice();
        auto& vs = voices_[static_cast<std::size_t>(vi)];

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
        vs.player.trigger(buildSpec(midiNote, params));
    }

    // -------------------------------------------------------------------------

    void SlicerMachine::process(const juce::MidiBuffer& events,
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

        // Check if any voice is active.
        bool anyActive = false;
        for (const auto& vs : voices_)
            anyActive |= (vs.player.isActive() || vs.choke.isFading() || vs.hasPending);

        if (!anyActive && triggerAt < 0)
            return;

        const int numOut = buffer.getNumChannels();

        for (int i = 0; i < numBlockSamples; ++i)
        {
            if (releaseAt >= 0 && i == releaseAt)
            {
                for (auto& vs : voices_)
                    if (vs.player.isActive())
                        vs.player.release();
                releaseAt = -1;
            }

            if (triggerAt >= 0 && i == triggerAt)
            {
                triggerVoice(triggerNote, params);
                triggerAt = -1;
            }

            float mixed = 0.0f;
            for (auto& vs : voices_)
            {
                const float chokeGain = vs.choke.isFading() ? vs.choke.nextGain() : 1.0f;

                if (!vs.choke.isFading() && vs.hasPending)
                {
                    vs.hasPending = false;
                    const int pn        = vs.pendingNote;
                    const ParamFrame pp = vs.pendingParams;
                    vs.midiNote = pn;
                    vs.age      = ++voiceCounter_;
                    vs.player.trigger(buildSpec(pn, pp));
                }

                if (!vs.player.isActive())
                    continue;

                const Sample* sample = pool_.get(vs.player.sampleIndex);
                if (sample == nullptr)
                {
                    vs.player = SamplePlayer{};
                    continue;
                }

                mixed += vs.player.step(sample->pcm) * chokeGain;
            }

            for (int ch = 0; ch < numOut; ++ch)
                buffer.addSample(ch, i, mixed);
        }
    }

    // -------------------------------------------------------------------------

    ParamSpec SlicerMachine::paramSpec(int index) const
    {
        using U = ParamSpec::Unit;
        using R = ParamSpec::Role;
        ParamSpec ps;
        switch (index)
        {
        case kSlotSampleId:
        {
            ps = { "slicer_sample_id", "Sample", 0.0f, 63.0f, 0.0f, true, U::None, 1, R::None };
            break;
        }
        case kSlotMode:
        {
            static constexpr const char* kModeLabels[] = { "SLICE", "SCRUB" };
            ps = { "slicer_mode", "Mode", 0.0f, 1.0f, 0.0f, true, U::None, 1, R::None };
            ps.valueLabels = kModeLabels;
            break;
        }
        case kSlotSliceSrc:
        {
            static constexpr const char* kSrcLabels[] = { "EQUAL", "TRANS" };
            ps = { "slicer_slice_src", "Slices", 0.0f, 1.0f, 0.0f, true, U::None, 1, R::None };
            ps.valueLabels = kSrcLabels;
            break;
        }
        case kSlotSliceCount:
            ps = { "slicer_slice_count", "Count", 1.0f, 16.0f, 8.0f, true, U::None, 1, R::None };
            break;
        case kSlotRate:
            ps = { "slicer_rate", "Rate", -2.0f, 2.0f, 1.0f, false, U::None, 1, R::None };
            break;
        case kSlotStart:
            ps = { "slicer_start", "Start", 0.0f, 1.0f, 0.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLength:
            ps = { "slicer_length", "Length", 0.0f, 1.0f, 1.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLoopMode:
        {
            static constexpr const char* kLoopLabels[] = { "OFF", "SUS", "S+R", "ALL" };
            ps = { "slicer_loop_mode", "Loop", 0.0f, 3.0f, 0.0f, true, U::None, 1, R::None };
            ps.valueLabels = kLoopLabels;
            break;
        }
        case kSlotLoopStart:
            ps = { "slicer_loop_start", "LpStart", 0.0f, 1.0f, 0.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotLoopLen:
            ps = { "slicer_loop_len", "LpLen", 0.0f, 1.0f, 1.0f, false, U::None, 1, R::None };
            ps.zeroCrossingSnap = true;
            break;
        case kSlotPitch:
            ps = { "slicer_pitch", "Pitch", -24.0f, 24.0f, 0.0f, false, U::Semitones, 1, R::Pitch };
            break;
        case kSlotVoiceMode:
        {
            static constexpr const char* kVoiceLabels[] = { "MONO", "POLY" };
            ps = { "slicer_voice_mode", "Voice", 0.0f, 1.0f, 0.0f, true, U::None, 6, R::None };
            ps.valueLabels = kVoiceLabels;
            break;
        }
        case kSlotFade:
            ps = { "slicer_fade", "Fade", 0.0f, 20.0f, 1.0f, false, U::Ms, 6, R::None };
            break;
        default:
            break;
        }
        return ps;
    }

    SectionInfo SlicerMachine::section(int index) const
    {
        switch (index)
        {
        case 1: return { "SRC" };
        case 6: return { "VOICE" };
        default: return {};
        }
    }
}
