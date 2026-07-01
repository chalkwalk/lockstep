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
        if (index != kSlotStart) return {};
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

    void StreamMachine::prepare(double /*sampleRate*/, int /*maxBlockSize*/)
    {
        if (!streamThread_.isThreadRunning())
            streamThread_.startThread();
        reset();
    }

    void StreamMachine::reset()
    {
        playing_ = false;
        readPos_ = 0;
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

        if (!playing_ || reader_ == nullptr) return;

        const juce::int64 remaining = lengthSamples_ - readPos_;
        const int n = static_cast<int>(std::min<juce::int64>(numSamples, std::max<juce::int64>(0, remaining)));
        if (n > 0)
            reader_->read(&buffer, 0, n, readPos_, true, true);

        readPos_ += n;
        if (n < numSamples || readPos_ >= lengthSamples_)
            playing_ = false;  // reached end of file
    }
}
