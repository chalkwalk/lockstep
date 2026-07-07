// Item 7 — the static section-stack table (SectionStackTable.h).
// Verifies the table is well-formed (no duplicate chords, valid actions/meta
// indices) and that appendSectionStackCandidates emits the right rows per key.

#include "TestHarness.h"
#include "../src/ui/mode/SectionStackTable.h"

#include <algorithm>
#include <set>
#include <tuple>

namespace lockstep
{
    static void testNoDuplicateChords()
    {
        // An (origin, key, funcQualified, stepQualified) tuple is a distinct chord;
        // two rows sharing one (i.e. both live in the same layer) would make
        // resolution non-deterministic. stepQualified is part of the key: the
        // step-held COND row shares (Machine,0,func=false) with nothing else only
        // because it is the sole step-qualified row there.
        std::set<std::tuple<int, int, bool, bool>> seen;
        for (const auto& r : kSectionStackTable)
        {
            const auto chord = std::make_tuple(static_cast<int>(r.origin), r.key,
                                               r.funcQualified, r.stepQualified);
            CHECK(seen.insert(chord).second, "no duplicate (origin,key,func,step) chord");
        }
    }

    static void testActionMetaConsistency()
    {
        for (const auto& r : kSectionStackTable)
        {
            CHECK(r.key >= 0 && r.key < 6, "key is a canonical section index 0-5");
            CHECK(r.label != nullptr && r.label[0] != '\0', "row has a non-empty label");
            switch (r.action)
            {
                case SecAction::MetaSection:
                    CHECK(r.metaIndex >= 0 && r.metaIndex <= 5,
                          "MetaSection row carries a valid masterSection index 0-5");
                    break;
                case SecAction::TimeSticky:
                    CHECK(r.metaIndex == -1, "TimeSticky row carries no meta index");
                    break;
                case SecAction::ParamSection:
                    CHECK(false, "the table never carries ParamSection rows");
                    break;
            }
        }
    }

    static void testKnownChords()
    {
        // Spot-check the frozen mapping the resolver depends on.
        const auto find = [](SecOrigin o, int key, bool fn) -> const SectionStackRow* {
            for (const auto& r : kSectionStackTable)
                if (r.origin == o && r.key == key && r.funcQualified == fn)
                    return &r;
            return nullptr;
        };

        const auto* div = find(SecOrigin::Track, 0, false);
        CHECK(div && div->action == SecAction::MetaSection && div->metaIndex == 3,
              "Track+TRIG = DIV meta (3)");
        const auto* len = find(SecOrigin::Phrase, 0, false);
        CHECK(len && len->action == SecAction::MetaSection && len->metaIndex == 4,
              "Phrase+TRIG = LEN meta (4)");
        CHECK(find(SecOrigin::Scene, 0, false)
                  && find(SecOrigin::Scene, 0, false)->action == SecAction::TimeSticky,
              "Scene+TRIG = TIME sticky");
        CHECK(find(SecOrigin::Song, 0, false)
                  && find(SecOrigin::Song, 0, false)->action == SecAction::TimeSticky,
              "Song+TRIG = TIME sticky");
        const auto* fx = find(SecOrigin::Song, 5, false);
        CHECK(fx && fx->action == SecAction::MetaSection && fx->metaIndex == 5,
              "Song+FX = master FX meta (5)");
        const auto* cond = find(SecOrigin::Machine, 0, true);
        CHECK(cond && cond->metaIndex == 0, "Func+TRIG = COND meta (0)");
        const auto* note = find(SecOrigin::Machine, 1, true);
        CHECK(note && note->metaIndex == 1, "Func+SRC = NOTE meta (1)");
        const auto* trsp = find(SecOrigin::Global, 2, true);
        CHECK(trsp && trsp->metaIndex == 2, "Func+FILTER = TRSP meta (2)");
    }

    static void testAppendCandidates()
    {
        // Key 0 (TRIG) collects DIV/LEN/two TIMEs + step-held COND + Func COND = 6.
        std::vector<SecCandidate> out;
        appendSectionStackCandidates(out, 0);
        CHECK(out.size() == 6, "key 0 appends six stack candidates");
        // The 9.26 held-step promotion row: primary layer (not funcQualified),
        // stepQualified, COND meta (0).
        const auto stepCond = std::find_if(out.begin(), out.end(), [](const SecCandidate& c) {
            return c.stepQualified;
        });
        CHECK(stepCond != out.end() && !stepCond->funcQualified
                  && stepCond->origin == SecOrigin::Machine && stepCond->metaIndex == 0,
              "key 0 carries the step-held COND row (Machine, primary, meta 0)");

        // Key 3 (AMP) has no stack rows.
        out.clear();
        appendSectionStackCandidates(out, 3);
        CHECK(out.empty(), "key 3 (AMP) has no stack candidates");

        // Appended candidates preserve the schema-derived prefix.
        out = { { 5, 1, SecOrigin::Track } };  // pretend a track FX page already present
        appendSectionStackCandidates(out, 5);
        CHECK(out.size() == 2 && out.front().origin == SecOrigin::Track,
              "append leaves existing candidates in place");
        CHECK(out.back().origin == SecOrigin::Song && out.back().metaIndex == 5,
              "key 5 appends the Song master-FX meta after the track page");
    }

    void runSectionStackTests()
    {
        testNoDuplicateChords();
        testActionMetaConsistency();
        testKnownChords();
        testAppendCandidates();
    }
}
