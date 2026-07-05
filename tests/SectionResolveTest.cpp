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

        // --- Track+SRC on a synth: SRC has only a machine layer (no track SRC
        // block, nothing deeper). Under NEAREST it falls *up* one step to the
        // machine SRC page — coloured MACHINE. SRC/MOD "stay machine-coloured under
        // every hold" (the pedagogy): no deeper layer exists to teach.
        if (proc.section(0, 1).firstSlot >= 0 && proc.section(0, 1).firstSlot < proc.numParams(0))
        {
            const auto r = resolveSectionKey(proc, 0, 1, O::Track, false);
            CHECK(r.hasContent && r.action == A::ParamSection && r.winner == O::Machine,
                  "Track+SRC on a synth → machine SRC (falls up; machine-coloured)");
        }

        // --- Scene+FILTER: there is NO per-scope param layer (OEB: step-override
        // ELSE track-base). Holding Scene does NOT create a scene filter — it only
        // sets the ceiling. FILTER's only real content here is the machine's own
        // filter page (this synth owns its filter), so the key falls *up* to it →
        // winner MACHINE (machine-coloured). The point is it is coloured by a REAL
        // layer, never the fictional Scene-green the shipped 9.20 pinned here.
        // (When a machine that leaves its filter to a track-DSP block ships, the
        //  same NEAREST rule lands on Track — no code change needed.)
        if (proc.section(0, 2).firstSlot >= 0)  // machine owns FILTER
        {
            const auto r = resolveSectionKey(proc, 0, 2, O::Scene, false);
            CHECK(r.hasContent && r.action == A::ParamSection && r.winner != O::Scene,
                  "Scene+FILTER → a real filter layer, NOT scene-coloured (fiction dead)");
            CHECK(r.winner == O::Machine,
                  "Scene+FILTER on a synth that owns its filter → machine filter (falls up)");
        }
        // Song+FILTER: falls all the way up to the machine filter (nearest real
        // layer, dist 4) — not dim, not song-coloured. Proves NEAREST falls *up*.
        {
            const auto r = resolveSectionKey(proc, 0, 2, O::Song, false);
            CHECK(r.hasContent && r.action == A::ParamSection && r.winner == O::Machine,
                  "Song+FILTER → machine filter (falls up to the nearest real layer)");
        }

        // --- Scene+FX: FX (key 5) has no machine/track param page on this synth; the
        // only real content is the Song master-FX meta. It falls *down* to it →
        // winner Song (masterFX). Proves NEAREST falls down too.
        {
            const auto r = resolveSectionKey(proc, 0, IMachine::kMaxSections - 1,
                                             O::Scene, false);
            CHECK(r.hasContent && r.action == A::MetaSection && r.metaIndex == 5
                      && r.winner == O::Song,
                  "Scene+FX → Song master-FX (falls down; winner Song)");
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
