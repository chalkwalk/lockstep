#include "SamplePlayingMachineBase.h"
#include "TransientDetector.h"
#include <algorithm>
#include <cmath>

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

    float SamplePlayingMachineBase::snapWrittenValue(int slot,
                                                      float v,
                                                      const SamplePool& pool,
                                                      const ParamFrame& baseParams) const
    {
        juce::ignoreUnused(slot);

        // Slot 0 is the sample_id in both SamplerMachine and SlicerMachine.
        if (baseParams.empty())
            return v;

        const int sampleIdx = static_cast<int>(baseParams[0]);
        const Sample* s = pool.get(sampleIdx);
        if (s == nullptr || s->pcm.getNumSamples() < 2)
            return v;

        const int numSamples = s->pcm.getNumSamples();
        const double targetPos = static_cast<double>(v) * static_cast<double>(numSamples);
        const double searchRadius = 0.005 * s->sampleRate;  // ±5 ms

        const int lo = std::max(0, static_cast<int>(targetPos - searchRadius));
        const int hi = std::min(numSamples - 2, static_cast<int>(targetPos + searchRadius));

        int bestIdx  = static_cast<int>(targetPos);
        double bestDist = searchRadius + 1.0;

        const float* ch0 = s->pcm.getReadPointer(0);
        for (int i = lo; i <= hi; ++i)
        {
            // Detect sign change (zero crossing between samples i and i+1).
            if ((ch0[i] >= 0.0f) != (ch0[i + 1] >= 0.0f))
            {
                const double dist = std::abs(static_cast<double>(i) - targetPos);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx  = i;
                }
            }
        }

        return static_cast<float>(bestIdx) / static_cast<float>(numSamples);
    }

    void SamplePlayingMachineBase::detectTransientSlices()
    {
        detectTransientSlices(numSlices_ > 0 ? numSlices_ : 8);
    }

    void SamplePlayingMachineBase::detectTransientSlices(int count)
    {
        const Sample* s = pool_.get(currentSampleIndex_);
        if (s == nullptr || s->missing || s->pcm.getNumSamples() < 2)
            return;

        const std::vector<int> positions = placeTransientSlices(
            s->pcm, s->sampleRate, count, s->analysis);

        numSlices_ = static_cast<int>(positions.size());
        numSlices_ = std::min(numSlices_, kMaxSlices);

        const int nSamp = s->pcm.getNumSamples();
        for (int i = 0; i < numSlices_; ++i)
        {
            slicePositions_[static_cast<std::size_t>(i)] =
                static_cast<float>(positions[static_cast<std::size_t>(i)])
                / static_cast<float>(nSamp);
        }
    }
}
