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

    static void testResolveMetaBandMasterSectionOutranksAll()
    {
        UiState ui;
        ui.masterSection = 1;
        ui.euclidHeld = true;
        ui.densityStickyMode = true;
        ui.songHeld = true;
        // masterSection always wins
        CHECK(resolveMetaBand(ui) == MetaBand::Trig, "masterSection outranks euclidHeld + song + sticky");
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
        CHECK(resolveMetaBand(ui) == MetaBand::Density, "sticky Amount → Density");
        ui.densitySubPage = UiState::DensitySubPage::Mode;
        CHECK(resolveMetaBand(ui) == MetaBand::DensityMode, "sticky Mode → DensityMode");
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

    void runMetaBandTests()
    {
        testResolveMetaBandMasterSection();
        testResolveMetaBandMasterSectionOutranksAll();
        testResolveMetaBandEuclid();
        testResolveMetaBandDensitySticky();
        testResolveMetaBandFuncSong();
        testResolveMetaBandSwing();
        testResolveMetaBandFuncAlone();
        testResolveMetaBandNone();

        testDensityEditsMaster();

        testDensityWriteTargetMaster();
        testDensityWriteTargetPage0();
        testDensityWriteTargetPage1();
        testDensityWriteTargetStickyBank();

        testWriteMetaFieldDensityPerTrack();
        testWriteMetaFieldDensityMasterIsGuardedNoOp();

        testMetaRotaryApplyViewTotality();
    }
}
