// MetaBandTest — unit tests for the density routing SSOT and MetaRotary::applyView totality.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/MetaBand.h"
#include "../src/ui/MetaRotary.h"
#include "../src/state/UiState.h"
#include "../src/io/EditContext.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // resolveMetaBand precedence table

    static void testResolveMetaBandMasterSection()
    {
        UiState ui;
        ui.masterSection = 0;  CHECK(resolveMetaBand(ui) == MetaBand::Cond,      "masterSection 0 → Cond");
        ui.masterSection = 1;  CHECK(resolveMetaBand(ui) == MetaBand::Trig,      "masterSection 1 → Trig");
        ui.masterSection = 2;  CHECK(resolveMetaBand(ui) == MetaBand::Transport, "masterSection 2 → Transport");
        ui.masterSection = 3;  CHECK(resolveMetaBand(ui) == MetaBand::Divider,   "masterSection 3 → Divider");
        ui.masterSection = 4;  CHECK(resolveMetaBand(ui) == MetaBand::PhraseLen, "masterSection 4 → PhraseLen");
        ui.masterSection = 5;  CHECK(resolveMetaBand(ui) == MetaBand::Global,    "masterSection 5 → Global");
    }

    static void testResolveMetaBandTransientOutranksMasterSection()
    {
        // Transient overlays outrank the latched masterSection page so that
        // arming euclid (or holding a modifier) over a latched DIV/LEN page
        // updates the band immediately instead of going stale.
        {
            UiState ui;
            ui.masterSection = 1;        // latched Trig page
            ui.euclidHeld = true;        // but euclid is armed
            CHECK(resolveMetaBand(ui) == MetaBand::Euclidean,
                  "euclidHeld outranks latched masterSection");
        }
        {
            UiState ui;
            ui.masterSection = 3;        // latched Divider page
            ui.densityStickyMode = true;
            ui.densitySubPage = UiState::DensitySubPage::Amount;
            CHECK(resolveMetaBand(ui) == MetaBand::Density,
                  "density sticky outranks latched masterSection");
        }
        {
            UiState ui;
            ui.masterSection = 4;        // latched PhraseLen page
            ui.funcHeld = true;
            ui.songHeld = true;
            CHECK(resolveMetaBand(ui) == MetaBand::Density,
                  "Func+Song outranks latched masterSection");
        }
        {
            // With nothing transient active, the latched page still resolves.
            UiState ui;
            ui.masterSection = 1;
            CHECK(resolveMetaBand(ui) == MetaBand::Trig,
                  "latched masterSection resolves when no transient is active");
        }
    }

    static void testResolveMetaBandEuclid()
    {
        UiState ui;
        ui.euclidHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Euclidean, "euclidHeld → Euclidean");
    }

    static void testResolveMetaBandDensitySticky()
    {
        UiState ui;
        ui.densityStickyMode = true;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(resolveMetaBand(ui) == MetaBand::Density,         "sticky Amount → Density");
        ui.densitySubPage = UiState::DensitySubPage::Musicality;
        CHECK(resolveMetaBand(ui) == MetaBand::DensityMode,     "sticky Musicality → DensityMode");
        ui.densitySubPage = UiState::DensitySubPage::Selection;
        CHECK(resolveMetaBand(ui) == MetaBand::DensitySelection, "sticky Selection → DensitySelection");
    }

    static void testResolveMetaBandFuncSong()
    {
        UiState ui;
        ui.funcHeld = true;
        ui.songHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Density, "Func+Song → Density");
    }

    static void testResolveMetaBandSwing()
    {
        UiState ui;
        ui.songHeld = true;
        ui.swingDismissed = false;
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "Song alone + !dismissed → Swing");

        ui.swingDismissed = true;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "Song + dismissed → None");
    }

    static void testResolveMetaBandFuncAlone()
    {
        UiState ui;
        ui.funcHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Density, "Func alone → Density");
    }

    static void testResolveMetaBandNone()
    {
        UiState ui;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "all-false → None");
    }

    // -------------------------------------------------------------------------
    // densityEditsMaster — pure routing predicate

    static void testDensityEditsMaster()
    {
        UiState ui;
        CHECK(!densityEditsMaster(ui), "no flags → per-track");
        ui.songHeld = true;
        CHECK(densityEditsMaster(ui), "songHeld → master");
        ui.funcHeld = true;
        CHECK(densityEditsMaster(ui), "songHeld+funcHeld → master");
        ui.songHeld = false;
        CHECK(!densityEditsMaster(ui), "only funcHeld → per-track");
    }

    // -------------------------------------------------------------------------
    // densityWriteTarget — paging formula

    static void testDensityWriteTargetMaster()
    {
        UiState ui;
        ui.songHeld = true;
        auto t = densityWriteTarget(ui, 3, 0);
        CHECK(t.master, "songHeld → master");
        CHECK(t.trackIdx == -1, "master target has trackIdx -1");
    }

    static void testDensityWriteTargetPage0()
    {
        UiState ui;
        // focusedTrack 0-7 → page 0
        auto t = densityWriteTarget(ui, 2, 5);
        CHECK(!t.master, "no song → per-track");
        CHECK(t.trackIdx == 2, "page0 field2 → track 2");
    }

    static void testDensityWriteTargetPage1()
    {
        UiState ui;
        // focusedTrack >= 8 → page 1
        auto t = densityWriteTarget(ui, 3, 8);
        CHECK(!t.master, "no song, track8 → per-track");
        CHECK(t.trackIdx == 11, "page1 field3 → track 11");
    }

    static void testDensityWriteTargetStickyBank()
    {
        UiState ui;
        ui.densityStickyMode = true;
        ui.densityBank = 1;
        auto t = densityWriteTarget(ui, 0, 0);  // focusedTrack 0 would be page0, but sticky overrides
        CHECK(!t.master, "sticky, no song → per-track");
        CHECK(t.trackIdx == 8, "sticky bank1 field0 → track 8");
    }

    // -------------------------------------------------------------------------
    // sectionSelectClearsDensitySticky — focus-change supersede policy

    static void testSectionSelectClearsDensitySticky()
    {
        UiState ui;

        // Not in sticky mode: predicate always false.
        ui.densityStickyMode = false;
        for (int i = 0; i <= 5; ++i)
            CHECK(!sectionSelectClearsDensitySticky(ui, i), "not sticky → false for all sections");

        // In sticky mode: every section except MOD (4) supersedes; MOD cycles sub-page.
        ui.densityStickyMode = true;
        for (int i = 0; i <= 5; ++i)
            if (i != 4)
                CHECK(sectionSelectClearsDensitySticky(ui, i), "sticky + non-MOD section → true");
        CHECK(!sectionSelectClearsDensitySticky(ui, 4), "sticky + section 4 → false (MOD cycles subpage)");

        // Sequenced: predicate true → escape clears mode → resolveMetaBand returns None.
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(sectionSelectClearsDensitySticky(ui, 2), "pre-escape predicate fires");
        ui.densityStickyMode = false;  // simulate escapeDensitySticky
        ui.densityBank = 0;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "post-escape → MetaBand::None");

        // Sequenced: section 4 (MOD) does not supersede → mode persists → still a Density* band.
        ui.densityStickyMode = true;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(!sectionSelectClearsDensitySticky(ui, 4), "section 4 (MOD) doesn't clear sticky");
        CHECK(resolveMetaBand(ui) == MetaBand::Density, "mode still active → Density band");
    }

    // -------------------------------------------------------------------------
    // Per-track side-effect via EngineHarness

    static void testWriteMetaFieldDensityPerTrack()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Sanity: master starts at 0
        CHECK(feq(proc.masterDensity(), 0.0f), "masterDensity starts at 0");

        UiState ui;  // songHeld=false
        EditContext ctx;
        const int focusedTrack = 0;

        // Write field 0 to 0.5 (50 on the 0-100 scale)
        writeMetaField(MetaBand::Density, 0, 0, 50.0f, proc, focusedTrack, ctx, ui);

        CHECK(feq(proc.trackDensity(0), 0.5f), "writeMetaField Density sets track 0");
        CHECK(feq(proc.masterDensity(), 0.0f), "masterDensity unchanged after per-track write");
    }

    static void testWriteMetaFieldDensityMasterIsGuardedNoOp()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.songHeld = true;  // master mode
        EditContext ctx;

        const float trackBefore = proc.trackDensity(0);
        const float masterBefore = proc.masterDensity();

        // Should be a no-op: master writes are handled upstream by the mouse delta
        // tracker or the encoder rawDelta path, not by writeMetaField
        writeMetaField(MetaBand::Density, 0, 0, 75.0f, proc, 0, ctx, ui);

        CHECK(feq(proc.trackDensity(0), trackBefore), "master guard: track unchanged");
        CHECK(feq(proc.masterDensity(), masterBefore), "master guard: master unchanged");
    }

    // -------------------------------------------------------------------------
    // MetaRotary::applyView totality — guards the bf20ac3 leak class

    static void testMetaRotaryApplyViewTotality()
    {
        MetaRotary mr;

        // Apply a fully-loaded view
        MetaRotary::View v;
        v.rangeLo = 10.0;
        v.rangeHi = 200.0;
        v.interval = 1.0;
        v.skew = 2.0;
        v.doubleClickEnabled = true;
        v.doubleClickValue = 50.0;
        v.value = 77.0;
        v.enabled = false;
        v.alpha = 0.4f;
        v.ringMode = RingMode::BipolarFromCentre;
        v.marks[0] = { true, 0.25f, 0xff00ff00, 1.0f };
        v.marks[1] = { true, 0.75f, 0xffff0000, 0.8f };
        v.densityCell = true;
        v.densityMasterOffset = 0.3f;
        v.densityEffective = 0.7f;
        mr.applyView(v);

        CHECK(feq(static_cast<float>(mr.getMinimum()), 10.0f),  "applyView: rangeLo");
        CHECK(feq(static_cast<float>(mr.getMaximum()), 200.0f), "applyView: rangeHi");
        CHECK(feq(static_cast<float>(mr.getValue()), 77.0f),    "applyView: value");
        CHECK(!mr.isEnabled(),                                   "applyView: enabled=false");
        CHECK(feq(mr.getAlpha(), 0.4f),                         "applyView: alpha");
        CHECK(mr.getRingMode() == RingMode::BipolarFromCentre,  "applyView: ringMode");
        CHECK(mr.getMarks()[0].present,                          "applyView: marks[0].present");
        CHECK(mr.isDensityCell(),                                "applyView: densityCell");
        CHECK(feq(mr.getDensityMasterOffset(), 0.3f),           "applyView: densityMasterOffset");
        CHECK(feq(mr.getDensityEffective(), 0.7f),              "applyView: densityEffective");

        // Apply a default (zeroed) view — every field must reset
        mr.applyView(MetaRotary::View{});

        CHECK(feq(static_cast<float>(mr.getMinimum()), 0.0f),   "reset: rangeLo");
        CHECK(feq(static_cast<float>(mr.getMaximum()), 1.0f),   "reset: rangeHi");
        CHECK(feq(static_cast<float>(mr.getValue()), 0.0f),     "reset: value");
        CHECK(mr.isEnabled(),                                    "reset: enabled=true");
        CHECK(feq(mr.getAlpha(), 1.0f),                         "reset: alpha");
        CHECK(mr.getRingMode() == RingMode::UnipolarFill,        "reset: ringMode");
        CHECK(!mr.getMarks()[0].present,                         "reset: marks[0] cleared");
        CHECK(!mr.isDensityCell(),                               "reset: densityCell=false");
        CHECK(feq(mr.getDensityMasterOffset(), 0.0f),           "reset: densityMasterOffset");
        CHECK(feq(mr.getDensityEffective(), 1.0f),              "reset: densityEffective");
    }

    // -------------------------------------------------------------------------
    // Tempo + TimeSig sticky mode: resolveMetaBand precedence

    static void testResolveMetaBandTempoTimeSig()
    {
        // timeSigStickyMode outranks everything except euclidHeld.
        {
            UiState ui;
            ui.timeSigStickyMode = true;
            CHECK(resolveMetaBand(ui) == MetaBand::TimeSig, "timeSigStickyMode → TimeSig");

            // outranks swing
            ui.songHeld = true;
            ui.swingDismissed = false;
            CHECK(resolveMetaBand(ui) == MetaBand::TimeSig, "timeSigStickyMode outranks swing");

            // outranks density sticky
            ui.densityStickyMode = true;
            CHECK(resolveMetaBand(ui) == MetaBand::TimeSig, "timeSigStickyMode outranks density sticky");

            // euclid outranks time-sig
            ui.euclidHeld = true;
            CHECK(resolveMetaBand(ui) == MetaBand::Euclidean, "euclidHeld outranks timeSigStickyMode");
        }
        // tempoStickyMode outranks swing and density; yields to timeSigStickyMode.
        {
            UiState ui;
            ui.tempoStickyMode = true;
            CHECK(resolveMetaBand(ui) == MetaBand::Tempo, "tempoStickyMode → Tempo");

            ui.songHeld = true;
            ui.swingDismissed = false;
            CHECK(resolveMetaBand(ui) == MetaBand::Tempo, "tempoStickyMode outranks swing");

            // timeSig sticky outranks tempo sticky
            ui.timeSigStickyMode = true;
            CHECK(resolveMetaBand(ui) == MetaBand::TimeSig, "timeSigStickyMode outranks tempoStickyMode");
        }
    }

    // -------------------------------------------------------------------------
    // timeSigScopeFor / tempoScopeFor — modifier routing + entry-scope fallback

    static void testTimeSigScopeFor()
    {
        // Func+Song = Set (1), Song = Song (2), Scene = Scene (3).
        {
            UiState ui;
            ui.funcHeld = true;
            ui.songHeld = true;
            CHECK(timeSigScopeFor(ui) == 1, "Func+Song → Set (1)");
        }
        {
            UiState ui;
            ui.songHeld = true;
            CHECK(timeSigScopeFor(ui) == 2, "Song alone → Song (2)");
        }
        {
            UiState ui;
            ui.sceneHeld = true;
            CHECK(timeSigScopeFor(ui) == 3, "Scene alone → Scene (3)");
        }
        // No modifier: returns entry scope, never 0 (scope-0 regression guard).
        {
            UiState ui;
            ui.timeSigEntryScope = 3;  // default
            const int s = timeSigScopeFor(ui);
            CHECK(s != 0, "no modifier → never returns 0 (scope-0 regression)");
            CHECK(s == 3, "no modifier → entry scope (3)");
        }
        {
            UiState ui;
            ui.timeSigEntryScope = 2;  // hypothetical Song entry
            const int s = timeSigScopeFor(ui);
            CHECK(s == 2, "no modifier → respects explicit entry scope");
        }
    }

    static void testTempoScopeFor()
    {
        {
            UiState ui;
            ui.funcHeld = true;
            ui.songHeld = true;
            CHECK(tempoScopeFor(ui) == 1, "Func+Song → global (1)");
        }
        {
            UiState ui;
            ui.songHeld = true;
            CHECK(tempoScopeFor(ui) == 2, "Song alone → Song (2)");
        }
        {
            UiState ui;
            ui.sceneHeld = true;
            CHECK(tempoScopeFor(ui) == 3, "Scene alone → Scene (3)");
        }
        // No modifier: returns entry scope, never 0.
        {
            UiState ui;
            ui.tempoEntryScope = 2;  // default
            const int s = tempoScopeFor(ui);
            CHECK(s != 0, "no modifier → never returns 0 (scope-0 regression)");
            CHECK(s == 2, "no modifier → entry scope (2)");
        }
    }

    // -------------------------------------------------------------------------
    // buildMetaBand Tempo / TimeSig: single active field, no dead field 1

    static void testTempoBandSingleField()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.tempoStickyMode = true;
        ui.tempoEntryScope = 2;
        EditContext ctx;

        const auto fields = buildMetaBand(MetaBand::Tempo, 0, proc, 0, ctx, ui);
        CHECK(fields[0].active,   "Tempo band: field 0 is active");
        CHECK(fields[0].writable, "Tempo band: field 0 is writable");
        CHECK(!fields[1].active,  "Tempo band: field 1 is inactive (no dead Effct knob)");
    }

    static void testTimeSigBandSingleField()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.timeSigStickyMode = true;
        ui.timeSigEntryScope = 3;
        EditContext ctx;

        const auto fields = buildMetaBand(MetaBand::TimeSig, 0, proc, 0, ctx, ui);
        CHECK(fields[0].active,   "TimeSig band: field 0 is active");
        CHECK(fields[0].writable, "TimeSig band: field 0 is writable");
        CHECK(!fields[1].active,  "TimeSig band: field 1 is inactive (no dead Effct knob)");
    }

    // -------------------------------------------------------------------------
    // writeMetaField Tempo: write round-trip, entry scope defaults to Song

    static void testWriteMetaFieldTempoSongScope()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Sanity: Song starts with no tempo override.
        CHECK(!proc.song().hasTempo, "song.hasTempo starts false");

        UiState ui;
        ui.tempoStickyMode = true;
        ui.tempoEntryScope = 2;  // Song
        ui.songHeld = true;      // explicit Song scope
        EditContext ctx;

        const double globalBpm = proc.clock().bpm();
        const float targetBpm = static_cast<float>(globalBpm) * 0.75f;

        writeMetaField(MetaBand::Tempo, 0, 0, targetBpm, proc, 0, ctx, ui);

        CHECK(proc.song().hasTempo, "after write: song.hasTempo=true");
        const double expectedRatio = static_cast<double>(targetBpm) / globalBpm;
        const double storedRatio = proc.song().tempoRatio;
        CHECK(std::abs(storedRatio - expectedRatio) < 1e-6,
              "stored ratio = targetBpm / globalBpm");
    }

    static void testWriteMetaFieldTempoEntryScope()
    {
        // No modifier held: write should target entry scope (Song=2), not Scene.
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.tempoStickyMode = true;
        ui.tempoEntryScope = 2;  // Song entry
        // No songHeld / sceneHeld
        EditContext ctx;

        const float targetBpm = 100.0f;

        writeMetaField(MetaBand::Tempo, 0, 0, targetBpm, proc, 0, ctx, ui);

        CHECK(proc.song().hasTempo, "entry-scope write sets song.hasTempo (not scene)");
        CHECK(!proc.section().hasTempo, "entry-scope write does NOT touch section.hasTempo");
    }

    // -------------------------------------------------------------------------
    // writeMetaField TimeSig: INHERIT (idx 0) clears override

    static void testWriteMetaFieldTimeSigInheritClearsOverride()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Set a Song time-sig override.
        proc.song().hasTimeSig = true;
        proc.song().timeSig.numerator = 3;
        proc.song().timeSig.denominator = 4;

        UiState ui;
        ui.timeSigStickyMode = true;
        ui.timeSigEntryScope = 2;  // Song
        ui.songHeld = true;
        EditContext ctx;

        // Write idx=0 (INHERIT) to Song scope.
        writeMetaField(MetaBand::TimeSig, 0, 0, 0.0f, proc, 0, ctx, ui);

        CHECK(!proc.song().hasTimeSig, "INHERIT (idx 0) clears song.hasTimeSig");
    }

    static void testWriteMetaFieldTimeSigSetsValue()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.timeSigStickyMode = true;
        ui.timeSigEntryScope = 3;  // Scene
        ui.sceneHeld = true;
        EditContext ctx;

        // Write idx=2 at Scene scope (idx 1 = first curated entry at Song/Scene, idx 0 = INHERIT)
        // kTimeSigs[0] is 4/4 at Set scope; at Song/Scene INHERIT is 0, 4/4 is idx 1.
        writeMetaField(MetaBand::TimeSig, 0, 0, 1.0f, proc, 0, ctx, ui);

        CHECK(proc.section().hasTimeSig, "write at Scene: hasTimeSig=true");
    }

    // -------------------------------------------------------------------------

    void runMetaBandTests()
    {
        testResolveMetaBandMasterSection();
        testResolveMetaBandTransientOutranksMasterSection();
        testResolveMetaBandEuclid();
        testResolveMetaBandDensitySticky();
        testResolveMetaBandFuncSong();
        testResolveMetaBandSwing();
        testResolveMetaBandFuncAlone();
        testResolveMetaBandNone();

        // Tempo + TimeSig sticky mode precedence + scope routing
        testResolveMetaBandTempoTimeSig();
        testTimeSigScopeFor();
        testTempoScopeFor();

        // Single-knob bands (no dead field 1)
        testTempoBandSingleField();
        testTimeSigBandSingleField();

        // Write round-trips + entry-scope correctness
        testWriteMetaFieldTempoSongScope();
        testWriteMetaFieldTempoEntryScope();
        testWriteMetaFieldTimeSigInheritClearsOverride();
        testWriteMetaFieldTimeSigSetsValue();

        testDensityEditsMaster();

        testDensityWriteTargetMaster();
        testDensityWriteTargetPage0();
        testDensityWriteTargetPage1();
        testDensityWriteTargetStickyBank();

        testSectionSelectClearsDensitySticky();

        testWriteMetaFieldDensityPerTrack();
        testWriteMetaFieldDensityMasterIsGuardedNoOp();

        testMetaRotaryApplyViewTotality();
    }
}
