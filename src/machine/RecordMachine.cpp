#include <cstdint>
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

    void RecordMachine::bindReel()
    {
        dc::Medium::Config cfg;
        cfg.topology = dc::Topology::Linear;   // a reel: past the ends is silence
        cfg.mediumRate = sampleRate_;
        cfg.numSubTracks = 1;
        cfg.channelsPerSubTrack = kChannels;
        cfg.capacitySamples = reelCap_;
        std::array<dc::Store, kChannels> planes{};
        for (int c = 0; c < kChannels; ++c)
            planes[static_cast<std::size_t>(c)] =
                dc::Store{ reel_.getWritePointer(c), static_cast<std::size_t>(reelCap_) };
        medium_.bindPlanes(cfg, planes.data(), kChannels);
    }

    double RecordMachine::chaseRatio() const noexcept
    {
        // r = K / samplesPerPpq(now). Uncalibrated (calSpp_ == 0) → unity, so a take
        // with no tempo context records 1:1 exactly as the pre-deck path did.
        if (calSpp_ <= 0.0) return 1.0;
        const double spp = (transport_.samplesPerBar > 0.0 && transport_.barPpq > 0.0)
            ? transport_.samplesPerBar / transport_.barPpq : 0.0;
        return spp > 0.0 ? calSpp_ / spp : 1.0;
    }

    void RecordMachine::startCapture(int targetSlot, float recSeconds)
    {
        const int poolIdx = pool_.nthVolatileIndex(targetSlot);
        auto* slot = pool_.mutableVolatilePcm(poolIdx);
        if (slot == nullptr || ! medium_.bound())
        {
            deck_.setState(dc::DeckState::Idle);
            samplesRemaining_ = 0;
            return;
        }

        // One-level undo: snapshot the slot's current take BEFORE this capture
        // overwrites it. `pcm.getNumSamples()` is its used length (0 when empty).
        const int priorLen = slot->getNumSamples();
        undoSlot_ = poolIdx;
        undoLen_ = priorLen;
        undoBars_ = pool_.sourceBars(poolIdx);
        undoOrigin_ = pool_.origin(poolIdx);
        undoBackup_.setSize(kChannels, std::max(1, priorLen), false, false, true);
        undoBackup_.clear();
        if (priorLen > 0)
        {
            const int c = std::min(kChannels, slot->getNumChannels());
            for (int ch = 0; ch < c; ++ch)
                undoBackup_.copyFrom(ch, 0, *slot, ch, 0, priorLen);
        }

        const int cap = pool_.volatileCapacity(poolIdx);
        int recLen = static_cast<int>(std::lround(
            static_cast<double>(recSeconds) * sampleRate_));
        // The wall-clock cap, in engine samples. The reel it produces may be longer
        // or shorter (varispeed), bounded by the reel + the pool slot capacity.
        recLen = std::clamp(recLen, 1, std::max(1, std::min(cap, reelCap_)));

        // Latch the reel calibration from the current tempo (§40.2): from here the
        // reel is addressed by musical time and any tempo deviation is varispeed.
        calSpp_ = (transport_.samplesPerBar > 0.0 && transport_.barPpq > 0.0)
            ? transport_.samplesPerBar / transport_.barPpq : 0.0;

        // Fresh reel: virgin (uncommitted) storage; the write head zeroes ahead as it
        // records, so nothing reads garbage.
        medium_.resetAllUsed();
        writeHead_.setPosition(0.0);
        writeHead_.setRate(chaseRatio());

        targetPoolIdx_ = poolIdx;
        samplesRemaining_ = recLen;
        deck_.setState(dc::DeckState::Recording);
    }

    void RecordMachine::writeInput(const juce::AudioBuffer<float>& input,
                                     int startSample, int numSamples)
    {
        if (! capturing() || ! medium_.bound() || samplesRemaining_ <= 0 || numSamples <= 0)
            return;

        const int n = std::min(numSamples, samplesRemaining_);
        const int chans = std::min(kChannels, input.getNumChannels());

        // The chase ratio is block-constant (the transport snapshot is per block).
        // Unity (constant/absent tempo) writes integer-for-integer, bit-exact with a
        // straight copy; a deviation drives the bandlimited varispeed write head.
        const double r = chaseRatio();
        writeHead_.setRate(r);
        const bool unity = std::abs(r - 1.0) < 1e-9;

        std::array<float, kChannels> frame{};
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < kChannels; ++ch)
                frame[static_cast<std::size_t>(ch)] =
                    ch < chans ? input.getSample(ch, startSample + i) : 0.0f;

            if (unity)
            {
                const auto pos = static_cast<std::int64_t>(std::llround(writeHead_.position()));
                medium_.ensureCommitted(0, static_cast<int>(pos) + 1);
                for (int ch = 0; ch < kChannels; ++ch)
                    medium_.write(0, ch, pos, frame[static_cast<std::size_t>(ch)]);
            }
            else
            {
                writeHead_.writeFrame(medium_, 0, frame.data(), kChannels);
            }
            writeHead_.step(medium_);
        }

        samplesRemaining_ -= n;
        if (samplesRemaining_ <= 0)
            closeCapture();
    }

    void RecordMachine::closeCapture()
    {
        deck_.setState(dc::DeckState::Idle);
        const int poolIdx = targetPoolIdx_;
        if (poolIdx < 0 || ! medium_.bound()) return;

        // The reel region actually recorded (its high-water), clamped to the pool
        // slot's capacity so the commit never overruns it.
        const int cap = pool_.volatileCapacity(poolIdx);
        // `used` is a reel coordinate and therefore 64-bit in chalkwalk-tape:
        // a windowed medium's tape outruns its memory, and the mark counts tape.
        // Nothing here records more than a pool slot holds, so the narrowing is
        // safe -- but it is written down rather than inferred.
        const int usedLen = static_cast<int>(
            std::clamp<std::int64_t>(medium_.used(0), 0, std::max(0, cap)));
        if (usedLen <= 0) return;

        // Claim + clear the pool slot region, then copy the reel take into it. A5:
        // beginVolatileCapture declares the used length, so nothing reads past it.
        auto* slot = pool_.beginVolatileCapture(poolIdx, usedLen, kChannels);
        if (slot == nullptr) return;
        const int chans = std::min(kChannels, slot->getNumChannels());
        for (int ch = 0; ch < chans; ++ch)
            slot->copyFrom(ch, 0, reel_, ch, 0, usedLen);

        // Stamp the captured musical length (bars) so a tempo-tracking Player can
        // stretch to the project tempo (B1/B2), and tag the origin (W3a).
        const double spb = transport_.samplesPerBar;
        pool_.setSourceBars(poolIdx, spb > 0.0 ? static_cast<double>(usedLen) / spb : 0.0);
        pool_.setVolatileOrigin(poolIdx, SampleOrigin::Record);
    }

    bool RecordMachine::undo()
    {
        if (! canUndo()) return false;
        const int poolIdx = undoSlot_;
        if (undoLen_ <= 0)
        {
            // The slot was empty before the capture: drop it back to empty.
            pool_.setSourceBars(poolIdx, 0.0);
            pool_.setVolatileOrigin(poolIdx, SampleOrigin::Empty);
            if (auto* slot = pool_.mutableVolatilePcm(poolIdx))
                slot->setSize(slot->getNumChannels(), 0, false, false, true);
        }
        else
        {
            auto* slot = pool_.beginVolatileCapture(poolIdx, undoLen_, kChannels);
            if (slot == nullptr) return false;
            const int chans = std::min({ kChannels, slot->getNumChannels(),
                                         undoBackup_.getNumChannels() });
            for (int ch = 0; ch < chans; ++ch)
                slot->copyFrom(ch, 0, undoBackup_, ch, 0, undoLen_);
            pool_.setSourceBars(poolIdx, undoBars_);
            pool_.setVolatileOrigin(poolIdx, undoOrigin_);
        }
        // One level only: consume the backup so a second undo is a no-op.
        undoSlot_ = -1;
        undoLen_ = -1;
        return true;
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
