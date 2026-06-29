#include "SamplePool.h"
#include "state/Hash.h"

namespace lockstep
{
    SamplePool::SamplePool()
    {
        formatManager_.registerBasicFormats();
    }

    SamplePool::~SamplePool() = default;

    int SamplePool::load(const juce::String& path)
    {
        juce::File file(path);
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(file));

        if (reader == nullptr)
            return -1;

        auto sample = std::make_unique<Sample>();
        sample->sampleRate = reader->sampleRate;
        sample->ref.path = path.toStdString();

        const auto numChannels = static_cast<int>(reader->numChannels);
        const auto numSamples = static_cast<int>(reader->lengthInSamples);

        sample->pcm.setSize(numChannels, numSamples);
        reader->read(&sample->pcm, 0, numSamples, 0, true, true);

        // Hash the raw float data; real xxHash32 lands in M7.
        sample->ref.hashXX32 = Hash::xx32(
            sample->pcm.getReadPointer(0),
            static_cast<std::size_t>(numSamples) * sizeof(float));

        sample->analysis = analyseSample(sample->pcm, sample->sampleRate);

        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    int SamplePool::addMissing(const SampleRef& ref)
    {
        auto sample = std::make_unique<Sample>();
        sample->ref = ref;
        sample->missing = true;
        // pcm left empty; SamplerMachine produces silence for zero-length buffers.
        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    int SamplePool::addVolatile()
    {
        auto sample = std::make_unique<Sample>();
        sample->isVolatile = true;
        // ref left empty (no file backing); pcm sized later by prepareVolatile().
        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    void SamplePool::prepareVolatile(double sampleRate, int numChannels, int maxSamples)
    {
        const int chans = std::max(1, numChannels);
        const int cap = std::max(0, maxSamples);
        for (auto& s : samples_)
        {
            if (!s->isVolatile) continue;
            s->sampleRate = sampleRate;
            // Capacity allocation happens here (message/prepare thread); a recorder
            // later shrinks the reported size with avoidReallocating, never grows
            // past this capacity.
            s->pcm.setSize(chans, cap, false, true, false);
            s->pcm.clear();
            s->volatileCapacity = cap;
        }
    }

    int SamplePool::volatileCapacity(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0;
        const auto& s = samples_[static_cast<std::size_t>(index)];
        return s->isVolatile ? s->volatileCapacity : 0;
    }

    void SamplePool::setSourceBars(int index, double bars)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return;
        auto& s = samples_[static_cast<std::size_t>(index)];
        if (s->isVolatile)
            s->sourceBars = bars;
    }

    double SamplePool::sourceBars(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0.0;
        const auto& s = samples_[static_cast<std::size_t>(index)];
        return s->isVolatile ? s->sourceBars : 0.0;
    }

    bool SamplePool::isVolatileIndex(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;
        return samples_[static_cast<std::size_t>(index)]->isVolatile;
    }

    int SamplePool::nthVolatileIndex(int n) const
    {
        if (n < 0) return -1;
        int seen = 0;
        for (int i = 0; i < static_cast<int>(samples_.size()); ++i)
        {
            if (!samples_[static_cast<std::size_t>(i)]->isVolatile) continue;
            if (seen == n) return i;
            ++seen;
        }
        return -1;
    }

    juce::AudioBuffer<float>* SamplePool::mutableVolatilePcm(int index)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return nullptr;
        auto& s = samples_[static_cast<std::size_t>(index)];
        if (!s->isVolatile) return nullptr;
        return &s->pcm;
    }

    bool SamplePool::relink(int index, const juce::String& newPath)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;

        juce::File file(newPath);
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(file));
        if (!reader) return false;

        auto& s = samples_[static_cast<std::size_t>(index)];
        s->sampleRate = reader->sampleRate;
        s->ref.path = newPath.toStdString();

        const int numChannels = static_cast<int>(reader->numChannels);
        const int numSamples = static_cast<int>(reader->lengthInSamples);
        s->pcm.setSize(numChannels, numSamples);
        reader->read(&s->pcm, 0, numSamples, 0, true, true);

        s->ref.hashXX32 = Hash::xx32(
            s->pcm.getReadPointer(0),
            static_cast<std::size_t>(numSamples) * sizeof(float));
        s->missing = false;
        return true;
    }

    bool SamplePool::isMissing(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;
        return samples_[static_cast<std::size_t>(index)]->missing;
    }

    bool SamplePool::remove(int index)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;
        samples_.erase(samples_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool SamplePool::swap(int a, int b)
    {
        const int n = static_cast<int>(samples_.size());
        if (a < 0 || a >= n || b < 0 || b >= n || a == b)
            return false;
        std::swap(samples_[static_cast<std::size_t>(a)],
                  samples_[static_cast<std::size_t>(b)]);
        return true;
    }

    const Sample* SamplePool::get(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return nullptr;
        return samples_[static_cast<std::size_t>(index)].get();
    }
}
