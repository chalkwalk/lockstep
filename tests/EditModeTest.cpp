// EditModeTest — characterisation tests for EditMode.
// Pins priority order, compound/same-column rules, and onVerbDispatched
// so that 8.3+ refactors cannot silently alter observable grammar.

#include "TestHarness.h"
#include "../src/io/EditMode.h"
#include "../src/io/ControllerEvent.h"

namespace lockstep
{
    using PS  = EditMode::PrimaryScope;
    using CB  = ControllerButton;
    using Ev  = ControllerEvent;
    using ET  = ControllerEvent::Type;

    static Ev down(CB b) { return { ET::ButtonDown, b, -1, 0 }; }
    static Ev up  (CB b) { return { ET::ButtonUp,   b, -1, 0 }; }

    // ── Priority order ─────────────────────────────────────────────────────
    // Priority (highest to lowest, from EditMode.h comment):
    //   Trig > Section > Track > Phrase > Scene > Mute > Morph > Song > Fill > Func.
    // Test pairwise: when both A and higher-priority B are held, B wins.

    static void testPriorityOrder()
    {
        EditMode em;

        // Default = None.
        CHECK(em.primaryScope() == PS::None, "default scope is None");

        // Func alone.
        em.onScopeEvent(down(CB::Func));
        CHECK(em.primaryScope() == PS::Func, "Func held → Func");

        // Track beats Func (Track > Func in priority).
        em.onScopeEvent(down(CB::TrackScope));
        CHECK(em.primaryScope() == PS::Track, "Track beats Func");

        // Track still beats Phrase when both held (Track > Phrase).
        em.onScopeEvent(down(CB::PhraseScope));
        CHECK(em.primaryScope() == PS::Track, "Track beats Phrase");

        // Release Track → Phrase wins.
        em.onScopeEvent(up(CB::TrackScope));
        CHECK(em.primaryScope() == PS::Phrase, "release Track → Phrase");

        // Release Phrase → Func wins.
        em.onScopeEvent(up(CB::PhraseScope));
        CHECK(em.primaryScope() == PS::Func, "release Phrase → Func");

        // Phrase > Scene: with Phrase + Scene held, Phrase wins.
        em.onScopeEvent(down(CB::PhraseScope));
        em.onScopeEvent(down(CB::SceneScope));
        CHECK(em.primaryScope() == PS::Phrase, "Phrase beats Scene");
        em.onScopeEvent(up(CB::PhraseScope));
        CHECK(em.primaryScope() == PS::Scene, "release Phrase → Scene");
        em.onScopeEvent(up(CB::SceneScope));

        // Mute > Morph: with Mute + Morph held, Mute wins.
        em.onScopeEvent(down(CB::MuteScope));
        em.onScopeEvent(down(CB::MorphScope));
        CHECK(em.primaryScope() == PS::Mute, "Mute beats Morph");
        em.onScopeEvent(up(CB::MuteScope));
        CHECK(em.primaryScope() == PS::Morph, "release Mute → Morph");

        // Morph > Song.
        em.onScopeEvent(down(CB::SongScope));
        CHECK(em.primaryScope() == PS::Morph, "Morph beats Song");
        em.onScopeEvent(up(CB::MorphScope));
        CHECK(em.primaryScope() == PS::Song, "release Morph → Song");

        // Song > Fill.
        em.onScopeEvent(down(CB::FillScope));
        CHECK(em.primaryScope() == PS::Song, "Song beats Fill");
        em.onScopeEvent(up(CB::SongScope));
        CHECK(em.primaryScope() == PS::Fill, "release Song → Fill");
        em.onScopeEvent(up(CB::FillScope));

        // Section beats all modifier keys.
        em.onScopeEvent(down(CB::FillScope));
        em.setSectionHeld(true);
        CHECK(em.primaryScope() == PS::Section, "Section beats Fill");

        // Trig beats Section.
        em.setTrigHeld(true);
        CHECK(em.primaryScope() == PS::Trig, "Trig beats Section (highest)");

        // Release trig → back to Section.
        em.setTrigHeld(false);
        CHECK(em.primaryScope() == PS::Section, "release trig → Section");

        // Release section → back to Fill.
        em.setSectionHeld(false);
        CHECK(em.primaryScope() == PS::Fill, "release section → Fill");
    }

    // ── Priority: each scope in isolation ──────────────────────────────────

    static void testSingleScopeIsolation()
    {
        struct Case { CB button; PS expected; };
        constexpr Case cases[] = {
            { CB::Func,        PS::Func   },
            { CB::TrackScope,  PS::Track  },
            { CB::PhraseScope, PS::Phrase },
            { CB::SceneScope,  PS::Scene  },
            { CB::MuteScope,   PS::Mute   },
            { CB::MorphScope,  PS::Morph  },
            { CB::SongScope,   PS::Song   },
            { CB::FillScope,   PS::Fill   },
        };
        for (const auto& c : cases)
        {
            EditMode em;
            em.onScopeEvent(down(c.button));
            CHECK(em.primaryScope() == c.expected,
                  juce::String("single-scope isolation: button ") + juce::String(static_cast<int>(c.button)));
            em.onScopeEvent(up(c.button));
            CHECK(em.primaryScope() == PS::None,
                  juce::String("single-scope release → None: button ") + juce::String(static_cast<int>(c.button)));
        }
    }

    // ── Compound scope ─────────────────────────────────────────────────────

