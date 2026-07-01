#include "RecordMachine.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    ParamSpec RecordMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;
        switch (index)
        {
            case kSlotInputSource:
                s.id = kInputSourceSlotId;
                s.label = "Source";
                s.minValue = 0.0f;
                s.maxValue = kInputSourceMaxValue;  // None/Ext/Master/Track N (DESIGN §27)
                s.defaultValue = 1.0f;  // External — the common live-resample tap
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                             kInputSourceLabels.size());
                return s;
            case kSlotTargetBuffer:
                s.id = "target_buffer";
                s.label = "Buffer";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kVolatileBufferLabels.size() - 1);
                s.defaultValue = 0.0f;
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kVolatileBufferLabels.data(),
                                                             kVolatileBufferLabels.size());
                return s;
            case kSlotRecLength:
                s.id = "rec_length";
                s.label = "Length";
                s.minValue = kMinRecSeconds;
                s.maxValue = kMaxRecSeconds;
                s.defaultValue = kDefaultRecSeconds;
                s.isStepped = false;
                // Value is in seconds; no Unit enum entry for it (None avoids a
                // mislabel). Default = a short grab; loop-length default is a
                // later refinement once the machine sees sequencer tempo context.
                return s;
            case kSlotMonitor:
                s.id = "rec_monitor";
                s.label = "Monitor";
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                s.defaultValue = 0.0f;  // Off — a clean silent tap by default
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kMonitorLabels.data(),
                                                             kMonitorLabels.size());
                return s;
            default:
                return {};
        }
    }

    void RecordMachine::startCapture(int targetSlot, float recSeconds)
    {
        const int poolIdx = pool_.nthVolatileIndex(targetSlot);
        target_ = pool_.mutableVolatilePcm(poolIdx);
        if (target_ == nullptr)
        {
            capturing_ = false;
            samplesRemaining_ = 0;
            return;
        }

        const int cap = pool_.volatileCapacity(poolIdx);
        int recLen = static_cast<int>(std::lround(
            static_cast<double>(recSeconds) * sampleRate_));
        recLen = std::clamp(recLen, 1, std::max(1, cap));

        // Shrink/grow the reported length to the capture length without
        // reallocating (capacity was pre-allocated in prepareVolatile, and recLen
        // is clamped to it), then clear so an interrupted capture has no stale tail.
        target_->setSize(target_->getNumChannels(), recLen, false, false, true);
        target_->clear();

        // Stamp the captured musical length (bars) so a tempo-tracking Player can
        // stretch the buffer to the project tempo (B1/B2). 0 when tempo is unknown.
        const double spb = transport_.samplesPerBar;
        pool_.setSourceBars(poolIdx, spb > 0.0 ? static_cast<double>(recLen) / spb : 0.0);

        writePos_ = 0;
        samplesRemaining_ = recLen;
        capturing_ = true;
    }

    void RecordMachine::writeInput(const juce::AudioBuffer<float>& input,
                                     int startSample, int numSamples)
    {
        if (!capturing_ || target_ == nullptr || samplesRemaining_ <= 0 || numSamples <= 0)
            return;

        const int n = std::min(numSamples, samplesRemaining_);
        const int chans = std::min(target_->getNumChannels(), input.getNumChannels());
        for (int ch = 0; ch < chans; ++ch)
            target_->copyFrom(ch, writePos_, input, ch, startSample, n);

        writePos_ += n;
        samplesRemaining_ -= n;
        if (samplesRemaining_ <= 0)
            capturing_ = false;
    }

    void RecordMachine::process(const juce::MidiBuffer& events,
                                  const ParamFrame& params,
                                  juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int targetSlot = (params.size() > kSlotTargetBuffer)
            ? static_cast<int>(std::lround(params[kSlotTargetBuffer])) : 0;
        const float recSeconds = (params.size() > kSlotRecLength)
            ? params[static_cast<std::size_t>(kSlotRecLength)] : kDefaultRecSeconds;

        // `buffer` already holds the input_source audio (filled by fillTrackInput).
        // Walk the block, restarting capture on each note-on (the recorder trig)
        // and copying input between edges into the target REC buffer. A new trig
        // mid-capture overwrites — the recorder holds no loop state (DESIGN §29.2).
        int pos = 0;
        for (const auto meta : events)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn()) continue;
            const int at = std::clamp(meta.samplePosition, 0, numSamples);
            writeInput(buffer, pos, at - pos);
            startCapture(targetSlot, recSeconds);
            pos = at;
        }
        writeInput(buffer, pos, numSamples - pos);

        // Output mode (DESIGN §30): Off (default) = silent tap — clear so a
        // Master/External capture never doubles back into the mix regardless of the
        // track's CHANNEL "Out". On = monitor — leave the input in the buffer so the
        // performer hears the source being recorded (route CHANNEL "Out" to taste).
        const bool monitor = (params.size() > kSlotMonitor)
            && std::lround(params[static_cast<std::size_t>(kSlotMonitor)]) >= 1;
        if (!monitor)
            buffer.clear();
    }
}
