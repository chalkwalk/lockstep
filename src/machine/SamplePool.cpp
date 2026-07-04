#include "SamplePool.h"
#include "dsp/TempoEstimate.h"
#include "dsp/KeyEstimate.h"
#include "dsp/SampleHints.h"
#include "state/Hash.h"

namespace lockstep
{
    SamplePool::SamplePool()
    {
        formatManager_.registerBasicFormats();
    }

    SamplePool::~SamplePool() = default;

    int SamplePool::load(const juce::String& path, const CachedAnalysis* cached)
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

        // Cheap per-block RMS analysis always runs — the slicer needs it even
        // for cached entries, and it is a single envelope pass.
        sample->analysis = analyseSample(sample->pcm, sample->sampleRate);

        if (cached != nullptr && cached->hashXX32 == sample->ref.hashXX32)
        {
            // Cache hit: adopt the stored analysis, skip re-detection.
            sample->detectedBpm    = cached->bpm;
            sample->keyRoot        = cached->keyRoot;
            sample->keyBrightness  = cached->keyBrightness;
            sample->tuningCents    = cached->tuningCents;
            sample->analysed       = true;
        }
        else
        {
            if (cached != nullptr)
                DBG("SamplePool: cached analysis hash mismatch for "
                    + path + " — re-analysing");
            // Collect hints while the reader (and its metadata) is still alive.
            const SampleHints hints = mergeHints(
                parseMetadataHints(reader->metadataValues),
                parseFilenameHints(file.getFileNameWithoutExtension().toStdString()));
            analyseNewPcm(*sample, hints);
        }

        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    void SamplePool::analyseNewPcm(Sample& s, const SampleHints& hints)
    {
        const double bpm = detectBpmFor(s);

        // Key detection under the same length gate as tempo: long-form / empty
        // material yields the default (unknown) KeyEstimate.
        KeyEstimate ke;
        if (s.sampleRate > 0.0 && s.pcm.getNumSamples() > 0)
        {
            const double seconds =
                static_cast<double>(s.pcm.getNumSamples()) / s.sampleRate;
            if (seconds <= kMaxAnalysisSeconds)
                ke = estimateKey(s.pcm, s.sampleRate);
        }

        const FusedAnalysis fa = fuseAnalysis(bpm, ke, hints);
        s.detectedBpm   = fa.bpm;
        s.keyRoot       = fa.keyRoot;
        s.keyBrightness = fa.keyBrightness;
        s.tuningCents   = fa.tuningCents;
        s.analysed      = true;
    }

