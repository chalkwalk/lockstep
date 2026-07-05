// Item 7 — the section-key resolver (SectionResolve). Drives the editor
// dispatch, the section-bar painter, and the MZ banner from one decision, so
// they cannot diverge. These tests run it against a real machine schema via the
// engine harness and assert the frozen scope→key mappings.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/SectionResolve.h"
#include "../src/ui/KeyLabel.h"  // originWord / originColour (7d)
#include "../src/machine/AnalogMachine.h"
#include "../src/machine/IMachine.h"

#include <set>
#include <string>

namespace lockstep
{
    // Find a canonical section (1..4) the machine itself owns (first slot inside
    // its param range). Returns -1 if none — the assertions guard on that.
    static int firstMachineOwnedKey(const LockstepProcessor& proc, int track)
    {
        const int mnp = proc.numParams(track);
        for (int k = 1; k < IMachine::kMaxSections; ++k)
        {
            const auto info = proc.section(track, k);
            if (info.firstSlot >= 0 && info.firstSlot < mnp)
                return k;
        }
        return -1;
    }

    static void testSectionResolve()
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.setTrackMachine(0, AnalogMachine::kMachineId);  // a synth: owns SRC/FILTER/…

        using O = SecOrigin;
        using A = SecAction;

        // --- Unqualified: a machine-owned section resolves to a Machine param page.
        const int owned = firstMachineOwnedKey(proc, 0);
        CHECK(owned >= 0, "synth owns at least one canonical section");
        if (owned >= 0)
        {
            const auto r = resolveSectionKey(proc, 0, owned, O::Machine, false);
            CHECK(r.hasContent && r.winner == O::Machine && r.action == A::ParamSection,
                  "unqualified owned section → Machine param page");
        }

        // --- No machine owns TRIG (section 0), so the Machine layer is empty there
        // and the underlay falls through to the Track layer's DIV meta: bare TRIG
        // shows DIV (the whole point of the underlay — no wasted real estate).
        CHECK(proc.section(0, 0).firstSlot < 0, "machine owns no TRIG params (precondition)");
        {
            const auto r = resolveSectionKey(proc, 0, 0, O::Machine, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 3
                      && r.winner == O::Track,
                  "bare TRIG falls through to the Track underlay → DIV (winner Track)");
        }

        // --- Track+TRIG → DIV meta (3).
        {
            const auto r = resolveSectionKey(proc, 0, 0, O::Track, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 3
                      && r.winner == O::Track,
                  "Track+TRIG → DIV meta (3)");
        }
        // --- Phrase+TRIG → LEN meta (4).
        {
            const auto r = resolveSectionKey(proc, 0, 0, O::Phrase, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 4,
                  "Phrase+TRIG → LEN meta (4)");
        }
        // --- Scene+TRIG and Song+TRIG → TIME sticky.
        CHECK(resolveSectionKey(proc, 0, 0, O::Scene, false).action == A::TimeSticky,
              "Scene+TRIG → TIME sticky");
        CHECK(resolveSectionKey(proc, 0, 0, O::Song, false).action == A::TimeSticky,
              "Song+TRIG → TIME sticky");

        // --- Song+FX → master FX meta (5).
        {
            const auto r = resolveSectionKey(proc, 0, IMachine::kMaxSections - 1, O::Song, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 5
                      && r.winner == O::Song,
                  "Song+FX → master FX meta (5)");
        }

        // --- Track+SRC on a synth (SRC machine-owned, no track SRC block) → dim.
        if (proc.section(0, 1).firstSlot >= 0 && proc.section(0, 1).firstSlot < proc.numParams(0))
        {
            CHECK(!resolveSectionKey(proc, 0, 1, O::Track, false).hasContent,
                  "Track+SRC on a synth → dim (machine SRC peeled, no track block)");
        }

