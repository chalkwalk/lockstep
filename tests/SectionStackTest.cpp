// Item 7 — the static section-stack table (SectionStackTable.h).
// Verifies the table is well-formed (no duplicate chords, valid actions/meta
// indices) and that appendSectionStackCandidates emits the right rows per key.

#include "TestHarness.h"
#include "../src/ui/mode/SectionStackTable.h"

#include <set>
#include <tuple>

namespace lockstep
{
    static void testNoDuplicateChords()
    {
        // A (origin, key, funcQualified) triple is a distinct chord; two rows
        // sharing one would make resolution non-deterministic.
        std::set<std::tuple<int, int, bool>> seen;
        for (const auto& r : kSectionStackTable)
        {
            const auto chord = std::make_tuple(static_cast<int>(r.origin), r.key,
                                               r.funcQualified);
            CHECK(seen.insert(chord).second, "no duplicate (origin,key,func) chord");
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
        // Key 0 (TRIG) collects DIV/LEN/two TIMEs + Func COND = 5 rows.
        std::vector<SecCandidate> out;
        appendSectionStackCandidates(out, 0);
        CHECK(out.size() == 5, "key 0 appends five stack candidates");

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