    void SamplePool::adoptCachedAnalysis(int index, const CachedAnalysis& ca)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        s.detectedBpm   = ca.bpm;
        s.keyRoot       = ca.keyRoot;
        s.keyBrightness = ca.keyBrightness;
        s.tuningCents   = ca.tuningCents;
        s.analysed      = true;
    }

    // Estimate the loop tempo from the cached RMS envelope, gated by length:
    // material longer than kMaxLoopSeconds is long-form (StreamMachine's domain)
    // and pays no analysis cost. 0 = unknown (short/non-rhythmic/too long).
    double SamplePool::detectBpmFor(const Sample& s)
    {
        if (s.sampleRate <= 0.0 || s.pcm.getNumSamples() <= 0)
            return 0.0;
        const double seconds = static_cast<double>(s.pcm.getNumSamples()) / s.sampleRate;
        if (seconds > kMaxAnalysisSeconds)
            return 0.0;
        return estimateBpm(s.analysis);
    }

    double SamplePool::detectedBpm(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0.0;
        return samples_[static_cast<std::size_t>(index)]->detectedBpm;
    }

    int SamplePool::keyRoot(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return -1;
        return samples_[static_cast<std::size_t>(index)]->keyRoot;
    }

    int SamplePool::keyBrightness(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return kAeolian;
        return samples_[static_cast<std::size_t>(index)]->keyBrightness;
    }

    double SamplePool::tuningCents(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0.0;
        return samples_[static_cast<std::size_t>(index)]->tuningCents;
    }

    int SamplePool::addMissing(const SampleRef& ref)
    {
        auto sample = std::make_unique<Sample>();
        sample->ref = ref;
        sample->missing = true;
        // pcm left empty; SampleMachine produces silence for zero-length buffers.
        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    int SamplePool::addStreamRef(const juce::String& path)
    {
        const std::string p = path.toStdString();
        // Dedupe against an existing Stream entry with the same path.
        for (int i = 0; i < static_cast<int>(samples_.size()); ++i)
        {
            const auto& e = *samples_[static_cast<std::size_t>(i)];
            if (e.origin == SampleOrigin::Stream && e.ref.path == p)
                return i;
        }

        juce::File file(path);
        auto sample = std::make_unique<Sample>();
        sample->origin = SampleOrigin::Stream;
        sample->ref.path = p;
        sample->missing = !file.existsAsFile();

        // Hash the first bytes of the file (NEVER decode the full PCM — the whole
        // point of a stream reference is that the audio never enters RAM). The hash
        // is a change-detector for the ref, not a content fingerprint of the audio.
        if (!sample->missing)
        {
            juce::FileInputStream in(file);
            if (in.openedOk())
            {
                constexpr int kHashBytes = 1 << 20;  // first ~1 MB
                juce::MemoryBlock mb;
                const auto want = static_cast<size_t>(
                    std::min<juce::int64>(kHashBytes, file.getSize()));
                mb.setSize(want);
                const int got = in.read(mb.getData(), static_cast<int>(want));
                if (got > 0)
                    sample->ref.hashXX32 = Hash::xx32(mb.getData(),
                                                      static_cast<std::size_t>(got));
            }
        }

        const int index = static_cast<int>(samples_.size());
        samples_.push_back(std::move(sample));
        return index;
    }

    int SamplePool::ensurePcm(int index)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return -1;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        if (s.pcm.getNumSamples() > 0)
            return index;  // already decoded
        if (s.missing || s.ref.path.empty())
            return -1;

        juce::File file(juce::String(s.ref.path));
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(file));
        if (reader == nullptr)
        {
            s.missing = true;
            return -1;
        }

        // Length guard: a Stream entry may reference a full song. On-demand decode is
        // only sane for reasonably short material; refuse anything longer than the
        // analysis ceiling (~kMaxAnalysisSeconds worth) and leave the entry PCM-less.
        const double sr = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
        const auto maxSamples = static_cast<juce::int64>(kMaxAnalysisSeconds * sr);
        if (reader->lengthInSamples > maxSamples)
            return -1;

        const auto numChannels = static_cast<int>(reader->numChannels);
        const auto numSamples = static_cast<int>(reader->lengthInSamples);
        s.pcm.setSize(numChannels, numSamples);
        reader->read(&s.pcm, 0, numSamples, 0, true, true);
        s.sampleRate = reader->sampleRate;
        s.ref.hashXX32 = Hash::xx32(
            s.pcm.getReadPointer(0),
            static_cast<std::size_t>(numSamples) * sizeof(float));
        s.analysis = analyseSample(s.pcm, s.sampleRate);
        if (!s.analysed)
        {
            const SampleHints hints;  // no reader metadata retained here
            analyseNewPcm(s, hints);
        }
        return index;
    }

    int SamplePool::addVolatile()
    {
        auto sample = std::make_unique<Sample>();
        sample->isVolatile = true;
        sample->origin = SampleOrigin::Empty;  // no capture written yet (W3a)
        sample->volatileId = nextVolatileId_++;  // stable session-local identity (9.18)
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

    void SamplePool::setVolatileOrigin(int index, SampleOrigin o)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return;
        auto& s = samples_[static_cast<std::size_t>(index)];
        if (s->isVolatile)
            s->origin = o;
    }

    SampleOrigin SamplePool::origin(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return SampleOrigin::File;
        return samples_[static_cast<std::size_t>(index)]->origin;
    }

    bool SamplePool::isVolatileIndex(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;
        return samples_[static_cast<std::size_t>(index)]->isVolatile;
    }

    juce::String SamplePool::displayName(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return "(none)";
        const auto& s = *samples_[static_cast<std::size_t>(index)];
        if (s.isVolatile)
        {
            // Captured slots are named by ordinal within their origin group
            // ("Record 1" / "Loop 2"), matching the browser. Empty (never-captured)
            // slots have no Record/Loop designation yet — they are the reserved REC
            // capture slots — so they are named by their volatile ordinal and badged
            // "(empty)" so they still read as a real, pickable slot (bug 14) rather
            // than a nameless row.
            if (s.origin != SampleOrigin::Record && s.origin != SampleOrigin::Loop)
            {
                int vord = 0;
                for (int i = 0; i <= index; ++i)
                    if (samples_[static_cast<std::size_t>(i)]->isVolatile)
                        ++vord;
                return "REC " + juce::String(vord) + " (empty)";
            }
            const char* kind = (s.origin == SampleOrigin::Loop) ? "Loop" : "Record";
            // Ordinal within this origin group (1-based), matching the browser.
            int ord = 0;
            for (int i = 0; i <= index; ++i)
                if (samples_[static_cast<std::size_t>(i)]->isVolatile
                    && samples_[static_cast<std::size_t>(i)]->origin == s.origin)
                    ++ord;
            return juce::String(kind) + " " + juce::String(ord);
        }
        const juce::File f(juce::String(s.ref.path));
        return f.getFileNameWithoutExtension();
    }

    int SamplePool::groupOrdinal(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0;
        const auto& s = *samples_[static_cast<std::size_t>(index)];
        int ord = 0;
        for (int i = 0; i <= index; ++i)
        {
            const auto& e = *samples_[static_cast<std::size_t>(i)];
            bool sameGroup = false;
            if (s.isVolatile)
            {
                if (s.origin == SampleOrigin::Record || s.origin == SampleOrigin::Loop)
                    // Record / Loop are numbered within their own kind.
                    sameGroup = e.isVolatile && e.origin == s.origin;
                else
                    // Empty REC slots are numbered among all volatiles (matches the
                    // "REC N (empty)" label in displayName).
                    sameGroup = e.isVolatile;
            }
            else
            {
                // FILE vs STREAM, numbered within their persistent origin.
                sameGroup = !e.isVolatile && e.origin == s.origin;
            }
            if (sameGroup) ++ord;
        }
        return ord;
    }

    juce::String SamplePool::displayHint(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return {};
        const auto& s = *samples_[static_cast<std::size_t>(index)];
        if (s.isVolatile)
        {
            // Prefer the captured musical length (bars); fall back to a bpm estimate.
            if (s.sourceBars > 0.0)
                return juce::String(s.sourceBars, 2) + " bars";
            if (s.detectedBpm > 0.0)
                return juce::String(juce::roundToInt(s.detectedBpm)) + " bpm";
            return {};
        }
        if (s.missing)
            return "MISSING";

        // Musical hint: "128 bpm  Amin" — either part may be absent. Key suffix
        // is maj (Ionian) / min (Aeolian) / a 3-letter mode tag otherwise. All
        // ASCII (juce::String asserts on non-ASCII char* literals).
        juce::String bpmLabel;
        if (s.detectedBpm > 0.0)
            bpmLabel = juce::String(juce::roundToInt(s.detectedBpm)) + " bpm";

        juce::String keyLabel;
        if (s.keyRoot >= 0)
        {
            juce::String suffix;
            if (s.keyBrightness == kIonian)       suffix = "maj";
            else if (s.keyBrightness == kAeolian) suffix = "min";
            else suffix = juce::String(modeName(s.keyBrightness)).substring(0, 3).toLowerCase();
            keyLabel = juce::String(pitchClassName(s.keyRoot)) + suffix;
        }

        if (bpmLabel.isNotEmpty() && keyLabel.isNotEmpty())
            return bpmLabel + "  " + keyLabel;
        if (bpmLabel.isNotEmpty())
            return bpmLabel;
        if (keyLabel.isNotEmpty())
            return keyLabel;

        // No tempo, no key. An analysed entry with neither is a one-shot (or
        // long-form); flag it so the pool browser distinguishes it from an
        // entry that simply has not been analysed yet.
        const juce::String parent =
            juce::File(juce::String(s.ref.path)).getParentDirectory().getFileName();
        return s.analysed ? parent + "  one-shot" : parent;
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
        s->analysis = analyseSample(s->pcm, s->sampleRate);
        // A relink means the bytes changed by definition — always re-analyse.
        const SampleHints hints = mergeHints(
            parseMetadataHints(reader->metadataValues),
            parseFilenameHints(file.getFileNameWithoutExtension().toStdString()));
        analyseNewPcm(*s, hints);
        s->missing = false;
        return true;
    }

    bool SamplePool::isMissing(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return false;
        return samples_[static_cast<std::size_t>(index)]->missing;
    }

    bool SamplePool::rescanMissing()
    {
        bool changed = false;
        for (auto& up : samples_)
        {
            Sample& s = *up;
            if (s.isVolatile) continue;         // RAM captures are never "missing"
            if (s.ref.path.empty()) continue;   // no on-disk backing to check
            const bool nowMissing =
                !juce::File(juce::String(s.ref.path)).existsAsFile();
            if (nowMissing != s.missing)
            {
                s.missing = nowMissing;         // PCM untouched (see header)
                changed = true;
            }
        }
        return changed;
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

    // ── Stable-identity resolution (9.18) ──────────────────────────────────────

    SampleId SamplePool::idOf(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return {};
        const auto& s = *samples_[static_cast<std::size_t>(index)];
        if (s.isVolatile)
            return { SampleId::Domain::Volatile, s.volatileId };
        // File / Stream — keyed by content hash. A hash of 0 (never hashed) is a
        // degenerate ref that resolves to nothing; treat it as None.
        if (s.ref.hashXX32 == 0)
            return {};
        return { SampleId::Domain::Persistent, s.ref.hashXX32 };
    }

    int SamplePool::indexOf(SampleId id) const
    {
        if (!id.valid())
            return -1;
        for (int i = 0; i < static_cast<int>(samples_.size()); ++i)
        {
            const auto& s = *samples_[static_cast<std::size_t>(i)];
            if (id.domain == SampleId::Domain::Volatile)
            {
                if (s.isVolatile && s.volatileId == id.key)
                    return i;
            }
            else  // Persistent
            {
                if (!s.isVolatile && s.ref.hashXX32 == id.key)
                    return i;
            }
        }
        return -1;
    }

    const Sample* SamplePool::resolve(SampleId id) const
    {
        return get(indexOf(id));
    }
}