    static void testCompoundScope()
    {
        {
            // Func + col-2 modifier = compound.
            EditMode em;
            em.onScopeEvent(down(CB::Func));
            em.onScopeEvent(down(CB::TrackScope));
            CHECK(em.hasCompoundScope(),    "Func+Track = compound");
            CHECK(!em.hasSameColumnConflict(), "Func+Track not a conflict");
        }
        {
            // Func + col-1 non-Func modifier = compound.
            EditMode em;
            em.onScopeEvent(down(CB::Func));
            em.onScopeEvent(down(CB::PhraseScope));
            CHECK(em.hasCompoundScope(),    "Func+Phrase = compound");
            CHECK(!em.hasSameColumnConflict(), "Func+Phrase not a conflict");
        }
        {
            // col-1 (non-Func) + col-2 = compound.
            EditMode em;
            em.onScopeEvent(down(CB::PhraseScope));
            em.onScopeEvent(down(CB::TrackScope));
            CHECK(em.hasCompoundScope(), "Phrase+Track = compound");
        }
        {
            // Single modifier alone = not compound.
            EditMode em;
            em.onScopeEvent(down(CB::TrackScope));
            CHECK(!em.hasCompoundScope(), "Track alone = not compound");
        }
        {
            // Func alone = not compound.
            EditMode em;
            em.onScopeEvent(down(CB::Func));
            CHECK(!em.hasCompoundScope(), "Func alone = not compound");
        }
    }

    // ── Same-column conflict ────────────────────────────────────────────────

    static void testSameColumnConflict()
    {
        {
            // Two col-1 non-Func modifiers = conflict.
            EditMode em;
            em.onScopeEvent(down(CB::PhraseScope));
            em.onScopeEvent(down(CB::MorphScope));
            CHECK(em.hasSameColumnConflict(), "Phrase+Morph = col-1 conflict");
            CHECK(!em.hasCompoundScope(),     "col-1 conflict is not compound");
        }
        {
            EditMode em;
            em.onScopeEvent(down(CB::PhraseScope));
            em.onScopeEvent(down(CB::MuteScope));
            CHECK(em.hasSameColumnConflict(), "Phrase+Mute = col-1 conflict");
        }
        {
            // Two col-2 modifiers = conflict.
            EditMode em;
            em.onScopeEvent(down(CB::TrackScope));
            em.onScopeEvent(down(CB::SceneScope));
            CHECK(em.hasSameColumnConflict(), "Track+Scene = col-2 conflict");
        }
        {
            // Cross-column = not a conflict.
            EditMode em;
            em.onScopeEvent(down(CB::PhraseScope));
            em.onScopeEvent(down(CB::TrackScope));
            CHECK(!em.hasSameColumnConflict(), "Phrase+Track = cross-column, not conflict");
        }
        {
            // Func + col-1 non-Func = not a conflict (Func is universal qualifier).
            EditMode em;
            em.onScopeEvent(down(CB::Func));
            em.onScopeEvent(down(CB::PhraseScope));
            CHECK(!em.hasSameColumnConflict(), "Func+Phrase = not a col-1 conflict");
        }
    }

    // ── onVerbDispatched callback ───────────────────────────────────────────

    static void testOnVerbDispatched()
    {
        {
            // Default scope: callback fires with None.
            EditMode em;
            PS capturedScope = PS::Trig; // non-default sentinel
            CB capturedVerb  = CB::None;
            em.onVerbDispatched = [&](PS s, CB v) { capturedScope = s; capturedVerb = v; };
            em.onVerb(CB::VerbPlay);
            CHECK(capturedScope == PS::None,    "verb with no scope → None");
            CHECK(capturedVerb  == CB::VerbPlay, "verb is VerbPlay");
        }
        {
            // Track scope: callback fires with Track.
            EditMode em;
            PS capturedScope = PS::None;
            em.onVerbDispatched = [&](PS s, CB) { capturedScope = s; };
            em.onScopeEvent(down(CB::TrackScope));
            em.onVerb(CB::VerbRecord);
            CHECK(capturedScope == PS::Track, "verb under Track scope → Track");
        }
        {
            // Trig overrides Track: fires with Trig.
            EditMode em;
            PS capturedScope = PS::None;
            em.onVerbDispatched = [&](PS s, CB) { capturedScope = s; };
            em.onScopeEvent(down(CB::TrackScope));
            em.setTrigHeld(true);
            em.onVerb(CB::VerbClear);
            CHECK(capturedScope == PS::Trig, "Trig beats Track at verb dispatch");
        }
    }

    // ── onScopeEvent: non-modifier buttons return false ────────────────────

    static void testNonModifierReturnsNotConsumed()
    {
        EditMode em;
        CHECK(!em.onScopeEvent(down(CB::Step)),       "Step not consumed by EditMode");
        CHECK(!em.onScopeEvent(down(CB::Section)),    "Section not consumed");
        CHECK(!em.onScopeEvent(down(CB::VerbPlay)),   "VerbPlay not consumed");
        CHECK(!em.onScopeEvent(down(CB::NavUp)),      "NavUp not consumed");
        CHECK(!em.onScopeEvent(down(CB::TapTempo)),   "TapTempo not consumed");
        // Modifier buttons ARE consumed.
        CHECK(em.onScopeEvent(down(CB::Func)),        "Func IS consumed");
        CHECK(em.onScopeEvent(down(CB::TrackScope)),  "TrackScope IS consumed");
    }

    void runEditModeTests()
    {
        testPriorityOrder();
        testSingleScopeIsolation();
        testCompoundScope();
        testSameColumnConflict();
        testOnVerbDispatched();
        testNonModifierReturnsNotConsumed();
    }
}
