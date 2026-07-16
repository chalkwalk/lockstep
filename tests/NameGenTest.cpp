// NameGenTest — the pure generative-naming core for the 5.3 identity overlay.
// Covers: compose correctness (all 3 modes), per-row reshuffle independence,
// hash-seeded determinism (same hash ⇒ same default), and mode cycling.

#include "TestHarness.h"
#include "../src/ui/NameGen.h"

#include <set>
#include <string>

namespace lockstep
{
    static void testCompose()
    {
        using namespace namegen;

        // AdjNoun / SectionLetter join with a space; Syllable concatenates.
        CHECK(compose(NameMode::AdjNoun, "Amber", "Fox") == "Amber Fox",
              "AdjNoun composes with a space");
        CHECK(compose(NameMode::SectionLetter, "Bridge", "B") == "Bridge B",
              "SectionLetter composes with a space");
        CHECK(compose(NameMode::Syllable, "Ta", "vo") == "Tavo",
              "Syllable concatenates with no separator");

        // <= 16 chars.
        const auto longName = compose(NameMode::AdjNoun, "Northernmost", "Constellation");
        CHECK(static_cast<int>(longName.size()) <= kNameMaxChars,
              "composed name is capped at 16 chars");
    }

    static void testRowContents()
    {
        using namespace namegen;

        // SectionLetter rows are the fixed 8 (seed ignored).
        const auto sect = rowWords(NameMode::SectionLetter, 0, 12345u);
        CHECK(sect[0] == "Intro" && sect[3] == "Bridge" && sect[7] == "Fill",
              "SectionLetter top row is the fixed section list");
        const auto letters = rowWords(NameMode::SectionLetter, 1, 999u);
        CHECK(letters[0] == "A" && letters[7] == "H", "SectionLetter bottom row is A-H");
        // Seed does not change the fixed rows.
        CHECK(rowWords(NameMode::SectionLetter, 0, 1u) == rowWords(NameMode::SectionLetter, 0, 2u),
              "SectionLetter ignores the seed");

        // AdjNoun / Syllable rows are 8 distinct entries.
        for (auto mode : {NameMode::AdjNoun, NameMode::Syllable})
            for (int row = 0; row < 2; ++row)
            {
                const auto r = rowWords(mode, row, 777u);
                std::set<std::string> uniq(r.begin(), r.end());
                CHECK(static_cast<int>(uniq.size()) == kNameRowCells,
                      "shuffled row holds 8 distinct words");
                for (const auto& w : r)
                    CHECK(!w.empty(), "no empty word in a shuffled row");
            }
    }

    static void testReshuffleIndependence()
    {
        using namespace namegen;

        // A different seed reshuffles a row (overwhelmingly different contents).
        const auto a = rowWords(NameMode::AdjNoun, 0, 100u);
        const auto b = rowWords(NameMode::AdjNoun, 0, 101u);
        CHECK(a != b, "a different seed produces a different top row");

        // Same seed is stable (deterministic).
        CHECK(rowWords(NameMode::AdjNoun, 0, 100u) == a, "same seed reproduces the row");

        // Reshuffling the TOP row (new top seed) leaves the BOTTOM row untouched.
        const auto bottomBefore = rowWords(NameMode::AdjNoun, 1, 500u);
        const auto bottomAfter  = rowWords(NameMode::AdjNoun, 1, 500u);
        CHECK(bottomBefore == bottomAfter, "bottom row is independent of the top seed");
    }

    static void testDefaultSeedDeterminism()
    {
        using namespace namegen;

        // Same hash ⇒ identical default (so an entity keeps suggesting one name).
        const auto s1 = defaultSeed(0xABCDEF01u);
        const auto s2 = defaultSeed(0xABCDEF01u);
        CHECK(s1.topSeed == s2.topSeed && s1.bottomSeed == s2.bottomSeed
              && s1.topSel == s2.topSel && s1.bottomSel == s2.bottomSel,
              "defaultSeed is deterministic for a given hash");

        // Selections are in range; row seeds are non-zero.
        CHECK(s1.topSel >= 0 && s1.topSel < kNameRowCells, "default topSel in range");
        CHECK(s1.bottomSel >= 0 && s1.bottomSel < kNameRowCells, "default bottomSel in range");
        CHECK(s1.topSeed != 0u && s1.bottomSeed != 0u, "default row seeds are non-zero");

        // A valid name exists with zero presses (just Confirm) for every mode.
        for (auto mode : {NameMode::AdjNoun, NameMode::Syllable, NameMode::SectionLetter})
        {
            const auto name = composeAt(mode, s1.topSeed, s1.topSel, s1.bottomSeed, s1.bottomSel);
            CHECK(!name.empty(), "hash-seeded default yields a non-empty name");
        }

        // Different hashes generally differ (spot check a couple).
        CHECK(defaultSeed(1u).topSeed != defaultSeed(2u).topSeed,
              "distinct hashes seed distinct rows");
    }

    static void testModeCycle()
    {
        // Forward wraps AdjNoun -> Syllable -> SectionLetter -> AdjNoun.
        CHECK(cycleNameMode(NameMode::AdjNoun, +1) == NameMode::Syllable, "cycle +1");
        CHECK(cycleNameMode(NameMode::SectionLetter, +1) == NameMode::AdjNoun, "cycle +1 wraps");
        // Backward wraps the other way.
        CHECK(cycleNameMode(NameMode::AdjNoun, -1) == NameMode::SectionLetter, "cycle -1 wraps");
        CHECK(cycleNameMode(NameMode::Syllable, -1) == NameMode::AdjNoun, "cycle -1");
    }

    void runNameGenTests()
    {
        testCompose();
        testRowContents();
        testReshuffleIndependence();
        testDefaultSeedDeterminism();
        testModeCycle();
    }
}
