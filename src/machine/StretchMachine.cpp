#include "StretchMachine.h"
#include <cmath>

namespace lockstep
{
    ParamSpec StretchMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;
        switch (index)
        {
            case kSlotSampleId:
                s.id = "sample_id";  // shares the pool-clamp + sample-picker path
                s.label = "Sample";
                s.minValue = 0.0f;
                s.maxValue = 127.0f;  // clamped to pool size at write time
                s.defaultValue = 0.0f;
                s.isStepped = true;
                return s;
            case kSlotPitch:
                s.id = "player_pitch";
                s.label = "Pitch";
                s.minValue = -24.0f;
                s.maxValue = 24.0f;
                s.defaultValue = 0.0f;
                s.isStepped = true;
                s.unit = ParamSpec::Unit::Semitones;
                return s;
            case kSlotTimestretch:
                s.id = "player_timestretch";
                s.label = "Stretch";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 1.0f;  // Tempo — track the project tempo by default
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kTsLabels.data(), kTsLabels.size());
                return s;
            case kSlotStart:
                s.id = "player_start";
                s.label = "Start";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;
                s.isStepped = false;
                return s;
            case kSlotTune:
                s.id = "player_tune";
                s.label = "Tune";
                s.minValue = -50.0f;
                s.maxValue = 50.0f;
                s.defaultValue = 0.0f;
                s.isStepped = false;
                s.unit = ParamSpec::Unit::Cents;
                s.role = ParamSpec::Role::Pitch;
                return s;
            case kSlotLoop:
                s.id = "player_loop";
                s.label = "Loop";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // Off — old tracks keep retrig-per-cycle
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kLoopLabels.data(), kLoopLabels.size());
                return s;
            case kSlotReverse:
                s.id = "player_reverse";
                s.label = "Rev";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // Fwd
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kRevLabels.data(), kRevLabels.size());
                return s;
            default:
                return {};
        }
    }

    void StretchMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock_ = maxBlockSize > 0 ? maxBlockSize : 512;
        // Fold mode (sourceRate <= 0): the engine is rate-agnostic and folds each
        // source's own rate into speed/pitch, so a p-locked sample_id can select a
        // different-rate buffer on the audio thread without reconstruction.
        engine_.prepare(-1.0, sampleRate_, 2, maxBlock_);
        // ~5 ms anti-click gate ramp.
        fadeInc_ = static_cast<float>(1.0 / (0.005 * sampleRate_));
        reset();
    }

    void StretchMachine::reset()
    {
        engine_.reset();
        playing_ = false;
        activeNote_ = -1;
        activeSampleId_ = -1;
        playedLen_ = 0;
        gain_ = 0.0f;
    }

    double StretchMachine::pitchRatioFor(int midiNote, const ParamFrame& params) const
    {
        const float pitchSemis = (params.size() > kSlotPitch) ? params[kSlotPitch] : 0.0f;
        const float tuneCents = (params.size() > kSlotTune) ? params[kSlotTune] : 0.0f;
        return std::pow(2.0, (static_cast<double>(midiNote - 60)
                             + static_cast<double>(pitchSemis)
                             + static_cast<double>(tuneCents) / 100.0) / 12.0);
    }

    double StretchMachine::timeRatioFor(int playedLen) const
    {
        if (tsMode_ < 1) return 1.0;  // Off — native duration
        const double spb = transport_.samplesPerBar;
        double bars = pool_.sourceBars(activeSampleId_);
        if (bars <= 0.0)
        {
            // No stamped musical length (a disk loop, not a captured buffer):
            // fall back to the auto-detected tempo. The played region spans
            // playedLen source samples; at the detected BPM that is this many
            // bars (4/4 assumed). Feeds the same stretch formula below, so a
            // detected loop tracks project tempo exactly like a recorded one.
            const double bpm = pool_.detectedBpm(activeSampleId_);
            const Sample* s = pool_.get(activeSampleId_);
            if (bpm > 0.0 && s != nullptr && s->sampleRate > 0.0)
            {
                constexpr double kBeatsPerBar = 4.0;  // 4/4 assumption
                const double srcSeconds = static_cast<double>(playedLen) / s->sampleRate;
                bars = srcSeconds * bpm / 60.0 / kBeatsPerBar;
            }
        }
        if (bars <= 0.0 || spb <= 0.0 || playedLen <= 0)
            return 1.0;  // unknown source tempo → no tracking
        // Play `bars` bars over the played region at the project tempo.
        return (bars * spb) / static_cast<double>(playedLen);
    }

    void StretchMachine::startNote(int midiNote, const ParamFrame& params)
    {
        const int sampleId = (params.size() > kSlotSampleId)
            ? static_cast<int>(std::lround(params[kSlotSampleId])) : 0;
        const Sample* s = pool_.get(sampleId);
        if (s == nullptr || s->pcm.getNumSamples() <= 0)
        {
            playing_ = false;
            return;
        }

        const float startNorm = (params.size() > kSlotStart)
            ? juce::jlimit(0.0f, 1.0f, params[kSlotStart]) : 0.0f;
        tsMode_ = (params.size() > kSlotTimestretch)
            ? static_cast<int>(std::lround(params[kSlotTimestretch])) : 1;
        const bool loop = (params.size() > kSlotLoop)
            && std::lround(params[kSlotLoop]) >= 1;
        const bool reverse = (params.size() > kSlotReverse)
            && std::lround(params[kSlotReverse]) >= 1;

        const int pcmLen = s->pcm.getNumSamples();
        const int startSample = std::min(pcmLen - 1,
                                         static_cast<int>(startNorm * static_cast<float>(pcmLen)));
        activeSampleId_ = sampleId;
        playedLen_ = std::max(1, reverse ? pcmLen : (pcmLen - startSample));
        activeNote_ = midiNote;

        source_.setSource(&s->pcm, s->sampleRate);
        engine_.setReverse(reverse);
        // Reverse plays from the buffer end toward 0; forward from the trim point.
        const double startPos = reverse ? static_cast<double>(pcmLen - 1)
                                        : static_cast<double>(startSample);
        engine_.start(&source_, startPos, timeRatioFor(playedLen_),
                      pitchRatioFor(midiNote, params));
        // Basic free-run loop over the played region; the Tempo-phase-locked
        // window + autoFit seeding land in Stage 4.
        if (loop)
            engine_.setLoop(reverse ? 0 : startSample, pcmLen);
        else
            engine_.setLoop(0, 0);
        playing_ = true;
    }

    void StretchMachine::process(const juce::MidiBuffer& events,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();

        // Apply note edges at block granularity (MVP): last note-on wins; a note-off
        // for the active note gates the voice out.
        for (const auto meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                startNote(msg.getNoteNumber(), params);
            else if (msg.isNoteOff() && msg.getNoteNumber() == activeNote_)
                playing_ = false;
        }

        if (!playing_ && gain_ <= 0.0f)
        {
            engine_.reset();
            buffer.clear();
            return;
        }

        // Live tempo tracking: refresh the stretch ratio each block (tempo glide).
        if (playing_)
            engine_.setRatios(timeRatioFor(playedLen_), pitchRatioFor(activeNote_, params));

        engine_.process(buffer, 0, numSamples);

        // Anti-click gate toward (playing ? 1 : 0).
        const float target = playing_ ? 1.0f : 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (gain_ < target)      gain_ = std::min(target, gain_ + fadeInc_);
            else if (gain_ > target) gain_ = std::max(target, gain_ - fadeInc_);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * gain_);
        }

        if (!engine_.isActive() && gain_ <= 0.0f)
            playing_ = false;
    }
}
