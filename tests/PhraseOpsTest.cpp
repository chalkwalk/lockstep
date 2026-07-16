// PhraseOpsTest — pure phrase copy/move helpers (5.3 / DESIGN §23.3,
// src/core/PhraseOps.h). Pins content-equality (the no-op-paste skip) and the
// free-slot allocator that fork-on-shared draws from. Pure, so testable without
// a processor.

#include "TestHarness.h"
#include "../src/core/PhraseOps.h"

namespace lockstep
{
    static Phrase makePhrase(int length, bool trig3)
    {
        Phrase p;
        p.length = length;
        p.initialised = true;
        p.steps[3].trig = trig3;
        return p;
    }

    static void testContentEqualIgnoresInitialised()
    {
        Phrase a = makePhrase(16, true);
        Phrase b = makePhrase(16, true);
        b.initialised = false;   // bookkeeping flag must not affect content equality
        CHECK(phraseContentEqual(a, b), "equal content differing only by initialised flag");
    }

    static void testContentEqualDetectsDifference()
    {
        Phrase a = makePhrase(16, true);
        CHECK(!phraseContentEqual(a, makePhrase(16, false)), "differing trig => not equal");
        CHECK(!phraseContentEqual(a, makePhrase(15, true)),  "differing length => not equal");

        // A P-Lock difference on one step is detected (exercises the PLock ==).
        Phrase c = makePhrase(16, true);
        c.steps[3].overrides.set(2, 0.5f);
        CHECK(!phraseContentEqual(a, c), "differing P-Lock => not equal");

        // Same P-Lock re-created independently => equal (order-stable storage).
        Phrase d = makePhrase(16, true);
        d.steps[3].overrides.set(2, 0.5f);
        CHECK(phraseContentEqual(c, d), "identical P-Lock content => equal");
    }

    static void testFirstFreeSlot()
    {
        std::array<Phrase, kPhrasesPerTrack> pool{};   // all uninitialised
        CHECK(firstFreePhraseSlot(pool) == 0, "empty pool: first free is slot 0");

        pool[0].initialised = true;
        pool[1].initialised = true;
        CHECK(firstFreePhraseSlot(pool) == 2, "first two taken: first free is slot 2");

        // A hole below an occupied slot is found first (lowest index wins).
        pool[2].initialised = true;
        pool[4].initialised = true;
        CHECK(firstFreePhraseSlot(pool) == 3, "lowest free index wins over a later hole");

        for (auto& ph : pool) ph.initialised = true;
        CHECK(firstFreePhraseSlot(pool) == -1, "full pool: no free slot");
    }

    void runPhraseOpsTests()
    {
        testContentEqualIgnoresInitialised();
        testContentEqualDetectsDifference();
        testFirstFreeSlot();
    }
}
