#include "PluginState.h"
#include "../PluginProcessor.h"
#include "../core/Bank.h"
#include "../core/Pattern.h"
#include "../core/Part.h"
#include "../core/Phrase.h"
#include "../core/Song.h"
#include "../core/Project.h"
#include "../core/Sequence.h"
#include "../core/TrackKit.h"
#include "../core/TrigCondition.h"
#include "../machine/StubMachine.h"
#include "../machine/MidiDevicePresets.h"
#include "../machine/MidiOutMachine.h"
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
        // Legacy: "fr" was fillRule (0=Always, 1=OnlyFill, 2=NeverFill).
        // Now handled at step level as fillTrigState (see stepFromNode).
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
        if (!pt.midiPresetName.empty())
            node.setProperty("midiPreset", juce::String(pt.midiPresetName), nullptr);

        // Use a temp machine matching pt.machineId so that slot IDs and param
        // counts reflect THIS Part's machine, not the currently installed one.
        auto tempMachine = proc.createMachineForId(pt.machineId);
        const int machinNp = tempMachine->numParams();
        const int np       = proc.numSlotsWithMachine(*tempMachine);
        if (np > 0)
        {
            juce::ValueTree bpNode("BaseParams");
            for (int s = 0; s < np; ++s)
            {
                const auto spec = proc.paramSpecWithMachine(*tempMachine, s);
                const juce::String id = spec.id;
                if (id.isEmpty()) continue;
                const float def = spec.defaultValue;
                float val;
                if (static_cast<std::size_t>(s) < pt.baseParams.size())
                    val = pt.baseParams[static_cast<std::size_t>(s)];
                else if (id.startsWith("lockstep.fltr."))
                    val = pt.fltrState.getSlot(s - machinNp);
                else if (id.startsWith("lockstep.amp."))
                {
                    const int ampBase = machinNp
                        + (tempMachine->hasInternalFilter() ? 0 : TrackFltrState::kNumSlots);
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

        // MF.4: serialize CC slot configuration for MIDI-out tracks.
        if (!pt.midiCCNumbers.empty() || !pt.midiCCLabels.empty())
        {
            juce::ValueTree ccNode("CCConfig");
            for (int ci = 0; ci < MidiOutMachine::kNumCCs; ++ci)
            {
                const int n = (ci < static_cast<int>(pt.midiCCNumbers.size()))
                                  ? pt.midiCCNumbers[static_cast<std::size_t>(ci)] : ci;
                const std::string& lbl = (ci < static_cast<int>(pt.midiCCLabels.size()))
                                             ? pt.midiCCLabels[static_cast<std::size_t>(ci)] : "";
                if (n == ci && lbl.empty()) continue;
                juce::ValueTree ccEntry("CC");
                ccEntry.setProperty("i", ci, nullptr);
                ccEntry.setProperty("n", n,  nullptr);
                if (!lbl.empty())
                    ccEntry.setProperty("label", juce::String(lbl), nullptr);
                ccNode.appendChild(ccEntry, nullptr);
            }
            if (ccNode.getNumChildren() > 0)
                node.appendChild(ccNode, nullptr);
        }

        return node;
    }

    // Writes the full Project hierarchy as a <Project> node.
    // Uses proc for slot-ID lookups and step P-Lock resolution.
    // ── Phase 7 / Stage F: new musical hierarchy serialization ───────────────

    static juce::ValueTree writePhraseNode(int phraseIdx, const Phrase& phrase)
    {
        juce::ValueTree node("Phrase");
        node.setProperty("i",   phraseIdx,     nullptr);
        node.setProperty("len", phrase.length, nullptr);

        if (phrase.noteSelection != NoteSelection::TopBias)
            node.setProperty("nsel", static_cast<int>(phrase.noteSelection), nullptr);
        if (!phrase.baseCond.isTrivial())
            node.appendChild(condToTree("BaseCond", phrase.baseCond), nullptr);

        const auto& td = phrase.trigDefaults;
        if (td.note != 60 || td.velocity != 100 || td.gateValue != MusicalGate::None)
        {
            juce::ValueTree tdNode("TrigDefaults");
            tdNode.setProperty("note",  td.note,     nullptr);
            tdNode.setProperty("vel",   td.velocity,  nullptr);
            tdNode.setProperty("gateV", static_cast<int>(static_cast<uint8_t>(td.gateValue)), nullptr);
            node.appendChild(tdNode, nullptr);
        }

        juce::ValueTree stepsNode("Steps");
        bool hasSteps = false;
        for (int s = 0; s < kMaxStepsPerTrack; ++s)
        {
            const auto& step = phrase.steps[static_cast<std::size_t>(s)];
            if (!step.trig && step.overrides.empty()
                && step.trigOverride.noteCount == 0 && !step.trigOverride.hasVelocity
                && !step.trigOverride.hasGate && step.condition.isTrivial()
                && step.fillTrigState == FillTrigState::Inherit
                && step.fillOverrides.empty()
                && step.fillTrigOverride.noteCount == 0) continue;
            hasSteps = true;
            juce::ValueTree stepNode("S");
            stepNode.setProperty("i", s,                 nullptr);
            stepNode.setProperty("t", step.trig ? 1 : 0, nullptr);
            if (!step.condition.isTrivial())
                stepNode.appendChild(condToTree("C", step.condition), nullptr);
            if (step.trigOverride.noteCount > 0 || step.trigOverride.hasVelocity
                || step.trigOverride.hasGate)
            {
                juce::ValueTree toNode("TO");
                if (step.trigOverride.noteCount > 0)
                {
                    toNode.setProperty("nc", step.trigOverride.noteCount, nullptr);
                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                        toNode.setProperty("n" + juce::String(ni),
                                           step.trigOverride.notes[static_cast<std::size_t>(ni)], nullptr);
                }
                if (step.trigOverride.hasVelocity)
                { toNode.setProperty("hv", 1, nullptr); toNode.setProperty("v", step.trigOverride.velocity, nullptr); }
                if (step.trigOverride.hasGate)
                { toNode.setProperty("hg", 1, nullptr); toNode.setProperty("gv", static_cast<int>(static_cast<uint8_t>(step.trigOverride.gateValue)), nullptr); }
                stepNode.appendChild(toNode, nullptr);
            }
            if (!step.overrides.empty())
            {
                juce::ValueTree plNode("PL");
                step.overrides.forEach([&](int slot, float value)
                {
                    juce::ValueTree pNode("P");
                    pNode.setProperty("s", slot, nullptr);
                    pNode.setProperty("v", static_cast<double>(value), nullptr);
                    plNode.appendChild(pNode, nullptr);
                });
                stepNode.appendChild(plNode, nullptr);
            }
            stepsNode.appendChild(stepNode, nullptr);
        }
        if (hasSteps) node.appendChild(stepsNode, nullptr);
        return node;
    }

    static void readPhraseFromNode(const juce::ValueTree& node, Phrase& phrase)
    {
        phrase.length = std::clamp(static_cast<int>(node.getProperty("len", 16)), 1, kMaxStepsPerTrack);
        if (node.hasProperty("nsel"))
            phrase.noteSelection = static_cast<NoteSelection>(static_cast<int>(node.getProperty("nsel", 0)));

        const auto bcNode = node.getChildWithName("BaseCond");
        if (bcNode.isValid()) phrase.baseCond = condFromTree(bcNode);

        const auto tdNode = node.getChildWithName("TrigDefaults");
        if (tdNode.isValid())
        {
            phrase.trigDefaults.note      = static_cast<int>(tdNode.getProperty("note",  60));
            phrase.trigDefaults.velocity  = static_cast<int>(tdNode.getProperty("vel",  100));
            phrase.trigDefaults.gateValue = static_cast<MusicalGate>(
                static_cast<uint8_t>(static_cast<int>(tdNode.getProperty("gateV", 0))));
        }

        const auto stepsNode = node.getChildWithName("Steps");
        if (!stepsNode.isValid()) return;
        for (auto stepNode : stepsNode)
        {
            const int s = static_cast<int>(stepNode.getProperty("i", -1));
            if (s < 0 || s >= kMaxStepsPerTrack) continue;
            auto& step = phrase.steps[static_cast<std::size_t>(s)];
            step.trig  = (static_cast<int>(stepNode.getProperty("t", 0)) != 0);
            const auto cNode = stepNode.getChildWithName("C");
            if (cNode.isValid()) step.condition = condFromTree(cNode);
            const auto toNode = stepNode.getChildWithName("TO");
            if (toNode.isValid())
            {
                step.trigOverride.noteCount = static_cast<int>(toNode.getProperty("nc", 0));
                for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                    step.trigOverride.notes[static_cast<std::size_t>(ni)] =
                        static_cast<int>(toNode.getProperty("n" + juce::String(ni), 60));
                if (static_cast<int>(toNode.getProperty("hv", 0)) != 0)
                { step.trigOverride.hasVelocity = true; step.trigOverride.velocity = static_cast<int>(toNode.getProperty("v", 100)); }
                if (static_cast<int>(toNode.getProperty("hg", 0)) != 0)
                { step.trigOverride.hasGate = true; step.trigOverride.gateValue = static_cast<MusicalGate>(static_cast<uint8_t>(static_cast<int>(toNode.getProperty("gv", 0)))); }
            }
            const auto plNode = stepNode.getChildWithName("PL");
            if (plNode.isValid())
                for (auto pNode : plNode)
                {
                    const int sl = static_cast<int>(pNode.getProperty("s", -1));
                    if (sl >= 0) step.overrides.set(sl, getFloat(pNode, "v", 0.0f));
                }
        }
    }

    static juce::ValueTree writeKitNode(int trackIdx, const TrackKit& kit, LockstepProcessor& proc)
    {
        juce::ValueTree node("Kit");
        node.setProperty("t",       trackIdx,              nullptr);
        node.setProperty("mId",     juce::String(kit.machineId),   nullptr);
        if (!kit.destinationId.empty())
            node.setProperty("dId", juce::String(kit.destinationId), nullptr);
        if (!kit.midiPresetName.empty())
            node.setProperty("mPreset", juce::String(kit.midiPresetName), nullptr);
        if (kit.divider != 1)
            node.setProperty("div", kit.divider, nullptr);

        // Base params + post-machine FLTR/AMP via temp machine to get correct
        // slot IDs. The slot range covers machine params, then the foundation
        // FLTR block, then AMP (ids "lockstep.fltr.*" / "lockstep.amp.*") — same
        // id-keyed scheme as the legacy PartTrack so the kit owns its sound.
        auto tempMachine = proc.createMachineForId(kit.machineId);
        const int machinNp = tempMachine->numParams();
        const int np       = proc.numSlotsWithMachine(*tempMachine);
        if (np > 0)
        {
            juce::ValueTree bpNode("BP");
            for (int s = 0; s < np; ++s)
            {
                const auto spec = proc.paramSpecWithMachine(*tempMachine, s);
                const juce::String id = spec.id;
                if (id.isEmpty()) continue;
                const float def = spec.defaultValue;
                float val;
                if (s < machinNp && static_cast<std::size_t>(s) < kit.baseParams.size())
                    val = kit.baseParams[static_cast<std::size_t>(s)];
                else if (id.startsWith("lockstep.fltr."))
                    val = kit.fltrState.getSlot(s - machinNp);
                else if (id.startsWith("lockstep.amp."))
                {
                    const int ampBase = machinNp
                        + (tempMachine->hasInternalFilter() ? 0 : TrackFltrState::kNumSlots);
                    val = kit.ampState.getSlot(s - ampBase);
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

    static void readKitFromNode(const juce::ValueTree& node, TrackKit& kit, LockstepProcessor& proc)
    {
        kit.machineId    = node.getProperty("mId",    juce::String(SamplerMachine::kMachineId)).toString().toStdString();
        kit.destinationId = node.getProperty("dId",   "").toString().toStdString();
        kit.midiPresetName = node.getProperty("mPreset", "").toString().toStdString();
        kit.divider = static_cast<int>(node.getProperty("div", 1));

        auto tempMachine = proc.createMachineForId(kit.machineId);
        const int machinNp = tempMachine->numParams();
        kit.baseParams.assign(static_cast<std::size_t>(machinNp), 0.0f);
        for (int s = 0; s < machinNp; ++s)
            kit.baseParams[static_cast<std::size_t>(s)] = tempMachine->paramSpec(s).defaultValue;

        const auto bpNode = node.getChildWithName("BP");
        if (bpNode.isValid())
        {
            for (auto pNode : bpNode)
            {
                const juce::String id  = pNode.getProperty("id", "").toString();
                const float        val = getFloat(pNode, "v", 0.0f);
                const int slot = proc.slotForIdWithMachine(*tempMachine, id);
                if (slot < 0) continue;  // unknown id (e.g. machine changed) — skip
                if (slot < machinNp)
                    kit.baseParams[static_cast<std::size_t>(slot)] = val;
                else if (id.startsWith("lockstep.fltr."))
                    kit.fltrState.setSlot(slot - machinNp, val);
                else if (id.startsWith("lockstep.amp."))
                {
                    const int ampBase = machinNp
                        + (tempMachine->hasInternalFilter() ? 0 : TrackFltrState::kNumSlots);
                    kit.ampState.setSlot(slot - ampBase, val);
                }
            }
        }
    }

    static void writeNewHierarchyNode(juce::ValueTree& root, LockstepProcessor& proc)
    {
        juce::ValueTree nhNode("NewHierarchy");
        nhNode.setProperty("activePiece", proc.activePieceIdx(),   nullptr);
        nhNode.setProperty("activeSect",  proc.activeSectionIdx(), nullptr);
        nhNode.setProperty("launchQuant", proc.project().launchQuantizeBars, nullptr);

        for (int pi = 0; pi < kNumSongs; ++pi)
        {
            const auto& song = proc.songAt(pi);
            bool pieceHasContent = false;

            juce::ValueTree songNode("Song");
            songNode.setProperty("i", pi, nullptr);

            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const auto& lane = song.tracks[static_cast<std::size_t>(t)];
                bool laneHasContent = false;
                juce::ValueTree songTrackNode("SongTrack");
                songTrackNode.setProperty("t", t, nullptr);

                // Write kit only if non-default.
                if (lane.kit.machineId != StubMachine::kMachineId || !lane.kit.baseParams.empty())
                {
                    songTrackNode.appendChild(writeKitNode(t, lane.kit, proc), nullptr);
                    laneHasContent = true;
                }
                // Write non-default phrases.
                for (int ph = 0; ph < kPhrasesPerTrack; ++ph)
                {
                    const auto& phrase = lane.phrases[static_cast<std::size_t>(ph)];
                    if (!phrase.initialised && phrase.length == 16) continue;
                    bool hasData = phrase.length != 16 || phrase.initialised;
                    if (!hasData) for (const auto& s : phrase.steps) if (s.trig) { hasData = true; break; }
                    if (!hasData) continue;
                    songTrackNode.appendChild(writePhraseNode(ph, phrase), nullptr);
                    laneHasContent = true;
                }
                if (laneHasContent)
                {
                    songNode.appendChild(songTrackNode, nullptr);
                    pieceHasContent = true;
                }
            }

            // Write non-default sections.
            for (int si = 0; si < kScenesPerSong; ++si)
            {
                const auto& sec = song.scenes[static_cast<std::size_t>(si)];
                if (!sceneHasContent(sec)) continue;
                juce::ValueTree sceneNode("Scene");
                sceneNode.setProperty("i", si, nullptr);
                sceneNode.setProperty("ct_n", sec.coreTime.numerator,   nullptr);
                sceneNode.setProperty("ct_d", sec.coreTime.denominator, nullptr);
                // phraseIdx bitfield (default all 0, only write non-zero).
                int anyNonZero = 0;
                for (const int idx : sec.phraseIdx) anyNonZero |= idx;
                if (anyNonZero != 0)
                {
                    juce::ValueTree piNode("PhraseIdx");
                    for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                        piNode.setProperty("t" + juce::String(t), sec.phraseIdx[static_cast<std::size_t>(t)], nullptr);
                    sceneNode.appendChild(piNode, nullptr);
                }
                // activeMask (default all true; only write if any false).
                bool anyMasked = false;
                for (const bool m : sec.activeMask) if (!m) { anyMasked = true; break; }
                if (anyMasked)
                {
                    int maskBits = 0;
                    for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                        if (!sec.activeMask[static_cast<std::size_t>(t)]) maskBits |= (1 << t);
                    sceneNode.setProperty("mutesMask", maskBits, nullptr);
                }
                // Scene A/B snapshots (Phase 7 Stage G; full morph impl = 5.2).
                auto writeSceneMap = [&](const char* tag,
                    const std::map<std::pair<int,int>,float>& sceneMap)
                {
                    if (sceneMap.empty()) return;
                    juce::ValueTree scNode(tag);
                    for (const auto& [key, val] : sceneMap)
                    {
                        juce::ValueTree eNode("E");
                        eNode.setProperty("t",  key.first,  nullptr);
                        eNode.setProperty("s",  key.second, nullptr);
                        eNode.setProperty("v",  static_cast<double>(val), nullptr);
                        scNode.appendChild(eNode, nullptr);
                    }
                    sceneNode.appendChild(scNode, nullptr);
                };
                writeSceneMap("MorphA", sec.morphA);
                writeSceneMap("MorphB", sec.morphB);
                songNode.appendChild(sceneNode, nullptr);
                pieceHasContent = true;
            }

            if (pieceHasContent || pi == proc.activePieceIdx())
                nhNode.appendChild(songNode, nullptr);
        }
        root.appendChild(nhNode, nullptr);
    }

    static void readNewHierarchyNode(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto nhNode = root.getChildWithName("NewHierarchy");
        if (!nhNode.isValid()) return;

        const int activePiece = static_cast<int>(nhNode.getProperty("activePiece", 0));
        const int activeSect  = static_cast<int>(nhNode.getProperty("activeSect",  0));
        proc.project().launchQuantizeBars = static_cast<int>(nhNode.getProperty("launchQuant", 1));

        for (auto songNode : nhNode)
        {
            if (songNode.getType() != juce::Identifier("Song")) continue;
            const int pi = static_cast<int>(songNode.getProperty("i", -1));
            if (pi < 0 || pi >= kNumSongs) continue;
            auto& song = proc.songAt(pi);

            for (auto child : songNode)
            {
                if (child.getType() == juce::Identifier("SongTrack"))
                {
                    const int t = static_cast<int>(child.getProperty("t", -1));
                    if (t < 0 || t >= static_cast<int>(kNumTracks)) continue;
                    auto& lane = song.tracks[static_cast<std::size_t>(t)];

                    const auto kitNode = child.getChildWithName("Kit");
                    if (kitNode.isValid())
                        readKitFromNode(kitNode, lane.kit, proc);

                    for (auto phraseNode : child)
                    {
                        if (phraseNode.getType() != juce::Identifier("Phrase")) continue;
                        const int ph = static_cast<int>(phraseNode.getProperty("i", -1));
                        if (ph < 0 || ph >= kPhrasesPerTrack) continue;
                        auto& phrase = song.tracks[static_cast<std::size_t>(t)].phrases[static_cast<std::size_t>(ph)];
                        readPhraseFromNode(phraseNode, phrase);
                        phrase.initialised = true;
                    }
                }
                else if (child.getType() == juce::Identifier("Scene"))
                {
                    const int si = static_cast<int>(child.getProperty("i", -1));
                    if (si < 0 || si >= kScenesPerSong) continue;
                    auto& sec = song.scenes[static_cast<std::size_t>(si)];
                    sec.coreTime.numerator   = static_cast<int>(child.getProperty("ct_n", 4));
                    sec.coreTime.denominator = static_cast<int>(child.getProperty("ct_d", 4));
                    sec.initialised = true;

                    const auto piNode = child.getChildWithName("PhraseIdx");
                    if (piNode.isValid())
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            sec.phraseIdx[static_cast<std::size_t>(t)] =
                                static_cast<int>(piNode.getProperty("t" + juce::String(t), 0));

                    if (child.hasProperty("mutesMask"))
                    {
                        const int maskBits = static_cast<int>(child.getProperty("mutesMask", 0));
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            sec.activeMask[static_cast<std::size_t>(t)] = !(maskBits & (1 << t));
                    }
                    // Scene A/B
                    auto readSceneMap = [&](const char* tag,
                        std::map<std::pair<int,int>,float>& sceneMap)
                    {
                        const auto scNode = child.getChildWithName(tag);
                        if (!scNode.isValid()) return;
                        for (auto eNode : scNode)
                        {
                            const int t2 = static_cast<int>(eNode.getProperty("t", -1));
                            const int s2 = static_cast<int>(eNode.getProperty("s", -1));
                            if (t2 >= 0 && s2 >= 0)
                                sceneMap[{t2, s2}] = getFloat(eNode, "v", 0.0f);
                        }
                    };
                    readSceneMap("MorphA", sec.morphA);
                    readSceneMap("MorphB", sec.morphB);
                }
            }
        }

        // Apply active indices after all data is loaded.
        proc.setActiveSong(std::clamp(activePiece, 0, kNumSongs - 1));
        proc.setActiveScene(std::clamp(activeSect, 0, kScenesPerSong - 1));
    }

    // ── End Phase 7 new hierarchy serialization ───────────────────────────────

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
                                       || track.trigDefaults.gateValue != MusicalGate::None);
                    const bool hasNoteSel = (track.noteSelection != NoteSelection::TopBias);
                    bool hasStep = false;
                    for (const auto& step : track.steps)
                    {
                        if (step.trig || !step.overrides.empty()
                            || step.trigOverride.noteCount > 0
                            || step.trigOverride.hasVelocity
                            || step.trigOverride.hasGate
                            || !step.condition.isTrivial()
                            || step.fillTrigState != FillTrigState::Inherit
                            || !step.fillOverrides.empty()
                            || step.fillTrigOverride.noteCount > 0
                            || step.fillTrigOverride.hasVelocity
                            || step.fillTrigOverride.hasGate)
                        {
                            hasStep = true;
                            break;
                        }
                    }
                    if (!hasCond && !hasTrig && !hasStep && !hasNoteSel) continue;

                    juce::ValueTree trackNode("Track");
                    trackNode.setProperty("i", t, nullptr);

                    if (hasNoteSel)
                        trackNode.setProperty("nsel",
                                              static_cast<int>(track.noteSelection),
                                              nullptr);

                    if (hasCond)
                        trackNode.appendChild(condToTree("BaseCond", track.baseCond), nullptr);

                    if (hasTrig)
                    {
                        juce::ValueTree tdNode("TrigDefaults");
                        tdNode.setProperty("note",  track.trigDefaults.note,     nullptr);
                        tdNode.setProperty("vel",   track.trigDefaults.velocity,  nullptr);
                        tdNode.setProperty("gateV", static_cast<int>(static_cast<uint8_t>(
                                               track.trigDefaults.gateValue)),    nullptr);
                        trackNode.appendChild(tdNode, nullptr);
                    }

                    if (hasStep)
                    {
                        juce::ValueTree stepsNode("Steps");
                        for (int s = 0; s < kMaxStepsPerTrack; ++s)
                        {
                            const auto& step = track.steps[static_cast<std::size_t>(s)];
                            const bool hp  = !step.overrides.empty();
                            const bool hto = step.trigOverride.noteCount > 0
                                          || step.trigOverride.hasVelocity
                                          || step.trigOverride.hasGate;
                            const bool hnc = !step.condition.isTrivial();
                            const bool hfts = (step.fillTrigState != FillTrigState::Inherit);
                            const bool hfp  = !step.fillOverrides.empty();
                            const bool hfto = step.fillTrigOverride.noteCount > 0
                                          || step.fillTrigOverride.hasVelocity
                                          || step.fillTrigOverride.hasGate;
                            if (!step.trig && !hp && !hto && !hnc
                                && !hfts && !hfp && !hfto) continue;

                            juce::ValueTree stepNode("S");
                            stepNode.setProperty("i", s,                  nullptr);
                            stepNode.setProperty("t", step.trig ? 1 : 0,  nullptr);

                            if (hnc)
                                stepNode.appendChild(condToTree("C", step.condition), nullptr);

                            if (hto)
                            {
                                juce::ValueTree toNode("TO");
                                if (step.trigOverride.noteCount > 0)
                                {
                                    toNode.setProperty("nc", step.trigOverride.noteCount, nullptr);
                                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                                        toNode.setProperty("n" + juce::String(ni),
                                                           step.trigOverride.notes[static_cast<std::size_t>(ni)],
                                                           nullptr);
                                }
                                if (step.trigOverride.hasVelocity)
                                {
                                    toNode.setProperty("hv", 1,                          nullptr);
                                    toNode.setProperty("v",  step.trigOverride.velocity,  nullptr);
                                }
                                if (step.trigOverride.hasGate)
                                {
                                    toNode.setProperty("hg", 1, nullptr);
                                    toNode.setProperty("gv", static_cast<int>(
                                        static_cast<uint8_t>(step.trigOverride.gateValue)), nullptr);
                                }
                                if (step.trigOverride.hasNoteVelocities)
                                {
                                    toNode.setProperty("hnv", 1, nullptr);
                                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                                        toNode.setProperty("vel" + juce::String(ni),
                                            static_cast<int>(step.trigOverride.velocities[
                                                static_cast<std::size_t>(ni)]),
                                            nullptr);
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

                            // Fill layer.
                            if (hfts)
                                stepNode.setProperty("fts",
                                    static_cast<int>(step.fillTrigState), nullptr);

                            if (hfto)
                            {
                                juce::ValueTree ftoNode("FTO");
                                if (step.fillTrigOverride.noteCount > 0)
                                {
                                    ftoNode.setProperty("nc", step.fillTrigOverride.noteCount, nullptr);
                                    for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                                        ftoNode.setProperty("n" + juce::String(ni),
                                                            step.fillTrigOverride.notes[static_cast<std::size_t>(ni)],
                                                            nullptr);
                                }
                                if (step.fillTrigOverride.hasVelocity)
                                {
                                    ftoNode.setProperty("hv", 1,                               nullptr);
                                    ftoNode.setProperty("v",  step.fillTrigOverride.velocity,  nullptr);
                                }
                                if (step.fillTrigOverride.hasGate)
                                {
                                    ftoNode.setProperty("hg", 1, nullptr);
                                    ftoNode.setProperty("gv", static_cast<int>(
                                        static_cast<uint8_t>(step.fillTrigOverride.gateValue)), nullptr);
                                }
                                stepNode.appendChild(ftoNode, nullptr);
                            }

                            if (hfp)
                            {
                                juce::ValueTree fplNode("FPL");
                                step.fillOverrides.forEach([&](int slot, float value) {
                                    const juce::String id = proc.idForSlot(t, slot);
                                    if (id.isEmpty()) return;
                                    juce::ValueTree lNode("L");
                                    lNode.setProperty("id", id,                         nullptr);
                                    lNode.setProperty("v",  static_cast<double>(value), nullptr);
                                    fplNode.appendChild(lNode, nullptr);
                                });
                                if (fplNode.getNumChildren() > 0)
                                    stepNode.appendChild(fplNode, nullptr);
                            }

                            stepsNode.appendChild(stepNode, nullptr);
                        }
                        if (stepsNode.getNumChildren() > 0)
                            trackNode.appendChild(stepsNode, nullptr);
                    }

                    patNode.appendChild(trackNode, nullptr);
                    patHasContent = true;
                }

                // Write all initialised patterns, plus the active one unconditionally.
                const bool isActive = (bi == proc.activeBankIdx()
                                    && pi == proc.activePatternIdx());
                if (patHasContent || isActive || pattern.initialised)
                {
                    if (pattern.initialised)
                        patNode.setProperty("init", 1, nullptr);
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

                // Write all initialised parts, plus Part 0 and the active Part unconditionally.
                const int activePartRef = proc.activePattern().partRef;
                const bool isActivePart = (bi == proc.activeBankIdx()
                                        && ri == activePartRef);
                if (partHasContent || ri == 0 || isActivePart || part.initialised)
                {
                    if (part.initialised)
                        partNode.setProperty("init", 1, nullptr);
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

        const int nsel = static_cast<int>(trackNode.getProperty("nsel", 0));
        track.noteSelection = (nsel == 1) ? NoteSelection::BottomBias
                                          : NoteSelection::TopBias;

        const auto tdNode = trackNode.getChildWithName("TrigDefaults");
        if (tdNode.isValid())
        {
            track.trigDefaults.note     = static_cast<int>(tdNode.getProperty("note",   60));
            track.trigDefaults.velocity = static_cast<int>(tdNode.getProperty("vel",   100));
            track.trigDefaults.gateValue = static_cast<MusicalGate>(
                std::clamp(static_cast<int>(tdNode.getProperty("gateV", 0)),
                           0, kMusicalGateCount - 1));
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
                // New format: "nc" = noteCount + "n0".."n3".
                // Legacy format: "hn" + "n" (single note) — migrated on load.
                if (toNode.hasProperty("nc"))
                {
                    step.trigOverride.noteCount = static_cast<int>(toNode.getProperty("nc", 0));
                    step.trigOverride.noteCount = std::clamp(step.trigOverride.noteCount,
                                                              0, kMaxNotesPerStep);
                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                        step.trigOverride.notes[static_cast<std::size_t>(ni)] =
                            static_cast<int>(toNode.getProperty("n" + juce::String(ni), 60));
                }
                else
                {
                    const bool legacy = (static_cast<int>(toNode.getProperty("hn", 0)) != 0);
                    if (legacy)
                    {
                        step.trigOverride.noteCount = 1;
                        step.trigOverride.notes[0]  = static_cast<int>(toNode.getProperty("n", 60));
                    }
                }

                step.trigOverride.hasVelocity = (static_cast<int>(toNode.getProperty("hv", 0)) != 0);
                if (step.trigOverride.hasVelocity)
                    step.trigOverride.velocity = static_cast<int>(toNode.getProperty("v", 100));

                step.trigOverride.hasGate = (static_cast<int>(toNode.getProperty("hg", 0)) != 0);
                if (step.trigOverride.hasGate)
                    step.trigOverride.gateValue = static_cast<MusicalGate>(
                        std::clamp(static_cast<int>(toNode.getProperty("gv", 0)),
                                   0, kMusicalGateCount - 1));

                step.trigOverride.hasNoteVelocities =
                    (static_cast<int>(toNode.getProperty("hnv", 0)) != 0);
                if (step.trigOverride.hasNoteVelocities)
                {
                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                        step.trigOverride.velocities[static_cast<std::size_t>(ni)] =
                            static_cast<uint8_t>(static_cast<int>(
                                toNode.getProperty("vel" + juce::String(ni), 100)));
                }
            }

            const auto plNode = stepNode.getChildWithName("PL");
            if (plNode.isValid())
            {
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

            // Fill layer — FillTrigState.
            // Also migrates legacy "fr" (fillRule) from old TrigCondition format.
            const int fts = static_cast<int>(stepNode.getProperty("fts", -1));
            if (fts >= 0)
            {
                step.fillTrigState = static_cast<FillTrigState>(
                    std::clamp(fts, 0, 2));
            }
            else
            {
                // Legacy migration: "fr" was stored on the per-step <C> node.
                const auto cNode = stepNode.getChildWithName("C");
                if (cNode.isValid())
                {
                    const int fr = static_cast<int>(cNode.getProperty("fr", 0));
                    if (fr == 1) step.fillTrigState = FillTrigState::On;
                    else if (fr == 2) step.fillTrigState = FillTrigState::Off;
                }
            }

            const auto ftoNode = stepNode.getChildWithName("FTO");
            if (ftoNode.isValid())
            {
                if (ftoNode.hasProperty("nc"))
                {
                    step.fillTrigOverride.noteCount = std::clamp(
                        static_cast<int>(ftoNode.getProperty("nc", 0)),
                        0, kMaxNotesPerStep);
                    for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                        step.fillTrigOverride.notes[static_cast<std::size_t>(ni)] =
                            static_cast<int>(ftoNode.getProperty("n" + juce::String(ni), 60));
                }
                step.fillTrigOverride.hasVelocity =
                    (static_cast<int>(ftoNode.getProperty("hv", 0)) != 0);
                if (step.fillTrigOverride.hasVelocity)
                    step.fillTrigOverride.velocity =
                        static_cast<int>(ftoNode.getProperty("v", 100));
                step.fillTrigOverride.hasGate =
                    (static_cast<int>(ftoNode.getProperty("hg", 0)) != 0);
                if (step.fillTrigOverride.hasGate)
                    step.fillTrigOverride.gateValue = static_cast<MusicalGate>(
                        std::clamp(static_cast<int>(ftoNode.getProperty("gv", 0)),
                                   0, kMusicalGateCount - 1));
            }

            const auto fplNode = stepNode.getChildWithName("FPL");
            if (fplNode.isValid())
            {
                for (auto lNode : fplNode)
                {
                    const juce::String id  = lNode.getProperty("id").toString();
                    const float        val = getFloat(lNode, "v", 0.0f);
                    const int slot = proc.slotForId(t, id);
                    if (slot < 0)
                    {
                        DBG("PluginState: unknown fill P-Lock id '" + id
                            + "' on track " + juce::String(t) + " -- skipping");
                        continue;
                    }
                    step.fillOverrides.set(slot, val);
                }
            }
        }
    }

    static void readPartTrackFromNode(const juce::ValueTree& ptNode,
                                       PartTrack& pt, LockstepProcessor& proc, int t)
    {
        pt.machineId      = ptNode.getProperty("machineId",
                                               "lockstep.sampler.v1").toString().toStdString();
        pt.destinationId  = ptNode.getProperty("destinationId",
                                               "").toString().toStdString();
        pt.midiPresetName = ptNode.getProperty("midiPreset",
                                               "").toString().toStdString();

        // MF.4: restore CC slot configuration.
        const auto ccNode = ptNode.getChildWithName("CCConfig");
        if (ccNode.isValid())
        {
            pt.midiCCNumbers.resize(static_cast<std::size_t>(MidiOutMachine::kNumCCs));
            pt.midiCCLabels .resize(static_cast<std::size_t>(MidiOutMachine::kNumCCs));
            for (int ci = 0; ci < MidiOutMachine::kNumCCs; ++ci)
                pt.midiCCNumbers[static_cast<std::size_t>(ci)] = ci;  // default
            for (auto ccEntry : ccNode)
            {
                const int ci = static_cast<int>(ccEntry.getProperty("i", -1));
                if (ci < 0 || ci >= MidiOutMachine::kNumCCs) continue;
                const auto csz = static_cast<std::size_t>(ci);
                pt.midiCCNumbers[csz] = static_cast<int>(ccEntry.getProperty("n", ci));
                pt.midiCCLabels[csz]  = ccEntry.getProperty("label", "")
                                            .toString().toStdString();
            }
        }

        // Always resize baseParams to match the saved machine's schema and seed
        // defaults, regardless of whether there are any non-default values to read.
        // Without this, a machine saved with all-default params (no BaseParams node)
        // would leave baseParams at the previous machine's size, causing an OOB crash.
        auto tempMachine = proc.createMachineForId(pt.machineId);
        const int np = tempMachine->numParams();
        pt.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
        for (int s = 0; s < np; ++s)
            pt.baseParams[static_cast<std::size_t>(s)] = tempMachine->paramSpec(s).defaultValue;

        const auto bpNode = ptNode.getChildWithName("BaseParams");
        if (!bpNode.isValid()) return;

        for (auto pNode : bpNode)
        {
            const juce::String id  = pNode.getProperty("id").toString();
            const float        val = getFloat(pNode, "v", 0.0f);
            const int slot = proc.slotForIdWithMachine(*tempMachine, id);
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
                const int ampBase = np + (tempMachine->hasInternalFilter() ? 0
                                                                            : TrackFltrState::kNumSlots);
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
                    pattern.initialised = (static_cast<int>(child.getProperty("init", 0)) != 0);
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
                    part.initialised = (static_cast<int>(child.getProperty("init", 0)) != 0);
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

    // v2 → v3
    // v2 format: TrigOverride gate stored as "hg"+"g" (float milliseconds).
    //            TrigDefaults gate stored as "gateMs" (float milliseconds).
    // v3 format: TrigOverride gate stored as "hg"+"gv" (int MusicalGate index).
    //            TrigDefaults gate stored as "gateV"   (int MusicalGate index).
    //            TrigOverride per-note velocities: "hnv"+"vel0".."vel3" (new; absent in v2).
    juce::ValueTree upgrade_v2_to_v3(const juce::ValueTree& v2)
    {
        juce::ValueTree v3 = v2.createCopy();
        v3.setProperty("version", 3, nullptr);

        // Walk Project/Bank/Pattern/Track/Steps/Step/TO nodes and convert gate.
        const auto projNode = v3.getChildWithName("Project");
        if (!projNode.isValid()) return v3;

        for (auto bankNode : projNode)
        {
            for (auto child : bankNode)
            {
                if (child.getType() != juce::Identifier("Pattern")) continue;
                for (auto trackNode : child)
                {
                    // Fix TrigDefaults: "gateMs" float → "gateV" int.
                    auto tdNode = trackNode.getChildWithName("TrigDefaults");
                    if (tdNode.isValid() && tdNode.hasProperty("gateMs"))
                    {
                        const float gms = static_cast<float>(
                            static_cast<double>(tdNode.getProperty("gateMs", 0.0)));
                        tdNode.setProperty("gateV",
                            static_cast<int>(static_cast<uint8_t>(
                                nearestMusicalGate(gms, 120.0))), nullptr);
                        tdNode.removeProperty("gateMs", nullptr);
                    }

                    // Fix each step's TO node: "g" double → "gv" int.
                    const auto stepsNode = trackNode.getChildWithName("Steps");
                    if (!stepsNode.isValid()) continue;
                    for (auto stepNode : stepsNode)
                    {
                        auto toNode = stepNode.getChildWithName("TO");
                        if (!toNode.isValid()) continue;
                        if (toNode.hasProperty("g"))
                        {
                            const float gms = static_cast<float>(
                                static_cast<double>(toNode.getProperty("g", 0.0)));
                            toNode.setProperty("gv",
                                static_cast<int>(static_cast<uint8_t>(
                                    nearestMusicalGate(gms, 120.0))), nullptr);
                            toNode.removeProperty("g", nullptr);
                        }
                    }
                }
            }
        }

        return v3;
    }

    juce::ValueTree upgrade_v3_to_v4(const juce::ValueTree& v3)
    {
        juce::ValueTree v4 = v3.createCopy();
        v4.setProperty("version", 4, nullptr);

        // All existing Pattern and Part nodes are from a pre-gestural-archetype
        // session where every slot was pre-seeded. Mark them all as initialised
        // so they don't trigger copy/create gestures on first touch.
        const auto projNode = v4.getChildWithName("Project");
        if (!projNode.isValid()) return v4;

        for (auto bankNode : projNode)
        {
            for (auto child : bankNode)
            {
                if (child.getType() == juce::Identifier("Pattern") ||
                    child.getType() == juce::Identifier("Part"))
                {
                    child.setProperty("init", 1, nullptr);
                }
            }
        }

        return v4;
    }

    // Phase 7 clean break (pre-release). Any older version is collapsed to the
    // current version: drop the legacy Project/Bank/Pattern/Part node, preserve
    // APVTS / SamplePool / Misc / NewHierarchy. The processor seeds Song[0] on
    // startup, and a NewHierarchy node (if present, e.g. from an interim v5/v6
    // build) is carried forward as-is. No faithful legacy migration — this is a
    // pre-release format with no shipped users.
    juce::ValueTree cleanBreakToCurrent(const juce::ValueTree& old)
    {
        juce::ValueTree cur("LockstepState");
        cur.setProperty("version", kCurrentVersion, nullptr);
        for (int i = 0; i < old.getNumChildren(); ++i)
        {
            const auto child = old.getChild(i);
            // Drop every legacy hierarchy node: "Project" (v2+) and the flat
            // pre-v2 "Sequence". Preserve APVTS ("Lockstep"), SamplePool, Misc,
            // CCMappings, and any already-present NewHierarchy.
            if (child.getType() != juce::Identifier("Project")
                && child.getType() != juce::Identifier("Sequence"))
                cur.appendChild(child.createCopy(), nullptr);
        }
        DBG("PluginState: clean break to v" + juce::String(kCurrentVersion)
            + ": legacy Project/Bank/Pattern/Part data discarded.");
        return cur;
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
        // Pre-v1 trees (bare "Lockstep" APVTS root) are first normalised into a
        // LockstepState wrapper; everything below the current version then
        // collapses to the current format via the clean break.
        if (version < 1) tree = upgrade_v0_to_v1(tree);
        if (version < kCurrentVersion) tree = cleanBreakToCurrent(tree);

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

        // Phase 7 new hierarchy: Song/SongTrack/Kit/Phrase/Section
        writeNewHierarchyNode(root, proc);

        // Legacy hierarchy retained for reference (can be removed in a later cleanup).
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

        // Sample pool must be restored before hierarchy so pool indices resolve.
        readSamplePool(root, proc);
        // Phase 7 new hierarchy.
        readNewHierarchyNode(root, proc);
        // Legacy hierarchy (kept for fallback; no-op if NewHierarchy node exists).
        readProjectNode(root, proc);
        // readMiscState sets focus track and legacy active pattern.
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

            beginTest("v1 (flat Sequence) clean-breaks to current");
            {
                // A pre-v2 tree carried a flat "Sequence" node. The Phase 7 clean
                // break discards all legacy hierarchy; APVTS/SamplePool/Misc stay.
                juce::ValueTree v1("LockstepState");
                v1.setProperty("version", 1, nullptr);
                v1.appendChild(juce::ValueTree("Lockstep"),   nullptr);
                v1.appendChild(juce::ValueTree("SamplePool"), nullptr);
                v1.appendChild(juce::ValueTree("Sequence"),   nullptr);
                v1.appendChild(juce::ValueTree("Misc"),       nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v1);

                expectEquals(static_cast<int>(result.getProperty("version", -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName("Sequence").isValid(),
                       "legacy flat Sequence discarded");
                expect(!result.getChildWithName("Project").isValid(),
                       "no legacy Project remains");
                expect(result.getChildWithName("Lockstep").isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName("SamplePool").isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName("Misc").isValid(),
                       "Misc preserved");
            }

            beginTest("legacy Project (v2-v4) clean-breaks to current");
            {
                // Any legacy Bank/Pattern/Part hierarchy is dropped wholesale by
                // the pre-release clean break; the processor re-seeds defaults.
                juce::ValueTree v2("LockstepState");
                v2.setProperty("version", 2, nullptr);
                v2.appendChild(juce::ValueTree("Lockstep"),    nullptr);
                v2.appendChild(juce::ValueTree("SamplePool"),  nullptr);

                juce::ValueTree proj("Project");
                juce::ValueTree bank("Bank");
                bank.appendChild(juce::ValueTree("Pattern"), nullptr);
                bank.appendChild(juce::ValueTree("Part"),    nullptr);
                proj.appendChild(bank, nullptr);
                v2.appendChild(proj, nullptr);
                v2.appendChild(juce::ValueTree("Misc"), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v2);

                expectEquals(static_cast<int>(result.getProperty("version", -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName("Project").isValid(),
                       "legacy Project/Bank/Pattern/Part discarded");
                expect(result.getChildWithName("Lockstep").isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName("SamplePool").isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName("Misc").isValid(),
                       "Misc preserved");
            }

            beginTest("current version passes through unchanged");
            {
                juce::ValueTree cur("LockstepState");
                cur.setProperty("version", lockstep::PluginState::kCurrentVersion, nullptr);
                cur.appendChild(juce::ValueTree("Lockstep"),     nullptr);
                cur.appendChild(juce::ValueTree("NewHierarchy"), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(cur);

                expectEquals(static_cast<int>(result.getProperty("version", -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "current version unchanged");
                expect(result.getChildWithName("NewHierarchy").isValid(),
                       "NewHierarchy node preserved at current version");
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