        // --- Scene is a write-target overlay, NOT a peel: FILTER stays editable
        // (machine params, scene-scoped write) and is coloured by the Scene scope.
        // Regression guard — the naive "peel everything above the floor" resolver
        // dimmed this (the plan's audit point).
        if (proc.section(0, 2).firstSlot >= 0)  // machine owns FILTER
        {
            const auto r = resolveSectionKey(proc, 0, 2, O::Scene, false);
            CHECK(r.hasContent && r.action == A::ParamSection && r.winner == O::Scene,
                  "Scene+FILTER → machine param page, scene-coloured (not dimmed)");
        }
        // Song does not scope-edit FILTER (per-scope policy) → dim.
        CHECK(!resolveSectionKey(proc, 0, 2, O::Song, false).hasContent,
              "Song+FILTER → dim (not a Song-scoped param)");

        // --- Downward fall-through below the ceiling: Scene has no FX content, so
        // the FX key falls through to the Song layer's master FX meta.
        {
            const auto r = resolveSectionKey(proc, 0, IMachine::kMaxSections - 1,
                                             O::Scene, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 5
                      && r.winner == O::Song,
                  "Scene+FX falls through to the Song master-FX underlay");
        }

        // --- Func layer: parallel hierarchy. Func TRIG → COND, Func FILTER → TRSP.
        {
            const auto cond = resolveSectionKey(proc, 0, 0, O::Machine, /*funcLayer*/ true);
            CHECK(cond.hasContent && cond.action == A::MetaSection && cond.metaIndex == 0,
                  "Func+TRIG → COND meta (0)");
            const auto trsp = resolveSectionKey(proc, 0, 2, O::Machine, /*funcLayer*/ true);
            CHECK(trsp.hasContent && trsp.action == A::MetaSection && trsp.metaIndex == 2,
                  "Func+FILTER → TRSP meta (2)");
            // Func released: the same TRIG key resolves in the primary hierarchy
            // (DIV via the underlay, metaIndex 3) — NOT the func COND meta (0).
            const auto rel = resolveSectionKey(proc, 0, 0, O::Machine, /*funcLayer*/ false);
            CHECK(rel.hasContent && rel.metaIndex == 3,
                  "Func released → primary hierarchy DIV (not func COND)");
        }

        // --- sectionFloorForScope mapping.
        using PS = EditMode::PrimaryScope;
        CHECK(sectionFloorForScope(PS::None) == O::Machine, "None → Machine floor");
        CHECK(sectionFloorForScope(PS::Track) == O::Track, "Track → Track floor");
        CHECK(sectionFloorForScope(PS::Phrase) == O::Phrase, "Phrase → Phrase floor");
        CHECK(sectionFloorForScope(PS::Scene) == O::Scene, "Scene → Scene floor");
        CHECK(sectionFloorForScope(PS::Song) == O::Song, "Song → Song floor");
    }

    // Item 7 (7d): the page-origin banner helpers are total and distinct.
    static void testOriginHelpers()
    {
        const SecOrigin all[] = { SecOrigin::Machine, SecOrigin::Track, SecOrigin::Phrase,
                                  SecOrigin::Scene, SecOrigin::Song, SecOrigin::Global };
        std::set<std::string> words;
        for (auto o : all)
        {
            const char* w = originWord(o);
            CHECK(w != nullptr && w[0] != '\0', "every origin has a non-empty banner word");
            words.insert(w);
        }
        // Machine..Song are distinct words; Global reuses the Song *colour* but
        // keeps its own word.
        CHECK(words.size() == 6, "banner words are distinct per origin");
        CHECK(originColour(SecOrigin::Machine).getARGB()
                  != originColour(SecOrigin::Track).getARGB(),
              "Machine and Track colours differ");
        CHECK(originColour(SecOrigin::Global).getARGB()
                  == originColour(SecOrigin::Song).getARGB(),
              "Global reuses the Song hue (transport globals under the Song umbrella)");
    }

    void runSectionResolveTests()
    {
        testSectionResolve();
        testOriginHelpers();
    }
}
