#include "PluginState.h"
#include "../PluginProcessor.h"
#include "../core/Sequence.h"
#include "../core/TrigCondition.h"
#include <cstdint>
#include <cstdio>

namespace lockstep::PluginState
{
    // -------------------------------------------------------------------------
    // Small helpers

    static bool floatNe(float a, float b)
    {
        // Bitwise equality: safe for comparing against declared literal defaults.
        return std::memcmp(&a, &b, sizeof(float)) != 0;
    }

    static float getFloat(const juce::ValueTree& v, const char* name, float def)
    {
        return static_cast<float>(
            static_cast<double>(v.getProperty(name, static_cast<double>(def))));
    }

    static juce::ValueTree condToTree(const juce::Identifier& type,
                                       const TrigCondition& c)
    {
        juce::ValueTree v(type);
        v.setProperty("p",  static_cast<int>(c.probabilityPercent), nullptr);
        v.setProperty("n",  static_cast<int>(c.iterNumerator),      nullptr);
        v.setProperty("d",  static_cast<int>(c.iterDenominator),    nullptr);
        v.setProperty("pd", static_cast<int>(c.prevDependency),     nullptr);
        return v;
    }

    static TrigCondition condFromTree(const juce::ValueTree& v)
    {
        TrigCondition c;
        c.probabilityPercent = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("p",  100)));
        c.iterNumerator  = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("n",   1)));
        c.iterDenominator = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("d",   1)));
        c.prevDependency  = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("pd",  0)));
        return c;
    }

    // -------------------------------------------------------------------------
    // Write

    static void writeSequence(juce::ValueTree& root, LockstepProcessor& proc)
    {
        juce::ValueTree seqNode("Sequence");
        const auto& seq = proc.sequence();

        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const auto& track = seq.tracks[static_cast<std::size_t>(t)];
            juce::ValueTree trackNode("Track");
            trackNode.setProperty("i", t, nullptr);

            // BaseCond — omit if trivial (no restriction)
            if (!track.baseCond.isTrivial())
                trackNode.appendChild(condToTree("BaseCond", track.baseCond), nullptr);

            // TrigDefaults — omit if all match structural defaults
            const auto& td = track.trigDefaults;
            const float kGateDefault = 0.0f;
            if (td.note != 60 || td.velocity != 100 || floatNe(td.gateMs, kGateDefault))
            {
                juce::ValueTree tdNode("TrigDefaults");
                tdNode.setProperty("note",   td.note,                     nullptr);
                tdNode.setProperty("vel",    td.velocity,                  nullptr);
                tdNode.setProperty("gateMs", static_cast<double>(td.gateMs), nullptr);
                trackNode.appendChild(tdNode, nullptr);
            }

            // BaseParams — emit slots whose value differs from machine default
            const int np = proc.numParams(t);
            if (np > 0)
            {
                juce::ValueTree bpNode("BaseParams");
                for (int s = 0; s < np; ++s)
                {
                    const juce::String id = proc.idForSlot(t, s);
                    if (id.isEmpty()) continue;

                    const float def = proc.paramSpec(t, s).defaultValue;
                    const float val = (static_cast<std::size_t>(s) < track.baseParams.size())
                                      ? track.baseParams[static_cast<std::size_t>(s)] : def;
                    if (!floatNe(val, def)) continue;

                    juce::ValueTree pNode("P");
                    pNode.setProperty("id", id,                        nullptr);
                    pNode.setProperty("v",  static_cast<double>(val),  nullptr);
                    bpNode.appendChild(pNode, nullptr);
                }
                if (bpNode.getNumChildren() > 0)
                    trackNode.appendChild(bpNode, nullptr);
            }

            // Steps — emit only those with any non-default content
            juce::ValueTree stepsNode("Steps");
            for (int s = 0; s < kMaxStepsPerTrack; ++s)
            {
                const auto& step = track.steps[static_cast<std::size_t>(s)];
                const bool hasPLock = !step.overrides.empty();
                const bool hasTrigOvr = step.trigOverride.hasNote
                                     || step.trigOverride.hasVelocity
                                     || step.trigOverride.hasGate;
                const bool hasNonTrivCond = !step.condition.isTrivial();
                if (!step.trig && !hasPLock && !hasTrigOvr && !hasNonTrivCond)
                    continue;

                juce::ValueTree stepNode("S");
                stepNode.setProperty("i", s,                   nullptr);
                stepNode.setProperty("t", step.trig ? 1 : 0,   nullptr);

                if (hasNonTrivCond)
                    stepNode.appendChild(condToTree("C", step.condition), nullptr);

                if (hasTrigOvr)
                {
                    juce::ValueTree toNode("TO");
                    if (step.trigOverride.hasNote)
                    {
                        toNode.setProperty("hn", 1,                    nullptr);
                        toNode.setProperty("n",  step.trigOverride.note, nullptr);
                    }
                    if (step.trigOverride.hasVelocity)
                    {
                        toNode.setProperty("hv", 1,                          nullptr);
                        toNode.setProperty("v",  step.trigOverride.velocity,  nullptr);
                    }
                    if (step.trigOverride.hasGate)
                    {
                        toNode.setProperty("hg", 1,                                         nullptr);
                        toNode.setProperty("g",  static_cast<double>(step.trigOverride.gateMs), nullptr);
                    }
                    stepNode.appendChild(toNode, nullptr);
                }

                if (hasPLock)
                {
                    juce::ValueTree plNode("PL");
                    step.overrides.forEach([&](int slot, float value) {
                        const juce::String id = proc.idForSlot(t, slot);
                        if (id.isEmpty()) return;
                        juce::ValueTree lNode("L");
                        lNode.setProperty("id", id,                          nullptr);
                        lNode.setProperty("v",  static_cast<double>(value),  nullptr);
                        plNode.appendChild(lNode, nullptr);
                    });
                    if (plNode.getNumChildren() > 0)
                        stepNode.appendChild(plNode, nullptr);
                }

                stepsNode.appendChild(stepNode, nullptr);
            }

            if (stepsNode.getNumChildren() > 0)
                trackNode.appendChild(stepsNode, nullptr);

            seqNode.appendChild(trackNode, nullptr);
        }

        root.appendChild(seqNode, nullptr);
    }

    // -------------------------------------------------------------------------
    // Read

    static void readSequence(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto seqNode = root.getChildWithName("Sequence");
        if (!seqNode.isValid()) return;

        auto& seq = proc.sequence();

        for (auto trackNode : seqNode)
        {
            const int t = static_cast<int>(trackNode.getProperty("i", -1));
            if (t < 0 || t >= static_cast<int>(kNumTracks)) continue;
            auto& track = seq.tracks[static_cast<std::size_t>(t)];

            const auto bcNode = trackNode.getChildWithName("BaseCond");
            if (bcNode.isValid())
                track.baseCond = condFromTree(bcNode);

            const auto tdNode = trackNode.getChildWithName("TrigDefaults");
            if (tdNode.isValid())
            {
                track.trigDefaults.note     = static_cast<int>(tdNode.getProperty("note",   60));
                track.trigDefaults.velocity = static_cast<int>(tdNode.getProperty("vel",   100));
                track.trigDefaults.gateMs   = getFloat(tdNode, "gateMs", 0.0f);
            }

            const auto bpNode = trackNode.getChildWithName("BaseParams");
            if (bpNode.isValid())
            {
                for (auto pNode : bpNode)
                {
                    const juce::String id  = pNode.getProperty("id").toString();
                    const float        val = getFloat(pNode, "v", 0.0f);
                    const int slot = proc.slotForId(t, id);
                    if (slot < 0)
                    {
                        DBG("PluginState: unknown param id '" + id
                            + "' on track " + juce::String(t) + " -- skipping");
                        continue;
                    }
                    if (static_cast<std::size_t>(slot) < track.baseParams.size())
                        track.baseParams[static_cast<std::size_t>(slot)] = val;
                }
            }

            const auto stepsNode = trackNode.getChildWithName("Steps");
            if (!stepsNode.isValid()) continue;

            for (auto stepNode : stepsNode)
            {
                const int s = static_cast<int>(stepNode.getProperty("i", -1));
                if (s < 0 || s >= kMaxStepsPerTrack) continue;
                auto& step = track.steps[static_cast<std::size_t>(s)];

                step.trig = (static_cast<int>(stepNode.getProperty("t", 0)) != 0);

                const auto cNode = stepNode.getChildWithName("C");
                if (cNode.isValid())
                    step.condition = condFromTree(cNode);

                const auto toNode = stepNode.getChildWithName("TO");
                if (toNode.isValid())
                {
                    step.trigOverride.hasNote = (static_cast<int>(toNode.getProperty("hn", 0)) != 0);
                    if (step.trigOverride.hasNote)
                        step.trigOverride.note = static_cast<int>(toNode.getProperty("n", 60));

                    step.trigOverride.hasVelocity = (static_cast<int>(toNode.getProperty("hv", 0)) != 0);
                    if (step.trigOverride.hasVelocity)
                        step.trigOverride.velocity = static_cast<int>(toNode.getProperty("v", 100));

                    step.trigOverride.hasGate = (static_cast<int>(toNode.getProperty("hg", 0)) != 0);
                    if (step.trigOverride.hasGate)
                        step.trigOverride.gateMs = getFloat(toNode, "g", 0.0f);
                }

                const auto plNode = stepNode.getChildWithName("PL");
                if (!plNode.isValid()) continue;

                for (auto lNode : plNode)
                {
                    const juce::String id  = lNode.getProperty("id").toString();
                    const float        val = getFloat(lNode, "v", 0.0f);
                    const int slot = proc.slotForId(t, id);
                    if (slot < 0)
                    {
                        DBG("PluginState: unknown P-Lock id '" + id
                            + "' on track " + juce::String(t) + " -- skipping");
                        continue;
                    }
                    step.overrides.set(slot, val);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Sample pool

    static juce::String hashToHex(std::uint32_t h)
    {
        char buf[9];
        std::snprintf(buf, sizeof(buf), "%08X", h);
        return juce::String(buf);
    }

    static std::uint32_t hexToHash(const juce::String& s)
    {
        return static_cast<std::uint32_t>(s.getHexValue32());
    }

    static void writeSamplePool(juce::ValueTree& root, LockstepProcessor& proc)
    {
        juce::ValueTree poolNode("SamplePool");
        const auto& pool = proc.samplePool();
        for (int i = 0; i < pool.size(); ++i)
        {
            const auto* s = pool.get(i);
            if (!s) continue;
            juce::ValueTree entry("Entry");
            entry.setProperty("i",    i,                               nullptr);
            entry.setProperty("path", juce::String(s->ref.path),       nullptr);
            entry.setProperty("hash", hashToHex(s->ref.hashXX32),      nullptr);
            poolNode.appendChild(entry, nullptr);
        }
        root.appendChild(poolNode, nullptr);
    }

    static void readSamplePool(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto poolNode = root.getChildWithName("SamplePool");
        if (!poolNode.isValid()) return;

        for (auto entry : poolNode)
        {
            const juce::String path = entry.getProperty("path").toString();
            const juce::String hex  = entry.getProperty("hash").toString();
            const std::uint32_t savedHash = hexToHash(hex);

            const int loaded = proc.samplePool().load(path);
            if (loaded >= 0)
            {
                // Warn if hash differs — file changed since last save, but still usable.
                const auto* s = proc.samplePool().get(loaded);
                if (s && s->ref.hashXX32 != savedHash)
                    DBG("PluginState: hash mismatch for '" + path + "' (file may have changed)");
            }
            else
            {
                // File not found — insert a placeholder so pool indices remain intact.
                SampleRef ref;
                ref.path     = path.toStdString();
                ref.hashXX32 = savedHash;
                proc.samplePool().addMissing(ref);
                DBG("PluginState: missing sample '" + path + "'");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Public API

    void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc)
    {
        juce::ValueTree root("LockstepState");
        root.setProperty("version", kCurrentVersion, nullptr);

        // APVTS state (parameters: gain, sync mode, channel mode, track lengths etc.)
        root.appendChild(proc.apvts().copyState(), nullptr);

        // Sample pool ({path, hash} refs — no PCM bytes)
        writeSamplePool(root, proc);

        // Sequence (trigs, P-Locks, base params, conditions)
        writeSequence(root, proc);

        if (auto xml = root.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, dest);
    }

    void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (!xml) return;

        auto root = juce::ValueTree::fromXml(*xml);
        if (!root.isValid()) return;

        // v0 compat: root type was the bare APVTS tree ("Lockstep").
        if (root.getType() != juce::Identifier("LockstepState"))
        {
            proc.apvts().replaceState(root);
            return;
        }

        // Restore APVTS parameters. The child type matches the APVTS valueTreeType
        // passed to the constructor ("Lockstep").
        const auto apvtsChild = root.getChildWithName("Lockstep");
        if (apvtsChild.isValid())
            proc.apvts().replaceState(apvtsChild);

        // Sample pool must be restored before sequence, so that pool indices
        // referenced in baseParams and P-Locks resolve to the right entries.
        readSamplePool(root, proc);
        readSequence(root, proc);
    }
}
