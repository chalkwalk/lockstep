#include "PluginState.h"
#include "../PluginProcessor.h"
#include "../core/Bank.h"
#include "../core/Pattern.h"
#include "../core/Part.h"
#include "../core/Project.h"
#include "../core/Sequence.h"
#include "../core/TrigCondition.h"
#include "../machine/SamplerMachine.h"
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
        if (c.fillRule != FillRule::Always)
            v.setProperty("fr", static_cast<int>(c.fillRule), nullptr);
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
        const int fr = static_cast<int>(v.getProperty("fr", 0));
        c.fillRule = static_cast<FillRule>(std::clamp(fr, 0, 2));
        return c;
    }

    // -------------------------------------------------------------------------
    // v2 helpers: separate pattern-track and part-track serialization


    // Writes machineId and base params for one PartTrack into a <PartTrack> node.
    static juce::ValueTree writePartTrackNode(int t, const PartTrack& pt,
                                               LockstepProcessor& proc)
    {
        juce::ValueTree node("PartTrack");
        node.setProperty("i", t, nullptr);
        node.setProperty("machineId", juce::String(pt.machineId), nullptr);
        if (!pt.destinationId.empty())
            node.setProperty("destinationId", juce::String(pt.destinationId), nullptr);

        const int np = proc.numParams(t);
        if (np > 0)
        {
            juce::ValueTree bpNode("BaseParams");
            for (int s = 0; s < np; ++s)
            {
                const juce::String id = proc.idForSlot(t, s);
                if (id.isEmpty()) continue;
                const float def = proc.paramSpec(t, s).defaultValue;
                float val;
                if (static_cast<std::size_t>(s) < pt.baseParams.size())
                    val = pt.baseParams[static_cast<std::size_t>(s)];
                else if (id.startsWith("lockstep.fltr."))
                    val = pt.fltrState.getSlot(s - static_cast<int>(pt.baseParams.size()));
                else if (id.startsWith("lockstep.amp."))
                {
                    const int ampBase = proc.numParams(t) - TrackAmpState::kNumSlots;
                    val = pt.ampState.getSlot(s - ampBase);
                }
                else
                    val = 0.0f;
                if (!floatNe(val, def)) continue;
                juce::ValueTree pNode("P");
                pNode.setProperty("id", id,                       nullptr);
                pNode.setProperty("v",  static_cast<double>(val), nullptr);
                bpNode.appendChild(pNode, nullptr);
            }
            if (bpNode.getNumChildren() > 0)
                node.appendChild(bpNode, nullptr);
        }
        return node;
    }

    // Writes the full Project hierarchy as a <Project> node.
    // Uses proc for slot-ID lookups and step P-Lock resolution.
    static void writeProjectNode(juce::ValueTree& root, LockstepProcessor& proc)
    {
        juce::ValueTree projNode("Project");

        for (int bi = 0; bi < static_cast<int>(kNumBanks); ++bi)
        {
            const auto& bank = proc.project().banks[static_cast<std::size_t>(bi)];
            juce::ValueTree bankNode("Bank");
            bankNode.setProperty("i", bi, nullptr);
            bool bankHasContent = false;

            // Write non-empty Patterns
            for (int pi = 0; pi < static_cast<int>(kPatternsPerBank); ++pi)
            {
                const auto& pattern = bank.patterns[static_cast<std::size_t>(pi)];
                juce::ValueTree patNode("Pattern");
                patNode.setProperty("i",       pi,              nullptr);
                patNode.setProperty("partRef", pattern.partRef, nullptr);

                // MD.7: pattern mute mask — serialize as a bitfield.
                int muteMask = 0;
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                    if (pattern.patternMutes[static_cast<std::size_t>(t)])
                        muteMask |= (1 << t);
                if (muteMask != 0)
                    patNode.setProperty("pmutes", muteMask, nullptr);

                bool patHasContent = false;
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    const auto& track = pattern.sequence.tracks[static_cast<std::size_t>(t)];
                    const bool hasCond  = !track.baseCond.isTrivial();
                    const bool hasTrig  = (track.trigDefaults.note != 60
                                       || track.trigDefaults.velocity != 100
                                       || floatNe(track.trigDefaults.gateMs, 0.0f));
                    bool hasStep = false;
                    for (const auto& step : track.steps)
                    {
                        if (step.trig || !step.overrides.empty()
                            || step.trigOverride.hasNote
                            || step.trigOverride.hasVelocity
                            || step.trigOverride.hasGate
                            || !step.condition.isTrivial())
                        {
                            hasStep = true;
                            break;
                        }
                    }
                    if (!hasCond && !hasTrig && !hasStep) continue;

                    juce::ValueTree trackNode("Track");
                    trackNode.setProperty("i", t, nullptr);

                    if (hasCond)
                        trackNode.appendChild(condToTree("BaseCond", track.baseCond), nullptr);

                    if (hasTrig)
                    {
                        juce::ValueTree tdNode("TrigDefaults");
                        tdNode.setProperty("note",   track.trigDefaults.note,   nullptr);
                        tdNode.setProperty("vel",    track.trigDefaults.velocity, nullptr);
                        tdNode.setProperty("gateMs", static_cast<double>(track.trigDefaults.gateMs), nullptr);
                        trackNode.appendChild(tdNode, nullptr);
                    }

                    if (hasStep)
                    {
                        juce::ValueTree stepsNode("Steps");
                        for (int s = 0; s < kMaxStepsPerTrack; ++s)
                        {
                            const auto& step = track.steps[static_cast<std::size_t>(s)];
                            const bool hp  = !step.overrides.empty();
                            const bool hto = step.trigOverride.hasNote
                                          || step.trigOverride.hasVelocity
                                          || step.trigOverride.hasGate;
                            const bool hnc = !step.condition.isTrivial();
                            if (!step.trig && !hp && !hto && !hnc) continue;

                            juce::ValueTree stepNode("S");
                            stepNode.setProperty("i", s,                  nullptr);
                            stepNode.setProperty("t", step.trig ? 1 : 0,  nullptr);

                            if (hnc)
                                stepNode.appendChild(condToTree("C", step.condition), nullptr);

                            if (hto)
                            {
                                juce::ValueTree toNode("TO");
                                if (step.trigOverride.hasNote)
                                {
                                    toNode.setProperty("hn", 1,                     nullptr);
                                    toNode.setProperty("n",  step.trigOverride.note, nullptr);
                                }
                                if (step.trigOverride.hasVelocity)
                                {
                                    toNode.setProperty("hv", 1,                          nullptr);
                                    toNode.setProperty("v",  step.trigOverride.velocity,  nullptr);
                                }
                                if (step.trigOverride.hasGate)
                                {
                                    toNode.setProperty("hg", 1,                                             nullptr);
                                    toNode.setProperty("g",  static_cast<double>(step.trigOverride.gateMs), nullptr);
                                }
                                stepNode.appendChild(toNode, nullptr);
                            }

                            if (hp)
                            {
                                juce::ValueTree plNode("PL");
                                step.overrides.forEach([&](int slot, float value) {
                                    const juce::String id = proc.idForSlot(t, slot);
                                    if (id.isEmpty()) return;
                                    juce::ValueTree lNode("L");
                                    lNode.setProperty("id", id,                         nullptr);
                                    lNode.setProperty("v",  static_cast<double>(value), nullptr);
                                    plNode.appendChild(lNode, nullptr);
                                });
                                if (plNode.getNumChildren() > 0)
                                    stepNode.appendChild(plNode, nullptr);
                            }

                            stepsNode.appendChild(stepNode, nullptr);
                        }
                        if (stepsNode.getNumChildren() > 0)
                            trackNode.appendChild(stepsNode, nullptr);
                    }

                    patNode.appendChild(trackNode, nullptr);
                    patHasContent = true;
                }

                // Always write the active pattern; skip empty non-active ones.
                const bool isActive = (bi == proc.activeBankIdx()
                                    && pi == proc.activePatternIdx());
                if (patHasContent || isActive)
                {
                    bankNode.appendChild(patNode, nullptr);
                    bankHasContent = true;
                }
            }

            // Write non-default Parts
            for (int ri = 0; ri < static_cast<int>(kPartsPerBank); ++ri)
            {
                const auto& part = bank.parts[static_cast<std::size_t>(ri)];
                juce::ValueTree partNode("Part");
                partNode.setProperty("i", ri, nullptr);
                bool partHasContent = false;

                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    const auto& pt = part.tracks[static_cast<std::size_t>(t)];
                    const juce::ValueTree ptNode = writePartTrackNode(t, pt, proc);
                    const bool nonDefaultId = (pt.machineId != "lockstep.sampler.v1");
                    const bool nonDefaultBp = ptNode.getChildWithName("BaseParams").isValid();
                    if (nonDefaultId || nonDefaultBp)
                    {
                        partNode.appendChild(ptNode, nullptr);
                        partHasContent = true;
                    }
                }

                // Always write Part 0 (the default kit) and the active Part.
                const int activePartRef = proc.activePattern().partRef;
                const bool isActivePart = (bi == proc.activeBankIdx()
                                        && ri == activePartRef);
                if (partHasContent || ri == 0 || isActivePart)
                {
                    bankNode.appendChild(partNode, nullptr);
                    bankHasContent = true;
                }
            }

            if (bankHasContent || bi == proc.activeBankIdx())
                projNode.appendChild(bankNode, nullptr);
        }

        root.appendChild(projNode, nullptr);
    }

    // -------------------------------------------------------------------------
    // v2 read helpers

    static void readPatternTrackFromNode(const juce::ValueTree& trackNode,
                                          Track& track, LockstepProcessor& proc, int t)
    {
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

        const auto stepsNode = trackNode.getChildWithName("Steps");
        if (!stepsNode.isValid()) return;

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

    static void readPartTrackFromNode(const juce::ValueTree& ptNode,
                                       PartTrack& pt, LockstepProcessor& proc, int t)
    {
        pt.machineId     = ptNode.getProperty("machineId",
                                              "lockstep.sampler.v1").toString().toStdString();
        pt.destinationId = ptNode.getProperty("destinationId",
                                              "").toString().toStdString();

        const auto bpNode = ptNode.getChildWithName("BaseParams");
        if (!bpNode.isValid()) return;

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
            const auto slotSz = static_cast<std::size_t>(slot);
            if (slotSz < pt.baseParams.size())
                pt.baseParams[slotSz] = val;
            else if (id.startsWith("lockstep.fltr."))
                pt.fltrState.setSlot(slot - static_cast<int>(pt.baseParams.size()), val);
            else if (id.startsWith("lockstep.amp."))
            {
                const int ampBase = proc.numParams(t) - TrackAmpState::kNumSlots;
                pt.ampState.setSlot(slot - ampBase, val);
            }
        }
    }

    static void readProjectNode(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto projNode = root.getChildWithName("Project");
        if (!projNode.isValid()) return;

        for (auto bankNode : projNode)
        {
            const int bi = static_cast<int>(bankNode.getProperty("i", -1));
            if (bi < 0 || bi >= static_cast<int>(kNumBanks)) continue;
            auto& bank = proc.project().banks[static_cast<std::size_t>(bi)];

            for (auto child : bankNode)
            {
                if (child.getType() == juce::Identifier("Pattern"))
                {
                    const int pi = static_cast<int>(child.getProperty("i", -1));
                    if (pi < 0 || pi >= static_cast<int>(kPatternsPerBank)) continue;
                    auto& pattern = bank.patterns[static_cast<std::size_t>(pi)];
                    pattern.partRef = std::clamp(
                        static_cast<int>(child.getProperty("partRef", 0)),
                        0, static_cast<int>(kPartsPerBank) - 1);

                    // MD.7: restore pattern mute mask from bitfield.
                    {
                        const int pmutes = static_cast<int>(child.getProperty("pmutes", 0));
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            pattern.patternMutes[static_cast<std::size_t>(t)] =
                                ((pmutes >> t) & 1) != 0;
                    }

                    for (auto trackNode : child)
                    {
                        const int t = static_cast<int>(trackNode.getProperty("i", -1));
                        if (t < 0 || t >= static_cast<int>(kNumTracks)) continue;
                        readPatternTrackFromNode(trackNode,
                            pattern.sequence.tracks[static_cast<std::size_t>(t)], proc, t);
                    }
                }
                else if (child.getType() == juce::Identifier("Part"))
                {
                    const int ri = static_cast<int>(child.getProperty("i", -1));
                    if (ri < 0 || ri >= static_cast<int>(kPartsPerBank)) continue;
                    auto& part = bank.parts[static_cast<std::size_t>(ri)];

                    for (auto ptNode : child)
                    {
                        const int t = static_cast<int>(ptNode.getProperty("i", -1));
                        if (t < 0 || t >= static_cast<int>(kNumTracks)) continue;
                        readPartTrackFromNode(ptNode,
                            part.tracks[static_cast<std::size_t>(t)], proc, t);
                    }
                }
            }
        }

        // After loading all banks/patterns/parts, sync the active pattern's
        // Track.baseParams from its Part so the audio thread has consistent data.
        const auto& activePart = proc.activePart();
        auto& seq = proc.sequence();
        for (std::size_t t = 0; t < kNumTracks; ++t)
            seq.tracks[t].baseParams = activePart.tracks[t].baseParams;
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
    // CC mappings + focus track + local BPM

    static const char* scopeToStr(CCScope s)
    {
        switch (s)
        {
        case CCScope::Global:        return "Global";
        case CCScope::Track:         return "Track";
        case CCScope::SelectedTrack: return "SelectedTrack";
        case CCScope::Contextual:    return "Contextual";
        }
        return "Track";
    }

    static CCScope strToScope(const juce::String& s)
    {
        if (s == "Global")        return CCScope::Global;
        if (s == "SelectedTrack") return CCScope::SelectedTrack;
        if (s == "Contextual")    return CCScope::Contextual;
        return CCScope::Track;
    }

    static void writeMiscState(juce::ValueTree& root, LockstepProcessor& proc)
    {
        // CC mappings — slot indices converted to stable string IDs.
        juce::ValueTree ccNode("CCMappings");
        for (const auto& m : proc.ccMappingTable().mappings())
        {
            juce::ValueTree mNode("M");
            mNode.setProperty("cc",    m.ccNumber,               nullptr);
            mNode.setProperty("scope", scopeToStr(m.scope),      nullptr);
            mNode.setProperty("track", m.trackIndex,             nullptr);
            mNode.setProperty("mz",    m.mzPosition,             nullptr);
            mNode.setProperty("apvts", juce::String(m.apvtsID),  nullptr);
            mNode.setProperty("rel",   m.isRelative ? 1 : 0,     nullptr);
            mNode.setProperty("scale", static_cast<double>(m.scale), nullptr);
            mNode.setProperty("enc",   static_cast<int>(m.encoding), nullptr);

            // Resolve the slot to a stable string ID so renames survive.
            // For SelectedTrack scope, track 0 is used as the schema reference.
            juce::String slotId;
            if (m.scope == CCScope::Track || m.scope == CCScope::SelectedTrack)
            {
                const int refTrack = (m.scope == CCScope::Track) ? m.trackIndex : 0;
                slotId = proc.idForSlot(refTrack, m.slot);
            }
            mNode.setProperty("slotId", slotId, nullptr);

            ccNode.appendChild(mNode, nullptr);
        }
        root.appendChild(ccNode, nullptr);

        // Focus track + standalone BPM + active pattern address.
        juce::ValueTree miscNode("Misc");
        miscNode.setProperty("focusTrack",      proc.focusTrack(),        nullptr);
        miscNode.setProperty("localBpm",        proc.clock().localBpm(),  nullptr);
        miscNode.setProperty("activeBankIdx",   proc.activeBankIdx(),     nullptr);
        miscNode.setProperty("activePatternIdx",proc.activePatternIdx(),  nullptr);
        root.appendChild(miscNode, nullptr);
    }

    static void readMiscState(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto ccNode = root.getChildWithName("CCMappings");
        if (ccNode.isValid())
        {
            proc.ccMappingTable().clear();
            for (auto mNode : ccNode)
            {
                CCMapping m;
                m.ccNumber   = static_cast<int>(mNode.getProperty("cc",  -1));
                m.scope      = strToScope(mNode.getProperty("scope").toString());
                m.trackIndex = static_cast<int>(mNode.getProperty("track", 0));
                m.mzPosition = static_cast<int>(mNode.getProperty("mz",   -1));
                m.apvtsID    = mNode.getProperty("apvts").toString().toStdString();
                m.isRelative = (static_cast<int>(mNode.getProperty("rel", 0)) != 0);
                m.scale      = getFloat(mNode, "scale", 1.0f / 128.0f);
                m.encoding   = static_cast<RelativeCCEncoding>(
                                   static_cast<int>(mNode.getProperty("enc", 0)));

                // Resolve slot ID back to a runtime integer.
                const juce::String slotId = mNode.getProperty("slotId").toString();
                if (!slotId.isEmpty())
                {
                    const int refTrack = (m.scope == CCScope::Track) ? m.trackIndex : 0;
                    m.slot = proc.slotForId(refTrack, slotId);
                    if (m.slot < 0)
                    {
                        DBG("PluginState: unknown CC slot id '" + slotId + "' -- skipping mapping");
                        continue;
                    }
                }

                if (m.ccNumber >= 0)
                    proc.ccMappingTable().addMapping(std::move(m));
            }
        }

        const auto miscNode = root.getChildWithName("Misc");
        if (miscNode.isValid())
        {
            proc.setFocusTrack(static_cast<int>(miscNode.getProperty("focusTrack", -1)));
            const double bpm = static_cast<double>(miscNode.getProperty("localBpm", 120.0));
            proc.clock().setLocalBpm(bpm);
            // Restore active pattern — setActivePattern validates range and syncs Part.
            const int bankIdx    = static_cast<int>(miscNode.getProperty("activeBankIdx",    0));
            const int patternIdx = static_cast<int>(miscNode.getProperty("activePatternIdx", 0));
            proc.setActivePattern(bankIdx, patternIdx);
        }
    }

    // -------------------------------------------------------------------------
    // Version upgrade chain
    //
    // Each upgrade_vN_to_vM function receives a tree at version N and returns
    // a tree at version M. applyUpgrades() calls them in order so that any
    // historical version round-trips into the current format before the
    // read-back functions run.
    //
    // To add a future version bump:
    //   1. Increment kCurrentVersion in PluginState.h.
    //   2. Add upgrade_v1_to_v2 (or vN_to_vN+1) here.
    //   3. Add the corresponding `if (version < N) tree = upgrade_v(N-1)_to_vN(tree);`
    //      line in applyUpgrades().
    //   4. Add a new test case in PluginStateUpgradeTest below.

    // v0 → v1
    // v0 format: root type "Lockstep" (bare APVTS tree), no version attribute.
    // v1 format: root type "LockstepState", version=1, with "Lockstep" (APVTS),
    //            "SamplePool", and "Sequence" children.
    juce::ValueTree upgrade_v0_to_v1(const juce::ValueTree& v0)
    {
        juce::ValueTree v1("LockstepState");
        v1.setProperty("version", 1, nullptr);
        v1.appendChild(v0.createCopy(), nullptr);
        // No SamplePool or Sequence in v0; those nodes are absent,
        // so the readers leave the pool empty and the sequence at defaults.
        return v1;
    }

    // v1 → v2
    // v1 format: flat "Sequence" node with per-Track BaseParams mixed in.
    // v2 format: "Project/Bank[0]/Pattern[0]" (trig data) +
    //            "Project/Bank[0]/Part[0]"    (machine identity + base params).
    juce::ValueTree upgrade_v1_to_v2(const juce::ValueTree& v1)
    {
        juce::ValueTree v2("LockstepState");
        v2.setProperty("version", 2, nullptr);

        // Copy all non-Sequence children unchanged.
        for (int i = 0; i < v1.getNumChildren(); ++i)
        {
            const auto child = v1.getChild(i);
            if (child.getType() != juce::Identifier("Sequence"))
                v2.appendChild(child.createCopy(), nullptr);
        }

        // Build Project/Bank[0]/Pattern[0] + Part[0] from the old Sequence.
        juce::ValueTree projNode("Project");
        juce::ValueTree bankNode("Bank");
        bankNode.setProperty("i", 0, nullptr);

        juce::ValueTree patNode("Pattern");
        patNode.setProperty("i",       0, nullptr);
        patNode.setProperty("partRef", 0, nullptr);

        juce::ValueTree partNode("Part");
        partNode.setProperty("i", 0, nullptr);

        const auto seqNode = v1.getChildWithName("Sequence");
        if (seqNode.isValid())
        {
            for (auto trackNode : seqNode)
            {
                const juce::var trackIdx = trackNode.getProperty("i", -1);
                if (static_cast<int>(trackIdx) < 0) continue;

                // Pattern track: BaseCond, TrigDefaults, Steps (no BaseParams).
                juce::ValueTree patTrackNode("Track");
                patTrackNode.setProperty("i", trackIdx, nullptr);

                // Part track: machineId + BaseParams.
                juce::ValueTree ptNode("PartTrack");
                ptNode.setProperty("i", trackIdx, nullptr);
                ptNode.setProperty("machineId", SamplerMachine::kMachineId, nullptr);

                for (int j = 0; j < trackNode.getNumChildren(); ++j)
                {
                    const auto child = trackNode.getChild(j);
                    if (child.getType() == juce::Identifier("BaseParams"))
                        ptNode.appendChild(child.createCopy(), nullptr);
                    else
                        patTrackNode.appendChild(child.createCopy(), nullptr);
                }

                patNode.appendChild(patTrackNode, nullptr);
                partNode.appendChild(ptNode, nullptr);
            }
        }

        bankNode.appendChild(patNode,  nullptr);
        bankNode.appendChild(partNode, nullptr);
        projNode.appendChild(bankNode, nullptr);
        v2.appendChild(projNode, nullptr);

        return v2;
    }

    juce::ValueTree applyUpgrades(juce::ValueTree tree)
    {
        // Determine the version. v0 has root type "Lockstep" and no version attribute.
        int version = 0;
        if (tree.getType() == juce::Identifier("LockstepState"))
            version = static_cast<int>(tree.getProperty("version", 0));

        if (version > kCurrentVersion)
            DBG("PluginState: state version " + juce::String(version)
                + " is newer than this build (supports up to v"
                + juce::String(kCurrentVersion) + "); loading anyway");

        // Apply each upgrade in order. Upgrades are idempotent with respect
        // to the chain: each runs only when needed by the version guard.
        if (version < 1) tree = upgrade_v0_to_v1(tree);
        if (version < 2) tree = upgrade_v1_to_v2(tree);

        return tree;
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

        // Full Project hierarchy (all banks, patterns, parts)
        writeProjectNode(root, proc);

        // CC mappings, focus track, standalone BPM
        writeMiscState(root, proc);

        if (auto xml = root.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, dest);
    }

    void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (!xml) return;

        auto root = juce::ValueTree::fromXml(*xml);
        if (!root.isValid()) return;

        root = applyUpgrades(root);

        // Restore APVTS parameters. The child type matches the valueTreeType
        // passed to AudioProcessorValueTreeState ("Lockstep").
        const auto apvtsChild = root.getChildWithName("Lockstep");
        if (apvtsChild.isValid())
            proc.apvts().replaceState(apvtsChild);

        // Sample pool must be restored before sequence so that pool indices
        // referenced by P-Locks and baseParams resolve to the right entries.
        readSamplePool(root, proc);
        // readProject reads the full hierarchy; readMiscState sets the active pattern.
        readProjectNode(root, proc);
        readMiscState(root, proc);
    }

} // namespace lockstep::PluginState

