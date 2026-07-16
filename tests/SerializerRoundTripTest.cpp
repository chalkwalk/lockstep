// SerializerRoundTripTest — characterisation of the v14/v15 serialized format.
//
// Design note: PluginState::writeTo/readFrom are coupled to LockstepProcessor
// and cannot run headlessly without a full plugin host context. This file pins
// the pure ValueTree sub-structure at the phrase/step/condition level:
//   1. TrigCondition ↔ ValueTree round-trip (property name oracle for 8.9a).
//   2. Phrase/Step ↔ ValueTree round-trip for every non-trivial field
//      (trigs, micro-offsets, P-Locks, trig overrides, conditions).
//
// The applyUpgrades() stability test lives in the JUCE UnitTest category
// "PluginState" (PluginState.cpp) rather than here, because including
// PluginState.h pulls in the whole PluginState translation unit which has
// unresolved references to LockstepProcessor in the headless test binary.
//
// When Stage 8.9a creates src/state/StateKeys.h, both sides will reference the
// same constexpr names and a typo in either direction becomes a compile error
// rather than a silent data-loss bug at runtime.
//
// Property name key (from PluginState.cpp v14):
//   Cond:        "p" (prob%), "n" (iterNumerator), "d" (iterDen), "pd" (prevDep)
//   Step:        "i" (idx), "t" (trig), "mo" (microOffset), child "C" (condition),
//                child "TO" (trig override), child "PL" / child "P" (p-locks)
//   TrigOverride:"nc" (noteCount), "n0".."n3" (notes), "hv"/"v" (vel),
//                "hg"/"gv" (gate), "hsi"/"si" (soundId), "hrt"/"rt" (retrig)
//   P-Lock v14:  "s" (slot), "v" (value)
//   P-Lock v15:  "id" (param id string), "v" (value)
//   Phrase:      "i" (idx), "len", "nsel", child "BaseCond", child "TrigDefaults",
//                child "Steps"
//   TrigDefaults:"note", "vel", "gateV"

#include "TestHarness.h"
#include "../src/core/Phrase.h"
#include "../src/core/Song.h"
#include "../src/core/SoundPool.h"      // SoundEntry (5.3 identity round-trip)
#include "../src/core/Scale.h"          // kAeolian / kDorian (key brightness)
#include "../src/core/TrigCondition.h"
#include "../src/state/StateKeys.h"     // 4.9 sample-analysis cache keys
#include "../src/machine/SamplePool.h"  // CachedAnalysis (read-side reconstruction)
#include "../src/machine/StubMachine.h"  // kMachineId (5.3 Item C song occupancy)
#include <juce_data_structures/juce_data_structures.h>

namespace lockstep
{
    // ── Helpers: mirror of PluginState.cpp condToTree / condFromTree ──────
    // These ARE the oracle. When 8.9a introduces StateKeys.h constants, both
    // this mirror and production code will reference the same constants.

