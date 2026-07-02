// P6 (grab-bag items 6+7): scope-aware section page-list selection.
// Covers the pure decision that drives sectionsForKey(): Track scope prefers
// track-level params, unqualified prefers machine params, each with fallback to
// the other when its own list is empty — and, crucially, the machine's owning of
// a section suppresses the "extra" appended track page (the buried-AMP bug).

#include "TestHarness.h"
#include "../src/ui/ScopeSectionSelect.h"

namespace lockstep
{
    static std::vector<int> ids(const std::vector<SecCandidate>& v)
    {
        std::vector<int> r;
        for (const auto& c : v) r.push_back(c.sectionIdx);
        return r;
    }

    static void testMachineOwnsSection()
    {
        // FLTR (canonical 2) owned by the machine, plus a relocated track FLTR
        // extension (idx 50). Machine-owned first in display order.
        const std::vector<SecCandidate> all = {
            { 2,  1, /*machineOwned=*/true },   // machine FLTR
            { 50, 1, /*machineOwned=*/false },  // relocated track FLTR
        };

        // Unqualified: machine only — the track extension must NOT be appended.
        CHECK(ids(selectScopeSections(all, /*trackScope=*/false)) == std::vector<int>{ 2 },
              "unqualified shows machine FLTR only, no appended track page");

        // Track scope: the track FLTR block only.
        CHECK(ids(selectScopeSections(all, /*trackScope=*/true)) == std::vector<int>{ 50 },
              "Track scope shows the track FLTR block only");
    }

    static void testMachineDoesNotOwnSection()
    {
        // Bare sampler: FLTR is a track block sitting on the canonical index.
        const std::vector<SecCandidate> all = {
            { 2, 1, /*machineOwned=*/false },   // track FLTR on the canonical index
        };

        // Unqualified: no machine params here → fall back to the track page.
        CHECK(ids(selectScopeSections(all, false)) == std::vector<int>{ 2 },
              "unqualified falls back to the track FLTR when machine owns nothing");
        // Track scope: the track page.
        CHECK(ids(selectScopeSections(all, true)) == std::vector<int>{ 2 },
              "Track scope shows the track FLTR");
    }

    static void testSectionWithNoTrackBlock()
    {
        // SRC/MOD: machine-only, no track-level block exists.
        const std::vector<SecCandidate> all = {
            { 1, 2, /*machineOwned=*/true },   // machine SRC (2 pages)
        };

        // Unqualified: machine SRC.
        CHECK(ids(selectScopeSections(all, false)) == std::vector<int>{ 1 },
              "unqualified shows machine SRC");
        // Track scope: no track block → fall back to machine so the key still works.
        CHECK(ids(selectScopeSections(all, true)) == std::vector<int>{ 1 },
              "Track scope falls back to machine SRC where no track block exists");
    }

    static void testEmpty()
    {
        const std::vector<SecCandidate> all = {};
        CHECK(selectScopeSections(all, false).empty(), "empty stays empty (unqualified)");
        CHECK(selectScopeSections(all, true).empty(), "empty stays empty (track)");
    }

    void runScopeSectionSelectTests()
    {
        testMachineOwnsSection();
        testMachineDoesNotOwnSection();
        testSectionWithNoTrackBlock();
        testEmpty();
    }
}
