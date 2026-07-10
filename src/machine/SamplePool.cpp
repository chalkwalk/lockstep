#include "SamplePool.h"
#include "InputSource.h"      // kMaxDeckChannels (widest deck = 8ch)
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
            sample->detectedBpm     = cached->bpm;
            sample->keyRoot         = cached->keyRoot;
            sample->keyBrightness   = cached->keyBrightness;
            sample->tuningCents     = cached->tuningCents;
            sample->detectedOneShot = cached->oneShot;
            sample->analysed        = true;
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
        s.detectedBpm     = fa.bpm;
        s.keyRoot         = fa.keyRoot;
        s.keyBrightness   = fa.keyBrightness;
        s.tuningCents     = fa.tuningCents;
        s.detectedOneShot = hints.oneShot;  // ACID / filename one-shot flag (was lost)
        s.analysed        = true;
    }

    void SamplePool::adoptCachedAnalysis(int index, const CachedAnalysis& ca)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        s.detectedBpm     = ca.bpm;
        s.keyRoot         = ca.keyRoot;
        s.keyBrightness   = ca.keyBrightness;
        s.tuningCents     = ca.tuningCents;
        s.detectedOneShot = ca.oneShot;
        s.analysed        = true;
    }

    // How far into a source we are willing to call an attack "the start". Past
    // this the material has a musical intro (a swell, a pickup), not a trimming
    // artefact, and moving the start point would eat performance.
    static constexpr double kOnsetSearchSeconds = 2.0;

    // findFirstOnsetSample reports an onset at its analysis block's *midpoint*, so
    // material that starts on the 1 comes back a half-block late rather than at 0.
    // Anything inside the first block is "starts on the 1" — the same rounding
    // placeSyncSlices does with its minimum-slice guard.
    static int zeroIfWithinFirstBlock(int onset, int blockSize)
    {
        return (blockSize > 0 && onset < blockSize) ? 0 : onset;
    }

    SamplePool::Onset SamplePool::firstOnset(int index)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return {};
        auto& s = *samples_[static_cast<std::size_t>(index)];
        if (s.onsetComputed)
            return { s.onsetNorm, s.onsetSeconds };

        s.onsetComputed = true;  // one attempt per entry, success or not

        // A volatile capture has no block analysis, so it reports {0,0} — which is
        // also the right answer: the performer's punch defined where it starts.
        if (s.pcm.getNumSamples() > 0 && s.sampleRate > 0.0)
        {
            const int total = s.pcm.getNumSamples();
            const int limit = std::min(
                total, static_cast<int>(kOnsetSearchSeconds * s.sampleRate));
            const int onset = zeroIfWithinFirstBlock(
                findFirstOnsetSample(s.analysis, limit), s.analysis.blockSize);
            s.onsetNorm    = static_cast<double>(onset) / static_cast<double>(total);
            s.onsetSeconds = static_cast<double>(onset) / s.sampleRate;
        }
        else if (!s.missing && !s.ref.path.empty())
        {
            // PCM-less Stream entry: decode only the head window we search.
            juce::File file(juce::String(s.ref.path));
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager_.createReaderFor(file));
            if (reader != nullptr && reader->sampleRate > 0.0
                && reader->lengthInSamples > 0)
            {
                const double sr = reader->sampleRate;
                const auto total = reader->lengthInSamples;
                const auto headLen = static_cast<int>(std::min(
                    total, static_cast<juce::int64>(kOnsetSearchSeconds * sr)));
                juce::AudioBuffer<float> head(
                    static_cast<int>(reader->numChannels), headLen);
                reader->read(&head, 0, headLen, 0, true, true);

                const BlockAnalysis ba = analyseSample(head, sr);
                const int onset = zeroIfWithinFirstBlock(
                    findFirstOnsetSample(ba, headLen), ba.blockSize);
                s.onsetNorm    = static_cast<double>(onset) / static_cast<double>(total);
                s.onsetSeconds = static_cast<double>(onset) / sr;
            }
        }
        return { s.onsetNorm, s.onsetSeconds };
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

    // ── Effective (override-else-detected) metadata (9.23) ───────────────────
    double SamplePool::effectiveBpm(int index) const
    {
        const Sample* s = get(index);
        if (s == nullptr) return 0.0;
        return s->userBpm > 0.0 ? s->userBpm : s->detectedBpm;
    }

    int SamplePool::effectiveKeyRoot(int index) const
    {
        const Sample* s = get(index);
        if (s == nullptr) return -1;
        return s->userKeyRoot >= 0 ? s->userKeyRoot : s->keyRoot;
    }

    int SamplePool::effectiveKeyBrightness(int index) const
    {
        const Sample* s = get(index);
        if (s == nullptr) return kAeolian;
        return s->userKeyRoot >= 0 ? s->userKeyBrightness : s->keyBrightness;
    }

    double SamplePool::effectiveTuningCents(int index) const
    {
        const Sample* s = get(index);
        if (s == nullptr) return 0.0;
        return s->hasUserTuning ? s->userTuningCents : s->tuningCents;
    }

    bool SamplePool::effectiveOneShot(int index) const
    {
        const Sample* s = get(index);
        if (s == nullptr) return false;
        return s->userOneShot >= 0 ? (s->userOneShot == 1) : s->detectedOneShot;
    }

    void SamplePool::setUserBpm(int index, double bpm)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size())) return;
        samples_[static_cast<std::size_t>(index)]->userBpm = bpm > 0.0 ? bpm : 0.0;
    }

    void SamplePool::setUserKey(int index, int keyRoot, int keyBrightness)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size())) return;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        s.userKeyRoot = keyRoot;
        s.userKeyBrightness = keyBrightness;
    }

    void SamplePool::setUserTuningCents(int index, double cents, bool has)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size())) return;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        s.userTuningCents = cents;
        s.hasUserTuning = has;
    }

    void SamplePool::setUserOneShot(int index, int state)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size())) return;
        samples_[static_cast<std::size_t>(index)]->userOneShot = state;
    }

    void SamplePool::clearUserOverrides(int index)
    {
        if (index < 0 || index >= static_cast<int>(samples_.size())) return;
        auto& s = *samples_[static_cast<std::size_t>(index)];
        s.userBpm = 0.0;
        s.userKeyRoot = -1;
        s.userKeyBrightness = kAeolian;
        s.userTuningCents = 0.0;
        s.hasUserTuning = false;
        s.userOneShot = -1;
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
        s.onsetComputed = false;  // now measurable from PCM, not a disk head
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
        // Allocate wide enough for the widest deck (§40.3: a four-sub-track Loop
        // records into one 8-channel slot) — address space only, no zero-fill, so
        // a slot nobody records eight channels into costs nothing resident. But
        // REPORT the requested natural width, which is the default a capture
        // inherits: a machine prepared for stereo captures stereo unless it asks
        // for more, so nothing but a wide deck sees eight channels.
        volatilePrepChannels_ = std::max(1, numChannels);
        const int allocChans = std::max(volatilePrepChannels_, kMaxDeckChannels);
        const int cap = std::max(0, maxSamples);
        for (auto& s : samples_)
        {
            if (!s->isVolatile) continue;
            s->sampleRate = sampleRate;
            // Capacity allocation happens here (message/prepare thread); a recorder
            // later grows the reported size with avoidReallocating, never past this
            // capacity.
            //
            // A5: allocate *without* zero-filling, so the pages of a slot nobody
            // records into are never committed — sixteen sixty-second slots cost
            // address space, not memory. The reported length is then dropped to
            // zero, which is the used length: nothing may read past it, because
            // past it the memory is uninitialised rather than silent. A recorder
            // grows it back to what it captured (LoopMachine / RecordMachine both
            // clear the region they are about to write).
            s->pcm.setSize(allocChans, cap, false, false, false);
            // Drop to the reported natural width, keeping the wide allocation
            // (avoidReallocating), so a later 8-channel capture never reallocates.
            s->pcm.setSize(volatilePrepChannels_, 0, false, false, /*avoidReallocating*/ true);
            s->volatileCapacity = cap;
        }
    }

    juce::AudioBuffer<float>* SamplePool::beginVolatileCapture(int index, int lengthSamples,
                                                               int numChannels)
    {
        auto* buf = mutableVolatilePcm(index);
        if (buf == nullptr || lengthSamples <= 0) return nullptr;
        if (lengthSamples > volatileCapacity(index)) return nullptr;

        // The capture declares its channel width: a stereo loop takes two, a
        // four-sub-track Loop eight (§40.3). prepareVolatile allocated the maximum,
        // so setting a narrower reported width — or growing back to the full
        // eight — never reallocates (avoidReallocating). Only the declared channels
        // are cleared, so an unfilled channel of a wide slot costs no resident page.
        // 0 = inherit the prepared natural width; otherwise the caller's declared
        // width (a four-sub-track Loop passes eight). Either way within the wide
        // allocation, so avoidReallocating never triggers a realloc on the audio
        // thread.
        const int chans = std::clamp(numChannels > 0 ? numChannels : volatilePrepChannels_,
                                     1, kMaxDeckChannels);
        buf->setSize(chans, lengthSamples, false, false, /*avoidReallocating*/ true);
        buf->clear();
        return buf;
    }

    int SamplePool::volatileUsedLength(int index) const
    {
        if (index < 0 || index >= static_cast<int>(samples_.size()))
            return 0;
        const auto& s = samples_[static_cast<std::size_t>(index)];
        // The reported buffer length *is* the used length: prepareVolatile drops it
        // to zero and a recorder grows it to what it captured.
        return s->isVolatile ? s->pcm.getNumSamples() : 0;
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

        // Musical hint from the EFFECTIVE (override-else-detected) metadata, e.g.
        // "128 bpm  Amin  +12c" — any part may be absent. A trailing "*" marks that
        // at least one value is a user override. Key suffix is maj (Ionian) / min
        // (Aeolian) / a 3-letter mode tag. All ASCII (juce::String asserts on
        // non-ASCII char* literals).
        const int idx = index;
        const double effBpm = effectiveBpm(idx);
        const int effRoot = effectiveKeyRoot(idx);
        const int effBright = effectiveKeyBrightness(idx);
        const double effTune = effectiveTuningCents(idx);
        const bool overridden = s.userBpm > 0.0 || s.userKeyRoot >= 0
                             || s.hasUserTuning || s.userOneShot >= 0;

        juce::String bpmLabel;
        if (effBpm > 0.0)
            bpmLabel = juce::String(juce::roundToInt(effBpm)) + " bpm";

        juce::String keyLabel;
        if (effRoot >= 0)
        {
            juce::String suffix;
            if (effBright == kIonian)       suffix = "maj";
            else if (effBright == kAeolian) suffix = "min";
            else suffix = juce::String(modeName(effBright)).substring(0, 3).toLowerCase();
            keyLabel = juce::String(pitchClassName(effRoot)) + suffix;
        }

        // Tuning is a deviation from A440 — only meaningful alongside a detected
        // key. A keyless one-shot with a stray tuning estimate shows no tune label.
        juce::String tuneLabel;
        if (effRoot >= 0 && juce::roundToInt(effTune) != 0)
            tuneLabel = (effTune >= 0.0 ? "+" : "") + juce::String(juce::roundToInt(effTune)) + "c";

        const juce::String star = overridden ? " *" : juce::String();

        juce::StringArray parts;
        if (bpmLabel.isNotEmpty())  parts.add(bpmLabel);
        if (keyLabel.isNotEmpty())  parts.add(keyLabel);
        if (tuneLabel.isNotEmpty()) parts.add(tuneLabel);
        if (!parts.isEmpty())
            return parts.joinIntoString("  ") + star;

        // No tempo, no key. An analysed entry with neither (or an effective
        // one-shot) is a one-shot; flag it so the pool browser distinguishes it
        // from an entry that simply has not been analysed yet.
        const juce::String parent =
            juce::File(juce::String(s.ref.path)).getParentDirectory().getFileName();
        return (s.analysed || effectiveOneShot(idx))
                   ? parent + "  one-shot" + star : parent;
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
        s->onsetComputed = false;  // different bytes, different attack
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
