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
        // Track scope: machine layer peeled and nothing at/below the floor →
        // EMPTY (dim key). Not a "strict track-only" rule any more (Item 7) — the
        // key is dim only because no lower layer owns content here.
        CHECK(selectScopeSections(all, SecOrigin::Track).empty(),
              "Track scope on a machine-only section is dim (nothing at/below floor)");
    }

    static void testFallThroughBelowFloor()
    {
        // Item 7: holding a scope is a *floor* — peel above, fall through below.
        // Key with a machine page + a Phrase meta page (LEN), no Track page.
        const std::vector<SecCandidate> all = {
            { 0, 1, SecOrigin::Machine },   // machine TRIG params
            { 0, 1, SecOrigin::Phrase, SecAction::MetaSection, 4, false },  // Phrase LEN
        };

        // Hold Track (floor=1): Machine peeled; no Track layer → fall through to
        // Phrase LEN below the floor (old strict-dim rule would have gone dim).
        const auto tk = selectScopeSections(all, SecOrigin::Track);
        CHECK(!tk.empty() && tk.front().origin == SecOrigin::Phrase
                  && tk.front().action == SecAction::MetaSection && tk.front().metaIndex == 4,
              "Track floor with no Track layer falls through to Phrase LEN");

        // Unqualified: Machine wins (highest precedence).
        const auto uq = selectScopeSections(all, SecOrigin::Machine);
        CHECK(!uq.empty() && uq.front().origin == SecOrigin::Machine
                  && uq.front().action == SecAction::ParamSection,
              "unqualified shows the machine TRIG page, not the Phrase meta");
    }

    static void testFuncHierarchyFilter()
    {
        // The Func layer is a parallel hierarchy: funcQualified rows are visible
        // only when funcLayer matches. TRIG key with a machine page (primary) and
        // a Func COND meta (func-qualified).
        const std::vector<SecCandidate> all = {
            { 0, 1, SecOrigin::Machine },   // primary machine TRIG params
            { 0, 1, SecOrigin::Machine, SecAction::MetaSection, 0, true },  // Func COND
        };

        // Func released: only the primary page survives.
        const auto primary = selectScopeSections(all, SecOrigin::Machine, /*funcLayer*/ false);
        CHECK(primary.size() == 1 && primary.front().action == SecAction::ParamSection,
              "Func released shows the primary machine page only");

        // Func held: only the func-qualified COND meta survives.
        const auto func = selectScopeSections(all, SecOrigin::Machine, /*funcLayer*/ true);
        CHECK(func.size() == 1 && func.front().action == SecAction::MetaSection
                  && func.front().metaIndex == 0,
              "Func held shows the COND meta only (primary filtered out)");

        // A key with no func-qualified row is dim under Func.
        const std::vector<SecCandidate> noFunc = { { 4, 1, SecOrigin::Machine } };
        CHECK(selectScopeSections(noFunc, SecOrigin::Machine, /*funcLayer*/ true).empty(),
              "Func held dims a key with no func-qualified candidate");
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
        testFallThroughBelowFloor();
        testFuncHierarchyFilter();
        testEmpty();
    }
}
