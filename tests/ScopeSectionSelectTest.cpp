// P6 + Part 4 uniform overlay: scope-aware section page-list selection.
// Covers the pure decision that drives sectionsForKey(): unqualified fills
// top-down (machine wins, empty sections fall through to the track layer),
// holding a scope peels every layer above it (Track hold hides the machine
// layer), each resolved group keeps its origin scope, and the machine's owning
// of a section suppresses the "extra" appended track page (buried-AMP bug).

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
            { 2,  1, SecOrigin::Machine },   // machine FLTR
            { 50, 1, SecOrigin::Track },     // relocated track FLTR
        };

        // Unqualified: machine only — the track extension must NOT be appended.
        const auto uq = selectScopeSections(all, SecOrigin::Machine);
        CHECK(ids(uq) == std::vector<int>{ 2 },
              "unqualified shows machine FLTR only, no appended track page");
        CHECK(!uq.empty() && uq.front().origin == SecOrigin::Machine,
              "unqualified FLTR resolves to Machine origin");

        // Track scope: the track FLTR block only (machine layer peeled).
        const auto tk = selectScopeSections(all, SecOrigin::Track);
        CHECK(ids(tk) == std::vector<int>{ 50 },
              "Track scope shows the track FLTR block only");
        CHECK(!tk.empty() && tk.front().origin == SecOrigin::Track,
              "Track FLTR resolves to Track origin (drives cyan colour)");
    }

    static void testMachineDoesNotOwnSection()
    {
        // Bare sampler: FLTR is a track block sitting on the canonical index.
        const std::vector<SecCandidate> all = {
            { 2, 1, SecOrigin::Track },   // track FLTR on the canonical index
        };

        // Unqualified: no machine params here → fall through to the track page,
        // which must be tagged Track (so the key reads cyan even unqualified).
        const auto uq = selectScopeSections(all, SecOrigin::Machine);
        CHECK(ids(uq) == std::vector<int>{ 2 },
              "unqualified falls through to the track FLTR when machine owns nothing");
        CHECK(!uq.empty() && uq.front().origin == SecOrigin::Track,
              "fallen-through FLTR keeps Track origin (colours the key cyan)");
        // Track scope: the same track page.
        CHECK(ids(selectScopeSections(all, SecOrigin::Track)) == std::vector<int>{ 2 },
              "Track scope shows the track FLTR");
    }

    static void testSectionWithNoTrackBlock()
    {
        // SRC/MOD: machine-only, no track-level block exists.
        const std::vector<SecCandidate> all = {
            { 1, 2, SecOrigin::Machine },   // machine SRC (2 pages)
        };

        // Unqualified: machine SRC.
        CHECK(ids(selectScopeSections(all, SecOrigin::Machine)) == std::vector<int>{ 1 },
              "unqualified shows machine SRC");
        // Track scope: machine layer peeled and no track block → EMPTY (dim key).
        // This is the strict "track sections only" peel — a change from the old
        // fall-back-to-machine behaviour.
        CHECK(selectScopeSections(all, SecOrigin::Track).empty(),
              "Track scope on a machine-only section is empty (dim, machine peeled)");
    }

    static void testEmpty()
    {
        const std::vector<SecCandidate> all = {};
        CHECK(selectScopeSections(all, SecOrigin::Machine).empty(),
              "empty stays empty (unqualified)");
        CHECK(selectScopeSections(all, SecOrigin::Track).empty(),
              "empty stays empty (track)");
    }

    void runScopeSectionSelectTests()
    {
        testMachineOwnsSection();
        testMachineDoesNotOwnSection();
        testSectionWithNoTrackBlock();
        testEmpty();
    }
}
