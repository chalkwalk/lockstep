#include "StretchMachine.h"
#include "StretchMath.h"
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
            case kSlotTuneMode:
                s.id = "player_tune_mode";
                s.label = "A440";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // Auto — cancel the sample's detected deviation
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kTuneModeLabels.data(), kTuneModeLabels.size());
                return s;
            case kSlotRelease:
                s.id = "player_release";
                s.label = "Release";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.5f;  // ~100 ms graceful-stop fade (see process())
                s.isStepped = false;
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
        releaseInc_ = fadeInc_;  // until a param is seen (see process())
        reset();
    }

    void StretchMachine::reset()
    {
        engine_.reset();
        playing_ = false;
        releasing_ = false;
        activeNote_ = -1;
        activeSampleId_ = -1;
        playedLen_ = 0;
        gain_ = 0.0f;
    }

    void StretchMachine::releaseAllVoices()
    {
        // Graceful stop: fade the held voice out over the release time rather than
        // note-off's fast gate. releaseInc_ is cached from player_release in
        // process(); the down-ramp below keys off `releasing_`.
        if (playing_ || gain_ > 0.0f)
        {
            releasing_ = true;
            playing_ = false;
        }
    }

    double StretchMachine::pitchRatioFor(int midiNote, const ParamFrame& params) const
    {
        const float pitchSemis = (params.size() > kSlotPitch) ? params[kSlotPitch] : 0.0f;
        const float tuneCents = (params.size() > kSlotTune) ? params[kSlotTune] : 0.0f;
        // A440: Auto (default) cancels the sample's detected deviation so it plays
        // in tune; Raw leaves it as recorded. (9.23 S7.)
        const bool autoA440 = (static_cast<int>(params.size()) <= kSlotTuneMode)
            || std::lround(params[kSlotTuneMode]) == 0;
        const double a440 = autoA440 ? -pool_.effectiveTuningCents(activeSampleId_) : 0.0;
        return std::pow(2.0, (static_cast<double>(midiNote - 60)
                             + static_cast<double>(pitchSemis)
                             + (static_cast<double>(tuneCents) + a440) / 100.0) / 12.0);
    }

    double StretchMachine::timeRatioFor(int playedLen) const
    {
        if (tsMode_ < 1) return 1.0;  // Off — native duration
        const Sample* s = pool_.get(activeSampleId_);
        const double srcRate = (s != nullptr && s->sampleRate > 0.0)
            ? s->sampleRate : sampleRate_;
        // sourceBars (a captured musical length) wins; else the effective BPM
        // (user override else detected, 9.23) derives bars from the played region.
        // The shared helper cannot drift from Stream.
        return stretchmath::stretchTimeRatio(
            pool_.sourceBars(activeSampleId_), pool_.effectiveBpm(activeSampleId_),
            playedLen, srcRate, transport_.samplesPerBar, sampleRate_);
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
        // Cache the loop-window ingredients so process() can re-apply the window
        // math if player_loop is toggled while this voice sustains.
        reverse_ = reverse;
        startSample_ = startSample;
        pcmLen_ = pcmLen;

        source_.setSource(&s->pcm, s->sampleRate);
        engine_.setReverse(reverse);
        // Reverse plays from the buffer end toward 0; forward from the trim point.
        const double startPos = reverse ? static_cast<double>(pcmLen - 1)
                                        : static_cast<double>(startSample);
        engine_.start(&source_, startPos, timeRatioFor(playedLen_),
                      pitchRatioFor(midiNote, params));
        applyLoop(loop);
        playing_ = true;
        releasing_ = false;  // a fresh note cancels any in-flight graceful release
    }

    void StretchMachine::applyLoop(bool loop)
    {
        // Loop window (9.23 S4): under Tempo the window is the full musical length
        // (the whole buffer), so the output period = bars * samplesPerBar exactly
        // and a launch-quantized start stays phase-locked (per-block setRatios keeps
        // the period matched as the tempo glides — no reset). Under Off it free-runs
        // over the trimmed region at native rate. Reverse/tsMode stay note-on latched.
        if (loop)
        {
            if (tsMode_ >= 1)
                engine_.setLoop(0, pcmLen_);
            else
                engine_.setLoop(reverse_ ? 0 : startSample_, pcmLen_);
        }
        else
        {
            engine_.setLoop(0, 0);
        }
        loopOn_ = loop;
    }

    void StretchMachine::process(const juce::MidiBuffer& events,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();

        // Cache the graceful-stop release ramp from player_release (0..1 -> ~5 ms .. 2 s,
        // exponential feel). releaseAllVoices() reads this to fade the voice out.
        {
            const float relT = (params.size() > kSlotRelease)
                ? juce::jlimit(0.0f, 1.0f, params[kSlotRelease]) : 0.5f;
            const double relSec = 0.005 * std::pow(400.0, static_cast<double>(relT));
            releaseInc_ = static_cast<float>(1.0 / (relSec * sampleRate_));
        }

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
        {
            engine_.setRatios(timeRatioFor(playedLen_), pitchRatioFor(activeNote_, params));
            // Live loop re-latch: a sustaining voice never re-fires (one-shots
            // re-arm only on transport/scene launch), so honour a mid-voice
            // player_loop toggle here or the Loop param is inert.
            const bool loopNow = (params.size() > kSlotLoop)
                && std::lround(params[kSlotLoop]) >= 1;
            if (loopNow != loopOn_)
                applyLoop(loopNow);
        }

        engine_.process(buffer, 0, numSamples);

        // Anti-click gate toward (playing ? 1 : 0). A graceful-stop release fades
        // down over releaseInc_ (slow); a note-off / note-on uses the fast gate.
        const float target = playing_ ? 1.0f : 0.0f;
        const float downInc = releasing_ ? releaseInc_ : fadeInc_;
        for (int i = 0; i < numSamples; ++i)
        {
            if (gain_ < target)      gain_ = std::min(target, gain_ + fadeInc_);
            else if (gain_ > target) gain_ = std::max(target, gain_ - downInc);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * gain_);
        }

        if (!engine_.isActive() && gain_ <= 0.0f)
            playing_ = false;
    }
}
