// ModeReducerTest — CUJ event-sequence tests for the overlay reducer.
//
// Tests drive the same reduce() path the editor uses, so exit-wire gaps that
// used to ship silently (e.g. the TIME "too sticky" Stage-0 bug) are caught
// here before they reach a build.
//
// Each test is a critical-user-journey (CUJ) sequence:
//   enter overlay → apply event → assert result + UiState.

#include "TestHarness.h"
#include "../src/ui/mode/ModeReducer.h"
#include "../src/state/UiState.h"

namespace lockstep
{
    // =========================================================================
    // Helpers
    // =========================================================================

    // Arm a specific overlay via the UiState fields (simulating the editor's
    // entry path, which is not yet through the reducer).
    static void enterDensity(UiState& ui)
    {
        ui.overlay = Overlay::Density;
        ui.densityBank = 0;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
    }

    static void enterVel(UiState& ui)
    {
        ui.overlay = Overlay::Vel;
        ui.velBank = 0;
        ui.velSubPage = UiState::VelSubPage::Depth;
    }

    static void enterTime(UiState& ui)
    {
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;
        ui.swingDismissed = false;
    }

    static void enterEuclid(UiState& ui)
    {
        ui.euclidHeld = true;
        ui.euclidPulses = 4;
        ui.euclidOffset = 0;
        ui.euclidAccents = 0;
    }

    // =========================================================================
    // activeOverlay
    // =========================================================================

    static void testActiveOverlayValues()
    {
        // Each overlay maps to its enum value; mutual exclusion is now structural.
        UiState ui;
        CHECK(activeOverlay(ui) == Overlay::None,    "default → None");

        ui.overlay = Overlay::Density;
        CHECK(activeOverlay(ui) == Overlay::Density, "overlay field Density");

        ui.overlay = Overlay::Vel;
        CHECK(activeOverlay(ui) == Overlay::Vel,     "overlay field Vel");

        ui.overlay = Overlay::Time;
        CHECK(activeOverlay(ui) == Overlay::Time,    "overlay field Time");

        // euclidHeld is transient and takes priority over the overlay field.
        ui.overlay = Overlay::Time;
        ui.euclidHeld = true;
        CHECK(activeOverlay(ui) == Overlay::Euclid,  "euclidHeld outranks overlay field");

        ui.euclidHeld = false;
        ui.overlay = Overlay::None;
    }

    // =========================================================================
    // escapeOverlay
    // =========================================================================

    static void testEscapeOverlayClearsDensity()
    {
        UiState ui;
        enterDensity(ui);
        ui.densitySubPage = UiState::DensitySubPage::Musicality;
        ui.densityBank = 1;

        escapeOverlay(ui, Overlay::Density);

        CHECK(!(ui.overlay == Overlay::Density), "density cleared");
        CHECK(ui.densityBank == 0, "bank reset");
        CHECK(ui.densitySubPage == UiState::DensitySubPage::Amount, "subpage reset");
        CHECK(activeOverlay(ui) == Overlay::None, "no overlay after escape");
    }

    static void testEscapeOverlayClearsVel()
    {
        UiState ui;
        enterVel(ui);
        ui.velSubPage = UiState::VelSubPage::Blend;
        ui.velBank = 1;

        escapeOverlay(ui, Overlay::Vel);

        CHECK(!(ui.overlay == Overlay::Vel), "vel cleared");
        CHECK(ui.velBank == 0, "bank reset");
        CHECK(ui.velSubPage == UiState::VelSubPage::Depth, "subpage reset");
    }

