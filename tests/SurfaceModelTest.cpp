// SurfaceModelTest — headless unit tests for pure SurfaceModel logic.
//
// Tests resolveKeyLabel() for key scenarios without a LockstepProcessor.
// Run via: lockstep_tests (exit 0 = pass, exit 1 = fail).

#include <juce_core/juce_core.h>
#include "../src/ui/KeyLabel.h"
#include "../src/ui/SurfaceModel.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // Minimal stubs for types resolveKeyLabel() needs but doesn't deeply use.
    // -------------------------------------------------------------------------

    // UiState stub — default is: nothing held, no latch, nothing active.
    // Only funcHeld/trackHeld/patternScopeHeld/partHeld/stepHeld matter here.
    static UiState makeUiState()
    {
        UiState ui{};
        return ui;
    }

    // EditContext stub — default: no step held, no active edit.
    static EditContext makeEditContext() { return EditContext{}; }

    // -------------------------------------------------------------------------
    // CHECK macro — prints failure info, increments counter.
    // -------------------------------------------------------------------------
    static int gFailed = 0;

    #define CHECK(cond, msg) \
        do { \
            if (!(cond)) { \
                juce::Logger::writeToLog(juce::String("FAIL [") + __FILE__ ":" \
                    + juce::String(__LINE__) + "] " + (msg)); \
                ++gFailed; \
            } \
        } while (false)

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for VerbClear (PANIC/O key)
    // -------------------------------------------------------------------------
    static void testPanicKeyLabel()
    {
        const KeyDef kd {
            KeyRole::VerbClear,
            "PANIC",  // natural
            "RST",    // funcLayer
            -1, true
        };

        // 1. No modifier held: primary="PANIC", hint="CLEAR" (always-on CPC hint).
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC no-mod: primary should be PANIC");
            CHECK(kl.hint    == "CLEAR", "PANIC no-mod: hint should be CLEAR");
            CHECK(!kl.disabled,          "PANIC no-mod: should not be disabled");
        }

        // 2. Func held only: primary="PANIC", hint="RST" (func-layer hint, not primary).
        //    This was the bug fixed in commit 7b71c16.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "PANIC", "PANIC func-held: primary must stay PANIC (not RST)");
            CHECK(kl.hint    == "RST",   "PANIC func-held: hint should be RST");
        }

        // 3. Track scope held: primary="CLEAR" (CPC active path).
        {
            auto ui = makeUiState();
            ui.trackHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "CLEAR", "PANIC track-scope: primary should be CLEAR");
            CHECK(kl.hint.isEmpty(),     "PANIC track-scope: hint should be empty");
        }

        // 4. Step held: primary="CLEAR" (CPC active path via isStepHeld).
        {
            auto ui = makeUiState();
            ui.stepHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "CLEAR", "PANIC step-held: primary should be CLEAR");
        }
    }

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for a Nav key with funcLayer secondary.
    // resolveKeyLabel's "All other keys" branch returns { natural, funcLayer }
    // regardless of modifier state — Func-hint promotion to primary is handled
    // at the builder level (SurfaceModel.cpp), not inside resolveKeyLabel.
    // -------------------------------------------------------------------------
    static void testNavKeyFuncPromotion()
    {
        // Use ASCII natural to avoid juce::String(const char*) non-ASCII assert.
        // (Real builder stores char8_t* and uses the char8_t* constructor path.)
        const KeyDef kd {
            KeyRole::Nav,
            "NAV",   // stand-in for ← (ASCII, avoids juce::String assert)
            "RST",
            -1, true
        };

        // No modifier: primary=natural, hint=funcLayer
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key: primary should be natural");
            CHECK(kl.hint    == "RST", "Nav key: hint should be funcLayer");
        }

        // Func held: resolveKeyLabel still returns { natural, funcLayer } for Nav.
        // Builder promotion (primary=funcLayer) happens after resolveKeyLabel is called.
        {
            auto ui = makeUiState();
            ui.funcHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "NAV", "Nav key func-held: resolveKeyLabel primary stays natural");
            CHECK(kl.hint    == "RST", "Nav key func-held: hint stays funcLayer");
        }
    }

    // -------------------------------------------------------------------------
    // Test: resolveKeyLabel() for a SectionKey
    // -------------------------------------------------------------------------
    static void testSectionKeyLabel()
    {
        const KeyDef kd {
            KeyRole::SectionKey,
            "TRIG",   // natural (canonical TRIG section)
            "COND",   // funcLayer (meta section label)
            0, true   // sectionIdx=0, machineHasSection=true
        };

        // No modifier: primary="TRIG", hint="COND" (meta hint)
        {
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            CHECK(kl.primary == "TRIG", "TRIG no-mod: primary should be TRIG");
            CHECK(!kl.disabled,         "TRIG no-mod: should not be disabled");
        }

        // Track scope held: primary comes from scoped matrix, not natural
        {
            auto ui = makeUiState();
            ui.trackHeld = true;
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kd, ui, ec);
            // scoped label depends on ScopedSectionMatrix — just verify non-disabled
            CHECK(!kl.disabled, "TRIG track-scope: should not be disabled (slot 0 always has content)");
        }

        // Machine doesn't have this section: disabled
        {
            const KeyDef kdNoSection {
                KeyRole::SectionKey,
                "MOD", "MOD",
                4, false   // machineHasSection=false
            };
            const auto ui = makeUiState();
            const auto ec = makeEditContext();
            const auto kl = resolveKeyLabel(kdNoSection, ui, ec);
            CHECK(kl.disabled, "MOD no-machine: should be disabled when machine lacks section");
        }
    }

} // namespace lockstep

// -------------------------------------------------------------------------
// main
// -------------------------------------------------------------------------
int main()
{
    // JUCE needs a minimal initialisation for juce::String in some configurations.
    juce::initialiseJuce_GUI();

    lockstep::testPanicKeyLabel();
    lockstep::testNavKeyFuncPromotion();
    lockstep::testSectionKeyLabel();

    const int failed = lockstep::gFailed;
    if (failed == 0)
        juce::Logger::writeToLog("All tests passed.");
    else
        juce::Logger::writeToLog(juce::String(failed) + " test(s) FAILED.");

    juce::shutdownJuce_GUI();
    return failed > 0 ? 1 : 0;
}
