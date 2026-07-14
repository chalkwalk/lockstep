#include "PluginState.h"
#include "StateKeys.h"
#include "../core/Subdivision.h"
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
#include "../machine/SampleMachine.h"
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

    static juce::String hashToHex(std::uint32_t h);  // defined below
    static std::uint32_t hexToHash(const juce::String& s);  // defined below

    // v31 (9.24 S15): read an insert slot's optional convolution IR ref. Stored as
    // a content hash (kIrHash) -> a Persistent SampleId; absent = None. The pool
    // resolves the hash to a live index via indexOf/resolve, so it survives reorder.
    static SampleId readIrRef(const juce::ValueTree& slotNode)
    {
        const juce::String h = slotNode.getProperty(keys::kIrHash, "").toString();
        if (h.isEmpty()) return {};
        return { SampleId::Domain::Persistent, hexToHash(h) };
    }

    // 9.18: the machine params that hold a reference to a SamplePool entry.
    static bool isSampleRefId(const juce::String& id)
    {
        return id == "sample_id" || id == "slicer_sample_id";
    }

    // 9.18: stamp a P node (base param or P-Lock) that holds a sample reference with
    // the referenced entry's durable content hash, so the reference survives a pool
    // reorder across reload (the flat-index rot fix). Only Persistent (File/Stream)
    // entries have a content hash; a Volatile REC/LOOP slot is not persisted, so its
    // reference rides the raw index and simply resolves to nothing on the next load.
    static void stampSampleRefHash(juce::ValueTree& pNode, const juce::String& id,
                                   float val, LockstepProcessor& proc)
    {
        if (!isSampleRefId(id)) return;
        const auto sid = proc.samplePool().idOf(juce::roundToInt(val));
        if (sid.domain == SampleId::Domain::Persistent)
            pNode.setProperty(keys::kSampleHash, hashToHex(sid.key), nullptr);
    }

    // v22: key signature (DESIGN §4.10). Four scoped properties; see StateKeys.
    static void writeKeySig(juce::ValueTree& node, const KeySig& k,
                            const char* rk, const char* bk, const char* mk, const char* sk)
    {
        node.setProperty(rk, static_cast<int>(k.root), nullptr);
        node.setProperty(bk, static_cast<int>(k.brightness), nullptr);
        node.setProperty(mk, static_cast<int>(packModifiers(k.modifiers)), nullptr);
        node.setProperty(sk, static_cast<int>(static_cast<uint8_t>(k.scaleType)), nullptr);
    }

    static KeySig readKeySig(const juce::ValueTree& node,
                             const char* rk, const char* bk, const char* mk, const char* sk)
    {
        KeySig k;
        k.root       = static_cast<uint8_t>(static_cast<int>(node.getProperty(rk, 0)));
        k.brightness = static_cast<int8_t>(static_cast<int>(node.getProperty(bk, static_cast<int>(kIonian))));
        k.modifiers  = unpackModifiers(static_cast<uint8_t>(static_cast<int>(node.getProperty(mk, 0))));
        k.scaleType  = static_cast<ScaleType>(static_cast<uint8_t>(static_cast<int>(node.getProperty(sk, 0))));
        return k;
    }

    static juce::ValueTree condToTree(const juce::Identifier& type,
                                      const TrigCondition& c)
    {
        juce::ValueTree v(type);
        v.setProperty("p", static_cast<int>(c.probabilityPercent), nullptr);
        v.setProperty("n", static_cast<int>(c.iterNumerator), nullptr);
        v.setProperty("d", static_cast<int>(c.iterDenominator), nullptr);
        v.setProperty(keys::kPd, static_cast<int>(c.prevDependency), nullptr);
        if (c.oneShot)
            v.setProperty("os", 1, nullptr);  // 5.6 one-shot
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
        c.oneShot = (static_cast<int>(v.getProperty("os", 0)) != 0);  // 5.6
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
            if (!step.trig && !step.lockOnly && step.overrides.empty() && step.trigOverride.noteCount == 0 && !step.trigOverride.hasVelocity && !step.trigOverride.hasGate && !step.trigOverride.hasSoundId && !step.trigOverride.hasRetrig && step.condition.isTrivial() && !floatNe(step.microOffset, 0.0f) && step.fillTrigState == FillTrigState::Inherit && step.fillOverrides.empty() && step.fillTrigOverride.noteCount == 0) continue;
            hasSteps = true;
            juce::ValueTree stepNode("S");
            stepNode.setProperty("i", s, nullptr);
            stepNode.setProperty("t", step.trig ? 1 : 0, nullptr);
            if (step.lockOnly)
                stepNode.setProperty("lo", 1, nullptr);  // 5.6 trigless / lock-only
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
                    stampSampleRefHash(pNode, sid, value, proc);  // 9.18
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
                    stampSampleRefHash(pNode, sid, value, proc);  // 9.18
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
            step.lockOnly = (static_cast<int>(stepNode.getProperty("lo", 0)) != 0);  // 5.6
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
        // Item 6 (v28): the streamed source is now a Stream pool entry + the track's
        // sample_id base param — kStreamPath is no longer written. It is still read
        // (below / readNewHierarchyNode) so ≤v27 projects migrate on load.
        if (!kit.midiPresetName.empty())
            node.setProperty(keys::kMPreset, juce::String(kit.midiPresetName), nullptr);
        if (kit.subdivIndex != kSubdivDefault)
            node.setProperty(keys::kDiv, kit.subdivIndex, nullptr);
        if (kit.densityMusicality != Density::Musicality::Mixed)
            node.setProperty(keys::kDensMus, static_cast<int>(kit.densityMusicality), nullptr);
        if (kit.densitySelection != Density::DensitySelection::Scrub)
            node.setProperty(keys::kDensSel, static_cast<int>(kit.densitySelection), nullptr);
        if (kit.velMode != VelMode::Off)
            node.setProperty(keys::kVelMode, static_cast<int>(kit.velMode), nullptr);
        if (kit.velBlend != VelBlend::Replace)
            node.setProperty(keys::kVelBlend, static_cast<int>(kit.velBlend), nullptr);
        if (kit.velDepth != 0.6f)
            node.setProperty(keys::kVelDepth, static_cast<double>(kit.velDepth), nullptr);
        if (kit.velCenter != 90)
            node.setProperty(keys::kVelCenter, kit.velCenter, nullptr);
        if (kit.scaleMode != ScaleMode::Off)
            node.setProperty(keys::kScaleMode, static_cast<int>(kit.scaleMode), nullptr);
        if (kit.launchQuant != kFollowGlobal)
            node.setProperty(keys::kLaunchQ, kit.launchQuant, nullptr);

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
                    const int chanOff = machinNp + TrackFltrState::kNumSlots;
                    const int envOff  = chanOff + TrackChannelState::kNumSlots;
                    if (s < envOff)
                        val = kit.channelState.getSlot(s - chanOff);
                    else
                        val = kit.envState.getSlot(s - envOff);
                }
                else
                    val = 0.0f;
                // 9.18: a sample reference that points at a Persistent (File/Stream)
                // entry is always written — even at the default index 0 — so its
                // durable content hash rides along and survives a pool reorder. A
                // Stream track very often references index 0, which default-elision
                // would otherwise silently drop (the "stream doesn't reload" trap).
                const bool persistentSampleRef =
                    isSampleRefId(id)
                    && proc.samplePool().idOf(juce::roundToInt(val)).domain
                           == SampleId::Domain::Persistent;
                if (!floatNe(val, def) && !persistentSampleRef) continue;
                juce::ValueTree pNode("P");
                pNode.setProperty("id", id, nullptr);
                pNode.setProperty("v", static_cast<double>(val), nullptr);
                stampSampleRefHash(pNode, id, val, proc);  // 9.18
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
            // v31: convolution IR ref (Persistent only — Volatile/None not persisted).
            if (insSlot.irRef.domain == SampleId::Domain::Persistent)
                insNode.setProperty(keys::kIrHash, hashToHex(insSlot.irRef.key), nullptr);
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
        kit.machineId = node.getProperty(keys::kMId, juce::String(SampleMachine::kMachineId)).toString().toStdString();
        kit.destinationId = node.getProperty(keys::kDId, "").toString().toStdString();
        kit.streamPath = node.getProperty(keys::kStreamPath, "").toString().toStdString();
        kit.midiPresetName = node.getProperty(keys::kMPreset, "").toString().toStdString();
        kit.subdivIndex = static_cast<int>(node.getProperty(keys::kDiv, kSubdivDefault));
        kit.densityMusicality = static_cast<Density::Musicality>(
            static_cast<int>(node.getProperty(keys::kDensMus,
                static_cast<int>(Density::Musicality::Mixed))));
        kit.densitySelection = static_cast<Density::DensitySelection>(
            static_cast<int>(node.getProperty(keys::kDensSel,
                static_cast<int>(Density::DensitySelection::Scrub))));
        kit.velMode   = static_cast<VelMode>(
            static_cast<int>(node.getProperty(keys::kVelMode, static_cast<int>(VelMode::Off))));
        kit.velBlend  = static_cast<VelBlend>(
            static_cast<int>(node.getProperty(keys::kVelBlend, static_cast<int>(VelBlend::Replace))));
        kit.velDepth  = static_cast<float>(
            static_cast<double>(node.getProperty(keys::kVelDepth, 0.6)));
        kit.velCenter = static_cast<int>(node.getProperty(keys::kVelCenter, 90));
        kit.scaleMode = static_cast<ScaleMode>(
            static_cast<int>(node.getProperty(keys::kScaleMode, static_cast<int>(ScaleMode::Off))));
        kit.launchQuant = static_cast<int>(node.getProperty(keys::kLaunchQ, kFollowGlobal));

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
                if (slot < 0) continue;
                if (slot < machinNp)
                {
                    float v = val;
                    // S1 migration: loop_sync collapsed to Free|Free Len|Sync (0..2).
                    // Old 1/2/4 Bar (2..4) + Steps (5) all map to Sync (2); dropped
                    // loop_div/loop_steps ids resolve to slot<0 above and are skipped.
                    if (kit.machineId == "lockstep.loop.v1" && id == "loop_sync" && v > 2.0f)
                        v = 2.0f;
                    kit.baseParams[static_cast<std::size_t>(slot)] = v;
                }
                else if (id.startsWith("lockstep.fltr."))
                    kit.fltrState.setSlot(slot - machinNp, val);
                else if (id.startsWith("lockstep.amp."))
                {
                    const int chanOff = machinNp + TrackFltrState::kNumSlots;
                    const int envOff  = chanOff + TrackChannelState::kNumSlots;
                    if (slot < envOff)
                        kit.channelState.setSlot(slot - chanOff, val);
                    else
                        kit.envState.setSlot(slot - envOff, val);
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
            insSlot.irRef = readIrRef(child);   // v31 (empty on older docs)
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
        // v25: launchQuant is the raw LaunchQuant enum on disk (pre-v25 files
        // stored a legacy bar count; upgrade_v24_to_v25 remaps them). See
        // LaunchQuant.h.
        nhNode.setProperty(keys::kLaunchQuant, proc.project().launchQuant, nullptr);
        // A5: volatile REC slot capacity (seconds). Additive — a doc without it
        // loads the 60 s default, which is exactly the pre-A5 behaviour scaled up,
        // so this needs no version bump.
        if (std::abs(proc.volatileMaxSeconds() - 60.0) > 1.0e-6)
            nhNode.setProperty(keys::kVolatileSecs, proc.volatileMaxSeconds(), nullptr);
        // A6: click level + count-in. Additive; defaults are the pre-A6 behaviour.
        if (std::abs(proc.project().metronomeLevel - 0.6f) > 1.0e-6f)
            nhNode.setProperty(keys::kMetroLevel, proc.project().metronomeLevel, nullptr);
        if (proc.project().preRollBars != 0)
            nhNode.setProperty(keys::kPreRollBars, proc.project().preRollBars, nullptr);
        // v27: hosted-Locked arm gate. Default-armed, so only write when parked to
        // keep old files byte-identical on re-save when armed.
        if (!proc.isPluginArmed())
            nhNode.setProperty(keys::kPluginArmed, 0, nullptr);
        // v21: Set-level default time signature (only write if non-default).
        const auto& setTs = proc.project().defaultTimeSig;
        if (!(setTs == TimeSig{}))
        {
            nhNode.setProperty(keys::kSetTsN, setTs.numerator, nullptr);
            nhNode.setProperty(keys::kSetTsD, setTs.denominator, nullptr);
        }
        // v22: Set-level default key signature (only write if non-default C Ionian).
        if (!(proc.project().defaultKeySig == KeySig{}))
            writeKeySig(nhNode, proc.project().defaultKeySig,
                        keys::kSetKsRoot, keys::kSetKsBri, keys::kSetKsMod, keys::kSetKsSym);

        for (int pi = 0; pi < kNumSongs; ++pi)
        {
            const auto& song = proc.songAt(pi);
            bool pieceHasContent = false;

            juce::ValueTree songNode(keys::kSong);
            songNode.setProperty("i", pi, nullptr);
            if (floatNe(song.swing, 0.0f))
                songNode.setProperty(keys::kSwing, static_cast<double>(song.swing), nullptr);
            // v21: optional Song-level time signature override.
            if (song.hasTimeSig)
            {
                songNode.setProperty(keys::kHasTs, 1, nullptr);
                songNode.setProperty(keys::kSongTsN, song.timeSig.numerator, nullptr);
                songNode.setProperty(keys::kSongTsD, song.timeSig.denominator, nullptr);
            }
            // v22: optional Song-level key-signature override.
            if (song.hasKeySig)
            {
                songNode.setProperty(keys::kHasKs, 1, nullptr);
                writeKeySig(songNode, song.keySig,
                            keys::kSongKsRoot, keys::kSongKsBri, keys::kSongKsMod, keys::kSongKsSym);
            }
            // v21: optional Song-level tempo ratio.
            if (song.hasTempo)
            {
                songNode.setProperty(keys::kHasTempo, 1, nullptr);
                songNode.setProperty(keys::kTempoRatio, song.tempoRatio, nullptr);
            }

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

            // Write non-default sections. The gate ORs in sceneDiagonalOccupied()
            // (Song.h) so a scene whose only content is its diagonal phrase row
            // still gets a node — otherwise it reloads unmarked and the Scene+step
            // handler takes the destructive create-on-empty path.
            for (int si = 0; si < kScenesPerSong; ++si)
            {
                const auto& sec = song.scenes[static_cast<std::size_t>(si)];
                if (!sceneHasContent(sec) && !sceneDiagonalOccupied(song, si)) continue;
                juce::ValueTree sceneNode(keys::kScene);
                sceneNode.setProperty("i", si, nullptr);
                // v21: only write coreTime when explicitly set via hasTimeSig.
                if (sec.hasTimeSig)
                {
                    sceneNode.setProperty(keys::kHasTs, 1, nullptr);
                    sceneNode.setProperty(keys::kCtN, sec.coreTime.numerator, nullptr);
                    sceneNode.setProperty(keys::kCtD, sec.coreTime.denominator, nullptr);
                }
                // v22: optional Scene-level key-signature override.
                if (sec.hasKeySig)
                {
                    sceneNode.setProperty(keys::kHasKs, 1, nullptr);
                    writeKeySig(sceneNode, sec.coreKeySig,
                                keys::kScKsRoot, keys::kScKsBri, keys::kScKsMod, keys::kScKsSym);
                }
                // v21: optional Scene-level tempo ratio.
                if (sec.hasTempo)
                {
                    sceneNode.setProperty(keys::kHasTempo, 1, nullptr);
                    sceneNode.setProperty(keys::kTempoRatio, sec.tempoRatio, nullptr);
                }
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
                auto tempEff = makeEffectForId(mIns.effectId, EffectTier::Master);
                if (!tempEff) continue;
                juce::ValueTree mInsNode(keys::kMasterIns);
                mInsNode.setProperty("slot", s, nullptr);
                mInsNode.setProperty(keys::kEid, juce::String(mIns.effectId), nullptr);
                if (mIns.bypass)
                    mInsNode.setProperty(keys::kBypass, 1, nullptr);
                if (mIns.irRef.domain == SampleId::Domain::Persistent)  // v31
                    mInsNode.setProperty(keys::kIrHash, hashToHex(mIns.irRef.key), nullptr);
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
            // v17: master send return slots for this song.
            for (int s = 0; s < 2; ++s)
            {
                const auto& mSnd = song.masterSends[static_cast<std::size_t>(s)];
                if (mSnd.effectId.empty()) continue;
                auto tempEff = makeEffectForId(mSnd.effectId, EffectTier::Master);
                if (!tempEff) continue;
                juce::ValueTree mSndNode(keys::kMasterSnd);
                mSndNode.setProperty("slot", s, nullptr);
                mSndNode.setProperty(keys::kEid, juce::String(mSnd.effectId), nullptr);
                if (mSnd.bypass)
                    mSndNode.setProperty(keys::kBypass, 1, nullptr);
                if (mSnd.irRef.domain == SampleId::Domain::Persistent)  // v31
                    mSndNode.setProperty(keys::kIrHash, hashToHex(mSnd.irRef.key), nullptr);
                const int np = tempEff->numParams();
                for (int p = 0; p < np; ++p)
                {
                    const auto spec = tempEff->paramSpec(p);
                    const float def = spec.defaultValue;
                    const float val = (p < static_cast<int>(mSnd.baseParams.size()))
                                          ? mSnd.baseParams[static_cast<std::size_t>(p)]
                                          : def;
                    if (!floatNe(val, def)) continue;
                    juce::ValueTree pNode("P");
                    pNode.setProperty("id", juce::String(spec.id), nullptr);
                    pNode.setProperty("v", static_cast<double>(val), nullptr);
                    mSndNode.appendChild(pNode, nullptr);
                }
                songNode.appendChild(mSndNode, nullptr);
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
        // v25: launchQuant is the raw LaunchQuant enum on disk (pre-v25 files are
        // remapped from a legacy bar count by upgrade_v24_to_v25). Missing → Bar.
        proc.project().launchQuant = static_cast<int>(
            nhNode.getProperty(keys::kLaunchQuant, static_cast<int>(LaunchQuant::Bar)));
        proc.setVolatileMaxSeconds(
            static_cast<double>(nhNode.getProperty(keys::kVolatileSecs, 60.0)));
        proc.project().metronomeLevel =
            static_cast<float>(static_cast<double>(nhNode.getProperty(keys::kMetroLevel, 0.6)));
        proc.project().preRollBars =
            static_cast<int>(nhNode.getProperty(keys::kPreRollBars, 0));
        // v27: hosted-Locked arm gate. Missing (v26 and earlier, or armed) → armed.
        proc.setPluginArmed(static_cast<int>(nhNode.getProperty(keys::kPluginArmed, 1)) != 0);
        // v21: Set-level default time signature.
        if (nhNode.hasProperty(keys::kSetTsN))
        {
            proc.project().defaultTimeSig.numerator =
                static_cast<int>(nhNode.getProperty(keys::kSetTsN, 4));
            proc.project().defaultTimeSig.denominator =
                static_cast<int>(nhNode.getProperty(keys::kSetTsD, 4));
        }
        // v22: Set-level default key signature.
        if (nhNode.hasProperty(keys::kSetKsRoot))
            proc.project().defaultKeySig = readKeySig(nhNode,
                keys::kSetKsRoot, keys::kSetKsBri, keys::kSetKsMod, keys::kSetKsSym);

        for (auto songNode : nhNode)
        {
            if (songNode.getType() != juce::Identifier(keys::kSong)) continue;
            const int pi = static_cast<int>(songNode.getProperty("i", -1));
            if (pi < 0 || pi >= kNumSongs) continue;
            auto& song = proc.songAt(pi);
            song.swing = getFloat(songNode, keys::kSwing, 0.0f);
            // v21: optional Song-level time signature override.
            if (static_cast<int>(songNode.getProperty(keys::kHasTs, 0)) != 0)
            {
                song.hasTimeSig = true;
                song.timeSig.numerator = static_cast<int>(songNode.getProperty(keys::kSongTsN, 4));
                song.timeSig.denominator = static_cast<int>(songNode.getProperty(keys::kSongTsD, 4));
            }
            // v22: optional Song-level key-signature override.
            if (songNode.hasProperty(keys::kSongKsRoot))
            {
                song.hasKeySig = true;
                song.keySig = readKeySig(songNode,
                    keys::kSongKsRoot, keys::kSongKsBri, keys::kSongKsMod, keys::kSongKsSym);
            }
            // v21: optional Song-level tempo ratio.
            if (static_cast<int>(songNode.getProperty(keys::kHasTempo, 0)) != 0)
            {
                song.hasTempo = true;
                song.tempoRatio = static_cast<double>(songNode.getProperty(keys::kTempoRatio, 1.0));
            }

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
                    const std::string effId = canonicalEffectId(
                        child.getProperty(keys::kEid, "").toString().toStdString());
                    if (effId.empty()) continue;
                    auto& mIns = song.masterInserts[static_cast<std::size_t>(s)];
                    mIns.effectId = effId;
                    mIns.bypass = (static_cast<int>(child.getProperty(keys::kBypass, 0)) != 0);
                    mIns.irRef = readIrRef(child);   // v31
                    auto tempEff = makeEffectForId(effId, EffectTier::Master);
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
                else if (child.getType() == juce::Identifier(keys::kMasterSnd))
                {
                    // v17: master send return slot.
                    const int s = static_cast<int>(child.getProperty("slot", -1));
                    if (s < 0 || s > 1) continue;
                    const std::string effId = canonicalEffectId(
                        child.getProperty(keys::kEid, "").toString().toStdString());
                    if (effId.empty()) continue;
                    auto& mSnd = song.masterSends[static_cast<std::size_t>(s)];
                    mSnd.effectId = effId;
                    mSnd.bypass = (static_cast<int>(child.getProperty(keys::kBypass, 0)) != 0);
                    mSnd.irRef = readIrRef(child);   // v31
                    auto tempEff = makeEffectForId(effId, EffectTier::Master);
                    if (tempEff)
                    {
                        const int np = tempEff->numParams();
                        mSnd.baseParams.assign(static_cast<std::size_t>(np), 0.0f);
                        for (int p = 0; p < np; ++p)
                            mSnd.baseParams[static_cast<std::size_t>(p)] = tempEff->paramSpec(p).defaultValue;
                        for (auto pNode : child)
                        {
                            if (pNode.getType() != juce::Identifier("P")) continue;
                            const juce::String id = pNode.getProperty("id", "").toString();
                            for (int p = 0; p < np; ++p)
                            {
                                if (juce::String(tempEff->paramSpec(p).id) == id)
                                {
                                    mSnd.baseParams[static_cast<std::size_t>(p)] = getFloat(pNode, "v", 0.0f);
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
                    // v21: hasTimeSig flag gates coreTime; v20 and older scenes that wrote
                    // kCtN/kCtD unconditionally are handled by the hasTs check (absent = false).
                    if (static_cast<int>(child.getProperty(keys::kHasTs, 0)) != 0
                        || (child.hasProperty(keys::kCtN)
                            && !child.hasProperty(keys::kHasTs)))
                    {
                        // Accept old v20 scenes that always wrote kCtN/kCtD (no hasTs flag).
                        sec.hasTimeSig = true;
                        sec.coreTime.numerator = static_cast<int>(child.getProperty(keys::kCtN, 4));
                        sec.coreTime.denominator = static_cast<int>(child.getProperty(keys::kCtD, 4));
                    }
                    // v22: optional Scene-level key-signature override.
                    if (child.hasProperty(keys::kScKsRoot))
                    {
                        sec.hasKeySig = true;
                        sec.coreKeySig = readKeySig(child,
                            keys::kScKsRoot, keys::kScKsBri, keys::kScKsMod, keys::kScKsSym);
                    }
                    // v21: optional Scene-level tempo ratio.
                    if (static_cast<int>(child.getProperty(keys::kHasTempo, 0)) != 0)
                    {
                        sec.hasTempo = true;
                        sec.tempoRatio = static_cast<double>(child.getProperty(keys::kTempoRatio, 1.0));
                    }
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

            // Rescue pre-fix projects: a scene whose only content was its diagonal
            // phrase row got no scene node (the old sceneHasContent() save gate), so
            // Scene::initialised was never set on load. Derive it from the diagonal
            // (Song.h) so sceneSlotOccupied() reports correctly and the Scene+step
            // handler does not take the destructive create-on-empty path. New saves
            // write the node directly (widened save gate); this only helps old files.
            deriveSceneOccupancyFromPhrases(song);
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
            if (s->isVolatile) continue;  // RAM-only REC buffers are not persisted (DESIGN §28)
            juce::ValueTree entry(keys::kEntry);
            entry.setProperty("i", i, nullptr);
            entry.setProperty(keys::kPath, juce::String(s->ref.path), nullptr);
            entry.setProperty(keys::kHash, hashToHex(s->ref.hashXX32), nullptr);
            // Item 6: mark Stream-origin (disk-streamed, PCM-less) entries so the
            // reader reconstructs them via addStreamRef, never decoding to RAM.
            if (s->origin == SampleOrigin::Stream)
                entry.setProperty(keys::kSampleOrigin,
                                  static_cast<int>(SampleOrigin::Stream), nullptr);
            // 4.9: cache the fused analysis, keyed by the sample hash above. Only
            // non-default fields are written; a hash match on load skips re-detect.
            if (s->analysed)
            {
                entry.setProperty(keys::kAnalysed, 1, nullptr);
                if (s->detectedBpm > 0.0)
                    entry.setProperty(keys::kBpm, s->detectedBpm, nullptr);
                if (s->keyRoot >= 0)
                {
                    entry.setProperty(keys::kKeyRoot, s->keyRoot, nullptr);
                    entry.setProperty(keys::kKeyBright, s->keyBrightness, nullptr);
                }
                if (s->tuningCents != 0.0)
                    entry.setProperty(keys::kTuneCents, s->tuningCents, nullptr);
                if (s->detectedOneShot)
                    entry.setProperty(keys::kOneShot, 1, nullptr);
            }
            // 9.23 (v30): user overrides — written only when set, so a v29 reader
            // is unaffected and a v29 file loads with detected values only. These
            // are also written for Stream-origin entries (no PCM, but a user BPM /
            // one-shot still matters for tempo tracking and autoFit).
            if (s->userBpm > 0.0)
                entry.setProperty(keys::kUserBpm, s->userBpm, nullptr);
            if (s->userKeyRoot >= 0)
            {
                entry.setProperty(keys::kUserKeyRoot, s->userKeyRoot, nullptr);
                entry.setProperty(keys::kUserKeyBright, s->userKeyBrightness, nullptr);
            }
            if (s->hasUserTuning)
                entry.setProperty(keys::kUserTuneCents, s->userTuningCents, nullptr);
            if (s->userOneShot >= 0)
                entry.setProperty(keys::kUserOneShot, s->userOneShot, nullptr);
            poolNode.appendChild(entry, nullptr);
        }
        root.appendChild(poolNode, nullptr);
    }

    // 9.23 (v30): apply the per-entry user overrides (BPM / key / tuning /
    // one-shot) stored on `entry` to the pool slot at `index`. Absent property =
    // unset, so a v29 entry leaves every override cleared. Shared by the Stream and
    // File/missing branches so both carry user metadata.
    static void applyUserOverrides(SamplePool& pool, int index, const juce::ValueTree& entry)
    {
        if (index < 0) return;
        if (entry.hasProperty(keys::kUserBpm))
            pool.setUserBpm(index, static_cast<double>(entry.getProperty(keys::kUserBpm, 0.0)));
        if (entry.hasProperty(keys::kUserKeyRoot))
            pool.setUserKey(index,
                            static_cast<int>(entry.getProperty(keys::kUserKeyRoot, -1)),
                            static_cast<int>(entry.getProperty(keys::kUserKeyBright,
                                                               static_cast<int>(kAeolian))));
        if (entry.hasProperty(keys::kUserTuneCents))
            pool.setUserTuningCents(index,
                                    static_cast<double>(entry.getProperty(keys::kUserTuneCents, 0.0)),
                                    true);
        if (entry.hasProperty(keys::kUserOneShot))
            pool.setUserOneShot(index, static_cast<int>(entry.getProperty(keys::kUserOneShot, -1)));
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

            // Item 6: a Stream-origin entry is a disk-streamed reference — rebuild
            // it via addStreamRef (path + hash, NO PCM decode), preserving the pool
            // index. A missing file still yields an entry (index/ref survive).
            const auto org = static_cast<SampleOrigin>(
                static_cast<int>(entry.getProperty(keys::kSampleOrigin,
                                                    static_cast<int>(SampleOrigin::File))));
            if (org == SampleOrigin::Stream)
            {
                const int idx = proc.samplePool().addStreamRef(path);
                applyUserOverrides(proc.samplePool(), idx, entry);  // v30: user BPM/one-shot
                continue;
            }

            // 4.9: reconstruct the cached analysis (if this entry carried one).
            const bool hasCache = entry.hasProperty(keys::kAnalysed);
            SamplePool::CachedAnalysis ca;
            if (hasCache)
            {
                ca.hashXX32 = savedHash;
                ca.bpm = static_cast<double>(entry.getProperty(keys::kBpm, 0.0));
                ca.keyRoot = static_cast<int>(entry.getProperty(keys::kKeyRoot, -1));
                ca.keyBrightness =
                    static_cast<int>(entry.getProperty(keys::kKeyBright, static_cast<int>(kAeolian)));
                ca.tuningCents = static_cast<double>(entry.getProperty(keys::kTuneCents, 0.0));
                ca.oneShot = static_cast<int>(entry.getProperty(keys::kOneShot, 0)) != 0;
            }

            const int loaded = proc.samplePool().load(path, hasCache ? &ca : nullptr);
            if (loaded >= 0)
            {
                // Warn if hash differs — file changed since last save, but still usable.
                const auto* s = proc.samplePool().get(loaded);
                if (s && s->ref.hashXX32 != savedHash)
                    DBG("PluginState: hash mismatch for '" + path + "' (file may have changed)");
                applyUserOverrides(proc.samplePool(), loaded, entry);  // v30
            }
            else
            {
                // File not found — insert a placeholder so pool indices remain intact.
                SampleRef ref;
                ref.path = path.toStdString();
                ref.hashXX32 = savedHash;
                const int idx = proc.samplePool().addMissing(ref);
                // Keep the cached analysis alive even though the file is absent,
                // so it survives a round-trip through an offline session.
                if (hasCache && idx >= 0)
                    proc.samplePool().adoptCachedAnalysis(idx, ca);
                applyUserOverrides(proc.samplePool(), idx, entry);  // v30
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
                    stampSampleRefHash(pNode, id, val, proc);  // 9.18
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
            e.machineId = entryNode.getProperty(keys::kMId, juce::String(SampleMachine::kMachineId))
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
        // 9.31: an epoch of 0 means "no epoch" (a legacy project) and is written as an
        // ABSENT property, not a zero. That is what lets the pristine default blob --
        // captured in the constructor, before the first stamp -- stay epoch-free, so
        // newProject()'s fresh stamp survives reading that blob back over the state.
        // Writing a literal 0 would hand every new project the same seed space.
        if (proc.projectEpoch() != 0u)
            miscNode.setProperty(keys::kProjectEpoch,
                                 static_cast<int>(proc.projectEpoch()), nullptr);
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

            // 9.31: present -> adopt it; ABSENT -> keep whatever the processor has.
            // The distinction is load-bearing. newProject() stamps a fresh epoch and
            // then reads the pristine default blob back over the state; that blob was
            // captured before any epoch existed, so a "missing means 0" rule here
            // would wipe the new project's epoch and hand every new project the same
            // seeds. A pre-v33 project on disk has no epoch either -- but it also
            // never had one, and 0 is its stable identity.
            if (miscNode.hasProperty(keys::kProjectEpoch))
                proc.setProjectEpoch(static_cast<std::uint32_t>(
                    static_cast<int>(miscNode.getProperty(keys::kProjectEpoch, 0))));
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
    //   4. Add a new test case in PluginStateUpgradeTest
    //      (tests/PluginStateUpgradeTest.cpp — run headlessly by lockstep_tests).

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
                ptNode.setProperty(keys::kMachineId, SampleMachine::kMachineId, nullptr);

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

            DBG(juce::String(u8"PluginState: v9→v10: migrated swing (global=")
                + juce::String(legacyGlobal) + ") into Song[0]");
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

    // v16 → v17: Song::masterSends (MasterSnd nodes) + AMP sendA/sendB slots 8-9.
    // Missing MasterSnd nodes = empty sends; sendA/B P-Lock ids are string-keyed so
    // they appear in the existing P-Lock load path without special upgrade steps.
    juce::ValueTree upgrade_v16_to_v17(const juce::ValueTree& v16)
    {
        juce::ValueTree v17 = v16.createCopy();
        v17.setProperty(keys::kVersion, 17, nullptr);
        return v17;
    }

    // v17 → v18: Trivial stamp. TrackAmpState split into TrackChannelState +
    // TrackEnvState; disk ids (lockstep.amp.* / lockstep.fltr.*) are unchanged.
    // slotForIdWithMachine() now routes them to the correct struct fields.
    static juce::ValueTree upgrade_v17_to_v18(const juce::ValueTree& v17)
    {
        juce::ValueTree v18 = v17.createCopy();
        v18.setProperty(keys::kVersion, 18, nullptr);
        return v18;
    }

    static juce::ValueTree upgrade_v18_to_v19(const juce::ValueTree& v18)
    {
        // v19 adds densityMusicality / densitySelection to Kit nodes.
        // New fields default to Mixed/Scrub on read; no data migration needed.
        juce::ValueTree v19 = v18.createCopy();
        v19.setProperty(keys::kVersion, 19, nullptr);
        return v19;
    }

    static juce::ValueTree upgrade_v19_to_v20(const juce::ValueTree& v19)
    {
        // v20: kDiv in Kit nodes changes from old int divider (1-16, PPQ = 0.25*d)
        // to the combined subdivision index (0-26, see Subdivision.h).
        // Remap each Kit node's "div" property to the nearest musical subdivision.
        // New vel fields (vMd/vBl/vDp/vCt) default-read; no migration needed.
        juce::ValueTree v20 = v19.createCopy();
        v20.setProperty(keys::kVersion, 20, nullptr);

        // Walk the entire tree to remap Kit nodes wherever they appear.
        std::function<void(juce::ValueTree&)> remapKits = [&](juce::ValueTree& node)
        {
            if (node.getType() == juce::Identifier(keys::kKit))
            {
                const int oldDiv = static_cast<int>(node.getProperty(keys::kDiv, 1));
                const double oldPpq = 0.25 * static_cast<double>(oldDiv);
                const int newIdx = nearestSubdivIndex(oldPpq);
                if (newIdx == kSubdivDefault)
                    node.removeProperty(keys::kDiv, nullptr); // omit default
                else
                    node.setProperty(keys::kDiv, newIdx, nullptr);
            }
            for (int ci = 0; ci < node.getNumChildren(); ++ci)
            {
                auto child = node.getChild(ci);
                remapKits(child);
            }
        };
        remapKits(v20);
        return v20;
    }

    static juce::ValueTree upgrade_v20_to_v21(const juce::ValueTree& v20)
    {
        // v21: hierarchical time-sig fields added.
        // Scene kCtN/kCtD written only when hasTimeSig=true going forward; existing v20
        // scenes that wrote kCtN/kCtD unconditionally are accepted by the read path.
        // No data migration needed; trivial stamp bump.
        juce::ValueTree v21 = v20.createCopy();
        v21.setProperty(keys::kVersion, 21, nullptr);
        return v21;
    }

    static juce::ValueTree upgrade_v21_to_v22(const juce::ValueTree& v21)
    {
        // v22: hierarchical key signature added. No data migration needed —
        // absent key-sig fields read as C Ionian with no overrides. Trivial
        // stamp bump.
        juce::ValueTree v22 = v21.createCopy();
        v22.setProperty(keys::kVersion, 22, nullptr);
        return v22;
    }

    static juce::ValueTree upgrade_v22_to_v23(const juce::ValueTree& v22)
    {
        // v23: per-track Scale stage added. Absent scaleMode reads as Off. Trivial
        // stamp bump.
        juce::ValueTree v23 = v22.createCopy();
        v23.setProperty(keys::kVersion, 23, nullptr);
        return v23;
    }

    static juce::ValueTree upgrade_v23_to_v24(const juce::ValueTree& v23)
    {
        // v24: per-step trigless / lock-only flag added (5.6). Absent "lo" reads
        // as false (a plain off/note step). Trivial stamp bump.
        juce::ValueTree v24 = v23.createCopy();
        v24.setProperty(keys::kVersion, 24, nullptr);
        return v24;
    }

    static juce::ValueTree upgrade_v24_to_v25(const juce::ValueTree& v24)
    {
        // v25 (9.17): Project.launchQuant is now the raw LaunchQuant enum on disk
        // (Instant/Beat/Bar/Bars2/Bars4/Bars8/PhraseEnd). Pre-v25 it was stored
        // as a legacy bar count (1/2/4/8). Remap the stored value in place so old
        // grids survive as the equivalent enum. Per-track launchQuant and the
        // loop_sync re-interpretation are default-on-missing, so no further
        // migration is needed here.
        juce::ValueTree v25 = v24.createCopy();
        auto nh = v25.getChildWithName(keys::kNewHierarchy);
        if (nh.isValid() && nh.hasProperty(keys::kLaunchQuant))
        {
            const int legacyBars = static_cast<int>(nh.getProperty(keys::kLaunchQuant, 1));
            nh.setProperty(keys::kLaunchQuant,
                           static_cast<int>(legacyBarsToQuant(legacyBars)), nullptr);
        }
        v25.setProperty(keys::kVersion, 25, nullptr);
        return v25;
    }

    static juce::ValueTree upgrade_v25_to_v26(const juce::ValueTree& v25)
    {
        // v26 (4.9): SamplePool entries may carry cached analysis (an/bpm/keyR/
        // keyB/tune). A v25 tree simply has none; absent props read as
        // "not analysed" and the entry is re-analysed on load — exactly v25
        // behaviour. Trivial stamp bump.
        juce::ValueTree v26 = v25.createCopy();
        v26.setProperty(keys::kVersion, 26, nullptr);
        return v26;
    }

    static juce::ValueTree upgrade_v26_to_v27(const juce::ValueTree& v26)
    {
        // v27 (transport AND-gate): the hosted-Locked sequencer now runs only when
        // the host is playing AND the plugin is armed (pluginArmed). A v26 tree has
        // no arm flag; readNewHierarchyNode defaults a missing property to armed
        // (1), so old sessions and rendered projects load in their prior behaviour
        // (host transport starts playback). Trivial stamp bump.
        juce::ValueTree v27 = v26.createCopy();
        v27.setProperty(keys::kVersion, 27, nullptr);
        return v27;
    }

    // 9.18: resolve every sample reference (sample_id / slicer_sample_id) in the tree
    // from its durable content hash to the pool entry's CURRENT position, so a saved
    // reference never drifts when the pool reorders across reload (the flat-index rot).
    // Runs on EVERY load (idempotent): the pool node's child ORDER equals the live
    // pool index after readSamplePool (volatiles are re-seeded above, not serialised),
    // so a matching entry's position is exactly the index the reference must hold.
    // v28 (and earlier) trees carry only a raw flat index and no "sh"; the pool node's
    // per-entry "i" attribute (the runtime index at save) bridges them the first time,
    // and the resolved hash is stamped as "sh" so subsequent loads are hash-driven.
    static void normalizeSampleRefs(juce::ValueTree& root)
    {
        const auto pool = root.getChildWithName(keys::kSamplePool);
        if (!pool.isValid()) return;

        struct Entry { juce::String hash; int savedIndex; };
        std::vector<Entry> entries;
        entries.reserve(static_cast<std::size_t>(pool.getNumChildren()));
        for (auto e : pool)
            entries.push_back({ e.getProperty(keys::kHash).toString(),
                                static_cast<int>(e.getProperty("i", -1)) });

        auto posForHash = [&entries](const juce::String& h) -> int {
            if (h.isEmpty()) return -1;
            for (int p = 0; p < static_cast<int>(entries.size()); ++p)
                if (entries[static_cast<std::size_t>(p)].hash == h) return p;
            return -1;
        };
        auto posForSavedIndex = [&entries](int savedI) -> int {
            for (int p = 0; p < static_cast<int>(entries.size()); ++p)
                if (entries[static_cast<std::size_t>(p)].savedIndex == savedI) return p;
            return -1;
        };

        std::function<void(juce::ValueTree)> walk = [&](juce::ValueTree n) {
            if (n.getType() == juce::Identifier(keys::kParam))  // "P"
            {
                const juce::String id = n.getProperty(keys::kParamId).toString();
                if (isSampleRefId(id))
                {
                    juce::String sh = n.getProperty(keys::kSampleHash).toString();
                    int pos = -1;
                    if (sh.isNotEmpty())
                    {
                        pos = posForHash(sh);
                    }
                    else  // legacy (≤v28): synthesise the hash from the saved flat index
                    {
                        const int savedI = juce::roundToInt(
                            static_cast<double>(n.getProperty(keys::kPLockVal, 0.0)));
                        pos = posForSavedIndex(savedI);
                        if (pos >= 0) sh = entries[static_cast<std::size_t>(pos)].hash;
                    }
                    if (pos >= 0)
                    {
                        n.setProperty(keys::kPLockVal, pos, nullptr);
                        if (sh.isNotEmpty())
                            n.setProperty(keys::kSampleHash, sh, nullptr);
                    }
                }
            }
            for (int c = 0; c < n.getNumChildren(); ++c)
                walk(n.getChild(c));
        };
        walk(root);
    }

    static juce::ValueTree upgrade_v28_to_v29(const juce::ValueTree& v28)
    {
        // v29 (sample-pool identity): sample references (sample_id / slicer_sample_id)
        // now carry a durable content hash ("sh") and re-resolve to the pool entry's
        // current position on load, instead of storing a raw flat index that drifts as
        // the pool reorders. The actual resolution runs unconditionally in
        // normalizeSampleRefs (below, at the end of applyUpgrades) — which also bridges
        // this v28 tree's raw indices via the pool node's "i" attribute — so nothing
        // structural is rewritten here. Stamp bump.
        juce::ValueTree v29 = v28.createCopy();
        v29.setProperty(keys::kVersion, 29, nullptr);
        return v29;
    }

    static juce::ValueTree upgrade_v29_to_v30(const juce::ValueTree& v29)
    {
        // v30 (sample-playback coherence, 9.23): SamplePool Entry nodes gain the
        // detected one-shot flag (osh) + per-entry user overrides (ubpm/ukeyR/
        // ukeyB/utune/uosh), all written only when set and read directly by
        // readSamplePool. A v29 tree simply has none of them → detected values
        // only. Nothing structural to rewrite; stamp bump.
        juce::ValueTree v30 = v29.createCopy();
        v30.setProperty(keys::kVersion, 30, nullptr);
        return v30;
    }

    static juce::ValueTree upgrade_v30_to_v31(const juce::ValueTree& v30)
    {
        // v31 (9.24 S15): insert slots gain an optional convolution IR ref
        // (kIrHash), written only when set and read directly by readIrRef. A v30
        // tree has none → every slot loads with an empty irRef. Nothing structural
        // to rewrite; stamp bump.
        juce::ValueTree v31 = v30.createCopy();
        v31.setProperty(keys::kVersion, 31, nullptr);
        return v31;
    }

    // v32 (9.27 A4): the HQ delay's time slot stopped being a stepped index into a
    // division table and became the beat fraction itself, so a bare turn can snap to
    // the divisions while `Func` + turn sweeps between them. Old documents stored the
    // index; rewrite it to the beats it named. Values at the old default (index 4 =
    // 1/4) were never written at all — the save path skips defaults — and the new
    // default is 1.0 beats, the same 1/4. So this only touches docs that had moved
    // the knob.
    static juce::ValueTree upgrade_v31_to_v32(const juce::ValueTree& v31)
    {
        static constexpr float kLegacyDivBeats[] = {
            0.25f, 1.0f/3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f
        };
        juce::ValueTree v32 = v31.createCopy();

        const std::function<void(juce::ValueTree&)> rewrite = [&](juce::ValueTree& node) {
            if (node.getType() == juce::Identifier("P")
                && node.getProperty("id", "").toString() == "lockstep.delayhq.time")
            {
                const int idx = juce::jlimit(0, 6,
                    static_cast<int>(std::lround(getFloat(node, "v", 4.0f))));
                node.setProperty("v", kLegacyDivBeats[static_cast<std::size_t>(idx)], nullptr);
            }
            for (auto child : node)
                rewrite(child);
        };
        rewrite(v32);

        v32.setProperty(keys::kVersion, 32, nullptr);
        return v32;
    }

    static juce::ValueTree upgrade_v27_to_v28(const juce::ValueTree& v27)
    {
        // v28 (stream-via-pool): a StreamMachine's source moves from a per-Kit
        // streamPath property to a Stream-origin SamplePool entry + the track's
        // sample_id base param. The conversion needs pool-index allocation and a
        // machine schema, so it runs at load time in finishStateLoad (which still
        // reads the legacy kStreamPath, migrates it, and clears the field). Nothing
        // structural to rewrite here — a stamp bump keeps old trees loading with
        // their streamPath intact for that load-time migration.
        juce::ValueTree v28 = v27.createCopy();
        v28.setProperty(keys::kVersion, 28, nullptr);
        return v28;
    }

    // v33 (9.31): projects gained an EPOCH -- a number stamped once at creation that
    // salts the generator seeds, so the same SEED is a different melody in a
    // different project. A stamp-only upgrade: an old project has no epoch and does
    // not acquire one here, so it reads as epoch 0. That is deliberate. 0 is a
    // perfectly good identity, and it is the one an old project has always
    // implicitly had -- inventing an epoch for it would silently re-roll every
    // melody the user already generated and saved.
    static juce::ValueTree upgrade_v32_to_v33(const juce::ValueTree& v32)
    {
        juce::ValueTree v33 = v32.createCopy();
        v33.setProperty(keys::kVersion, 33, nullptr);
        return v33;
    }

    // v34 (9.31): temporal parameters became tempo-relative (DESIGN §32.1z). Delay
    // time is now BEATS, and the modulation rates are PERIODS IN BEATS, where they
    // used to be seconds and Hz. Convert every stored value through the project's own
    // SAVED BPM, so a project loads sounding the way it was written -- the numbers
    // change, the sound does not.
    //
    // The walker visits every "P" node, which is the same node type used for base
    // params, P-Locks AND fill-P-Locks: a delay time automated per step migrates with
    // the base value, because they are the same id in the same shape of node.
    static juce::ValueTree upgrade_v33_to_v34(const juce::ValueTree& v33)
    {
        juce::ValueTree v34 = v33.createCopy();

        // The tempo the project was written at. Absent (or nonsense) reads as 120 --
        // the same default the Clock starts at, so the conversion is at worst the one
        // a fresh project would have made.
        double bpm = 120.0;
        {
            const auto misc = v34.getChildWithName(keys::kMisc);
            if (misc.isValid())
            {
                const double saved = static_cast<double>(misc.getProperty(keys::kLocalBpm, 120.0));
                if (saved > 1.0 && saved < 1000.0) bpm = saved;
            }
        }
        const double beatSecs = 60.0 / bpm;

        // Hz -> period in beats. A rate of 0 (or negative, or absurd) would divide by
        // zero, so it lands on the slow end rather than on infinity.
        const auto hzToBeats = [beatSecs](float hz, float lo, float hi) {
            if (hz <= 1.0e-4f) return hi;
            const auto beats = static_cast<float>(1.0 / (static_cast<double>(hz) * beatSecs));
            return juce::jlimit(lo, hi, beats);
        };

        const std::function<void(juce::ValueTree&)> rewrite = [&](juce::ValueTree& node) {
            if (node.getType() == juce::Identifier("P"))
            {
                const juce::String id = node.getProperty("id", "").toString();

                if (id == "lockstep.delay.time")
                {
                    // Seconds -> beats. Clamped to the new lattice's range: below 60 BPM
                    // a 2-beat delay exceeds the 2 s buffer, and a slapback under 1/64
                    // of a beat has nowhere to go. Both were accepted at design time.
                    const float secs = getFloat(node, "v", 0.25f);
                    const auto beats = static_cast<float>(static_cast<double>(secs) / beatSecs);
                    node.setProperty("v", juce::jlimit(0.0625f, 2.0f, beats), nullptr);
                }
                else if (id == "lockstep.chorus.rate")
                {
                    // The chorus rate was a NORMALISED knob, scaled to Hz inside
                    // process() as jlimit(0.1, 5, v * 5) -- so the migration has to
                    // repeat that exact mapping, not guess at it, or every chorus in
                    // every old project moves.
                    const float norm = getFloat(node, "v", 0.1f);
                    const float hz = juce::jlimit(0.1f, 5.0f, norm * 5.0f);
                    node.setProperty("v", hzToBeats(hz, 0.25f, 64.0f), nullptr);
                }
                else if (id == "lockstep.flanger.rate" || id == "lockstep.phaser.rate")
                {
                    node.setProperty("v", hzToBeats(getFloat(node, "v", 0.5f), 0.25f, 64.0f),
                                     nullptr);
                }
                else if (id == "va_lfo_rate")
                {
                    node.setProperty("v", hzToBeats(getFloat(node, "v", 3.0f), 0.03125f, 64.0f),
                                     nullptr);
                }
            }
            for (auto child : node)
                rewrite(child);
        };
        rewrite(v34);

        v34.setProperty(keys::kVersion, 34, nullptr);
        return v34;
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
        if (version < 17) tree = upgrade_v16_to_v17(tree);
        if (version < 18) tree = upgrade_v17_to_v18(tree);
        if (version < 19) tree = upgrade_v18_to_v19(tree);
        if (version < 20) tree = upgrade_v19_to_v20(tree);
        if (version < 21) tree = upgrade_v20_to_v21(tree);
        if (version < 22) tree = upgrade_v21_to_v22(tree);
        if (version < 23) tree = upgrade_v22_to_v23(tree);
        if (version < 24) tree = upgrade_v23_to_v24(tree);
        if (version < 25) tree = upgrade_v24_to_v25(tree);
        if (version < 26) tree = upgrade_v25_to_v26(tree);
        if (version < 27) tree = upgrade_v26_to_v27(tree);
        if (version < 28) tree = upgrade_v27_to_v28(tree);
        if (version < 29) tree = upgrade_v28_to_v29(tree);
        if (version < 30) tree = upgrade_v29_to_v30(tree);
        if (version < 31) tree = upgrade_v30_to_v31(tree);
        if (version < 32) tree = upgrade_v31_to_v32(tree);
        if (version < 33) tree = upgrade_v32_to_v33(tree);
        if (version < 34) tree = upgrade_v33_to_v34(tree);

        // 9.18: unconditional — resolve sample references to current pool positions
        // (hash-driven for v29 trees, "i"-bridged for the v28 tree just upgraded).
        normalizeSampleRefs(tree);

        return tree;
    }

    // -------------------------------------------------------------------------
    // Public API

    juce::ValueTree buildStateTree(LockstepProcessor& proc)
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

        return root;
    }

    void applyStateTree(juce::ValueTree root, LockstepProcessor& proc)
    {
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

    void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc)
    {
        const auto root = buildStateTree(proc);
        if (auto xml = root.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, dest);
    }

    void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (!xml) return;
        auto root = juce::ValueTree::fromXml(*xml);
        if (!root.isValid()) return;
        applyStateTree(root, proc);
    }

    void writeToFile(const juce::File& file, LockstepProcessor& proc)
    {
        // Caller is responsible for flushing working → active (e.g. via saveProjectFile).
        const auto root = buildStateTree(proc);
        if (auto xml = root.createXml())
            xml->writeTo(file);
    }

    bool readFromFile(const juce::File& file, LockstepProcessor& proc)
    {
        // Parse the entire XML before touching proc so a bad file never leaves
        // the processor in a partially-applied state.
        const auto xml = juce::XmlDocument::parse(file);
        if (!xml) return false;
        auto root = juce::ValueTree::fromXml(*xml);
        if (!root.isValid()) return false;
        applyStateTree(root, proc);
        return true;
    }

} // namespace lockstep::PluginState

