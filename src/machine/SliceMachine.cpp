#include "SliceMachine.h"
#include "MachineParamTable.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    SliceMachine::SliceMachine(SamplePool& pool)
        : SamplePlayingMachineBase(pool) {}

    SliceMachine::~SliceMachine() = default;

    // -------------------------------------------------------------------------

    SliceMachine::Polyphony SliceMachine::currentVoices(const ParamFrame& params) const
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

    SamplePlayer::Spec SliceMachine::buildSpec(int midiNote,
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

        const int modeInt = static_cast<int>(std::round(p(kSlotMode)));
        const bool isSlice = (modeInt == 0);
        const double rateParam = static_cast<double>(p(kSlotRate));
        // In SLICE mode the note selects the slice; pitch is fixed by kSlotPitch only.
        // In SCRUB mode the note transposes normally.
        // 9.23 S7 — fine-tune (cents) + A440: Auto cancels the sample's detected
        // deviation from A440; Raw leaves it as recorded. Folds into the rate.
        const double tuneCents = (params.size() > static_cast<std::size_t>(kSlotTune))
            ? static_cast<double>(p(kSlotTune)) : 0.0;
        const bool autoA440 = (params.size() <= static_cast<std::size_t>(kSlotTuneMode))
            || std::lround(p(kSlotTuneMode)) == 0;   // default (absent) = Auto
        const double effCents = tuneCents
            + (autoA440 ? -pool_.effectiveTuningCents(sampleIdx) : 0.0);
        const double pitchSemis = (isSlice ? 0.0 : static_cast<double>(midiNote - 60))
            + static_cast<double>(p(kSlotPitch)) + effCents / 100.0;

        // Determine the playback window.  In SLICE mode the note selects a slice
        // and start/length are relative to that slice.  In SCRUB mode they are
        // relative to the full sample.
        double winStart = 0.0;
        double winEnd = numSrcSamples;

        if (isSlice && numSlices_ > 0)
        {
            // Map note chromatically from root (60): note 60 = slice 0, 61 = slice 1, etc.
            const int rel = midiNote - 60;
            const int sliceIdx = ((rel % numSlices_) + numSlices_) % numSlices_;
            const double sliceStart = static_cast<double>(
                                          slicePositions_[static_cast<std::size_t>(sliceIdx)]) *
                                      numSrcSamples;
            const double sliceEnd = (sliceIdx + 1 < numSlices_)
                                        ? static_cast<double>(slicePositions_[static_cast<std::size_t>(sliceIdx + 1)]) * numSrcSamples
                                        : numSrcSamples;
            const double sliceLen = sliceEnd - sliceStart;

            const double startNorm = static_cast<double>(p(kSlotStart));
            const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
            winStart = sliceStart + startNorm * sliceLen;
            winEnd = std::min(sliceStart + (startNorm + lengthNorm) * sliceLen, sliceEnd);
        }
        else
        {
            const double startNorm = static_cast<double>(p(kSlotStart));
            const double lengthNorm = std::max(0.001, static_cast<double>(p(kSlotLength)));
            winStart = startNorm * numSrcSamples;
            winEnd = std::min((startNorm + lengthNorm) * numSrcSamples, numSrcSamples);
        }

        // Loop region.
        const int loopModeInt = static_cast<int>(std::round(p(kSlotLoopMode)));
        const auto loopMode = static_cast<SamplePlayer::LoopMode>(
            std::clamp(loopModeInt, 0,
                       static_cast<int>(SamplePlayer::LoopMode::All)));

        const double loopStartNorm = static_cast<double>(p(kSlotLoopStart));
        const double loopLenNorm = static_cast<double>(p(kSlotLoopLen));
        const double windowLen = winEnd - winStart;

        double absLoopStart = 0.0;
        double absLoopEnd = 0.0;

        switch (loopMode)
        {
            case SamplePlayer::LoopMode::Off:
                break;
            case SamplePlayer::LoopMode::Sust:
                absLoopStart = winStart + loopStartNorm * windowLen;
                absLoopEnd = absLoopStart + loopLenNorm * windowLen;
                break;
            case SamplePlayer::LoopMode::SustAndRel:
                absLoopStart = winStart + loopStartNorm * windowLen;
                absLoopEnd = winEnd;
                break;
            case SamplePlayer::LoopMode::All:
                absLoopStart = winStart;
                absLoopEnd = winEnd;
                break;
        }
        absLoopEnd = std::min(absLoopEnd, winEnd);

        const float fadeSamples = static_cast<float>(
            msToSamples(p(kSlotFade), sampleRate_));

        SamplePlayer::Spec spec;
        spec.sampleIndex = sampleIdx;
        spec.positionStart = winStart;
        spec.windowStart = winStart;
        spec.windowEnd = winEnd;
        // R2 sample-rate correctness: fold fileRate/engineRate into the read rate
        // so an off-rate slice plays at the right pitch/length (slice positions are
        // file-sample based and unaffected — see SampleMachine::buildSpec).
        const double fileRate = (sample != nullptr && sample->sampleRate > 0.0)
                                    ? sample->sampleRate : sampleRate_;
        const double rateComp = (sampleRate_ > 0.0) ? fileRate / sampleRate_ : 1.0;
        spec.rate = rateParam * std::pow(2.0, pitchSemis / 12.0) * rateComp;
        spec.level = 1.0f;
        spec.attackSamples = static_cast<int>(fadeSamples);
        spec.holdSamples = 0;
        spec.decaySamples = 0;
        spec.sustainLevel = 1.0f;
        spec.releaseSamples = static_cast<int>(fadeSamples);
        spec.loopStart = absLoopStart;
        spec.loopEnd = absLoopEnd;
        spec.loopMode = loopMode;
        return spec;
    }

    void SliceMachine::triggerVoice(int midiNote, const ParamFrame& params)
    {
        // Lazy-seed slices if not yet initialised (e.g. after state load).
        if (numSlices_ == 0)
        {
            currentSampleIndex_ = static_cast<int>(
                params[static_cast<std::size_t>(kSlotSampleId)]);
            const int src = static_cast<int>(std::round(
                params[static_cast<std::size_t>(kSlotSliceSrc)]));
            const int count = static_cast<int>(std::round(
                params[static_cast<std::size_t>(kSlotSliceCount)]));
            if (src == 2)
                detectSyncSlices(count);
            else if (src == 1)
                detectTransientSlices(count);
            else
                setEqualSlices(count);
        }

        // Choke any released-but-still-running voices so they don't overlap the new note.
        for (auto& zv : voices_)
        {
            if (zv.player.isActive() && zv.midiNote < 0 && !zv.hasPending && !zv.choke.isFading())
            {
                zv.hasPending = true;
                zv.pendingNote = -1;  // sentinel: kill voice after choke completes
                zv.choke.trigger();
            }
        }

        const int vi = allocVoice();
        auto& vs = voices_[static_cast<std::size_t>(vi)];

        if (vs.player.isActive())
        {
            vs.pendingNote = midiNote;
            vs.pendingParams = params;
            vs.hasPending = true;
            if (!vs.choke.isFading())
                vs.choke.trigger();
            return;
        }

        vs.midiNote = midiNote;
        vs.age = ++voiceCounter_;
        vs.player.trigger(buildSpec(midiNote, params));
    }

    // -------------------------------------------------------------------------

    void SliceMachine::process(const juce::MidiBuffer& events,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        int triggerAt = -1;
        int triggerNote = 60;
        int releaseAt = -1;
        int releaseNote = -1;

        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                triggerAt = meta.samplePosition;
                triggerNote = msg.getNoteNumber();
            }
            else if (msg.isNoteOff() && releaseAt < 0)
            {
                releaseAt = meta.samplePosition;
                releaseNote = msg.getNoteNumber();
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
                const int vi = findVoiceByNote(releaseNote);
                if (vi >= 0)
                {
                    // Keep the voice producing audio so the AMP release envelope
                    // has something to fade. The processor resets the machine when
                    // the AMP goes Idle.
                    voices_[static_cast<std::size_t>(vi)].midiNote = -1;
                }
                releaseAt = -1;
                releaseNote = -1;
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
                    if (vs.pendingNote < 0)
                    {
                        vs.player = SamplePlayer{};  // kill zombie voice after choke
                        continue;
                    }
                    const int pn = vs.pendingNote;
                    const ParamFrame pp = vs.pendingParams;
                    vs.midiNote = pn;
                    vs.age = ++voiceCounter_;
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

    namespace sl_u
    {
        static constexpr uint8_t None = 0, Ms = 1, Semi = 2, Cents = 4;
    }
    namespace sl_r
    {
        static constexpr uint8_t None = 0, Pitch = 1;
    }

    namespace
    {
        static constexpr const char* kSLModeLabels[] = { "SLICE", "SCRUB", nullptr };
        static constexpr const char* kSLSrcLabels[] = { "EQUAL", "TRANS", "SYNC", nullptr };
        // 4.9 SYNC: when slice_src == SYNC the Count slot names a clock division
        // (parallel to kSyncBeatsPerSlice). Values 8..16 clamp to 1/16.
        static constexpr const char* kSyncDivLabels[] =
            { "4bar", "2bar", "1bar", "1/2", "1/4", "1/8", "1/16" };
        static_assert(std::size(kSyncDivLabels) == static_cast<std::size_t>(kNumSyncDivisions),
                      "kSyncDivLabels must match kSyncBeatsPerSlice");
        static constexpr const char* kSLLoopLabels[] = { "OFF", "SUS", "SUS+REL", "ALL", nullptr };
        static constexpr const char* kSLVoiceLabels[] = { "MONO", "POLY", nullptr };
        static constexpr const char* kSLTuneModeLabels[] = { "Auto", "Raw", nullptr };
    }

    // { id, label, min, max, def, skew, stepped, unit, role, variant, section, zcSnap, labels }
    static constexpr ParamRow kSLParams[] = {
        // --- SRC (section 1) ---
        { "slicer_sample_id", "Sample", 0.f, 63.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, nullptr }, //  0
        { "slicer_mode", "Mode", 0.f, 1.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, kSLModeLabels }, //  1
        { "slicer_slice_src", "Slices", 0.f, 2.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, kSLSrcLabels }, //  2
        { "slicer_slice_count", "Count", 1.f, 16.f, 8.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, nullptr }, //  3
        { "slicer_rate", "Rate", -2.f, 2.f, 1.f, 1.f, 0, sl_u::None, sl_r::None, 0, 1, 0, nullptr }, //  4
        { "slicer_start", "Start", 0.f, 1.f, 0.f, 1.f, 0, sl_u::None, sl_r::None, 0, 1, 1, nullptr }, //  5  zcSnap
        { "slicer_length", "Length", 0.f, 1.f, 1.f, 1.f, 0, sl_u::None, sl_r::None, 0, 1, 1, nullptr }, //  6  zcSnap
        { "slicer_loop_mode", "Loop", 0.f, 3.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, kSLLoopLabels }, //  7
        { "slicer_loop_start", "LpStart", 0.f, 1.f, 0.f, 1.f, 0, sl_u::None, sl_r::None, 0, 1, 1, nullptr }, //  8  zcSnap
        { "slicer_loop_len", "LpLen", 0.f, 1.f, 1.f, 1.f, 0, sl_u::None, sl_r::None, 0, 1, 1, nullptr }, //  9  zcSnap
        { "slicer_pitch", "Pitch", -24.f, 24.f, 0.f, 1.f, 0, sl_u::Semi, sl_r::Pitch, 0, 1, 0, nullptr }, // 10
        // --- VOICE (section 6) ---
        { "slicer_voice_mode", "Voice", 0.f, 1.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 6, 0, kSLVoiceLabels }, // 11
        { "slicer_fade", "Fade", 0.f, 20.f, 1.f, 1.f, 0, sl_u::Ms, sl_r::None, 0, 6, 0, nullptr }, // 12
        // 9.23 S7 — fine-tune + A440 mode (default Auto cancels the detected deviation).
        { "slicer_tune", "Tune", -50.f, 50.f, 0.f, 1.f, 0, sl_u::Cents, sl_r::Pitch, 0, 1, 0, nullptr }, // 13
        { "slicer_tune_mode", "A440", 0.f, 1.f, 0.f, 1.f, 1, sl_u::None, sl_r::None, 0, 1, 0, kSLTuneModeLabels }, // 14
    };
    static_assert(std::size(kSLParams) == SliceMachine::kNumSlots,
                  "kSLParams row count must equal kNumSlots");

    namespace
    {
        // §6.10 contextLabel: LpStart/LpLen annotations based on loop mode.
        // kSlotLoopMode = 7 (hardcoded to avoid private-member access).
        int slLoopMode(const ParamFrame& f)
        {
            if (static_cast<int>(f.size()) <= 7) return 0;
            return std::clamp(static_cast<int>(std::round(f[7])), 0, 3);
        }

        juce::String slLpStartLabel(const ParamFrame& f)
        {
            return (slLoopMode(f) == 3) ? juce::String("LpStart (auto)") : juce::String("LpStart");
        }

        juce::String slLpLenLabel(const ParamFrame& f)
        {
            const int m = slLoopMode(f);
            return (m == 2 || m == 3) ? juce::String("LpLen (auto)") : juce::String("LpLen");
        }

        // 4.9: when Slices == SYNC (2), the Count slot is a clock division; show
        // the effective division ("Div 1/4") so the MZ reads the beat grid, not
        // a raw count. kSlotSliceSrc = 2, kSlotSliceCount = 3 (hardcoded to avoid
        // private-member access, mirroring slLoopMode).
        juce::String slCountLabel(const ParamFrame& f)
        {
            if (static_cast<int>(f.size()) <= 3) return "Count";
            const int src = std::clamp(static_cast<int>(std::round(f[2])), 0, 2);
            if (src != 2) return "Count";
            const int div = std::clamp(static_cast<int>(std::round(f[3])), 1, kNumSyncDivisions) - 1;
            return juce::String("Div ") + kSyncDivLabels[static_cast<std::size_t>(div)];
        }
    }

    ParamSpec SliceMachine::paramSpec(int index) const
    {
        if (index < 0 || index >= kNumSlots) return {};
        auto spec = toParamSpec(kSLParams[static_cast<std::size_t>(index)]);
        if (index == kSlotLoopStart)  spec.contextLabel = slLpStartLabel;
        if (index == kSlotLoopLen)    spec.contextLabel = slLpLenLabel;
        if (index == kSlotSliceCount) spec.contextLabel = slCountLabel;
        return spec;
    }

    SectionInfo SliceMachine::section(int index) const
    {
        switch (index)
        {
            case 1:  return { "SRC" };
            case 6:  return { "VOICE" };
            default: return {};
        }
    }
}