    static juce::ValueTree condToTree(const juce::Identifier& type,
                                      const TrigCondition& c)
    {
        juce::ValueTree v(type);
        v.setProperty("p", static_cast<int>(c.probabilityPercent), nullptr);
        v.setProperty("n", static_cast<int>(c.iterNumerator), nullptr);
        v.setProperty("d", static_cast<int>(c.iterDenominator), nullptr);
        v.setProperty("pd", static_cast<int>(c.prevDependency), nullptr);
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
            static_cast<int>(v.getProperty("pd", 0)));
        c.oneShot = (static_cast<int>(v.getProperty("os", 0)) != 0);  // 5.6
        return c;
    }

    // ── TrigCondition round-trip ──────────────────────────────────────────

    static void testCondRoundTrip()
    {
        TrigCondition orig;
        orig.probabilityPercent = 73;
        orig.iterNumerator = 2;
        orig.iterDenominator = 3;
        orig.prevDependency = 1;

        const auto tree = condToTree("TestCond", orig);
        CHECK(static_cast<int>(tree.getProperty("p")) == 73, "cond.p written");
        CHECK(static_cast<int>(tree.getProperty("n")) == 2, "cond.n written");
        CHECK(static_cast<int>(tree.getProperty("d")) == 3, "cond.d written");
        CHECK(static_cast<int>(tree.getProperty("pd")) == 1, "cond.pd written");

        const auto back = condFromTree(tree);
        CHECK(back.probabilityPercent == 73, "cond.prob round-trips");
        CHECK(back.iterNumerator == 2, "cond.iterNum round-trips");
        CHECK(back.iterDenominator == 3, "cond.iterDen round-trips");
        CHECK(back.prevDependency == 1, "cond.prevDep round-trips");

        // 5.6 one-shot round-trip + isTrivial participation.
        TrigCondition os;
        os.oneShot = true;
        CHECK(!os.isTrivial(), "a one-shot condition is non-trivial");
        const auto osBack = condFromTree(condToTree("OneShot", os));
        CHECK(osBack.oneShot, "cond.oneShot round-trips");

        // Trivial condition round-trip.
        TrigCondition trivial;
        CHECK(trivial.isTrivial(), "default TrigCondition is trivial");
        const auto trivTree = condToTree("Trivial", trivial);
        const auto trivBack = condFromTree(trivTree);
        CHECK(trivBack.isTrivial(), "trivial condition round-trips as trivial");
    }

    // ── Step node: build + parse at ValueTree level ───────────────────────
    // Mirror of writePhraseNode / readPhraseFromNode in PluginState.cpp.

    static juce::ValueTree buildStepNode(int idx, const Step& step)
    {
        juce::ValueTree node("S");
        node.setProperty("i", idx, nullptr);
        node.setProperty("t", step.trig ? 1 : 0, nullptr);
        if (step.lockOnly)
            node.setProperty("lo", 1, nullptr);  // 5.6 trigless / lock-only
        if (std::abs(step.microOffset) > 1e-7f)
            node.setProperty("mo", static_cast<double>(step.microOffset), nullptr);
        if (!step.condition.isTrivial())
            node.appendChild(condToTree("C", step.condition), nullptr);
        if (step.trigOverride.noteCount > 0 || step.trigOverride.hasVelocity || step.trigOverride.hasGate || step.trigOverride.hasSoundId || step.trigOverride.hasRetrig)
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
                toNode.setProperty("hv", 1, nullptr);
                toNode.setProperty("v", step.trigOverride.velocity, nullptr);
            }
            if (step.trigOverride.hasGate)
            {
                toNode.setProperty("hg", 1, nullptr);
                toNode.setProperty("gv", static_cast<int>(static_cast<std::uint8_t>(step.trigOverride.gateValue)), nullptr);
            }
            if (step.trigOverride.hasSoundId)
            {
                toNode.setProperty("hsi", 1, nullptr);
                toNode.setProperty("si", step.trigOverride.soundId, nullptr);
            }
            if (step.trigOverride.hasRetrig)
            {
                toNode.setProperty("hrt", 1, nullptr);
                toNode.setProperty("rt", step.trigOverride.retrigRate, nullptr);
            }
            node.appendChild(toNode, nullptr);
        }
        if (!step.overrides.empty())
        {
            juce::ValueTree plNode("PL");
            step.overrides.forEach([&](int slot, float value) {
                juce::ValueTree pNode("P");
                pNode.setProperty("s", slot, nullptr);
                pNode.setProperty("v", static_cast<double>(value), nullptr);
                plNode.appendChild(pNode, nullptr);
            });
            node.appendChild(plNode, nullptr);
        }
        return node;
    }

    static Step parseStepNode(const juce::ValueTree& node)
    {
        Step step;
        step.trig = (static_cast<int>(node.getProperty("t", 0)) != 0);
        step.lockOnly = (static_cast<int>(node.getProperty("lo", 0)) != 0);  // 5.6
        step.microOffset = static_cast<float>(static_cast<double>(
            node.getProperty("mo", 0.0)));
        const auto cNode = node.getChildWithName("C");
        if (cNode.isValid()) step.condition = condFromTree(cNode);
        const auto toNode = node.getChildWithName("TO");
        if (toNode.isValid())
        {
            step.trigOverride.noteCount = static_cast<int>(toNode.getProperty("nc", 0));
            for (int ni = 0; ni < step.trigOverride.noteCount; ++ni)
                step.trigOverride.notes[static_cast<std::size_t>(ni)] =
                    static_cast<int>(toNode.getProperty("n" + juce::String(ni), 60));
            if (static_cast<int>(toNode.getProperty("hv", 0)) != 0)
            {
                step.trigOverride.hasVelocity = true;
                step.trigOverride.velocity = static_cast<int>(toNode.getProperty("v", 100));
            }
            if (static_cast<int>(toNode.getProperty("hg", 0)) != 0)
            {
                step.trigOverride.hasGate = true;
                step.trigOverride.gateValue = static_cast<MusicalGate>(
                    static_cast<std::uint8_t>(static_cast<int>(toNode.getProperty("gv", 0))));
            }
            if (static_cast<int>(toNode.getProperty("hsi", 0)) != 0)
            {
                step.trigOverride.hasSoundId = true;
                step.trigOverride.soundId = static_cast<int>(toNode.getProperty("si", -1));
            }
            if (static_cast<int>(toNode.getProperty("hrt", 0)) != 0)
            {
                step.trigOverride.hasRetrig = true;
                step.trigOverride.retrigRate = static_cast<double>(toNode.getProperty("rt", 0.25));
            }
        }
        const auto plNode = node.getChildWithName("PL");
        if (plNode.isValid())
            for (auto pNode : plNode)
            {
                const int sl = static_cast<int>(pNode.getProperty("s", -1));
                if (sl >= 0) step.overrides.set(sl, static_cast<float>(
                                                        static_cast<double>(pNode.getProperty("v", 0.0))));
            }
        return step;
    }

    // ── Step round-trip tests ─────────────────────────────────────────────

    static void testStepRoundTrip()
    {
        // Basic trig + microOffset.
        {
            Step s;
            s.trig = true;
            s.microOffset = 0.125f;
            const auto tree = buildStepNode(3, s);
            CHECK(static_cast<int>(tree.getProperty("i")) == 3, "step idx written");
            CHECK(static_cast<int>(tree.getProperty("t")) == 1, "step trig written");
            const auto back = parseStepNode(tree);
            CHECK(back.trig, "step trig round-trips");
            CHECK(feq(back.microOffset, 0.125f), "step microOffset round-trips");
        }

        // 5.6 lock-only (trigless) round-trip.
        {
            Step s;
            s.trig = false;
            s.lockOnly = true;
            s.overrides.set(2, 0.4f);  // a P-Lock to ride onto the voice
            const auto tree = buildStepNode(6, s);
            CHECK(static_cast<int>(tree.getProperty("lo")) == 1, "step lock-only written");
            const auto back = parseStepNode(tree);
            CHECK(back.lockOnly, "step lock-only round-trips");
            CHECK(!back.trig, "lock-only step keeps trig off");
            CHECK(feq(back.overrides.get(2, -1.0f), 0.4f), "lock-only P-Lock round-trips");
        }

        // Condition on step.
        {
            Step s;
            s.trig = true;
            s.condition.probabilityPercent = 50;
            s.condition.iterNumerator = 1;
            s.condition.iterDenominator = 2;
            const auto tree = buildStepNode(0, s);
            const auto cNode = tree.getChildWithName("C");
            CHECK(cNode.isValid(), "step condition node written");
            const auto back = parseStepNode(tree);
            CHECK(back.condition.probabilityPercent == 50, "step cond.prob round-trips");
            CHECK(back.condition.iterDenominator == 2, "step cond.iterDen round-trips");
        }

        // P-Lock.
        {
            Step s;
            s.trig = true;
            s.overrides.set(2, 0.75f);
            s.overrides.set(7, 0.33f);
            const auto tree = buildStepNode(5, s);
            CHECK(tree.getChildWithName("PL").isValid(), "PL node written");
            const auto back = parseStepNode(tree);
            CHECK(feq(back.overrides.get(2, -1.0f), 0.75f), "plock slot 2 round-trips");
            CHECK(feq(back.overrides.get(7, -1.0f), 0.33f), "plock slot 7 round-trips");
            CHECK(feq(back.overrides.get(0, -1.0f), -1.0f), "missing plock returns sentinel");
        }

        // Trig override: velocity + note chord.
        {
            Step s;
            s.trig = true;
            s.trigOverride.hasVelocity = true;
            s.trigOverride.velocity = 88;
            s.trigOverride.noteCount = 2;
            s.trigOverride.notes[0] = 60;
            s.trigOverride.notes[1] = 64;
            const auto tree = buildStepNode(1, s);
            const auto toNode = tree.getChildWithName("TO");
            CHECK(toNode.isValid(), "TO node written");
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasVelocity, "TO.hasVelocity round-trips");
            CHECK(back.trigOverride.velocity == 88, "TO.velocity round-trips");
            CHECK(back.trigOverride.noteCount == 2, "TO.noteCount round-trips");
            CHECK(back.trigOverride.notes[0] == 60, "TO.note[0] round-trips");
            CHECK(back.trigOverride.notes[1] == 64, "TO.note[1] round-trips");
        }

        // Trig override: soundId.
        {
            Step s;
            s.trig = true;
            s.trigOverride.hasSoundId = true;
            s.trigOverride.soundId = 7;
            const auto tree = buildStepNode(0, s);
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasSoundId, "TO.hasSoundId round-trips");
            CHECK(back.trigOverride.soundId == 7, "TO.soundId round-trips");
        }

        // Trig override: retrig.
        {
            Step s;
            s.trig = true;
            s.trigOverride.hasRetrig = true;
            s.trigOverride.retrigRate = 0.5;
            const auto tree = buildStepNode(0, s);
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasRetrig, "TO.hasRetrig round-trips");
            CHECK(std::abs(back.trigOverride.retrigRate - 0.5) < 1e-6,
                  "TO.retrigRate round-trips");
        }
    }

    // ── Phrase property names oracle ───────────────────────────────────────
    // Builds a Phrase node manually and reads back, asserting property names.

    static void testPhrasePropertyNames()
    {
        // Build a Phrase node in the known format.
        juce::ValueTree phraseNode("Phrase");
        phraseNode.setProperty("i", 5, nullptr);  // phrase index
        phraseNode.setProperty("len", 12, nullptr);
        phraseNode.setProperty("nsel", 1, nullptr);   // NoteSelection::BotBias

        TrigCondition bc;
        bc.probabilityPercent = 80;
        phraseNode.appendChild(condToTree("BaseCond", bc), nullptr);

        juce::ValueTree tdNode("TrigDefaults");
        tdNode.setProperty("note", 48, nullptr);
        tdNode.setProperty("vel", 90, nullptr);
        tdNode.setProperty("gateV", 2, nullptr);
        phraseNode.appendChild(tdNode, nullptr);

        juce::ValueTree stepsNode("Steps");
        Step s;
        s.trig = true;
        s.microOffset = 0.25f;
        s.condition.iterDenominator = 4;
        s.overrides.set(3, 0.9f);
        stepsNode.appendChild(buildStepNode(0, s), nullptr);
        phraseNode.appendChild(stepsNode, nullptr);

        // Verify property names.
        CHECK(static_cast<int>(phraseNode.getProperty("i")) == 5, "phrase.i property name");
        CHECK(static_cast<int>(phraseNode.getProperty("len")) == 12, "phrase.len property name");
        CHECK(static_cast<int>(phraseNode.getProperty("nsel")) == 1, "phrase.nsel property name");
        CHECK(phraseNode.getChildWithName("BaseCond").isValid(), "phrase BaseCond child name");
        CHECK(phraseNode.getChildWithName("TrigDefaults").isValid(), "phrase TrigDefaults child name");
        CHECK(phraseNode.getChildWithName("Steps").isValid(), "phrase Steps child name");

        // BaseCond survives.
        const auto bcNode = phraseNode.getChildWithName("BaseCond");
        CHECK(static_cast<int>(bcNode.getProperty("p")) == 80, "phrase BaseCond.p preserved");

        // TrigDefaults survives.
        const auto tdBack = phraseNode.getChildWithName("TrigDefaults");
        CHECK(static_cast<int>(tdBack.getProperty("note")) == 48, "TrigDefaults.note preserved");
        CHECK(static_cast<int>(tdBack.getProperty("vel")) == 90, "TrigDefaults.vel preserved");
        CHECK(static_cast<int>(tdBack.getProperty("gateV")) == 2, "TrigDefaults.gateV preserved");

        // Step survives.
        const auto stepsBack = phraseNode.getChildWithName("Steps");
        const auto stepBack = stepsBack.getChild(0);
        const auto stepParsed = parseStepNode(stepBack);
        CHECK(stepParsed.trig, "phrase step trig preserved");
        CHECK(feq(stepParsed.microOffset, 0.25f), "phrase step mo preserved");
        CHECK(stepParsed.condition.iterDenominator == 4, "phrase step cond preserved");
        CHECK(feq(stepParsed.overrides.get(3, -1.f), 0.9f), "phrase step plock preserved");
    }

    // ── Fill-field round-trip (8.9b gap fix) ────────────────────────────────

    static juce::ValueTree buildStepNodeFull(int idx, const Step& step)
    {
        // Mirror of the updated writePhraseNode in PluginState.cpp, including
        // the fill-specific fields added in 8.9b.
        auto node = buildStepNode(idx, step);  // primary fields
        if (step.fillTrigState != FillTrigState::Inherit)
            node.setProperty("fts", static_cast<int>(step.fillTrigState), nullptr);
        if (step.fillTrigOverride.noteCount > 0 || step.fillTrigOverride.hasVelocity || step.fillTrigOverride.hasGate || step.fillTrigOverride.hasSoundId || step.fillTrigOverride.hasRetrig)
        {
            juce::ValueTree fto("FTO");
            if (step.fillTrigOverride.noteCount > 0)
            {
                fto.setProperty("nc", step.fillTrigOverride.noteCount, nullptr);
                for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                    fto.setProperty("n" + juce::String(ni),
                                    step.fillTrigOverride.notes[static_cast<std::size_t>(ni)],
                                    nullptr);
            }
            if (step.fillTrigOverride.hasVelocity)
            {
                fto.setProperty("hv", 1, nullptr);
                fto.setProperty("v", step.fillTrigOverride.velocity, nullptr);
            }
            if (step.fillTrigOverride.hasSoundId)
            {
                fto.setProperty("hsi", 1, nullptr);
                fto.setProperty("si", step.fillTrigOverride.soundId, nullptr);
            }
            node.appendChild(fto, nullptr);
        }
        if (!step.fillOverrides.empty())
        {
            juce::ValueTree fpl("FPL");
            step.fillOverrides.forEach([&](int sl, float val) {
                juce::ValueTree p("P");
                p.setProperty("s", sl, nullptr);
                p.setProperty("v", static_cast<double>(val), nullptr);
                fpl.appendChild(p, nullptr);
            });
            node.appendChild(fpl, nullptr);
        }
        return node;
    }

    static Step parseStepNodeFull(const juce::ValueTree& node)
    {
        auto step = parseStepNode(node);
        if (node.hasProperty("fts"))
            step.fillTrigState = static_cast<FillTrigState>(
                static_cast<int>(node.getProperty("fts", 0)));
        const auto fto = node.getChildWithName("FTO");
        if (fto.isValid())
        {
            step.fillTrigOverride.noteCount = static_cast<int>(fto.getProperty("nc", 0));
            for (int ni = 0; ni < step.fillTrigOverride.noteCount; ++ni)
                step.fillTrigOverride.notes[static_cast<std::size_t>(ni)] =
                    static_cast<int>(fto.getProperty("n" + juce::String(ni), 60));
            if (static_cast<int>(fto.getProperty("hv", 0)) != 0)
            {
                step.fillTrigOverride.hasVelocity = true;
                step.fillTrigOverride.velocity = static_cast<int>(fto.getProperty("v", 100));
            }
            if (static_cast<int>(fto.getProperty("hsi", 0)) != 0)
            {
                step.fillTrigOverride.hasSoundId = true;
                step.fillTrigOverride.soundId = static_cast<int>(fto.getProperty("si", -1));
            }
        }
        const auto fpl = node.getChildWithName("FPL");
        if (fpl.isValid())
            for (auto p : fpl)
            {
                const int sl = static_cast<int>(p.getProperty("s", -1));
                if (sl >= 0) step.fillOverrides.set(sl, static_cast<float>(
                                                            static_cast<double>(p.getProperty("v", 0.0))));
            }
        return step;
    }

    static void testFillFieldRoundTrip()
    {
        // fillTrigState ON
        {
            Step s;
            s.trig = true;
            s.fillTrigState = FillTrigState::On;
            const auto tree = buildStepNodeFull(0, s);
            CHECK(tree.hasProperty("fts"), "fillTrigState property written");
            CHECK(static_cast<int>(tree.getProperty("fts")) == static_cast<int>(FillTrigState::On),
                  "fillTrigState value correct");
            const auto back = parseStepNodeFull(tree);
            CHECK(back.fillTrigState == FillTrigState::On, "fillTrigState round-trips");
        }
        // fillTrigState OFF
        {
            Step s;
            s.trig = true;
            s.fillTrigState = FillTrigState::Off;
            const auto back = parseStepNodeFull(buildStepNodeFull(0, s));
            CHECK(back.fillTrigState == FillTrigState::Off, "fillTrigState Off round-trips");
        }
        // fillTrigOverride soundId
        {
            Step s;
            s.fillTrigOverride.hasSoundId = true;
            s.fillTrigOverride.soundId = 5;
            const auto tree = buildStepNodeFull(0, s);
            CHECK(tree.getChildWithName("FTO").isValid(), "FTO node written");
            const auto back = parseStepNodeFull(tree);
            CHECK(back.fillTrigOverride.hasSoundId, "FTO.hasSoundId round-trips");
            CHECK(back.fillTrigOverride.soundId == 5, "FTO.soundId round-trips");
        }
        // fillOverrides (fill P-Locks)
        {
            Step s;
            s.fillOverrides.set(4, 0.6f);
            s.fillOverrides.set(9, 0.25f);
            const auto tree = buildStepNodeFull(0, s);
            CHECK(tree.getChildWithName("FPL").isValid(), "FPL node written");
            const auto back = parseStepNodeFull(tree);
            CHECK(feq(back.fillOverrides.get(4, -1.f), 0.6f), "fillOverride slot 4 round-trips");
            CHECK(feq(back.fillOverrides.get(9, -1.f), 0.25f), "fillOverride slot 9 round-trips");
        }
        // Inherit state: no fts property written
        {
            Step s;
            s.trig = true;
            const auto tree = buildStepNodeFull(0, s);
            CHECK(!tree.hasProperty("fts"), "Inherit state omits fts property");
            const auto back = parseStepNodeFull(tree);
            CHECK(back.fillTrigState == FillTrigState::Inherit, "default fillTrigState is Inherit");
        }
    }

    // ── Mutation self-test: every field change is detectable ────────────────
    // Verifies that our round-trip comparator is exhaustive: mutate one field
    // in the serialized tree and confirm the deserialized result differs.

    static void testMutationDetectability()
    {
        // Build a maximally-populated step.
        Step orig;
        orig.trig = true;
        orig.microOffset = 0.125f;
        orig.condition.probabilityPercent = 75;
        orig.trigOverride.hasVelocity = true;
        orig.trigOverride.velocity = 80;
        orig.trigOverride.hasSoundId = true;
        orig.trigOverride.soundId = 3;
        orig.overrides.set(2, 0.5f);
        orig.fillTrigState = FillTrigState::On;
        orig.fillOverrides.set(5, 0.3f);

        const auto origTree = buildStepNodeFull(0, orig);

        // trig field
        {
            auto t = origTree.createCopy();
            t.setProperty("t", 0, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(!back.trig, "mutation: trig=false detectable");
        }
        // microOffset field
        {
            auto t = origTree.createCopy();
            t.setProperty("mo", 0.5, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(!feq(back.microOffset, orig.microOffset), "mutation: microOffset detectable");
        }
        // condition.probabilityPercent
        {
            auto t = origTree.createCopy();
            auto cNode = t.getChildWithName("C");
            cNode.setProperty("p", 50, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(back.condition.probabilityPercent != 75, "mutation: cond.prob detectable");
        }
        // velocity override
        {
            auto t = origTree.createCopy();
            auto toNode = t.getChildWithName("TO");
            toNode.setProperty("v", 50, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(back.trigOverride.velocity != 80, "mutation: TO.velocity detectable");
        }
        // soundId override
        {
            auto t = origTree.createCopy();
            auto toNode = t.getChildWithName("TO");
            toNode.setProperty("si", 9, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(back.trigOverride.soundId != 3, "mutation: TO.soundId detectable");
        }
        // P-Lock value
        {
            auto t = origTree.createCopy();
            auto plNode = t.getChildWithName("PL");
            auto pNode = plNode.getChild(0);
            pNode.setProperty("v", 0.9, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(!feq(back.overrides.get(2, -1.f), 0.5f), "mutation: P-Lock detectable");
        }
        // fillTrigState
        {
            auto t = origTree.createCopy();
            t.setProperty("fts", static_cast<int>(FillTrigState::Off), nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(back.fillTrigState == FillTrigState::Off, "mutation: fillTrigState detectable");
        }
        // fill P-Lock value
        {
            auto t = origTree.createCopy();
            auto fplNode = t.getChildWithName("FPL");
            auto pNode = fplNode.getChild(0);
            pNode.setProperty("v", 0.9, nullptr);
            const auto back = parseStepNodeFull(t);
            CHECK(!feq(back.fillOverrides.get(5, -1.f), 0.3f), "mutation: fill P-Lock detectable");
        }
    }

    // ── v15 P-Lock format: string id keys ─────────────────────────────────────
    // Mirrors the v15 production write path and the dual-path read path
    // in readPhraseFromNode (production code). Tests:
    //   1. v15 "id"-keyed P-Locks round-trip via a resolver lambda.
    //   2. Legacy v14 "s"-keyed P-Locks still parse (backward compat).
    //   3. Unknown ids silently drop (resolver returns -1).

    // Simulates a v15 P-Lock write: writes "id"/"v" instead of "s"/"v".
    static juce::ValueTree buildPLockNodeV15(const juce::String& id, float value)
    {
        juce::ValueTree p("P");
        p.setProperty("id", id, nullptr);
        p.setProperty("v", static_cast<double>(value), nullptr);
        return p;
    }

    // Parses a PL node using a slotResolver (mirrors production readPhraseFromNode logic).
    static PLock parsePLNodeDualPath(const juce::ValueTree& plNode,
                                     std::function<int(const juce::String&)> slotResolver)
    {
        PLock result;
        for (auto pNode : plNode)
        {
            const float val = static_cast<float>(
                static_cast<double>(pNode.getProperty("v", 0.0)));
            const juce::String sid = pNode.getProperty("id").toString();
            if (sid.isNotEmpty())
            {
                const int sl = slotResolver(sid);
                if (sl >= 0) result.set(sl, val);
            }
            else
            {
                const int sl = static_cast<int>(pNode.getProperty("s", -1));
                if (sl >= 0) result.set(sl, val);
            }
        }
        return result;
    }

    static void testV15PLockFormat()
    {
        // Simple slot-id map for testing: "filter.cutoff"→2, "amp.gain"→5.
        auto resolver = [](const juce::String& id) -> int {
            if (id == "filter.cutoff") return 2;
            if (id == "amp.gain") return 5;
            return -1;
        };

        // v15: id-keyed P-Locks round-trip via resolver.
        {
            juce::ValueTree plNode("PL");
            plNode.appendChild(buildPLockNodeV15("filter.cutoff", 0.75f), nullptr);
            plNode.appendChild(buildPLockNodeV15("amp.gain", 0.33f), nullptr);
            const auto locks = parsePLNodeDualPath(plNode, resolver);
            CHECK(feq(locks.get(2, -1.f), 0.75f), "v15: filter.cutoff resolves to slot 2");
            CHECK(feq(locks.get(5, -1.f), 0.33f), "v15: amp.gain resolves to slot 5");
            CHECK(feq(locks.get(0, -1.f), -1.f), "v15: unlocked slot returns sentinel");
        }

        // v14 legacy: s-keyed P-Locks still load via dual-path reader.
        {
            juce::ValueTree plNode("PL");
            juce::ValueTree p("P");
            p.setProperty("s", 2, nullptr);
            p.setProperty("v", 0.88, nullptr);
            plNode.appendChild(p, nullptr);
            const auto locks = parsePLNodeDualPath(plNode, resolver);
            CHECK(feq(locks.get(2, -1.f), 0.88f), "v14 legacy: s-keyed slot 2 still loads");
        }

        // Unknown id: silently dropped.
        {
            juce::ValueTree plNode("PL");
            plNode.appendChild(buildPLockNodeV15("unknown.param", 0.5f), nullptr);
            plNode.appendChild(buildPLockNodeV15("filter.cutoff", 0.6f), nullptr);
            const auto locks = parsePLNodeDualPath(plNode, resolver);
            CHECK(feq(locks.get(2, -1.f), 0.6f), "v15: known id resolves correctly");
            CHECK(locks.empty() || feq(locks.get(2, -1.f), 0.6f),
                  "v15: unknown id dropped; known id intact");
            // Verify the unknown slot is not spuriously set.
            CHECK(feq(locks.get(0, -1.f), -1.f), "v15: slot 0 not set by unknown id");
        }
    }

    // Test that a ValueTree round-trips through XML to a temp file and back losslessly.
    // This pins the file I/O path added in v16 (writeToFile / readFromFile) independently
    // of LockstepProcessor (which cannot instantiate headlessly).
    static void testFileXmlRoundTrip()
    {
        // Build a minimal v16-shaped LockstepState tree with representative children.
        juce::ValueTree root("LockstepState");
        root.setProperty("version", 16, nullptr);
        root.appendChild(juce::ValueTree("Lockstep"), nullptr);

        juce::ValueTree sp("SoundPool");
        juce::ValueTree se("SE");
        se.setProperty("nm", "Test Sound", nullptr);
        se.setProperty("mId", "lockstep.analog.v1", nullptr);
        se.setProperty("spi", -1, nullptr);
        sp.appendChild(se, nullptr);
        root.appendChild(sp, nullptr);

        root.appendChild(juce::ValueTree("NewHierarchy"), nullptr);

        // Write to a temp file as XML.
        const auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("lockstep_test_roundtrip.lockstep");
        tmpFile.deleteFile();
        const auto xml1 = root.createXml();
        CHECK(xml1 != nullptr, "createXml succeeded");
        const bool written = xml1->writeTo(tmpFile);
        CHECK(written, "xml->writeTo(file) succeeds");
        CHECK(tmpFile.existsAsFile(), "temp file exists after write");

        // Read back and compare.
        const auto xml2 = juce::XmlDocument::parse(tmpFile);
        CHECK(xml2 != nullptr, "XmlDocument::parse succeeds on written file");
        if (xml2)
        {
            const auto tree2 = juce::ValueTree::fromXml(*xml2);
            CHECK(tree2.isValid(), "read-back tree is valid");
            CHECK(xml1->toString() == xml2->toString(), "XML file round-trip is lossless");
        }

        tmpFile.deleteFile();
    }

    // ── Scene-occupancy derivation (Part 1 scene save/load data-loss fix) ──────
    // A scene whose only content is its diagonal phrase row used to get no scene
    // node on save (sceneHasContent() ignores phrase data), so Scene::initialised
    // was never set on reload → sceneSlotOccupied() reported the scene empty and
    // the Scene+step handler ran the destructive create-on-empty path, clobbering
    // the loaded phrases. These pin the two shared Song.h helpers that fix both
    // sides: sceneDiagonalOccupied() (widened save gate) and
    // deriveSceneOccupancyFromPhrases() (load-side rescue for pre-fix files).

    static void testSceneOccupancyDerivation()
    {
        Song song;

        // Scenes 0–2: trigs on their diagonal phrase rows, NO scene-level attrs.
        for (int si = 0; si <= 2; ++si)
            for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            {
                auto& ph = song.tracks[static_cast<std::size_t>(t)]
                               .phrases[static_cast<std::size_t>(si)];
                ph.initialised = true;
                ph.steps[static_cast<std::size_t>(si)].trig = true;  // a distinct trig per scene
            }

        // Save-gate helper: diagonal occupancy detected for 0–2, not for 5.
        CHECK(sceneDiagonalOccupied(song, 0), "diagonal 0 occupied");
        CHECK(sceneDiagonalOccupied(song, 2), "diagonal 2 occupied");
        CHECK(!sceneDiagonalOccupied(song, 5), "untouched diagonal 5 unoccupied");

        // Pre-fix load state: no scene node existed, so initialised is false.
        for (const auto& sc : song.scenes)
            CHECK(!sc.initialised, "scenes start un-initialised (pre-fix load state)");

        // Load-side rescue derives occupancy from the diagonal phrases.
        deriveSceneOccupancyFromPhrases(song);

        CHECK(song.scenes[0].initialised, "scene 0 rescued as occupied");
        CHECK(song.scenes[1].initialised, "scene 1 rescued as occupied");
        CHECK(song.scenes[2].initialised, "scene 2 rescued as occupied");
        CHECK(!song.scenes[5].initialised, "genuinely-empty scene 5 stays unoccupied");

        // Trigs survived untouched (the data the bug was destroying).
        CHECK(song.tracks[0].phrases[1].steps[1].trig, "scene 1 diagonal trig survives");
        CHECK(song.tracks[0].phrases[2].steps[2].trig, "scene 2 diagonal trig survives");

        // Idempotent: a second derivation pass changes nothing.
        deriveSceneOccupancyFromPhrases(song);
        CHECK(!song.scenes[5].initialised, "derivation stays a no-op on empty scenes");
    }

    // 5.3 Item C: songs come into being on select (Song::initialised), mirroring
    // scenes. New saves persist the kSoInit flag; pre-v36 saves have no flag, so a
    // song the user built loads un-created and must be backfilled from content
    // (songHasContent). These pin the write→read of the flag and the backfill.
    static void testSongOccupancyRoundTrip()
    {
        namespace k = keys;
        auto stub = [](const TrackKit& kit) { return kit.machineId == StubMachine::kMachineId; };

        // Song embeds tens of tracks × phrases by value (MBs) — heap-allocate so a
        // test frame never stack-blows (mirrors makeSeededArrangement's guidance).
        // A created-but-empty song has no other content, so songHasContent is false
        // — its existence hangs entirely on the persisted flag.
        {
            auto song = std::make_unique<Song>();
            CHECK(!songHasContent(*song, stub), "a blank song reports no content");
            song->initialised = true;

            // Writer forces the node + kSoInit=1 (mirrors PluginState song writer).
            juce::ValueTree n(k::kSong);
            if (song->initialised) n.setProperty(k::kSoInit, 1, nullptr);
            const bool backInit = static_cast<int>(n.getProperty(k::kSoInit, 0)) != 0;
            CHECK(backInit, "created-but-empty song's existence round-trips");
        }

        // Backfill: a pre-v36 song (no flag → initialised false) that holds content
        // is rescued; a genuinely-empty one stays un-created.
        {
            auto song = std::make_unique<Song>();  // reused; reset fields between cases
            song->scenes[0].initialised = true;    // content but no flag (old save)
            CHECK(!song->initialised, "pre-v36 song loads un-created");
            CHECK(songHasContent(*song, stub), "a song with an initialised scene has content");
            song->scenes[0].initialised = false;

            song->name = "Verse";                  // named-only content
            CHECK(songHasContent(*song, stub), "a named song has content");
            song->name.clear();

            CHECK(!songHasContent(*song, stub), "a blank song has no content to rescue");
        }
    }

    // 4.9: the cached sample-analysis properties (v26) survive an XML round-trip
    // and reconstruct into the same CachedAnalysis the read path builds. Mirrors
    // the writeSamplePool/readSamplePool property layout without a processor.
    static void testSampleAnalysisCacheRoundTrip()
    {
        namespace k = keys;

        // Build a pool Entry the way writeSamplePool does for an analysed entry.
        juce::ValueTree entry(k::kEntry);
        entry.setProperty("i", 0, nullptr);
        entry.setProperty(k::kPath, "/samples/loop.wav", nullptr);
        entry.setProperty(k::kHash, "abcd1234", nullptr);
        entry.setProperty(k::kAnalysed, 1, nullptr);
        entry.setProperty(k::kBpm, 128.0, nullptr);
        entry.setProperty(k::kKeyRoot, 9, nullptr);            // A
        entry.setProperty(k::kKeyBright, static_cast<int>(kDorian), nullptr);
        entry.setProperty(k::kTuneCents, -12.5, nullptr);
        // 9.23 (v30): detected one-shot + user overrides.
        entry.setProperty(k::kOneShot, 1, nullptr);
        entry.setProperty(k::kUserBpm, 140.0, nullptr);
        entry.setProperty(k::kUserKeyRoot, 2, nullptr);
        entry.setProperty(k::kUserKeyBright, static_cast<int>(kAeolian), nullptr);
        entry.setProperty(k::kUserTuneCents, 7.0, nullptr);
        entry.setProperty(k::kUserOneShot, 0, nullptr);

        // XML round-trip.
        const auto xml = entry.createXml();
        CHECK(xml != nullptr, "cache entry createXml succeeds");
        const auto reparsed = juce::ValueTree::fromXml(*xml);
        CHECK(reparsed.isValid(), "cache entry re-parses from XML");

        // Reconstruct CachedAnalysis exactly as readSamplePool does.
        CHECK(reparsed.hasProperty(k::kAnalysed), "analysed flag survives round-trip");
        SamplePool::CachedAnalysis ca;
        ca.bpm = static_cast<double>(reparsed.getProperty(k::kBpm, 0.0));
        ca.keyRoot = static_cast<int>(reparsed.getProperty(k::kKeyRoot, -1));
        ca.keyBrightness =
            static_cast<int>(reparsed.getProperty(k::kKeyBright, static_cast<int>(kAeolian)));
        ca.tuningCents = static_cast<double>(reparsed.getProperty(k::kTuneCents, 0.0));
        ca.oneShot = static_cast<int>(reparsed.getProperty(k::kOneShot, 0)) != 0;

        CHECK(feq(static_cast<float>(ca.bpm), 128.0f), "cached bpm round-trips");
        CHECK(ca.keyRoot == 9, "cached key root round-trips");
        CHECK(ca.keyBrightness == kDorian, "cached key brightness round-trips");
        CHECK(feq(static_cast<float>(ca.tuningCents), -12.5f), "cached tuning round-trips");
        CHECK(ca.oneShot, "cached detected one-shot round-trips");

        // v30 user overrides reconstruct exactly as readSamplePool's applyUserOverrides.
        CHECK(feq(static_cast<float>(static_cast<double>(reparsed.getProperty(k::kUserBpm, 0.0))),
                  140.0f), "user bpm round-trips");
        CHECK(static_cast<int>(reparsed.getProperty(k::kUserKeyRoot, -1)) == 2,
              "user key root round-trips");
        CHECK(feq(static_cast<float>(static_cast<double>(reparsed.getProperty(k::kUserTuneCents, 0.0))),
                  7.0f), "user tuning round-trips");
        CHECK(static_cast<int>(reparsed.getProperty(k::kUserOneShot, -1)) == 0,
              "user one-shot (loop) round-trips");

        // An entry with NO analysis props (a legacy v25 entry) reconstructs to
        // "no cache" — the read path re-analyses instead of adopting.
        juce::ValueTree legacy(k::kEntry);
        legacy.setProperty(k::kPath, "/samples/old.wav", nullptr);
        legacy.setProperty(k::kHash, "0000", nullptr);
        CHECK(!legacy.hasProperty(k::kAnalysed),
              "legacy entry carries no analysed flag (triggers re-analysis)");
    }

    // ── 5.3 identity round-trip (Song / Scene / SoundEntry name + colour) ──────
    // Mirrors the production write/read logic in PluginState.cpp (the Song/Scene
    // writers and readProjectSoundPool) at the ValueTree level: name is written
    // only when non-empty, colour only when >= 0, and both default on absence
    // (empty name / colour -1) — the v34→v35 trivial upgrade.
    static void testIdentityRoundTrip()
    {
        namespace k = keys;

        // Song identity survives. (Song is ~3 MB — the Arrangement-size gotcha —
        // so we exercise the field-level write/read logic without a stack Song.)
        {
            const std::string name = "Amber Fox";
            const int colour = 3;

            juce::ValueTree n(k::kSong);
            n.setProperty("i", 0, nullptr);
            if (!name.empty())
                n.setProperty(k::kSoName, juce::String(name), nullptr);
            if (colour >= 0)
                n.setProperty(k::kSoColour, colour, nullptr);

            const auto backName =
                n.getProperty(k::kSoName, juce::String()).toString().toStdString();
            const int backColour = static_cast<int>(n.getProperty(k::kSoColour, -1));
            CHECK(backName == "Amber Fox", "Song name round-trips");
            CHECK(backColour == 3, "Song colour round-trips");
        }

        // Scene identity survives; sceneHasContent gates on it.
        {
            Scene sc;
            CHECK(!sceneHasContent(sc), "default Scene has no content");
            sc.name = "Bridge B";
            CHECK(sceneHasContent(sc), "named Scene forces persistence");
            sc.name.clear();
            sc.colour = 5;
            CHECK(sceneHasContent(sc), "coloured Scene forces persistence");

            sc.name = "Bridge B";
            juce::ValueTree n(k::kScene);
            if (!sc.name.empty())
                n.setProperty(k::kScName, juce::String(sc.name), nullptr);
            if (sc.colour >= 0)
                n.setProperty(k::kScColour, sc.colour, nullptr);

            Scene back;
            back.name = n.getProperty(k::kScName, juce::String()).toString().toStdString();
            back.colour = static_cast<int>(n.getProperty(k::kScColour, -1));
            CHECK(back.name == "Bridge B", "Scene name round-trips");
            CHECK(back.colour == 5, "Scene colour round-trips");
        }

        // SoundEntry colour survives (name already round-trips pre-v35).
        {
            SoundEntry e;
            e.name = "Tavo";
            e.colour = 7;

            juce::ValueTree n(k::kSoundEntry);
            n.setProperty(k::kSeName, juce::String(e.name), nullptr);
            if (e.colour >= 0)
                n.setProperty(k::kSeColour, e.colour, nullptr);

            SoundEntry back;
            back.name = n.getProperty(k::kSeName, "Sound").toString().toStdString();
            back.colour = static_cast<int>(n.getProperty(k::kSeColour, -1));
            CHECK(back.name == "Tavo", "SoundEntry name round-trips");
            CHECK(back.colour == 7, "SoundEntry colour round-trips");
        }

        // v34→v35 upgrade: absent identity fields default cleanly.
        {
            juce::ValueTree song(k::kSong);   // no identity props written
            const auto name =
                song.getProperty(k::kSoName, juce::String()).toString().toStdString();
            const int colour = static_cast<int>(song.getProperty(k::kSoColour, -1));
            CHECK(name.empty(), "v34 Song loads with empty name");
            CHECK(colour == -1, "v34 Song loads with colour -1 (unset)");

            juce::ValueTree se(k::kSoundEntry);
            SoundEntry seBack;
            seBack.colour = static_cast<int>(se.getProperty(k::kSeColour, -1));
            CHECK(seBack.colour == -1, "v34 SoundEntry loads with colour -1 (unset)");
        }
    }

    void runSerializerRoundTripTests()
    {
        testCondRoundTrip();
        testIdentityRoundTrip();
        testSceneOccupancyDerivation();
        testSongOccupancyRoundTrip();
        testStepRoundTrip();
        testPhrasePropertyNames();
        testFillFieldRoundTrip();
        testMutationDetectability();
        testV15PLockFormat();
        testFileXmlRoundTrip();
        testSampleAnalysisCacheRoundTrip();
    }
}