// ---------------------------------------------------------------------------
// Upgrade-chain guard test (debug builds only)
//
// Registered with the global JUCE unit-test runner under the "PluginState"
// category. The processor constructor runs this category in debug mode so
// failures surface immediately at load time.
#if JUCE_DEBUG
namespace
{
    using namespace lockstep;

    class PluginStateUpgradeTest : public juce::UnitTest
    {
    public:
        PluginStateUpgradeTest()
            : juce::UnitTest("Upgrade chain", "PluginState") {}

        void runTest() override
        {
            beginTest("v0 -> v1: bare APVTS tree is wrapped correctly");
            {
                juce::ValueTree v0("Lockstep");
                v0.setProperty("someParam", 0.5, nullptr);

                const auto v1 = lockstep::PluginState::applyUpgrades(v0);

                expect(v1.getType() == juce::Identifier("LockstepState"),
                       "root type must be LockstepState");
                expectEquals(static_cast<int>(v1.getProperty("version", -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version must be current");
                expect(v1.getChildWithName("Lockstep").isValid(),
                       "upgraded tree must contain APVTS child");
                expect(!v1.getChildWithName("SamplePool").isValid(),
                       "v0 upgrades must not invent a SamplePool node");
                expect(!v1.getChildWithName("Sequence").isValid(),
                       "v0 upgrades must not invent a Sequence node");
            }

            beginTest("v1 -> v2: Sequence split into Project/Bank/Pattern+Part");
            {
                // Build a minimal v1 tree with a Track containing BaseParams and Steps.
                juce::ValueTree v1("LockstepState");
                v1.setProperty("version", 1, nullptr);
                v1.appendChild(juce::ValueTree("Lockstep"),   nullptr);
                v1.appendChild(juce::ValueTree("SamplePool"), nullptr);

                juce::ValueTree seq("Sequence");
                juce::ValueTree track0("Track");
                track0.setProperty("i", 0, nullptr);

                juce::ValueTree bp("BaseParams");
                juce::ValueTree p("P");
                p.setProperty("id", "sample_id", nullptr);
                p.setProperty("v",  1.0,          nullptr);
                bp.appendChild(p, nullptr);
                track0.appendChild(bp, nullptr);

                juce::ValueTree steps("Steps");
                juce::ValueTree s("S");
                s.setProperty("i", 0, nullptr);
                s.setProperty("t", 1, nullptr);
                steps.appendChild(s, nullptr);
                track0.appendChild(steps, nullptr);
                seq.appendChild(track0, nullptr);
                v1.appendChild(seq, nullptr);

                v1.appendChild(juce::ValueTree("CCMappings"), nullptr);
                v1.appendChild(juce::ValueTree("Misc"),       nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v1);

                expectEquals(static_cast<int>(result.getProperty("version", -1)), 2,
                             "version bumped to 2");
                expect(!result.getChildWithName("Sequence").isValid(),
                       "old Sequence node removed");
                const auto proj = result.getChildWithName("Project");
                expect(proj.isValid(), "Project node present");

                const auto bank0 = proj.getChildWithName("Bank");
                expect(bank0.isValid(), "Bank[0] present");

                const auto pat0  = bank0.getChildWithName("Pattern");
                expect(pat0.isValid(), "Pattern[0] present");
                expectEquals(static_cast<int>(pat0.getProperty("partRef", -1)), 0,
                             "Pattern[0] references Part[0]");

                // Track[0] in Pattern[0] must have Steps but no BaseParams.
                const auto patTrack0 = pat0.getChildWithName("Track");
                expect(patTrack0.isValid(), "Pattern Track[0] present");
                expect(patTrack0.getChildWithName("Steps").isValid(),
                       "Steps moved to Pattern track");
                expect(!patTrack0.getChildWithName("BaseParams").isValid(),
                       "BaseParams must NOT be in Pattern track");

                // Part[0]/PartTrack[0] must have BaseParams.
                const auto part0 = bank0.getChildWithName("Part");
                expect(part0.isValid(), "Part[0] present");
                const auto pt0 = part0.getChildWithName("PartTrack");
                expect(pt0.isValid(), "PartTrack[0] present");
                expect(pt0.getChildWithName("BaseParams").isValid(),
                       "BaseParams present in PartTrack");
                expect(pt0.getProperty("machineId").toString()
                       == lockstep::SamplerMachine::kMachineId,
                       "machineId set to sampler.v1");
            }

            beginTest("v2 passthrough: current format unchanged");
            {
                juce::ValueTree v2("LockstepState");
                v2.setProperty("version", 2, nullptr);
                v2.appendChild(juce::ValueTree("Lockstep"),    nullptr);
                v2.appendChild(juce::ValueTree("SamplePool"),  nullptr);
                v2.appendChild(juce::ValueTree("Project"),     nullptr);
                v2.appendChild(juce::ValueTree("CCMappings"),  nullptr);
                v2.appendChild(juce::ValueTree("Misc"),        nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v2);

                expectEquals(static_cast<int>(result.getProperty("version", -1)), 2,
                             "version preserved at 2");
                expect(result.getChildWithName("Project").isValid(), "Project child present");
                expect(!result.getChildWithName("Sequence").isValid(), "no legacy Sequence");
            }

            beginTest("future version: valid tree returned without crash");
            {
                juce::ValueTree future("LockstepState");
                future.setProperty("version", lockstep::PluginState::kCurrentVersion + 1, nullptr);
                future.appendChild(juce::ValueTree("Lockstep"), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(future);

                expect(result.isValid(), "future version must return a valid tree");
                expect(result.getChildWithName("Lockstep").isValid(),
                       "APVTS child must be intact");
            }
        }
    } gUpgradeTest;
} // anonymous namespace
#endif
