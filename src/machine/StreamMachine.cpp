#include "StreamMachine.h"
#include "StretchMath.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    StreamMachine::StreamMachine()
    {
        formatManager_.registerBasicFormats();
        streamThread_.startThread();
    }

    StreamMachine::~StreamMachine()
    {
        reader_.reset();  // release the buffering reader before stopping its thread
        streamThread_.stopThread(1000);
    }

    ParamSpec StreamMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;
        switch (index)
        {
            case kSlotSampleId:
                // Item 6: SamplePool handle. id "sample_id" makes the MZ swap the
                // rotary for the sample-picker button (ManipulationZone isSampleSlot).
                s.id = "sample_id";
                s.label = "Sample";
                s.minValue = 0.0f;
                s.maxValue = 63.0f;
                s.defaultValue = 0.0f;
                s.isStepped = true;
                return s;
            case kSlotStart:
                // Existing id — renaming would orphan saved projects.
                s.id = "start";
                s.label = "Start";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;
                s.isStepped = false;
                return s;
            case kSlotPitch:
                s.id = "player_pitch";
                s.label = "Pitch";
                s.minValue = -24.0f;
                s.maxValue = 24.0f;
                s.defaultValue = 0.0f;
                s.isStepped = true;
                s.unit = ParamSpec::Unit::Semitones;
                s.role = ParamSpec::Role::Pitch;
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
            case kSlotTimestretch:
                s.id = "player_timestretch";
                s.label = "Stretch";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 1.0f;  // Tempo (no effective BPM yet ⇒ ratio 1.0)
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kTsLabels.data(), kTsLabels.size());
                return s;
            case kSlotLoop:
                s.id = "player_loop";
                s.label = "Loop";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // Off
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kLoopLabels.data(), kLoopLabels.size());
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

    double StreamMachine::pitchRatioFor(const ParamFrame& params)
    {
        const float pitchSemis = (params.size() > kSlotPitch) ? params[kSlotPitch] : 0.0f;
        const float tuneCents = (params.size() > kSlotTune) ? params[kSlotTune] : 0.0f;
        return std::pow(2.0, (static_cast<double>(pitchSemis)
                             + static_cast<double>(tuneCents) / 100.0) / 12.0);
    }

    double StreamMachine::timeRatioFor() const
    {
        if (tsMode_ < 1) return 1.0;  // Off — native duration (still resampled)
        // Stream carries no captured bars; a per-entry effective BPM arrives in
        // 9.23 S5. Until then effBpm = 0 ⇒ ratio 1.0 (natural tempo, correctly
        // rate-converted — the missing-resample fix).
        return stretchmath::stretchTimeRatio(
            /*sourceBars*/ 0.0, /*effBpm*/ 0.0, lengthSamples_, fileRate_,
            transport_.samplesPerBar, sampleRate_);
    }

    void StreamMachine::rebuildEngine()
    {
        // Native-rate mode: build the engine at the file's rate so Bungee converts
        // file↔engine internally (this is the missing-resample fix). Called on the
        // message thread / withQuiescedEngine (setFilePath, prepare).
        const double src = fileRate_ > 0.0 ? fileRate_ : sampleRate_;
        engine_.prepare(src, sampleRate_, 2, maxBlock_);
        source_.setReader(reader_.get(), lengthSamples_, fileChannels_, src);
    }

    bool StreamMachine::setFilePath(const juce::String& path)
    {
        // Caller must have quiesced the engine: this swaps the reader the audio
        // thread reads from.
        playing_ = false;
        reader_.reset();
        lengthSamples_ = 0;
        fileRate_ = 0.0;
        fileChannels_ = 1;
        path_ = path;

        if (path.isEmpty()) { rebuildEngine(); return false; }

        std::unique_ptr<juce::AudioFormatReader> base(
            formatManager_.createReaderFor(juce::File(path)));
        if (base == nullptr) { path_ = {}; rebuildEngine(); return false; }

        lengthSamples_ = base->lengthInSamples;
        fileRate_ = base->sampleRate;
        fileChannels_ = static_cast<int>(base->numChannels);
        // ~1 s prefetch buffer on the background thread; release() — the
        // BufferingAudioReader takes ownership of the source reader.
        const int prefetch = std::max(8192, static_cast<int>(base->sampleRate));
        reader_ = std::make_unique<juce::BufferingAudioReader>(
            base.release(), streamThread_, prefetch);
        reader_->setReadTimeout(0);  // non-blocking: silence if not yet prefetched
        // Rebuild the stretch engine at the file's native rate (the resample fix).
        rebuildEngine();
        return true;
    }

    void StreamMachine::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock_ = maxBlockSize > 0 ? maxBlockSize : 512;
        // ~5 ms anti-click gate ramp.
        fadeInc_ = static_cast<float>(1.0 / (0.005 * sampleRate_));
        releaseInc_ = fadeInc_;  // until a param is seen (see process())
        if (!streamThread_.isThreadRunning())
            streamThread_.startThread();
        rebuildEngine();  // (re)build at the current engine rate + any open reader
        reset();
    }

    void StreamMachine::reset()
    {
        engine_.reset();
        playing_ = false;
        releasing_ = false;
        gain_ = 0.0f;
    }

    void StreamMachine::releaseAllVoices()
    {
        // Graceful stop: fade the stream out over player_release rather than the
        // fast note-off gate. releaseInc_ is cached in process(); the down-ramp
        // below keys off `releasing_`.
        if (playing_ || gain_ > 0.0f)
        {
            releasing_ = true;
            playing_ = false;
        }
    }

    void StreamMachine::applyLoop(bool loop)
    {
        // Loop window (9.23 S4): Tempo loops the full musical length (phase-locked
        // via the exact bars*samplesPerBar period, kept matched by per-block
        // setRatios); Off free-runs over the region from the trim point. Stream has
        // no reverse (forward prefetch). tsMode stays note-on latched.
        if (loop)
            engine_.setLoop(tsMode_ >= 1 ? 0 : startFrame_, lengthSamples_);
        else
            engine_.setLoop(0, 0);
        loopOn_ = loop;
    }

    void StreamMachine::process(const juce::MidiBuffer& events,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        buffer.clear();

        // Cache the graceful-stop release ramp from player_release (0..1 -> ~5 ms .. 2 s).
        {
            const float relT = (params.size() > kSlotRelease)
                ? juce::jlimit(0.0f, 1.0f, params[kSlotRelease]) : 0.5f;
            const double relSec = 0.005 * std::pow(400.0, static_cast<double>(relT));
            releaseInc_ = static_cast<float>(1.0 / (relSec * sampleRate_));
        }

        // Note-on starts streaming from `start`; note-off stops. (Take the last
        // event of each kind in the block — monophonic.)
        for (const auto meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const float start = (params.size() > kSlotStart)
                    ? std::clamp(params[kSlotStart], 0.0f, 1.0f) : 0.0f;
                const juce::int64 startFrame = static_cast<juce::int64>(
                    static_cast<double>(start) * static_cast<double>(lengthSamples_));
                tsMode_ = (params.size() > kSlotTimestretch)
                    ? static_cast<int>(std::lround(params[kSlotTimestretch])) : 1;
                const bool loop = (params.size() > kSlotLoop)
                    && std::lround(params[kSlotLoop]) >= 1;

                if (reader_ != nullptr && lengthSamples_ > 0)
                {
                    engine_.start(&source_, static_cast<double>(startFrame),
                                  timeRatioFor(), pitchRatioFor(params));
                    startFrame_ = startFrame;  // cache for a live loop re-latch
                    applyLoop(loop);
                    playing_ = true;
                    releasing_ = false;  // fresh note cancels an in-flight release
                }
                else
                {
                    playing_ = false;
                }
            }
            else if (msg.isNoteOff())
            {
                playing_ = false;
            }
        }

        // Keep rendering while the gate is still fading out, even after playing_
        // has gone false (note-off / end-of-file), so the tail declicks over real
        // material instead of stepping to zero.
        if (!playing_ && gain_ <= 0.0f)
        {
            gain_ = 0.0f;
            return;
        }

        // Live tempo/pitch tracking: refresh ratios each block (tempo + pitch glide).
        if (playing_)
        {
            engine_.setRatios(timeRatioFor(), pitchRatioFor(params));
            // Live loop re-latch: honour a mid-voice player_loop toggle (the
            // sustaining voice never re-fires, so the Loop param is otherwise inert).
            const bool loopNow = (params.size() > kSlotLoop)
                && std::lround(params[kSlotLoop]) >= 1;
            if (loopNow != loopOn_)
                applyLoop(loopNow);
        }

        engine_.process(buffer, 0, numSamples);

        if (!engine_.isActive())
            playing_ = false;  // end of file — fade out from here

        // Anti-click gate toward (playing ? 1 : 0). A graceful-stop release fades
        // down over releaseInc_ (slow); note-on/off and EOF use the fast gate.
        const float target = playing_ ? 1.0f : 0.0f;
        const float downInc = releasing_ ? releaseInc_ : fadeInc_;
        const int chans = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            if (gain_ < target)      gain_ = std::min(target, gain_ + fadeInc_);
            else if (gain_ > target) gain_ = std::max(target, gain_ - downInc);
            for (int ch = 0; ch < chans; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * gain_);
        }
    }
}