    // Item 7 / 9.21: a committed section selection (masterSection) is orthogonal
    // to the overlay field. Entering a sticky overlay (Vel/Density/Time) does NOT
    // clobber the selection, and escapeOverlay restores only the overlay — so the
    // MZ falls back to the previously-selected section when the overlay clears.
    // This is the "selected section underlays the overlay" layer of the precedence
    // stack: single owner (overlay in UiState::overlay, selection in masterSection).
    static void testStickyOverlayPreservesSectionSelection()
    {
        for (const auto ov : { Overlay::Density, Overlay::Vel, Overlay::Time })
        {
            UiState ui;
            ui.masterSection = 3;  // DIV meta selected (paging it)
            switch (ov)
            {
                case Overlay::Density: enterDensity(ui); break;
                case Overlay::Vel:     enterVel(ui);     break;
                case Overlay::Time:    enterTime(ui);    break;
                case Overlay::Euclid:
                case Overlay::Melodic:
                case Overlay::Harmony:
                case Overlay::SampleProps:
                case Overlay::None:    break;
            }
            CHECK(ui.masterSection == 3, "overlay entry does not clobber the selection");
            escapeOverlay(ui, ov);
            CHECK(ui.overlay == Overlay::None, "overlay cleared on escape");
            CHECK(ui.masterSection == 3,
                  "selected section survives the overlay round-trip (underlay layer)");
        }
    }

    static void testEscapeOverlayClearsTime()
    {
        UiState ui;
        enterTime(ui);

        escapeOverlay(ui, Overlay::Time);

        CHECK(!(ui.overlay == Overlay::Time), "time cleared");
        CHECK(ui.swingDismissed, "swingDismissed set (guards against swing re-trigger)");
    }

    static void testEscapeOverlayClearsEuclid()
    {
        UiState ui;
        enterEuclid(ui);
        ui.euclidPulses = 8;
        ui.euclidOffset = 2;
        ui.euclidAccents = 1;

        escapeOverlay(ui, Overlay::Euclid);

        CHECK(!ui.euclidHeld, "euclidHeld cleared");
        CHECK(ui.euclidPulses == 4, "pulses reset");
        CHECK(ui.euclidOffset == 0, "offset reset");
        CHECK(ui.euclidAccents == 0, "accents reset");
    }

    // =========================================================================
    // handleOverlayEvent — SectionPress
    // =========================================================================

