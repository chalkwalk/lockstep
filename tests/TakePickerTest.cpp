// TakePickerTest — the 6/5 take-group picker rule (DESIGN §40.7).
//
// A promoted 4-track take is 4 sub-track entries + 1 downmix, one group id. The
// rule: a sample-class picker sees the 5 members; a deck-class picker sees the
// group as one entity PLUS the 5 members (6 rows). Plain samples are one row
// either way. Pure over the pool, so it is tested without a processor.

#include "TestHarness.h"
#include "../src/machine/TakePicker.h"

namespace lockstep
{
    namespace
    {
        // Seed a pool with `plain` plain entries, then one take-group of `subs`
        // sub-tracks + a downmix. Returns the pool (volatile entries stand in for
        // "any accepted entry"; the rule only reads takeGroupId/takeMember).
        void seedGroup(SamplePool& pool, int plain, int subs)
        {
            for (int i = 0; i < plain; ++i) pool.addVolatile();
            const std::uint32_t g = pool.nextTakeGroupId();
            // downmix (member 0) then subs (member 1..N)
            const int mix = pool.addVolatile();
            pool.setTakeGroup(mix, g, 0);
            for (int k = 1; k <= subs; ++k)
            {
                const int idx = pool.addVolatile();
                pool.setTakeGroup(idx, g, k);
            }
        }
    }

    void runTakePickerTests()
    {
        const auto acceptAll = [](int) { return true; };

        // ── Sample-class: a 4-track take reads as five members, no group entity ─
        {
            SamplePool pool;
            seedGroup(pool, 0, 4);   // 1 downmix + 4 subs = 5 members
            const auto rows = buildTakePickerRows(pool, acceptAll, /*deckClass*/ false);
            CHECK(rows.size() == 5, "sample-class sees the five members");
            for (const auto& r : rows)
                CHECK(r.kind == TakePickerRow::Kind::Sample, "and no group entity");
        }

        // ── Deck-class: the same take reads as six (group + five members) ────
        {
            SamplePool pool;
            seedGroup(pool, 0, 4);
            const auto rows = buildTakePickerRows(pool, acceptAll, /*deckClass*/ true);
            CHECK(rows.size() == 6, "deck-class sees the group entity plus five members");

            int groups = 0, members = 0;
            for (const auto& r : rows)
                (r.kind == TakePickerRow::Kind::TakeGroup ? groups : members)++;
            CHECK(groups == 1 && members == 5, "exactly one group entity, five members");

            // The group entity comes before its members and points at the downmix.
            CHECK(rows[0].kind == TakePickerRow::Kind::TakeGroup, "the group entity leads");
            const auto* rep = pool.get(rows[0].poolIndex);
            CHECK(rep != nullptr && rep->takeMember == 0,
                  "the group entity is represented by the downmix");
        }

        // ── Plain samples are one row in either class ────────────────────────
        {
            SamplePool pool;
            seedGroup(pool, 3, 2);   // 3 plain + (1 mix + 2 subs)
            const auto s = buildTakePickerRows(pool, acceptAll, false);
            const auto d = buildTakePickerRows(pool, acceptAll, true);
            CHECK(s.size() == 3 + 3, "sample-class: 3 plain + 3 members");
            CHECK(d.size() == 3 + 1 + 3, "deck-class: 3 plain + 1 group + 3 members");
            // The three plain entries are Sample rows with no group.
            int plainRows = 0;
            for (const auto& r : s)
                if (r.groupId == 0) ++plainRows;
            CHECK(plainRows == 3, "the plain entries carry no group id");
        }

        // ── The acceptance filter still applies ──────────────────────────────
        {
            SamplePool pool;
            seedGroup(pool, 0, 2);   // indices 0=mix,1=sub1,2=sub2
            // Accept only the downmix (index 0).
            const auto onlyMix = [](int i) { return i == 0; };
            const auto d = buildTakePickerRows(pool, onlyMix, true);
            // Only the downmix passes → the group entity fires on it, plus that one
            // member. (A real filter that hides members would shrink the group.)
            CHECK(d.size() == 2, "a filter that admits one member yields group + that member");
        }
    }
}
