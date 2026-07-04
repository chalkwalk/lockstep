#include "StreamMachine.h"
#include <algorithm>

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
        if (index == kSlotStart)
        {
            ParamSpec s;
            s.id = "start";
            s.label = "Start";
            s.minValue = 0.0f;
            s.maxValue = 1.0f;
            s.defaultValue = 0.0f;
            s.isStepped = false;
            s.sectionIndex = kSrcSecIdx;
            return s;
        }
        if (index == kSlotSampleId)
        {
            // Item 6: SamplePool handle. id "sample_id" makes the MZ swap the rotary
            // for the sample-picker button automatically (ManipulationZone isSampleSlot).
            ParamSpec s;
            s.id = "sample_id";
            s.label = "Sample";
            s.minValue = 0.0f;
            s.maxValue = 63.0f;
            s.defaultValue = 0.0f;
            s.isStepped = true;
            s.sectionIndex = kSrcSecIdx;
            return s;
        }
        return {};
    }

    bool StreamMachine::setFilePath(const juce::String& path)
    {
        // Caller must have quiesced the engine: this swaps the reader the audio
        // thread reads from.
        playing_ = false;
        readPos_ = 0;
        reader_.reset();
        lengthSamples_ = 0;
        path_ = path;

        if (path.isEmpty()) return false;

        std::unique_ptr<juce::AudioFormatReader> base(
            formatManager_.createReaderFor(juce::File(path)));
        if (base == nullptr) { path_ = {}; return false; }

        lengthSamples_ = base->lengthInSamples;
        // ~1 s prefetch buffer on the background thread; release() — the
        // BufferingAudioReader takes ownership of the source reader.
        const int prefetch = std::max(8192, static_cast<int>(base->sampleRate));
        reader_ = std::make_unique<juce::BufferingAudioReader>(
            base.release(), streamThread_, prefetch);
        reader_->setReadTimeout(0);  // non-blocking: silence if not yet prefetched
        return true;
    }

    void StreamMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        // ~5 ms anti-click gate ramp.
        fadeInc_ = static_cast<float>(1.0 / (0.005 * sampleRate_));
        if (!streamThread_.isThreadRunning())
            streamThread_.startThread();
        reset();
    }

    void StreamMachine::reset()
    {
        playing_ = false;
        readPos_ = 0;
        gain_ = 0.0f;
    }

    void StreamMachine::process(const juce::MidiBuffer& events,
                                const ParamFrame& params,
                                juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        buffer.clear();

        // Note-on starts streaming from `start`; note-off stops. (Take the last
        // event of each kind in the block — monophonic.)
        for (const auto meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const float start = (params.size() > kSlotStart)
                    ? std::clamp(params[kSlotStart], 0.0f, 1.0f) : 0.0f;
                readPos_ = static_cast<juce::int64>(
                    static_cast<double>(start) * static_cast<double>(lengthSamples_));
                playing_ = (reader_ != nullptr && lengthSamples_ > 0);
            }
            else if (msg.isNoteOff())
            {
                playing_ = false;
            }
        }

        // Keep rendering while the gate is still fading out, even after playing_
        // has gone false (note-off / end-of-file), so the tail declicks over real
        // material instead of stepping to zero.
        const bool active = playing_ || gain_ > 0.0f;
        if (!active || reader_ == nullptr)
        {
            gain_ = 0.0f;
            return;
        }

        // Read forward from readPos_ regardless of playing_ so a note-off fade-out
        // still plays a few ms of real audio (playing_ only sets the gate target).
        const juce::int64 remaining = lengthSamples_ - readPos_;
        const int n = static_cast<int>(
            std::min<juce::int64>(numSamples, std::max<juce::int64>(0, remaining)));
        if (n > 0)
            reader_->read(&buffer, 0, n, readPos_, true, true);

        readPos_ += n;
        if (n < numSamples || readPos_ >= lengthSamples_)
            playing_ = false;  // reached end of file — fade out from here

        // Anti-click gate toward (playing ? 1 : 0), ramped over ~5 ms.
        const float target = playing_ ? 1.0f : 0.0f;
        const int chans = buffer.getNumChannels();
        for (int i = 0; i < numSamples; ++i)
        {
            if (gain_ < target)      gain_ = std::min(target, gain_ + fadeInc_);
            else if (gain_ > target) gain_ = std::max(target, gain_ - fadeInc_);
            for (int ch = 0; ch < chans; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * gain_);
        }
    }
}
