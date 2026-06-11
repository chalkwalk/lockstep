#include "PluginState.h"
#include "StateKeys.h"
#include "../PluginProcessor.h"
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
#include "../machine/EffectFactory.h"
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
        v.setProperty("p", static_cast<int>(c.probabilityPercent), nullptr);
        v.setProperty("n", static_cast<int>(c.iterNumerator), nullptr);
        v.setProperty("d", static_cast<int>(c.iterDenominator), nullptr);
        v.setProperty(keys::kPd, static_cast<int>(c.prevDependency), nullptr);
        return v;
    }

    static TrigCondition condFromTree(const juce::ValueTree& v)
    {
        TrigCondition c;
        c.probabilityPercent = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("p", 100)));
        c.iterNumerator = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("n", 1)));
        c.iterDenominator = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty("d", 1)));
        c.prevDependency = static_cast<std::uint8_t>(
            static_cast<int>(v.getProperty(keys::kPd, 0)));
        // Legacy: "fr" was fillRule (0=Always, 1=OnlyFill, 2=NeverFill).
        // Now handled at step level as fillTrigState (see stepFromNode).
        return c;
    }

    // ── Phase 7 / Stage F: new musical hierarchy serialization ───────────────

    static juce::ValueTree writePhraseNode(int phraseIdx, const Phrase& phrase,
                                           LockstepProcessor& proc, int trackIdx)
    {
        juce::ValueTree node(keys::kPhrase);
        node.setProperty("i", phraseIdx, nullptr);
        node.setProperty(keys::kLen, phrase.length, nullptr);

        if (phrase.noteSelection != NoteSelection::TopBias)
            node.setProperty(keys::kNSel, static_cast<int>(phrase.noteSelection), nullptr);
        if (!phrase.baseCond.isTrivial())
            node.appendChild(condToTree(keys::kBaseCond, phrase.baseCond), nullptr);

        const auto& td = phrase.trigDefaults;
        if (td.note != 60 || td.velocity != 100 || td.gateValue != MusicalGate::None)
        {
            juce::ValueTree tdNode(keys::kTrigDefaults);
            tdNode.setProperty(keys::kNote, td.note, nullptr);
            tdNode.setProperty(keys::kVel, td.velocity, nullptr);
            tdNode.setProperty(keys::kGateV, static_cast<int>(static_cast<uint8_t>(td.gateValue)), nullptr);
            node.appendChild(tdNode, nullptr);
        }

        juce::ValueTree stepsNode(keys::kSteps);
        bool hasSteps = false;
        for (int s = 0; s < kMaxStepsPerTrack; ++s)
        {
            const auto& step = phrase.steps[static_cast<std::size_t>(s)];
            if (!step.trig && step.overrides.empty() && step.trigOverride.noteCount == 0 && !step.trigOverride.hasVelocity && !step.trigOverride.hasGate && !step.trigOverride.hasSoundId && !step.trigOverride.hasRetrig && step.condition.isTrivial() && !floatNe(step.microOffset, 0.0f) && step.fillTrigState == FillTrigState::Inherit && step.fillOverrides.empty() && step.fillTrigOverride.noteCount == 0) continue;
            hasSteps = true;
            juce::ValueTree stepNode("S");
            stepNode.setProperty("i", s, nullptr);
            stepNode.setProperty("t", step.trig ? 1 : 0, nullptr);
            if (floatNe(step.microOffset, 0.0f))
                stepNode.setProperty(keys::kMo, static_cast<double>(step.microOffset), nullptr);
            if (!step.condition.isTrivial())
                stepNode.appendChild(condToTree("C", step.condition), nullptr);
            if (step.trigOverride.noteCount > 0 || step.trigOverride.hasVelocity || step.trigOverride.hasGate || step.trigOverride.hasSoundId || step.trigOverride.hasRetrig)
            {
                juce::ValueTree toNode("TO");
                if (step.trigOverride.noteCount > 0)
                {
                    toNode.setProperty(keys::kNc, step.trigOverride.noteCount, nullptr);
                    for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                        toNode.setProperty("n" + juce::String(ni),
                                           step.trigOverride.notes[static_cast<std::size_t>(ni)], nullptr);
                }
                if (step.trigOverride.hasVelocity)
                {
                    toNode.setProperty(keys::kHv, 1, nullptr);
                    toNode.setProperty("v", step.trigOverride.velocity, nullptr);
                }
                if (step.trigOverride.hasGate)
                {
                    toNode.setProperty(keys::kHg, 1, nullptr);
                    toNode.setProperty(keys::kGv, static_cast<int>(static_cast<uint8_t>(step.trigOverride.gateValue)), nullptr);
                }
                if (step.trigOverride.hasSoundId)
                {
                    toNode.setProperty(keys::kHsi, 1, nullptr);
                    toNode.setProperty(keys::kSi, step.trigOverride.soundId, nullptr);
                }
                if (step.trigOverride.hasRetrig)
                {
                    toNode.setProperty(keys::kHrt, 1, nullptr);
                    toNode.setProperty(keys::kRt, step.trigOverride.retrigRate, nullptr);
                }
                stepNode.appendChild(toNode, nullptr);
            }
            if (!step.overrides.empty())
            {
                juce::ValueTree plNode("PL");
                step.overrides.forEach([&](int slot, float value) {
                    const juce::String sid = proc.idForSlot(trackIdx, slot);
                    if (sid.isEmpty()) return;
                    juce::ValueTree pNode("P");
                    pNode.setProperty(keys::kParamId, sid, nullptr);
                    pNode.setProperty(keys::kPLockVal, static_cast<double>(value), nullptr);
                    plNode.appendChild(pNode, nullptr);
                });
                stepNode.appendChild(plNode, nullptr);
            }
            if (step.fillTrigState != FillTrigState::Inherit)
                stepNode.setProperty(keys::kFillTS, static_cast<int>(step.fillTrigState), nullptr);
            if (step.fillTrigOverride.noteCount > 0 || step.fillTrigOverride.hasVelocity || step.fillTrigOverride.hasGate || step.fillTrigOverride.hasSoundId || step.fillTrigOverride.hasRetrig)
            {
                juce::ValueTree ftoNode(keys::kFillTO);
                if (step.fillTrigOverride.noteCount > 0)
                {
                    ftoNode.setProperty(keys::kNc, step.fillTrigOverride.noteCount, nullptr);
                    for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                        ftoNode.setProperty("n" + juce::String(ni),
                                            step.fillTrigOverride.notes[static_cast<std::size_t>(ni)], nullptr);
                }
                if (step.fillTrigOverride.hasVelocity)
                {
                    ftoNode.setProperty(keys::kHv, 1, nullptr);
                    ftoNode.setProperty("v", step.fillTrigOverride.velocity, nullptr);
                }
                if (step.fillTrigOverride.hasGate)
                {
                    ftoNode.setProperty(keys::kHg, 1, nullptr);
                    ftoNode.setProperty(keys::kGv, static_cast<int>(static_cast<uint8_t>(step.fillTrigOverride.gateValue)), nullptr);
                }
                if (step.fillTrigOverride.hasSoundId)
                {
                    ftoNode.setProperty(keys::kHsi, 1, nullptr);
                    ftoNode.setProperty(keys::kSi, step.fillTrigOverride.soundId, nullptr);
                }
                if (step.fillTrigOverride.hasRetrig)
                {
                    ftoNode.setProperty(keys::kHrt, 1, nullptr);
                    ftoNode.setProperty(keys::kRt, step.fillTrigOverride.retrigRate, nullptr);
                }
                stepNode.appendChild(ftoNode, nullptr);
            }
            if (!step.fillOverrides.empty())
            {
                juce::ValueTree fplNode(keys::kFillPLocks);
                step.fillOverrides.forEach([&](int slot, float value) {
                    const juce::String sid = proc.idForSlot(trackIdx, slot);
                    if (sid.isEmpty()) return;
                    juce::ValueTree pNode("P");
                    pNode.setProperty(keys::kParamId, sid, nullptr);
                    pNode.setProperty(keys::kPLockVal, static_cast<double>(value), nullptr);
                    fplNode.appendChild(pNode, nullptr);
                });
                stepNode.appendChild(fplNode, nullptr);
            }
            stepsNode.appendChild(stepNode, nullptr);
        }
        if (hasSteps) node.appendChild(stepsNode, nullptr);
        return node;
    }

    // slotResolver(id) → runtime slot index for a string param id; returns -1 if unknown.
    // Pass a no-op lambda (returning -1) when no machine context is available.
    static void readPhraseFromNode(const juce::ValueTree& node, Phrase& phrase,
                                   std::function<int(const juce::String&)> slotResolver)
    {
        phrase.length = std::clamp(static_cast<int>(node.getProperty(keys::kLen, 16)), 1, kMaxStepsPerTrack);
        if (node.hasProperty(keys::kNSel))
            phrase.noteSelection = static_cast<NoteSelection>(static_cast<int>(node.getProperty(keys::kNSel, 0)));

        const auto bcNode = node.getChildWithName(keys::kBaseCond);
        if (bcNode.isValid()) phrase.baseCond = condFromTree(bcNode);

        const auto tdNode = node.getChildWithName(keys::kTrigDefaults);
        if (tdNode.isValid())
        {
            phrase.trigDefaults.note = static_cast<int>(tdNode.getProperty(keys::kNote, 60));
            phrase.trigDefaults.velocity = static_cast<int>(tdNode.getProperty(keys::kVel, 100));
            phrase.trigDefaults.gateValue = static_cast<MusicalGate>(
                static_cast<uint8_t>(static_cast<int>(tdNode.getProperty(keys::kGateV, 0))));
        }

        const auto stepsNode = node.getChildWithName(keys::kSteps);
        if (!stepsNode.isValid()) return;
        for (auto stepNode : stepsNode)
        {
            const int s = static_cast<int>(stepNode.getProperty("i", -1));
            if (s < 0 || s >= kMaxStepsPerTrack) continue;
            auto& step = phrase.steps[static_cast<std::size_t>(s)];
            step.trig = (static_cast<int>(stepNode.getProperty("t", 0)) != 0);
            step.microOffset = getFloat(stepNode, keys::kMo, 0.0f);
            const auto cNode = stepNode.getChildWithName("C");
            if (cNode.isValid()) step.condition = condFromTree(cNode);
            const auto toNode = stepNode.getChildWithName("TO");
            if (toNode.isValid())
            {
                step.trigOverride.noteCount = static_cast<int>(toNode.getProperty(keys::kNc, 0));
                for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                    step.trigOverride.notes[static_cast<std::size_t>(ni)] =
                        static_cast<int>(toNode.getProperty("n" + juce::String(ni), 60));
                if (static_cast<int>(toNode.getProperty(keys::kHv, 0)) != 0)
                {
                    step.trigOverride.hasVelocity = true;
                    step.trigOverride.velocity = static_cast<int>(toNode.getProperty("v", 100));
                }
                if (static_cast<int>(toNode.getProperty(keys::kHg, 0)) != 0)
                {
                    step.trigOverride.hasGate = true;
                    step.trigOverride.gateValue = static_cast<MusicalGate>(static_cast<uint8_t>(static_cast<int>(toNode.getProperty(keys::kGv, 0))));
                }
                if (static_cast<int>(toNode.getProperty(keys::kHsi, 0)) != 0)
                {
                    step.trigOverride.hasSoundId = true;
                    step.trigOverride.soundId = static_cast<int>(toNode.getProperty(keys::kSi, -1));
                }
                if (static_cast<int>(toNode.getProperty(keys::kHrt, 0)) != 0)
                {
                    step.trigOverride.hasRetrig = true;
                    step.trigOverride.retrigRate = static_cast<double>(toNode.getProperty(keys::kRt, 0.25));
                }
            }
            const auto plNode = stepNode.getChildWithName(keys::kPLocks);
            if (plNode.isValid())
                for (auto pNode : plNode)
                {
                    const float val = getFloat(pNode, keys::kPLockVal, 0.0f);
                    // v15+: string id; v14-: integer slot (legacy compat).
                    const juce::String sid = pNode.getProperty(keys::kParamId).toString();
                    if (sid.isNotEmpty())
                    {
                        const int sl = slotResolver(sid);
                        if (sl >= 0) step.overrides.set(sl, val);
                    }
                    else
                    {
                        const int sl = static_cast<int>(pNode.getProperty(keys::kPLockSlot, -1));
                        if (sl >= 0) step.overrides.set(sl, val);
                    }
                }
            if (stepNode.hasProperty(keys::kFillTS))
                step.fillTrigState = static_cast<FillTrigState>(
                    static_cast<int>(stepNode.getProperty(keys::kFillTS, 0)));
            const auto ftoNode = stepNode.getChildWithName(keys::kFillTO);
            if (ftoNode.isValid())
            {
                step.fillTrigOverride.noteCount = static_cast<int>(ftoNode.getProperty(keys::kNc, 0));
                for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                    step.fillTrigOverride.notes[static_cast<std::size_t>(ni)] =
                        static_cast<int>(ftoNode.getProperty("n" + juce::String(ni), 60));
                if (static_cast<int>(ftoNode.getProperty(keys::kHv, 0)) != 0)
                {
                    step.fillTrigOverride.hasVelocity = true;
                    step.fillTrigOverride.velocity = static_cast<int>(ftoNode.getProperty("v", 100));
                }
                if (static_cast<int>(ftoNode.getProperty(keys::kHg, 0)) != 0)
                {
                    step.fillTrigOverride.hasGate = true;
                    step.fillTrigOverride.gateValue = static_cast<MusicalGate>(static_cast<uint8_t>(static_cast<int>(ftoNode.getProperty(keys::kGv, 0))));
                }
                if (static_cast<int>(ftoNode.getProperty(keys::kHsi, 0)) != 0)
                {
                    step.fillTrigOverride.hasSoundId = true;
                    step.fillTrigOverride.soundId = static_cast<int>(ftoNode.getProperty(keys::kSi, -1));
                }
                if (static_cast<int>(ftoNode.getProperty(keys::kHrt, 0)) != 0)
                {
                    step.fillTrigOverride.hasRetrig = true;
                    step.fillTrigOverride.retrigRate = static_cast<double>(ftoNode.getProperty(keys::kRt, 0.25));
                }
            }
            const auto fplNode = stepNode.getChildWithName(keys::kFillPLocks);
            if (fplNode.isValid())
                for (auto pNode : fplNode)
                {
                    const float val = getFloat(pNode, keys::kPLockVal, 0.0f);
                    const juce::String sid = pNode.getProperty(keys::kParamId).toString();
                    if (sid.isNotEmpty())
                    {
                        const int sl = slotResolver(sid);
                        if (sl >= 0) step.fillOverrides.set(sl, val);
                    }
                    else
                    {
                        const int sl = static_cast<int>(pNode.getProperty(keys::kPLockSlot, -1));
                        if (sl >= 0) step.fillOverrides.set(sl, val);
                    }
                }
        }
    }

    static juce::ValueTree writeKitNode(int trackIdx, const TrackKit& kit, LockstepProcessor& proc)
    {
        juce::ValueTree node(keys::kKit);
        node.setProperty("t", trackIdx, nullptr);
        node.setProperty(keys::kMId, juce::String(kit.machineId), nullptr);
        if (!kit.destinationId.empty())
            node.setProperty(keys::kDId, juce::String(kit.destinationId), nullptr);
        if (!kit.midiPresetName.empty())
            node.setProperty(keys::kMPreset, juce::String(kit.midiPresetName), nullptr);
        if (kit.divider != 1)
            node.setProperty(keys::kDiv, kit.divider, nullptr);

        // Base params + post-machine FLTR/AMP via temp machine to get correct
        // slot IDs. The slot range covers machine params, then the foundation
        // FLTR block, then AMP (ids "lockstep.fltr.*" / "lockstep.amp.*") — same
        // id-keyed scheme as the legacy PartTrack so the kit owns its sound.
        auto tempMachine = proc.createMachineForId(kit.machineId);
        const int machinNp = tempMachine->numParams();
        const int np = proc.numSlotsWithMachine(*tempMachine);
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
                    const int ampBase = machinNp + (tempMachine->hasInternalFilter() ? 0 : TrackFltrState::kNumSlots);
                    val = kit.ampState.getSlot(s - ampBase);
                }
                else
                    val = 0.0f;
                if (!floatNe(val, def)) continue;
                juce::ValueTree pNode("P");
                pNode.setProperty("id", id, nullptr);
                pNode.setProperty("v", static_cast<double>(val), nullptr);
                bpNode.appendChild(pNode, nullptr);
            }
            if (bpNode.getNumChildren() > 0)
                node.appendChild(bpNode, nullptr);
        }

        // v13: insert chain slots.
        for (int s = 0; s < 2; ++s)
        {
            const auto& insSlot = kit.inserts[static_cast<std::size_t>(s)];
            if (insSlot.effectId.empty()) continue;
            auto tempEff = makeEffectForId(insSlot.effectId);
            if (!tempEff) continue;
            juce::ValueTree insNode("Ins");
            insNode.setProperty("slot", s, nullptr);
            insNode.setProperty(keys::kEid, juce::String(insSlot.effectId), nullptr);
            if (insSlot.bypass)
                insNode.setProperty(keys::kBypass, 1, nullptr);
            const int effNp = tempEff->numParams();
            for (int p = 0; p < effNp; ++p)
            {
                const auto spec = tempEff->paramSpec(p);
                const float def = spec.defaultValue;
                const float val = (p < static_cast<int>(insSlot.baseParams.size()))
                                      ? insSlot.baseParams[static_cast<std::size_t>(p)]
                                      : def;
                if (!floatNe(val, def)) continue;
                juce::ValueTree pNode("P");
                pNode.setProperty("id", juce::String(spec.id), nullptr);
                pNode.setProperty("v", static_cast<double>(val), nullptr);
                insNode.appendChild(pNode, nullptr);
            }
            node.appendChild(insNode, nullptr);
        }
        return node;
    }

    static void readKitFromNode(const juce::ValueTree& node, TrackKit& kit, LockstepProcessor& proc)
    {
        kit.machineId = node.getProperty(keys::kMId, juce::String(SamplerMachine::kMachineId)).toString().toStdString();
        kit.destinationId = node.getProperty(keys::kDId, "").toString().toStdString();
        kit.midiPresetName = node.getProperty(keys::kMPreset, "").toString().toStdString();
        kit.divider = static_cast<int>(node.getProperty(keys::kDiv, 1));

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
                const juce::String id = pNode.getProperty("id", "").toString();
                const float val = getFloat(pNode, "v", 0.0f);
                const int slot = proc.slotForIdWithMachine(*tempMachine, id);
                if (slot < 0) continue;  // unknown id (e.g. machine changed) — skip
                if (slot < machinNp)
                    kit.baseParams[static_cast<std::size_t>(slot)] = val;
                else if (id.startsWith("lockstep.fltr."))
                    kit.fltrState.setSlot(slot - machinNp, val);
                else if (id.startsWith("lockstep.amp."))
                {
                    const int ampBase = machinNp + (tempMachine->hasInternalFilter() ? 0 : TrackFltrState::kNumSlots);
                    kit.ampState.setSlot(slot - ampBase, val);
                }
            }
        }

        // v13: insert chain slots.
        for (auto child : node)
        {
            if (child.getType() != juce::Identifier("Ins")) continue;
            const int s = static_cast<int>(child.getProperty("slot", -1));
            if (s < 0 || s > 1) continue;
            const std::string effId = child.getProperty(keys::kEid, "").toString().toStdString();
            if (effId.empty()) continue;
            auto& insSlot = kit.inserts[static_cast<std::size_t>(s)];
            insSlot.effectId = effId;
            insSlot.bypass = (static_cast<int>(child.getProperty(keys::kBypass, 0)) != 0);
            auto tempEff = makeEffectForId(effId);
            if (tempEff)
            {
                const int np = tempEff->numParams();
                insSlot.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                for (int p = 0; p < np; ++p)
                    insSlot.baseParams[static_cast<std::size_t>(p)] = tempEff->paramSpec(p).defaultValue;
                for (auto pNode : child)
                {
                    if (pNode.getType() != juce::Identifier("P")) continue;
                    const juce::String id = pNode.getProperty("id", "").toString();
                    for (int p = 0; p < np; ++p)
                    {
                        if (juce::String(tempEff->paramSpec(p).id) == id)
                        {
                            insSlot.baseParams[static_cast<std::size_t>(p)] = getFloat(pNode, "v", 0.0f);
                            break;
                        }
                    }
                }
            }
        }
    }

    static void writeNewHierarchyNode(juce::ValueTree& root, LockstepProcessor& proc)
    {
        juce::ValueTree nhNode(keys::kNewHierarchy);
        nhNode.setProperty(keys::kActivePiece, proc.activePieceIdx(), nullptr);
        nhNode.setProperty(keys::kActiveSect, proc.activeSectionIdx(), nullptr);
        nhNode.setProperty(keys::kLaunchQuant, proc.project().launchQuantizeBars, nullptr);

        for (int pi = 0; pi < kNumSongs; ++pi)
        {
            const auto& song = proc.songAt(pi);
            bool pieceHasContent = false;

            juce::ValueTree songNode(keys::kSong);
            songNode.setProperty("i", pi, nullptr);
            if (floatNe(song.swing, 0.0f))
                songNode.setProperty(keys::kSwing, static_cast<double>(song.swing), nullptr);

            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                const auto& lane = song.tracks[static_cast<std::size_t>(t)];
                bool laneHasContent = false;
                juce::ValueTree songTrackNode(keys::kSongTrack);
                songTrackNode.setProperty("t", t, nullptr);
                if (floatNe(lane.swing, 0.0f))
                    songTrackNode.setProperty(keys::kSwing, static_cast<double>(lane.swing), nullptr);

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
                    if (!hasData)
                        for (const auto& s : phrase.steps)
                            if (s.trig)
                            {
                                hasData = true;
                                break;
                            }
                    if (!hasData) continue;
                    songTrackNode.appendChild(writePhraseNode(ph, phrase, proc, t), nullptr);
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
                juce::ValueTree sceneNode(keys::kScene);
                sceneNode.setProperty("i", si, nullptr);
                sceneNode.setProperty(keys::kCtN, sec.coreTime.numerator, nullptr);
                sceneNode.setProperty(keys::kCtD, sec.coreTime.denominator, nullptr);
                if (floatNe(sec.swing, 0.0f))
                    sceneNode.setProperty(keys::kSwing, static_cast<double>(sec.swing), nullptr);
                // activeMask (default all true; only write if any false).
                bool anyMasked = false;
                for (const bool m : sec.activeMask)
                    if (!m)
                    {
                        anyMasked = true;
                        break;
                    }
                if (anyMasked)
                {
                    int maskBits = 0;
                    for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                        if (!sec.activeMask[static_cast<std::size_t>(t)]) maskBits |= (1 << t);
                    sceneNode.setProperty(keys::kMutesMask, maskBits, nullptr);
                }
                // Scene A/B snapshots (Phase 7 Stage G; full morph impl = 5.2).
                auto writeSceneMap = [&](const char* tag,
                                         const std::map<std::pair<int, int>, float>& sceneMap) {
                    if (sceneMap.empty()) return;
                    juce::ValueTree scNode(tag);
                    for (const auto& [key, val] : sceneMap)
                    {
                        juce::ValueTree eNode("E");
                        eNode.setProperty("t", key.first, nullptr);
                        eNode.setProperty("s", key.second, nullptr);
                        eNode.setProperty("v", static_cast<double>(val), nullptr);
                        scNode.appendChild(eNode, nullptr);
                    }
                    sceneNode.appendChild(scNode, nullptr);
                };
                writeSceneMap(keys::kMorphA, sec.morphA);
                writeSceneMap(keys::kMorphB, sec.morphB);
                songNode.appendChild(sceneNode, nullptr);
                pieceHasContent = true;
            }

            // v14: master FX inserts for this song.
            for (int s = 0; s < 2; ++s)
            {
                const auto& mIns = song.masterInserts[static_cast<std::size_t>(s)];
                if (mIns.effectId.empty()) continue;
                auto tempEff = makeEffectForId(mIns.effectId);
                if (!tempEff) continue;
                juce::ValueTree mInsNode(keys::kMasterIns);
                mInsNode.setProperty("slot", s, nullptr);
                mInsNode.setProperty(keys::kEid, juce::String(mIns.effectId), nullptr);
                if (mIns.bypass)
                    mInsNode.setProperty(keys::kBypass, 1, nullptr);
                const int np = tempEff->numParams();
                for (int p = 0; p < np; ++p)
                {
                    const auto spec = tempEff->paramSpec(p);
                    const float def = spec.defaultValue;
                    const float val = (p < static_cast<int>(mIns.baseParams.size()))
                                          ? mIns.baseParams[static_cast<std::size_t>(p)]
                                          : def;
                    if (!floatNe(val, def)) continue;
                    juce::ValueTree pNode("P");
                    pNode.setProperty("id", juce::String(spec.id), nullptr);
                    pNode.setProperty("v", static_cast<double>(val), nullptr);
                    mInsNode.appendChild(pNode, nullptr);
                }
                songNode.appendChild(mInsNode, nullptr);
                pieceHasContent = true;
            }

            if (pieceHasContent || pi == proc.activePieceIdx())
                nhNode.appendChild(songNode, nullptr);
        }
        root.appendChild(nhNode, nullptr);
    }

    static void readNewHierarchyNode(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto nhNode = root.getChildWithName(keys::kNewHierarchy);
        if (!nhNode.isValid()) return;

        const int activePiece = static_cast<int>(nhNode.getProperty(keys::kActivePiece, 0));
        const int activeSect = static_cast<int>(nhNode.getProperty(keys::kActiveSect, 0));
        proc.project().launchQuantizeBars = static_cast<int>(nhNode.getProperty(keys::kLaunchQuant, 1));

        for (auto songNode : nhNode)
        {
            if (songNode.getType() != juce::Identifier(keys::kSong)) continue;
            const int pi = static_cast<int>(songNode.getProperty("i", -1));
            if (pi < 0 || pi >= kNumSongs) continue;
            auto& song = proc.songAt(pi);
            song.swing = getFloat(songNode, keys::kSwing, 0.0f);

            // Collect legacy globalPhrase values (v10 and earlier stored a movable home
            // row; absent "gp" defaults to si = no migration needed for new saves).
            std::array<int, kScenesPerSong> legacyGp{};
            for (int s = 0; s < kScenesPerSong; ++s)
                legacyGp[static_cast<std::size_t>(s)] = s;

            for (auto child : songNode)
            {
                if (child.getType() == juce::Identifier(keys::kSongTrack))
                {
                    const int t = static_cast<int>(child.getProperty("t", -1));
                    if (t < 0 || t >= static_cast<int>(kNumTracks)) continue;
                    auto& lane = song.tracks[static_cast<std::size_t>(t)];
                    lane.swing = getFloat(child, keys::kSwing, 0.0f);

                    const auto kitNode = child.getChildWithName(keys::kKit);
                    if (kitNode.isValid())
                        readKitFromNode(kitNode, lane.kit, proc);

                    for (auto phraseNode : child)
                    {
                        if (phraseNode.getType() != juce::Identifier(keys::kPhrase)) continue;
                        const int ph = static_cast<int>(phraseNode.getProperty("i", -1));
                        if (ph < 0 || ph >= kPhrasesPerTrack) continue;
                        auto& phrase = song.tracks[static_cast<std::size_t>(t)].phrases[static_cast<std::size_t>(ph)];
                        readPhraseFromNode(phraseNode, phrase,
                                           [&](const juce::String& id) { return proc.slotForId(t, id); });
                        phrase.initialised = true;
                    }
                }
                else if (child.getType() == juce::Identifier(keys::kMasterIns))
                {
                    // v14: master FX insert slot.
                    const int s = static_cast<int>(child.getProperty("slot", -1));
                    if (s < 0 || s > 1) continue;
                    const std::string effId = child.getProperty(keys::kEid, "").toString().toStdString();
                    if (effId.empty()) continue;
                    auto& mIns = song.masterInserts[static_cast<std::size_t>(s)];
                    mIns.effectId = effId;
                    mIns.bypass = (static_cast<int>(child.getProperty(keys::kBypass, 0)) != 0);
                    auto tempEff = makeEffectForId(effId);
                    if (tempEff)
                    {
                        const int np = tempEff->numParams();
                        mIns.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                        for (int p = 0; p < np; ++p)
                            mIns.baseParams[static_cast<std::size_t>(p)] = tempEff->paramSpec(p).defaultValue;
                        for (auto pNode : child)
                        {
                            if (pNode.getType() != juce::Identifier("P")) continue;
                            const juce::String id = pNode.getProperty("id", "").toString();
                            for (int p = 0; p < np; ++p)
                            {
                                if (juce::String(tempEff->paramSpec(p).id) == id)
                                {
                                    mIns.baseParams[static_cast<std::size_t>(p)] = getFloat(pNode, "v", 0.0f);
                                    break;
                                }
                            }
                        }
                    }
                }
                else if (child.getType() == juce::Identifier(keys::kScene))
                {
                    const int si = static_cast<int>(child.getProperty("i", -1));
                    if (si < 0 || si >= kScenesPerSong) continue;
                    auto& sec = song.scenes[static_cast<std::size_t>(si)];
                    sec.coreTime.numerator = static_cast<int>(child.getProperty(keys::kCtN, 4));
                    sec.coreTime.denominator = static_cast<int>(child.getProperty(keys::kCtD, 4));
                    legacyGp[static_cast<std::size_t>(si)] = static_cast<int>(child.getProperty(keys::kGp, si));
                    sec.swing = getFloat(child, keys::kSwing, 0.0f);
                    sec.initialised = true;

                    if (child.hasProperty(keys::kMutesMask))
                    {
                        const int maskBits = static_cast<int>(child.getProperty(keys::kMutesMask, 0));
                        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                            sec.activeMask[static_cast<std::size_t>(t)] = !(maskBits & (1 << t));
                    }
                    // Scene A/B
                    auto readSceneMap = [&](const char* tag,
                                            std::map<std::pair<int, int>, float>& sceneMap) {
                        const auto scNode = child.getChildWithName(tag);
                        if (!scNode.isValid()) return;
                        for (auto eNode : scNode)
                        {
                            const int t2 = static_cast<int>(eNode.getProperty("t", -1));
                            const int s2 = static_cast<int>(eNode.getProperty("s", -1));
                            if (t2 >= 0 && s2 >= 0)
                                sceneMap[{ t2, s2 }] = getFloat(eNode, "v", 0.0f);
                        }
                    };
                    readSceneMap(keys::kMorphA, sec.morphA);
                    readSceneMap(keys::kMorphB, sec.morphB);
                }
            }

            // Materialise legacy re-homed scenes onto the diagonal.
            // For any scene si where the saved gp != si, copy phrases[gp] → phrases[si]
            // so the scene sounds identical on its new canonical row.
            for (int si = 0; si < kScenesPerSong; ++si)
            {
                const int gp = legacyGp[static_cast<std::size_t>(si)];
                if (gp == si || gp < 0 || gp >= kPhrasesPerTrack) continue;
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    auto& trk = song.tracks[static_cast<std::size_t>(t)];
                    if (trk.phrases[static_cast<std::size_t>(gp)].initialised)
                        trk.phrases[static_cast<std::size_t>(si)] = trk.phrases[static_cast<std::size_t>(gp)];
                }
            }
        }

        // Apply active indices after all data is loaded.
        // Load path: set the playhead WITHOUT a write-back. setActiveSong/Scene
        // would flush the stale working buffer over the previous scene's
        // just-loaded phrases (the multi-scene save/reload content-loss bug).
        proc.loadActivePosition(activePiece, activeSect);
    }

    // ── End Phase 7 new hierarchy serialization ───────────────────────────────
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
        juce::ValueTree poolNode(keys::kSamplePool);
        const auto& pool = proc.samplePool();
        for (int i = 0; i < pool.size(); ++i)
        {
            const auto* s = pool.get(i);
            if (!s) continue;
            juce::ValueTree entry(keys::kEntry);
            entry.setProperty("i", i, nullptr);
            entry.setProperty(keys::kPath, juce::String(s->ref.path), nullptr);
            entry.setProperty(keys::kHash, hashToHex(s->ref.hashXX32), nullptr);
            poolNode.appendChild(entry, nullptr);
        }
        root.appendChild(poolNode, nullptr);
    }

    static void readSamplePool(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto poolNode = root.getChildWithName(keys::kSamplePool);
        if (!poolNode.isValid()) return;

        for (auto entry : poolNode)
        {
            const juce::String path = entry.getProperty(keys::kPath).toString();
            const juce::String hex = entry.getProperty(keys::kHash).toString();
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
                ref.path = path.toStdString();
                ref.hashXX32 = savedHash;
                proc.samplePool().addMissing(ref);
                DBG("PluginState: missing sample '" + path + "'");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Project sound pool (SoundEntry library)

    static void writeProjectSoundPool(juce::ValueTree& root, LockstepProcessor& proc)
    {
        const int n = proc.soundPoolSize();
        if (n == 0) return;

        juce::ValueTree bankNode(keys::kSoundPool);
        for (int i = 0; i < n; ++i)
        {
            const auto* e = proc.soundPoolEntry(i);
            if (!e) continue;

            juce::ValueTree entry(keys::kSoundEntry);
            entry.setProperty(keys::kIdx, i, nullptr);
            entry.setProperty(keys::kSeName, juce::String(e->name), nullptr);
            entry.setProperty(keys::kMId, juce::String(e->machineId), nullptr);
            entry.setProperty(keys::kSeSampleIdx, e->samplePoolIndex, nullptr);
            if (!e->destinationId.empty())
                entry.setProperty(keys::kDId, juce::String(e->destinationId), nullptr);

            // Base params keyed by string ID so they survive machine param reordering.
            if (!e->baseParams.empty())
            {
                auto tempMachine = proc.createMachineForId(e->machineId);
                const int np = tempMachine->numParams();
                juce::ValueTree bpNode(keys::kBaseParams);
                for (int s = 0; s < np; ++s)
                {
                    if (static_cast<std::size_t>(s) >= e->baseParams.size()) break;
                    const auto spec = tempMachine->paramSpec(s);
                    const juce::String id = spec.id;
                    if (id.isEmpty()) continue;
                    const float val = e->baseParams[static_cast<std::size_t>(s)];
                    if (!floatNe(val, spec.defaultValue)) continue;
                    juce::ValueTree pNode(keys::kParam);
                    pNode.setProperty(keys::kParamId, id, nullptr);
                    pNode.setProperty(keys::kV, static_cast<double>(val), nullptr);
                    bpNode.appendChild(pNode, nullptr);
                }
                if (bpNode.getNumChildren() > 0)
                    entry.appendChild(bpNode, nullptr);
            }

            bankNode.appendChild(entry, nullptr);
        }
        root.appendChild(bankNode, nullptr);
    }

    static void readProjectSoundPool(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto bankNode = root.getChildWithName(keys::kSoundPool);
        if (!bankNode.isValid()) return;

        for (auto entryNode : bankNode)
        {
            if (entryNode.getType() != juce::Identifier(keys::kSoundEntry)) continue;

            SoundEntry e;
            e.name = entryNode.getProperty(keys::kSeName, "Sound").toString().toStdString();
            e.machineId = entryNode.getProperty(keys::kMId, juce::String(SamplerMachine::kMachineId))
                              .toString()
                              .toStdString();
            e.samplePoolIndex = static_cast<int>(entryNode.getProperty(keys::kSeSampleIdx, -1));
            e.destinationId = entryNode.getProperty(keys::kDId, "").toString().toStdString();

            auto tempMachine = proc.createMachineForId(e.machineId);
            const int np = tempMachine->numParams();
            e.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
            for (int s = 0; s < np; ++s)
                e.baseParams[static_cast<std::size_t>(s)] = tempMachine->paramSpec(s).defaultValue;

            const auto bpNode = entryNode.getChildWithName(keys::kBaseParams);
            if (bpNode.isValid())
            {
                for (auto pNode : bpNode)
                {
                    const juce::String id = pNode.getProperty(keys::kParamId, "").toString();
                    const float val = getFloat(pNode, keys::kV, 0.0f);
                    const int slot = proc.slotForIdWithMachine(*tempMachine, id);
                    if (slot >= 0 && slot < np)
                        e.baseParams[static_cast<std::size_t>(slot)] = val;
                }
            }

            proc.pushSoundEntry(std::move(e));
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
            case CCScope::Crossfader:    return "Crossfader";
        }
        return "Track";
    }

    static CCScope strToScope(const juce::String& s)
    {
        if (s == "Global") return CCScope::Global;
        if (s == "SelectedTrack") return CCScope::SelectedTrack;
        if (s == "Contextual") return CCScope::Contextual;
        if (s == "Crossfader") return CCScope::Crossfader;
        return CCScope::Track;
    }

    static void writeMiscState(juce::ValueTree& root, LockstepProcessor& proc)
    {
        // CC mappings — slot indices converted to stable string IDs.
        juce::ValueTree ccNode(keys::kCCMappings);
        for (const auto& m : proc.ccMappingTable().mappings())
        {
            juce::ValueTree mNode("M");
            mNode.setProperty("cc", m.ccNumber, nullptr);
            mNode.setProperty(keys::kScope, scopeToStr(m.scope), nullptr);
            mNode.setProperty(keys::kCcTrack, m.trackIndex, nullptr);
            mNode.setProperty(keys::kMz, m.mzPosition, nullptr);
            mNode.setProperty(keys::kApvts, juce::String(m.apvtsID), nullptr);
            mNode.setProperty(keys::kRel, m.isRelative ? 1 : 0, nullptr);
            mNode.setProperty(keys::kScale, static_cast<double>(m.scale), nullptr);
            mNode.setProperty(keys::kEnc, static_cast<int>(m.encoding), nullptr);

            // Resolve the slot to a stable string ID so renames survive.
            // For SelectedTrack scope, track 0 is used as the schema reference.
            juce::String slotId;
            if (m.scope == CCScope::Track || m.scope == CCScope::SelectedTrack)
            {
                const int refTrack = (m.scope == CCScope::Track) ? m.trackIndex : 0;
                slotId = proc.idForSlot(refTrack, m.slot);
            }
            mNode.setProperty(keys::kSlotId, slotId, nullptr);

            ccNode.appendChild(mNode, nullptr);
        }
        root.appendChild(ccNode, nullptr);

        // Focus track + standalone BPM.
        juce::ValueTree miscNode(keys::kMisc);
        miscNode.setProperty(keys::kFocusTrack, proc.focusTrack(), nullptr);
        miscNode.setProperty(keys::kLocalBpm, proc.clock().localBpm(), nullptr);
        root.appendChild(miscNode, nullptr);
    }

    static void readMiscState(const juce::ValueTree& root, LockstepProcessor& proc)
    {
        const auto ccNode = root.getChildWithName(keys::kCCMappings);
        if (ccNode.isValid())
        {
            proc.ccMappingTable().clear();
            for (auto mNode : ccNode)
            {
                CCMapping m;
                m.ccNumber = static_cast<int>(mNode.getProperty("cc", -1));
                m.scope = strToScope(mNode.getProperty(keys::kScope).toString());
                m.trackIndex = static_cast<int>(mNode.getProperty(keys::kCcTrack, 0));
                m.mzPosition = static_cast<int>(mNode.getProperty(keys::kMz, -1));
                m.apvtsID = mNode.getProperty(keys::kApvts).toString().toStdString();
                m.isRelative = (static_cast<int>(mNode.getProperty(keys::kRel, 0)) != 0);
                m.scale = getFloat(mNode, keys::kScale, 1.0f / 128.0f);
                m.encoding = static_cast<RelativeCCEncoding>(
                    static_cast<int>(mNode.getProperty(keys::kEnc, 0)));

                // Resolve slot ID back to a runtime integer.
                const juce::String slotId = mNode.getProperty(keys::kSlotId).toString();
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

        const auto miscNode = root.getChildWithName(keys::kMisc);
        if (miscNode.isValid())
        {
            proc.setFocusTrack(static_cast<int>(miscNode.getProperty(keys::kFocusTrack, -1)));
            const double bpm = static_cast<double>(miscNode.getProperty(keys::kLocalBpm, 120.0));
            proc.clock().setLocalBpm(bpm);
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
        juce::ValueTree v1(keys::kLockstepState);
        v1.setProperty(keys::kVersion, 1, nullptr);
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
        juce::ValueTree v2(keys::kLockstepState);
        v2.setProperty(keys::kVersion, 2, nullptr);

        // Copy all non-Sequence children unchanged.
        for (int i = 0; i < v1.getNumChildren(); ++i)
        {
            const auto child = v1.getChild(i);
            if (child.getType() != juce::Identifier("Sequence"))
                v2.appendChild(child.createCopy(), nullptr);
        }

        // Build Project/Bank[0]/Pattern[0] + Part[0] from the old Sequence.
        juce::ValueTree projNode(keys::kProject);
        juce::ValueTree bankNode(keys::kBank);
        bankNode.setProperty("i", 0, nullptr);

        juce::ValueTree patNode(keys::kPattern);
        patNode.setProperty("i", 0, nullptr);
        patNode.setProperty(keys::kPartRef, 0, nullptr);

        juce::ValueTree partNode(keys::kPart);
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
                juce::ValueTree ptNode(keys::kPartTrack);
                ptNode.setProperty("i", trackIdx, nullptr);
                ptNode.setProperty(keys::kMachineId, SamplerMachine::kMachineId, nullptr);

                for (int j = 0; j < trackNode.getNumChildren(); ++j)
                {
                    const auto child = trackNode.getChild(j);
                    if (child.getType() == juce::Identifier(keys::kBaseParamsLegacy))
                        ptNode.appendChild(child.createCopy(), nullptr);
                    else
                        patTrackNode.appendChild(child.createCopy(), nullptr);
                }

                patNode.appendChild(patTrackNode, nullptr);
                partNode.appendChild(ptNode, nullptr);
            }
        }

        bankNode.appendChild(patNode, nullptr);
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
        v3.setProperty(keys::kVersion, 3, nullptr);

        // Walk Project/Bank/Pattern/Track/Steps/Step/TO nodes and convert gate.
        const auto projNode = v3.getChildWithName(keys::kProject);
        if (!projNode.isValid()) return v3;

        for (auto bankNode : projNode)
        {
            for (auto child : bankNode)
            {
                if (child.getType() != juce::Identifier(keys::kPattern)) continue;
                for (auto trackNode : child)
                {
                    // Fix TrigDefaults: "gateMs" float → "gateV" int.
                    auto tdNode = trackNode.getChildWithName(keys::kTrigDefaults);
                    if (tdNode.isValid() && tdNode.hasProperty(keys::kGateMs))
                    {
                        const float gms = static_cast<float>(
                            static_cast<double>(tdNode.getProperty(keys::kGateMs, 0.0)));
                        tdNode.setProperty(keys::kGateV,
                                           static_cast<int>(static_cast<uint8_t>(
                                               nearestMusicalGate(gms, 120.0))),
                                           nullptr);
                        tdNode.removeProperty(keys::kGateMs, nullptr);
                    }

                    // Fix each step's TO node: "g" double → "gv" int.
                    const auto stepsNode = trackNode.getChildWithName(keys::kSteps);
                    if (!stepsNode.isValid()) continue;
                    for (auto stepNode : stepsNode)
                    {
                        auto toNode = stepNode.getChildWithName("TO");
                        if (!toNode.isValid()) continue;
                        if (toNode.hasProperty("g"))
                        {
                            const float gms = static_cast<float>(
                                static_cast<double>(toNode.getProperty("g", 0.0)));
                            toNode.setProperty(keys::kGv,
                                               static_cast<int>(static_cast<uint8_t>(
                                                   nearestMusicalGate(gms, 120.0))),
                                               nullptr);
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
        v4.setProperty(keys::kVersion, 4, nullptr);

        // All existing Pattern and Part nodes are from a pre-gestural-archetype
        // session where every slot was pre-seeded. Mark them all as initialised
        // so they don't trigger copy/create gestures on first touch.
        const auto projNode = v4.getChildWithName(keys::kProject);
        if (!projNode.isValid()) return v4;

        for (auto bankNode : projNode)
        {
            for (auto child : bankNode)
            {
                if (child.getType() == juce::Identifier(keys::kPattern) ||
                    child.getType() == juce::Identifier(keys::kPart))
                {
                    child.setProperty(keys::kInit, 1, nullptr);
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
        juce::ValueTree cur(keys::kLockstepState);
        cur.setProperty(keys::kVersion, kCurrentVersion, nullptr);
        for (int i = 0; i < old.getNumChildren(); ++i)
        {
            const auto child = old.getChild(i);
            // Drop every legacy hierarchy node: "Project" (v2+) and the flat
            // pre-v2 "Sequence". Preserve APVTS ("Lockstep"), SamplePool, Misc,
            // CCMappings, and any already-present NewHierarchy.
            if (child.getType() != juce::Identifier(keys::kProject) && child.getType() != juce::Identifier("Sequence"))
                cur.appendChild(child.createCopy(), nullptr);
        }
        DBG("PluginState: clean break to v" + juce::String(kCurrentVersion) + ": legacy Project/Bank/Pattern/Part data discarded.");
        return cur;
    }

    // v9 → v10
    // Swing moved from APVTS into Song/SongTrack/Scene musical state.
    // Migration: read legacy APVTS "swing" (song-all) and "track_<t>_swing"
    // (song-track delta) and inject them into Song[0]'s NewHierarchy nodes.
    // Track swing stored in APVTS was a per-track override — migrate as-is since
    // the old model was: effective = globalSwing + trackSwing (same 2-addend sum).
    juce::ValueTree upgrade_v9_to_v10(const juce::ValueTree& v9)
    {
        juce::ValueTree v10 = v9.createCopy();
        v10.setProperty(keys::kVersion, 10, nullptr);

        // Extract legacy APVTS swing values.
        float legacyGlobal = 0.0f;
        std::array<float, kNumTracks> legacyTrack{};
        legacyTrack.fill(0.0f);

        const auto apvtsNode = v10.getChildWithName(keys::kLockstep);
        if (apvtsNode.isValid())
        {
            for (auto paramNode : apvtsNode)
            {
                if (paramNode.getType() != juce::Identifier("PARAM")) continue;
                const auto id = paramNode.getProperty("id").toString();
                const float val = static_cast<float>(
                    static_cast<double>(paramNode.getProperty("value", 0.0)));
                if (id == juce::String(keys::kSwing))
                {
                    legacyGlobal = std::clamp(val, -0.5f, 0.5f);
                }
                else
                {
                    for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                    {
                        const juce::String tid = "track_" + juce::String(t) + "_swing";
                        if (id == tid)
                        {
                            legacyTrack[static_cast<std::size_t>(t)] = std::clamp(val, -0.5f, 0.5f);
                            break;
                        }
                    }
                }
            }
        }

        // Skip migration if both levels are zero (default state — no-op).
        const bool hasLegacySwing = floatNe(legacyGlobal, 0.0f);
        bool hasLegacyTrackSwing = false;
        for (float v : legacyTrack)
            if (floatNe(v, 0.0f))
            {
                hasLegacyTrackSwing = true;
                break;
            }

        if (hasLegacySwing || hasLegacyTrackSwing)
        {
            auto nhNode = v10.getChildWithName(keys::kNewHierarchy);
            if (!nhNode.isValid())
            {
                // No NewHierarchy yet (bare v9 state) — inject a Song[0] node.
                nhNode = juce::ValueTree(keys::kNewHierarchy);
                nhNode.setProperty(keys::kActivePiece, 0, nullptr);
                nhNode.setProperty(keys::kActiveSect, 0, nullptr);
                v10.appendChild(nhNode, nullptr);
            }

            // Find or create Song[0].
            juce::ValueTree song0;
            for (auto child : nhNode)
            {
                if (child.getType() == juce::Identifier(keys::kSong) && static_cast<int>(child.getProperty("i", -1)) == 0)
                {
                    song0 = child;
                    break;
                }
            }
            if (!song0.isValid())
            {
                song0 = juce::ValueTree(keys::kSong);
                song0.setProperty("i", 0, nullptr);
                nhNode.appendChild(song0, nullptr);
            }

            if (hasLegacySwing)
                song0.setProperty(keys::kSwing, static_cast<double>(legacyGlobal), nullptr);

            if (hasLegacyTrackSwing)
            {
                for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                {
                    const float tv = legacyTrack[static_cast<std::size_t>(t)];
                    if (!floatNe(tv, 0.0f)) continue;
                    // Find or create SongTrack[t].
                    juce::ValueTree trackNode;
                    for (auto child : song0)
                    {
                        if (child.getType() == juce::Identifier(keys::kSongTrack) && static_cast<int>(child.getProperty("t", -1)) == t)
                        {
                            trackNode = child;
                            break;
                        }
                    }
                    if (!trackNode.isValid())
                    {
                        trackNode = juce::ValueTree(keys::kSongTrack);
                        trackNode.setProperty("t", t, nullptr);
                        song0.appendChild(trackNode, nullptr);
                    }
                    trackNode.setProperty(keys::kSwing, static_cast<double>(tv), nullptr);
                }
            }

            DBG("PluginState: v9→v10: migrated swing (global=" + juce::String(legacyGlobal) + ") into Song[0]");
        }

        return v10;
    }

    // v10 → v11
    // Stopped persisting Scene::globalPhrase as 'gp'; reader materialises legacy
    // re-homed phrases on load. No tree-level transform needed — just stamp the version.
    juce::ValueTree upgrade_v10_to_v11(const juce::ValueTree& v10)
    {
        juce::ValueTree v11 = v10.createCopy();
        v11.setProperty(keys::kVersion, 11, nullptr);
        return v11;
    }

    // v11 → v12
    // Adds per-step retrig rate (hasRetrig/retrigRate) and persists the previously
    // unserialised sound_id P-Lock (hasSoundId/soundId) to the TrigOverride "TO" node.
    // Missing fields default correctly on load; no tree-level transform needed.
    juce::ValueTree upgrade_v11_to_v12(const juce::ValueTree& v11)
    {
        juce::ValueTree v12 = v11.createCopy();
        v12.setProperty(keys::kVersion, 12, nullptr);
        return v12;
    }

    // v12 → v13
    // Adds per-track insert chains (effectId/baseParams/bypass) inside Kit nodes.
    // Missing "Ins" children on load default to empty (no effect), which is correct.
    juce::ValueTree upgrade_v12_to_v13(const juce::ValueTree& v12)
    {
        juce::ValueTree v13 = v12.createCopy();
        v13.setProperty(keys::kVersion, 13, nullptr);
        return v13;
    }

    // v13 → v14: adds per-song master FX inserts (MasterIns nodes).
    // Missing MasterIns nodes on load default to empty (no effect) — trivial upgrade.
    juce::ValueTree upgrade_v13_to_v14(const juce::ValueTree& v13)
    {
        juce::ValueTree v14 = v13.createCopy();
        v14.setProperty(keys::kVersion, 14, nullptr);
        return v14;
    }

    // v14 → v15: P-Lock entries now written with string id ("id") instead of int
    // slot ("s"). The loader accepts both formats, so no tree transform is needed;
    // the upgrade is a pure version-stamp to correctly identify new files.
    juce::ValueTree upgrade_v14_to_v15(const juce::ValueTree& v14)
    {
        juce::ValueTree v15 = v14.createCopy();
        v15.setProperty(keys::kVersion, 15, nullptr);
        return v15;
    }

    // v15 → v16: Project::soundPool serialized. Missing SoundPool node = empty pool (trivial).
    juce::ValueTree upgrade_v15_to_v16(const juce::ValueTree& v15)
    {
        juce::ValueTree v16 = v15.createCopy();
        v16.setProperty(keys::kVersion, 16, nullptr);
        return v16;
    }

    juce::ValueTree applyUpgrades(juce::ValueTree tree)
    {
        // Determine the version. v0 has root type "Lockstep" and no version attribute.
        int version = 0;
        if (tree.getType() == juce::Identifier(keys::kLockstepState))
            version = static_cast<int>(tree.getProperty(keys::kVersion, 0));

        if (version > kCurrentVersion)
            DBG("PluginState: state version " + juce::String(version) + " is newer than this build (supports up to v" + juce::String(kCurrentVersion) + "); loading anyway");

        // Apply each upgrade in order. Upgrades are idempotent with respect
        // to the chain: each runs only when needed by the version guard.
        // Pre-v1 trees (bare "Lockstep" APVTS root) are first normalised into a
        // LockstepState wrapper; everything below the current version then
        // collapses to the current format via the clean break.
        if (version < 1) tree = upgrade_v0_to_v1(tree);
        if (version < 10) tree = cleanBreakToCurrent(tree);
        if (version == 9) tree = upgrade_v9_to_v10(tree);
        if (version < 11) tree = upgrade_v10_to_v11(tree);
        if (version < 12) tree = upgrade_v11_to_v12(tree);
        if (version < 13) tree = upgrade_v12_to_v13(tree);
        if (version < 14) tree = upgrade_v13_to_v14(tree);
        if (version < 15) tree = upgrade_v14_to_v15(tree);
        if (version < 16) tree = upgrade_v15_to_v16(tree);

        return tree;
    }

    // -------------------------------------------------------------------------
    // Public API

    void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc)
    {
        juce::ValueTree root(keys::kLockstepState);
        root.setProperty(keys::kVersion, kCurrentVersion, nullptr);

        // APVTS state (parameters: gain, sync mode, channel mode, track lengths etc.)
        root.appendChild(proc.apvts().copyState(), nullptr);

        // Sample pool ({path, hash} refs — no PCM bytes)
        writeSamplePool(root, proc);

        // Project sound bank (SoundEntry library)
        writeProjectSoundPool(root, proc);

        // Phase 7 new hierarchy: Song/SongTrack/Kit/Phrase/Section
        writeNewHierarchyNode(root, proc);

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
        const auto apvtsChild = root.getChildWithName(keys::kLockstep);
        if (apvtsChild.isValid())
            proc.apvts().replaceState(apvtsChild);

        // Sample pool must be restored before hierarchy so pool indices resolve.
        readSamplePool(root, proc);
        // Project sound bank (SoundEntry library). Restored after sample pool so
        // samplePoolIndex refs into the restored pool are valid.
        readProjectSoundPool(root, proc);
        // Phase 7 new hierarchy.
        readNewHierarchyNode(root, proc);
        // CC mappings, focus track, standalone BPM.
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
                juce::ValueTree v0(keys::kLockstep);
                v0.setProperty("someParam", 0.5, nullptr);

                const auto v1 = lockstep::PluginState::applyUpgrades(v0);

                expect(v1.getType() == juce::Identifier(keys::kLockstepState),
                       "root type must be LockstepState");
                expectEquals(static_cast<int>(v1.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version must be current");
                expect(v1.getChildWithName(keys::kLockstep).isValid(),
                       "upgraded tree must contain APVTS child");
                expect(!v1.getChildWithName(keys::kSamplePool).isValid(),
                       "v0 upgrades must not invent a SamplePool node");
                expect(!v1.getChildWithName("Sequence").isValid(),
                       "v0 upgrades must not invent a Sequence node");
            }

            beginTest("v1 (flat Sequence) clean-breaks to current");
            {
                // A pre-v2 tree carried a flat "Sequence" node. The Phase 7 clean
                // break discards all legacy hierarchy; APVTS/SamplePool/Misc stay.
                juce::ValueTree v1(keys::kLockstepState);
                v1.setProperty(keys::kVersion, 1, nullptr);
                v1.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v1.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);
                v1.appendChild(juce::ValueTree("Sequence"), nullptr);
                v1.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v1);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName("Sequence").isValid(),
                       "legacy flat Sequence discarded");
                expect(!result.getChildWithName(keys::kProject).isValid(),
                       "no legacy Project remains");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName(keys::kSamplePool).isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName(keys::kMisc).isValid(),
                       "Misc preserved");
            }

            beginTest("legacy Project (v2-v4) clean-breaks to current");
            {
                // Any legacy Bank/Pattern/Part hierarchy is dropped wholesale by
                // the pre-release clean break; the processor re-seeds defaults.
                juce::ValueTree v2(keys::kLockstepState);
                v2.setProperty(keys::kVersion, 2, nullptr);
                v2.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v2.appendChild(juce::ValueTree(keys::kSamplePool), nullptr);

                juce::ValueTree proj(keys::kProject);
                juce::ValueTree bank(keys::kBank);
                bank.appendChild(juce::ValueTree(keys::kPattern), nullptr);
                bank.appendChild(juce::ValueTree(keys::kPart), nullptr);
                proj.appendChild(bank, nullptr);
                v2.appendChild(proj, nullptr);
                v2.appendChild(juce::ValueTree(keys::kMisc), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v2);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "version collapsed to current");
                expect(!result.getChildWithName(keys::kProject).isValid(),
                       "legacy Project/Bank/Pattern/Part discarded");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS preserved");
                expect(result.getChildWithName(keys::kSamplePool).isValid(),
                       "SamplePool preserved");
                expect(result.getChildWithName(keys::kMisc).isValid(),
                       "Misc preserved");
            }

            beginTest("current version passes through unchanged");
            {
                juce::ValueTree cur(keys::kLockstepState);
                cur.setProperty(keys::kVersion, lockstep::PluginState::kCurrentVersion, nullptr);
                cur.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                cur.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(cur);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "current version unchanged");
                expect(result.getChildWithName(keys::kNewHierarchy).isValid(),
                       "NewHierarchy node preserved at current version");
            }

            beginTest("v9 -> v10: APVTS swing migrated into Song[0] NewHierarchy");
            {
                // Build a v9 state with non-zero global swing and per-track swing.
                juce::ValueTree v9(keys::kLockstepState);
                v9.setProperty(keys::kVersion, 9, nullptr);

                juce::ValueTree apvts(keys::kLockstep);
                {
                    juce::ValueTree p1("PARAM");
                    p1.setProperty("id", keys::kSwing, nullptr);
                    p1.setProperty("value", 0.25, nullptr);
                    apvts.appendChild(p1, nullptr);

                    juce::ValueTree p2("PARAM");
                    p2.setProperty("id", "track_0_swing", nullptr);
                    p2.setProperty("value", -0.1, nullptr);
                    apvts.appendChild(p2, nullptr);
                }
                v9.appendChild(apvts, nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v9);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v9->v10+: version reaches current after full chain");

                const auto nh = result.getChildWithName(keys::kNewHierarchy);
                expect(nh.isValid(), "v9->v10: NewHierarchy present after migration");

                juce::ValueTree song0;
                for (auto c : nh)
                    if (c.getType() == juce::Identifier(keys::kSong) && static_cast<int>(c.getProperty("i", -1)) == 0)
                    {
                        song0 = c;
                        break;
                    }
                expect(song0.isValid(), "v9->v10: Song[0] node created");

                expectWithinAbsoluteError(
                    static_cast<float>(static_cast<double>(song0.getProperty(keys::kSwing, 0.0))),
                    0.25f, 0.001f, "v9->v10: song-all swing migrated");

                juce::ValueTree trk0;
                for (auto c : song0)
                    if (c.getType() == juce::Identifier(keys::kSongTrack) && static_cast<int>(c.getProperty("t", -1)) == 0)
                    {
                        trk0 = c;
                        break;
                    }
                expect(trk0.isValid(), "v9->v10: SongTrack[0] created for non-zero track swing");
                expectWithinAbsoluteError(
                    static_cast<float>(static_cast<double>(trk0.getProperty(keys::kSwing, 0.0))),
                    -0.1f, 0.001f, "v9->v10: track-0 swing migrated");
            }

            beginTest("v15 -> v16: missing SoundPool node loads as empty pool");
            {
                // A v15 tree has no SoundPool child; upgrade stamps to v16 but doesn't add the node.
                // readProjectSoundPool on that tree yields an empty pool (tested here at tree level).
                juce::ValueTree v15(keys::kLockstepState);
                v15.setProperty(keys::kVersion, 15, nullptr);
                v15.appendChild(juce::ValueTree(keys::kLockstep), nullptr);
                v15.appendChild(juce::ValueTree(keys::kNewHierarchy), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(v15);

                expectEquals(static_cast<int>(result.getProperty(keys::kVersion, -1)),
                             lockstep::PluginState::kCurrentVersion,
                             "v15->v16: version stamped to current");
                expect(!result.getChildWithName(keys::kSoundPool).isValid(),
                       "v15->v16: no SoundPool node invented (upgrade is trivial)");
                expect(result.getChildWithName(keys::kNewHierarchy).isValid(),
                       "v15->v16: NewHierarchy preserved");
            }

            beginTest("future version: valid tree returned without crash");
            {
                juce::ValueTree future(keys::kLockstepState);
                future.setProperty(keys::kVersion, lockstep::PluginState::kCurrentVersion + 1, nullptr);
                future.appendChild(juce::ValueTree(keys::kLockstep), nullptr);

                const auto result = lockstep::PluginState::applyUpgrades(future);

                expect(result.isValid(), "future version must return a valid tree");
                expect(result.getChildWithName(keys::kLockstep).isValid(),
                       "APVTS child must be intact");
            }
        }
    } gUpgradeTest;
} // anonymous namespace
#endif
