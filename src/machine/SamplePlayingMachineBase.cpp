#include "SamplePlayingMachineBase.h"
#include <algorithm>

namespace lockstep
{
    SamplePlayingMachineBase::SamplePlayingMachineBase(SamplePool& pool)
        : pool_(pool) {}

    SamplePlayingMachineBase::~SamplePlayingMachineBase() = default;

    void SamplePlayingMachineBase::prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        for (auto& v : voices_)
            v.choke.prepare(sampleRate_, 1.5f);
        juce::ignoreUnused(maxBlockSize);
    }

    void SamplePlayingMachineBase::reset()
    {
        for (auto& v : voices_)
        {
            v.player     = SamplePlayer{};
            v.hasPending = false;
            v.midiNote   = -1;
            v.age        = 0;
            v.choke.prepare(sampleRate_, 1.5f);
        }
        voiceCounter_ = 0;
    }

    bool SamplePlayingMachineBase::isVoiceActive() const
    {
        for (const auto& v : voices_)
        {
            if (v.player.isActive() || v.choke.isFading() || v.hasPending)
                return true;
        }
        return false;
    }

    void SamplePlayingMachineBase::setEqualSlices(int count)
    {
        count = std::clamp(count, 1, kMaxSlices);
        numSlices_ = count;
        for (int i = 0; i < count; ++i)
            slicePositions_[static_cast<std::size_t>(i)] =
                static_cast<float>(i) / static_cast<float>(count);
    }

    void SamplePlayingMachineBase::clearSlices()
    {
        numSlices_ = 0;
    }

    int SamplePlayingMachineBase::allocVoice()
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& v = voices_[static_cast<std::size_t>(i)];
            if (!v.player.isActive() && !v.choke.isFading() && !v.hasPending)
                return i;
        }
        // All busy: steal the oldest active voice.
        int oldest = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (voices_[static_cast<std::size_t>(i)].age
                < voices_[static_cast<std::size_t>(oldest)].age)
                oldest = i;
        return oldest;
    }

    int SamplePlayingMachineBase::findVoiceByNote(int midiNote) const
    {
        int best = -1;
        std::uint64_t bestAge = 0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& v = voices_[static_cast<std::size_t>(i)];
            if (v.player.isActive() && v.midiNote == midiNote)
            {
                if (best < 0 || v.age > bestAge)
                {
                    best = i;
                    bestAge = v.age;
                }
            }
        }
        return best;
    }
}