    // Density: MOD (index 4) is internal → Consumed; any other exits.
    static void testDensitySectionPressInternalConsumed()
    {
        UiState ui;
        enterDensity(ui);

        const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 4 });

        CHECK(r == OverlayResult::Consumed, "MOD → Consumed");
        CHECK((ui.overlay == Overlay::Density), "density still active after subpage cycle");
        CHECK(ui.densitySubPage == UiState::DensitySubPage::Musicality,
              "Amount → Musicality on first press");
    }

    static void testDensitySectionPressInternalCyclesFull()
    {
        UiState ui;
        enterDensity(ui);

        auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 4 });
        CHECK(r == OverlayResult::Consumed, "cycle 1 → Consumed");
        CHECK(ui.densitySubPage == UiState::DensitySubPage::Musicality, "→ Musicality");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 4 });
        CHECK(r == OverlayResult::Consumed, "cycle 2 → Consumed");
        CHECK(ui.densitySubPage == UiState::DensitySubPage::Selection,  "→ Selection");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 4 });
        CHECK(r == OverlayResult::Consumed, "cycle 3 → Consumed");
        CHECK(ui.densitySubPage == UiState::DensitySubPage::Amount,     "→ Amount (wrap)");
    }

    static void testDensitySectionPressForeignExits()
    {
        for (int sec = 0; sec < 6; ++sec)
        {
            if (sec == 4) continue;  // internal section; skip
            UiState ui;
            enterDensity(ui);

            const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, sec });

            CHECK(r == OverlayResult::Exited,
                  "density foreign sec " + juce::String(sec) + " → Exited");
            CHECK(!(ui.overlay == Overlay::Density),
                  "density cleared for sec " + juce::String(sec));
        }
    }

    // Vel: AMP (index 3) is internal → Consumed; any other exits.
    static void testVelSectionPressInternalConsumed()
    {
        UiState ui;
        enterVel(ui);
        const ScopeCtx ctx { /*velAnyEnabled=*/true };

        const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);

        CHECK(r == OverlayResult::Consumed, "AMP → Consumed");
        CHECK((ui.overlay == Overlay::Vel), "vel still active");
        CHECK(ui.velSubPage == UiState::VelSubPage::Center,
              "Depth → Center on first press");
    }

    static void testVelSectionPressInternalCyclesFull()
    {
        UiState ui;
        enterVel(ui);
        const ScopeCtx ctx { true };

        auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "vel cycle 1 → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Center, "→ Center");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "vel cycle 2 → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Mode, "→ Mode");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "vel cycle 3 → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Blend, "→ Blend");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "vel cycle 4 → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Depth, "→ Depth (wrap)");
    }

    static void testVelSectionPressInternalSkipsDisabled()
    {
        UiState ui;
        enterVel(ui);
        const ScopeCtx ctx { /*velAnyEnabled=*/false };

        // When all tracks are Off, every internal-section press should land on Mode.
        auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "disabled → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Mode, "disabled → Mode");
        r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 3 }, ctx);
        CHECK(r == OverlayResult::Consumed, "disabled again → Consumed");
        CHECK(ui.velSubPage == UiState::VelSubPage::Mode, "stays Mode");
    }

    static void testVelSectionPressForeignExits()
    {
        for (int sec = 0; sec < 6; ++sec)
        {
            if (sec == 3) continue;  // internal section; skip
            UiState ui;
            enterVel(ui);

            const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, sec });

            CHECK(r == OverlayResult::Exited,
                  "vel foreign sec " + juce::String(sec) + " → Exited");
            CHECK(!(ui.overlay == Overlay::Vel),
                  "vel cleared for sec " + juce::String(sec));
        }
    }

    // Time: TRIG (index 0) is internal → Consumed, cycling the TIME <-> KEY page.
    static void testTimeSectionPressTrigCyclesPage()
    {
        UiState ui;
        enterTime(ui);
        CHECK(ui.sigPage == UiState::SigPage::Time, "TIME band opens on the TIME page");

        const auto r0 = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 0 });
        CHECK(r0 == OverlayResult::Consumed, "TRIG → Consumed (page cycle)");
        CHECK((ui.overlay == Overlay::Time), "TIME band still active after cycle");
        CHECK(ui.sigPage == UiState::SigPage::Key, "first TRIG re-press → KEY page");

        const auto r1 = handleOverlayEvent(ui, { ModeEventKind::SectionPress, 0 });
        CHECK(r1 == OverlayResult::Consumed, "TRIG → Consumed (page cycle back)");
        CHECK(ui.sigPage == UiState::SigPage::Time, "second TRIG re-press → back to TIME page");
    }

    static void testTimeSectionPressForeignExits()
    {
        for (int sec = 1; sec < 6; ++sec)  // TRIG (0) is internal; 1-5 are foreign
        {
            UiState ui;
            enterTime(ui);

            const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, sec });

            CHECK(r == OverlayResult::Exited,
                  "TIME foreign sec " + juce::String(sec) + " → Exited");
            CHECK(!(ui.overlay == Overlay::Time),
                  "TIME cleared for sec " + juce::String(sec));
        }
    }

    // Euclid: no section exits (sections aren't in the euclid surface).
    static void testEuclidSectionPressNotConsumed()
    {
        for (int sec = 0; sec < 6; ++sec)
        {
            UiState ui;
            enterEuclid(ui);

            const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, sec });

            const juce::String secStr { sec };
            CHECK(r == OverlayResult::NotConsumed,
                  "euclid sec " + secStr + " → NotConsumed");
            CHECK(ui.euclidHeld,
                  "euclid still active for sec " + secStr);
        }
    }

    // =========================================================================
    // handleOverlayEvent — ScopePress (foreign scope exits, own scope passes)
    // =========================================================================

    static void testDensityScopePressOwnScopes()
    {
        // Song is "own" for density (master write target); pressing it does NOT exit.
        UiState ui;
        enterDensity(ui);
        const auto r = handleOverlayEvent(ui,
            { ModeEventKind::ScopePress, -1, ControllerButton::SongScope });
        CHECK(r == OverlayResult::NotConsumed, "Song is own → NotConsumed");
        CHECK((ui.overlay == Overlay::Density), "density still active");
    }

    static void testDensityScopePressAllForeignScopes()
    {
        using CB = ControllerButton;
        const CB foreignScopes[] = {
            CB::TrackScope, CB::PhraseScope, CB::SceneScope,
            CB::MorphScope, CB::MuteScope,   CB::FillScope,
        };
        for (const auto scope : foreignScopes)
        {
            UiState ui;
            enterDensity(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::ScopePress, -1, scope });
            CHECK(r == OverlayResult::Exited, "density exits on foreign scope");
            CHECK(!(ui.overlay == Overlay::Density), "density cleared");
        }
    }

    static void testTimeScopePressOwnScopes()
    {
        // Song and Scene are "own" for TIME (they retarget timeScopeFor).
        UiState ui;
        enterTime(ui);
        auto r = handleOverlayEvent(ui,
            { ModeEventKind::ScopePress, -1, ControllerButton::SongScope });
        CHECK(r == OverlayResult::NotConsumed, "TIME: Song is own");
        CHECK((ui.overlay == Overlay::Time), "TIME still active after Song press");

        r = handleOverlayEvent(ui,
            { ModeEventKind::ScopePress, -1, ControllerButton::SceneScope });
        CHECK(r == OverlayResult::NotConsumed, "TIME: Scene is own");
        CHECK((ui.overlay == Overlay::Time), "TIME still active after Scene press");
    }

    static void testTimeScopePressAllForeignScopes()
    {
        using CB = ControllerButton;
        const CB foreignScopes[] = {
            CB::TrackScope, CB::PhraseScope,
            CB::MorphScope, CB::MuteScope, CB::FillScope,
        };
        for (const auto scope : foreignScopes)
        {
            UiState ui;
            enterTime(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::ScopePress, -1, scope });
            CHECK(r == OverlayResult::Exited, "TIME exits on foreign scope");
            CHECK(!(ui.overlay == Overlay::Time), "TIME cleared");
        }
    }

    // =========================================================================
    // handleOverlayEvent — DoubleTapFunc
    // =========================================================================

    static void testDoubleTapFuncExitsAllStickies()
    {
        // Density
        {
            UiState ui;
            enterDensity(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
            CHECK(r == OverlayResult::Exited, "Func dbl-tap exits density");
            CHECK(!(ui.overlay == Overlay::Density), "density cleared");
        }
        // Vel
        {
            UiState ui;
            enterVel(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
            CHECK(r == OverlayResult::Exited, "Func dbl-tap exits vel");
            CHECK(!(ui.overlay == Overlay::Vel), "vel cleared");
        }
        // TIME
        {
            UiState ui;
            enterTime(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
            CHECK(r == OverlayResult::Exited, "Func dbl-tap exits TIME");
            CHECK(!(ui.overlay == Overlay::Time), "TIME cleared");
            CHECK(ui.swingDismissed, "swingDismissed set on TIME exit");
        }
        // Euclid
        {
            UiState ui;
            enterEuclid(ui);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
            CHECK(r == OverlayResult::Exited, "Func dbl-tap exits euclid");
            CHECK(!ui.euclidHeld, "euclidHeld cleared");
        }
    }

    static void testDoubleTapFuncNoOverlayIsNotConsumed()
    {
        UiState ui;
        const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
        CHECK(r == OverlayResult::NotConsumed, "no overlay → NotConsumed");
    }

    // =========================================================================
    // SampleProps (9.23 S6) — entry + exit + index reset
    // =========================================================================

    static void enterSampleProps(UiState& ui, int poolIndex)
    {
        ui.overlay = Overlay::SampleProps;
        ui.samplePropsPoolIndex = poolIndex;
    }

    static void testSamplePropsEscapeResetsIndex()
    {
        UiState ui;
        enterSampleProps(ui, 3);
        CHECK(activeOverlay(ui) == Overlay::SampleProps, "SampleProps active on entry");

        escapeOverlay(ui, Overlay::SampleProps);

        CHECK(!(ui.overlay == Overlay::SampleProps), "SampleProps cleared");
        CHECK(ui.samplePropsPoolIndex == -1, "pool index reset to -1");
        CHECK(activeOverlay(ui) == Overlay::None, "no overlay after escape");
    }

    static void testSamplePropsDoubleTapFuncExits()
    {
        UiState ui;
        enterSampleProps(ui, 2);
        const auto r = handleOverlayEvent(ui, { ModeEventKind::DoubleTapFunc });
        CHECK(r == OverlayResult::Exited, "Func dbl-tap exits SampleProps");
        CHECK(!(ui.overlay == Overlay::SampleProps), "SampleProps cleared");
        CHECK(ui.samplePropsPoolIndex == -1, "pool index reset on dbl-tap exit");
    }

    static void testSamplePropsForeignSectionExits()
    {
        for (int sec = 0; sec < 6; ++sec)
        {
            UiState ui;
            enterSampleProps(ui, 1);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::SectionPress, sec });
            CHECK(r == OverlayResult::Exited,
                  "SampleProps exits on section " + juce::String(sec));
            CHECK(ui.samplePropsPoolIndex == -1, "index reset on section exit");
        }
    }

    static void testSamplePropsForeignScopeExits()
    {
        using CB = ControllerButton;
        const CB scopes[] = {
            CB::TrackScope, CB::PhraseScope, CB::SceneScope,
            CB::MorphScope, CB::MuteScope,   CB::FillScope, CB::SongScope,
        };
        for (const auto scope : scopes)
        {
            UiState ui;
            enterSampleProps(ui, 1);
            const auto r = handleOverlayEvent(ui, { ModeEventKind::ScopePress, -1, scope });
            CHECK(r == OverlayResult::Exited, "SampleProps exits on foreign scope");
            CHECK(ui.samplePropsPoolIndex == -1, "index reset on scope exit");
        }
    }

    // =========================================================================
    // Mutual exclusion — entering one overlay while another is active
    // =========================================================================

    static void testMutualExclusionViaEscapeOverlay()
    {
        // Density entry escapes any prior sticky overlay (as applyTimeEntry does).
        UiState ui;
        enterTime(ui);
        CHECK(activeOverlay(ui) == Overlay::Time, "TIME active before density entry");

        // Simulating the entry guard that applyTimeEntry already does:
        escapeOverlay(ui, Overlay::Time);
        enterDensity(ui);

        CHECK(activeOverlay(ui) == Overlay::Density, "only Density after transition");
        CHECK(!(ui.overlay == Overlay::Time), "TIME cleared");
    }

    // =========================================================================
    // overlayInternalSectionLabel (presentation)
    // =========================================================================

    static void testInternalSectionLabelDensity()
    {
        UiState ui;
        enterDensity(ui);

        // internalSection is 4; non-4 should return nullptr.
        CHECK(overlayInternalSectionLabel(ui, 0) == nullptr, "non-internal → nullptr");
        CHECK(overlayInternalSectionLabel(ui, 4) != nullptr, "internal → non-null");

        // Cycle through subpages and verify labels are correct next-subpage names.
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 4)) == "MUSIC",
              "Amount → MUSIC (next)");

        ui.densitySubPage = UiState::DensitySubPage::Musicality;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 4)) == "SELECT",
              "Musicality → SELECT (next)");

        ui.densitySubPage = UiState::DensitySubPage::Selection;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 4)) == "AMOUNT",
              "Selection → AMOUNT (wrap)");
    }

    static void testInternalSectionLabelVel()
    {
        UiState ui;
        enterVel(ui);

        CHECK(overlayInternalSectionLabel(ui, 3) != nullptr, "vel internal (3) → non-null");

        ui.velSubPage = UiState::VelSubPage::Depth;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 3)) == "CENTER", "Depth → CENTER");

        ui.velSubPage = UiState::VelSubPage::Center;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 3)) == "MODE", "Center → MODE");

        ui.velSubPage = UiState::VelSubPage::Mode;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 3)) == "BLEND", "Mode → BLEND");

        ui.velSubPage = UiState::VelSubPage::Blend;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 3)) == "DEPTH", "Blend → DEPTH");
    }

    static void testInternalSectionLabelTime()
    {
        UiState ui;
        enterTime(ui);

        // The label shows the DESTINATION of the next press: on the TIME page the
        // TRIG key reads "KEY", and on the KEY page it reads "TIME".
        CHECK(overlayInternalSectionLabel(ui, 0) != nullptr, "TIME internal (0) → non-null");
        CHECK(juce::String(overlayInternalSectionLabel(ui, 0)) == "KEY", "TIME page → next is \"KEY\"");
        ui.sigPage = UiState::SigPage::Key;
        CHECK(juce::String(overlayInternalSectionLabel(ui, 0)) == "TIME", "KEY page → next is \"TIME\"");
        CHECK(overlayInternalSectionLabel(ui, 1) == nullptr, "non-internal → nullptr");
    }

    static void testInternalSectionLabelNoneWhenNoOverlay()
    {
        UiState ui;
        for (int sec = 0; sec < 6; ++sec)
        {
            CHECK(overlayInternalSectionLabel(ui, sec) == nullptr,
                  "no overlay → nullptr for sec " + juce::String(sec));
        }
    }

    // =========================================================================
    // Registration
    // =========================================================================

    void runModeReducerTests()
    {
        // activeOverlay priority
        testActiveOverlayValues();

        // escapeOverlay correctness
        testEscapeOverlayClearsDensity();
        testEscapeOverlayClearsVel();
        testEscapeOverlayClearsTime();
        testEscapeOverlayClearsEuclid();
        testStickyOverlayPreservesSectionSelection();

        // SectionPress — Density
        testDensitySectionPressInternalConsumed();
        testDensitySectionPressInternalCyclesFull();
        testDensitySectionPressForeignExits();

        // SectionPress — Vel
        testVelSectionPressInternalConsumed();
        testVelSectionPressInternalCyclesFull();
        testVelSectionPressInternalSkipsDisabled();
        testVelSectionPressForeignExits();

        // SectionPress — Time
        testTimeSectionPressTrigCyclesPage();
        testTimeSectionPressForeignExits();

        // SectionPress — Euclid
        testEuclidSectionPressNotConsumed();

        // ScopePress — foreign vs own
        testDensityScopePressOwnScopes();
        testDensityScopePressAllForeignScopes();
        testTimeScopePressOwnScopes();
        testTimeScopePressAllForeignScopes();

        // DoubleTapFunc
        testDoubleTapFuncExitsAllStickies();
        testDoubleTapFuncNoOverlayIsNotConsumed();

        // SampleProps (9.23 S6)
        testSamplePropsEscapeResetsIndex();
        testSamplePropsDoubleTapFuncExits();
        testSamplePropsForeignSectionExits();
        testSamplePropsForeignScopeExits();

        // Mutual exclusion
        testMutualExclusionViaEscapeOverlay();

        // Presentation (overlayInternalSectionLabel)
        testInternalSectionLabelDensity();
        testInternalSectionLabelVel();
        testInternalSectionLabelTime();
        testInternalSectionLabelNoneWhenNoOverlay();
    }
}
