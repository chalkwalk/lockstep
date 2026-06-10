// SerializerRoundTripTest — characterisation of the v14 serialized format.
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
//   P-Lock:      "s" (slot), "v" (value)
//   Phrase:      "i" (idx), "len", "nsel", child "BaseCond", child "TrigDefaults",
//                child "Steps"
//   TrigDefaults:"note", "vel", "gateV"

#include "TestHarness.h"
#include "../src/core/Phrase.h"
#include "../src/core/TrigCondition.h"
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

    // ── TrigCondition round-trip ──────────────────────────────────────────

    static void testCondRoundTrip()
    {
        TrigCondition orig;
        orig.probabilityPercent = 73;
        orig.iterNumerator      = 2;
        orig.iterDenominator    = 3;
        orig.prevDependency     = 1;

        const auto tree = condToTree("TestCond", orig);
        CHECK(static_cast<int>(tree.getProperty("p"))  == 73, "cond.p written");
        CHECK(static_cast<int>(tree.getProperty("n"))  == 2,  "cond.n written");
        CHECK(static_cast<int>(tree.getProperty("d"))  == 3,  "cond.d written");
        CHECK(static_cast<int>(tree.getProperty("pd")) == 1,  "cond.pd written");

        const auto back = condFromTree(tree);
        CHECK(back.probabilityPercent == 73, "cond.prob round-trips");
        CHECK(back.iterNumerator      == 2,  "cond.iterNum round-trips");
        CHECK(back.iterDenominator    == 3,  "cond.iterDen round-trips");
        CHECK(back.prevDependency     == 1,  "cond.prevDep round-trips");

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
        if (std::abs(step.microOffset) > 1e-7f)
            node.setProperty("mo", static_cast<double>(step.microOffset), nullptr);
        if (!step.condition.isTrivial())
            node.appendChild(condToTree("C", step.condition), nullptr);
        if (step.trigOverride.noteCount > 0 || step.trigOverride.hasVelocity
            || step.trigOverride.hasGate || step.trigOverride.hasSoundId
            || step.trigOverride.hasRetrig)
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
                toNode.setProperty("v",  step.trigOverride.velocity, nullptr);
            }
            if (step.trigOverride.hasGate)
            {
                toNode.setProperty("hg",  1, nullptr);
                toNode.setProperty("gv",  static_cast<int>(static_cast<std::uint8_t>(
                                              step.trigOverride.gateValue)), nullptr);
            }
            if (step.trigOverride.hasSoundId)
            {
                toNode.setProperty("hsi", 1, nullptr);
                toNode.setProperty("si",  step.trigOverride.soundId, nullptr);
            }
            if (step.trigOverride.hasRetrig)
            {
                toNode.setProperty("hrt", 1, nullptr);
                toNode.setProperty("rt",  step.trigOverride.retrigRate, nullptr);
            }
            node.appendChild(toNode, nullptr);
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
            node.appendChild(plNode, nullptr);
        }
        return node;
    }

    static Step parseStepNode(const juce::ValueTree& node)
    {
        Step step;
        step.trig        = (static_cast<int>(node.getProperty("t", 0)) != 0);
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
                step.trigOverride.velocity    = static_cast<int>(toNode.getProperty("v", 100));
            }
            if (static_cast<int>(toNode.getProperty("hg", 0)) != 0)
            {
                step.trigOverride.hasGate  = true;
                step.trigOverride.gateValue = static_cast<MusicalGate>(
                    static_cast<std::uint8_t>(static_cast<int>(toNode.getProperty("gv", 0))));
            }
            if (static_cast<int>(toNode.getProperty("hsi", 0)) != 0)
            {
                step.trigOverride.hasSoundId = true;
                step.trigOverride.soundId    = static_cast<int>(toNode.getProperty("si", -1));
            }
            if (static_cast<int>(toNode.getProperty("hrt", 0)) != 0)
            {
                step.trigOverride.hasRetrig   = true;
                step.trigOverride.retrigRate  = static_cast<double>(toNode.getProperty("rt", 0.25));
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
            s.trig        = true;
            s.microOffset = 0.125f;
            const auto tree = buildStepNode(3, s);
            CHECK(static_cast<int>(tree.getProperty("i")) == 3, "step idx written");
            CHECK(static_cast<int>(tree.getProperty("t")) == 1, "step trig written");
            const auto back = parseStepNode(tree);
            CHECK(back.trig,                              "step trig round-trips");
            CHECK(feq(back.microOffset, 0.125f),          "step microOffset round-trips");
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
            CHECK(cNode.isValid(),                         "step condition node written");
            const auto back = parseStepNode(tree);
            CHECK(back.condition.probabilityPercent == 50, "step cond.prob round-trips");
            CHECK(back.condition.iterDenominator    == 2,  "step cond.iterDen round-trips");
        }

        // P-Lock.
        {
            Step s;
            s.trig = true;
            s.overrides.set(2, 0.75f);
            s.overrides.set(7, 0.33f);
            const auto tree = buildStepNode(5, s);
            CHECK(tree.getChildWithName("PL").isValid(),   "PL node written");
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
            s.trigOverride.velocity    = 88;
            s.trigOverride.noteCount   = 2;
            s.trigOverride.notes[0]    = 60;
            s.trigOverride.notes[1]    = 64;
            const auto tree = buildStepNode(1, s);
            const auto toNode = tree.getChildWithName("TO");
            CHECK(toNode.isValid(),                        "TO node written");
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasVelocity,           "TO.hasVelocity round-trips");
            CHECK(back.trigOverride.velocity == 88,        "TO.velocity round-trips");
            CHECK(back.trigOverride.noteCount == 2,        "TO.noteCount round-trips");
            CHECK(back.trigOverride.notes[0] == 60,        "TO.note[0] round-trips");
            CHECK(back.trigOverride.notes[1] == 64,        "TO.note[1] round-trips");
        }

        // Trig override: soundId.
        {
            Step s;
            s.trig = true;
            s.trigOverride.hasSoundId = true;
            s.trigOverride.soundId    = 7;
            const auto tree = buildStepNode(0, s);
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasSoundId,            "TO.hasSoundId round-trips");
            CHECK(back.trigOverride.soundId == 7,          "TO.soundId round-trips");
        }

        // Trig override: retrig.
        {
            Step s;
            s.trig = true;
            s.trigOverride.hasRetrig   = true;
            s.trigOverride.retrigRate  = 0.5;
            const auto tree = buildStepNode(0, s);
            const auto back = parseStepNode(tree);
            CHECK(back.trigOverride.hasRetrig,             "TO.hasRetrig round-trips");
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
        phraseNode.setProperty("i",   5,   nullptr);  // phrase index
        phraseNode.setProperty("len", 12,  nullptr);
        phraseNode.setProperty("nsel", 1,  nullptr);   // NoteSelection::BotBias

        TrigCondition bc;
        bc.probabilityPercent = 80;
        phraseNode.appendChild(condToTree("BaseCond", bc), nullptr);

        juce::ValueTree tdNode("TrigDefaults");
        tdNode.setProperty("note",  48,  nullptr);
        tdNode.setProperty("vel",   90,  nullptr);
        tdNode.setProperty("gateV", 2,   nullptr);
        phraseNode.appendChild(tdNode, nullptr);

        juce::ValueTree stepsNode("Steps");
        Step s;
        s.trig        = true;
        s.microOffset = 0.25f;
        s.condition.iterDenominator = 4;
        s.overrides.set(3, 0.9f);
        stepsNode.appendChild(buildStepNode(0, s), nullptr);
        phraseNode.appendChild(stepsNode, nullptr);

        // Verify property names.
        CHECK(static_cast<int>(phraseNode.getProperty("i")) == 5,   "phrase.i property name");
        CHECK(static_cast<int>(phraseNode.getProperty("len")) == 12, "phrase.len property name");
        CHECK(static_cast<int>(phraseNode.getProperty("nsel")) == 1, "phrase.nsel property name");
        CHECK(phraseNode.getChildWithName("BaseCond").isValid(),     "phrase BaseCond child name");
        CHECK(phraseNode.getChildWithName("TrigDefaults").isValid(), "phrase TrigDefaults child name");
        CHECK(phraseNode.getChildWithName("Steps").isValid(),        "phrase Steps child name");

        // BaseCond survives.
        const auto bcNode = phraseNode.getChildWithName("BaseCond");
        CHECK(static_cast<int>(bcNode.getProperty("p")) == 80, "phrase BaseCond.p preserved");

        // TrigDefaults survives.
        const auto tdBack = phraseNode.getChildWithName("TrigDefaults");
        CHECK(static_cast<int>(tdBack.getProperty("note"))  == 48, "TrigDefaults.note preserved");
        CHECK(static_cast<int>(tdBack.getProperty("vel"))   == 90, "TrigDefaults.vel preserved");
        CHECK(static_cast<int>(tdBack.getProperty("gateV")) == 2,  "TrigDefaults.gateV preserved");

        // Step survives.
        const auto stepsBack = phraseNode.getChildWithName("Steps");
        const auto stepBack  = stepsBack.getChild(0);
        const auto stepParsed = parseStepNode(stepBack);
        CHECK(stepParsed.trig,                        "phrase step trig preserved");
        CHECK(feq(stepParsed.microOffset, 0.25f),     "phrase step mo preserved");
        CHECK(stepParsed.condition.iterDenominator == 4, "phrase step cond preserved");
        CHECK(feq(stepParsed.overrides.get(3, -1.f), 0.9f), "phrase step plock preserved");
    }

    void runSerializerRoundTripTests()
    {
        testCondRoundTrip();
        testStepRoundTrip();
        testPhrasePropertyNames();
    }
}
