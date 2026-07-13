// LayerResolveTest — golden table for QwertyOverlay::resolve().
// Enumerates every mapped key × {plain, funcHeld, trackHeld, muteHeld,
// track+func, mute+func} and asserts exact (button, index) output.
// This is the oracle for Stage 8.3: after resolveLayer() replaces the three
// diverged implementations, this table must still pass unchanged.
//
// Priority (pinned here): trackHeld > muteHeld > funcHeld.
// QwertyOverlay.cpp:188–196 implements this order; kLayerRemaps must encode it.

#include "TestHarness.h"
#include "../src/io/QwertyOverlay.h"
#include "../src/io/ControllerEvent.h"

namespace lockstep
{
    using B = ControllerButton;

    static void checkKey(const char* label,
                         int keyCode,
                         bool funcHeld, bool trackHeld, bool muteHeld,
                         B expectedButton, int expectedIndex)
    {
        QwertyOverlay ov;
        const auto ev = ov.resolve(keyCode, funcHeld, trackHeld, muteHeld);
        CHECK(ev.button == expectedButton,
              juce::String(label) + " button");
        CHECK(ev.index == expectedIndex,
              juce::String(label) + " index");
    }

    static constexpr int code(char c) { return static_cast<int>(c); }

    // ── Modifier cluster: always return scope button, no layers override ──

    static void testModifierClusterKeys()
    {
        // Col 1
        checkKey("key1-plain", code('1'), false, false, false, B::Func, -1);
        checkKey("keyQ-plain", code('Q'), false, false, false, B::PhraseScope, -1);
        checkKey("keyA-plain", code('A'), false, false, false, B::MorphScope, -1);
        checkKey("keyZ-plain", code('Z'), false, false, false, B::MuteScope, -1);
        // Modifier cluster ignores layer flags.
        checkKey("key1-func", code('1'), true, false, false, B::Func, -1);
        checkKey("key1-track", code('1'), false, true, false, B::Func, -1);
        checkKey("key1-mute", code('1'), false, false, true, B::Func, -1);
        // Col 2
        checkKey("key2-plain", code('2'), false, false, false, B::TrackScope, -1);
        checkKey("keyW-plain", code('W'), false, false, false, B::SceneScope, -1);
        checkKey("keyS-plain", code('S'), false, false, false, B::SongScope, -1);
        checkKey("keyX-plain", code('X'), false, false, false, B::FillScope, -1);
    }

    // ── Row-1 utility keys (plain layer) ──────────────────────────────────

    static void testUtilityKeys()
    {
        checkKey("key3-plain", code('3'), false, false, false, B::TapTempo, -1);
        checkKey("key4-plain", code('4'), false, false, false, B::NavUp, -1);
    }

    // ── Section keys 5-0: Section plain, MetaSection under Func ──────────

    static void testSectionKeys()
    {
        constexpr int sectionKeys[] = { code('5'), code('6'), code('7'),
                                        code('8'), code('9'), code('0') };
        for (int i = 0; i < 6; ++i)
        {
            const juce::String lbl = "section[" + juce::String(i) + "]";
            checkKey((lbl + " plain").toRawUTF8(),
                     sectionKeys[i], false, false, false, B::Section, i);
            checkKey((lbl + " func").toRawUTF8(),
                     sectionKeys[i], true, false, false, B::MetaSection, i);
            // Track/Mute don't remap section keys (they remap step keys only).
            checkKey((lbl + " track").toRawUTF8(),
                     sectionKeys[i], false, true, false, B::Section, i);
            checkKey((lbl + " mute").toRawUTF8(),
                     sectionKeys[i], false, false, true, B::Section, i);
        }
    }

    // ── Navigation keys (plain layer, func falls through) ─────────────────

    static void testNavKeys()
    {
        // Nav falls through from Func layer to primary.
        checkKey("navLeft-plain", code('E'), false, false, false, B::NavLeft, -1);
        checkKey("navDown-plain", code('R'), false, false, false, B::NavDown, -1);
        checkKey("navRight-plain", code('T'), false, false, false, B::NavRight, -1);

        checkKey("navLeft-func", code('E'), true, false, false, B::NavLeft, -1);
        checkKey("navDown-func", code('R'), true, false, false, B::NavDown, -1);
        checkKey("navRight-func", code('T'), true, false, false, B::NavRight, -1);
    }

    // ── Verb keys (Y/U/I/O/P) ────────────────────────────────────────────

    static void testVerbKeys()
    {
        // Plain
        checkKey("Y-plain", code('Y'), false, false, false, B::VerbSnapshot, -1);
        checkKey("U-plain", code('U'), false, false, false, B::VerbRecord, -1);
        checkKey("I-plain", code('I'), false, false, false, B::VerbPlay, -1);
        checkKey("O-plain", code('O'), false, false, false, B::VerbClear, -1);
        checkKey("P-plain", code('P'), false, false, false, B::VerbConfirm, -1);

        // Func layer remaps
        checkKey("Y-func", code('Y'), true, false, false, B::Restore, -1);
        checkKey("U-func", code('U'), true, false, false, B::VerbRecord, -1); // VerbRecord kept (omni copy)
        checkKey("I-func", code('I'), true, false, false, B::VerbPlay, -1); // falls through to primary
        // 9.29: Func+O is no longer rewritten to VerbDelete. Delete is scope+hold(O);
        // Func stays a QUALIFIER over Clear, which is what makes Trig+Func+O (clear the
        // P-Locks, keep the trig) reachable -- the remap used to eat it.
        checkKey("O-func", code('O'), true, false, false, B::VerbClear, -1);
        checkKey("P-func", code('P'), true, false, false, B::VerbConfirm, -1);

        // 9.10: Func+3 remap to MetronomeToggle removed; metronome moved to TIME band field 2.
        checkKey("key3-func", code('3'), true, false, false, B::TapTempo, -1);
    }

    // ── Step grid row 1: D-;, indices 0-7 ────────────────────────────────

    static void testStepGridRow1()
    {
        constexpr int stepKeys[] = { code('D'), code('F'), code('G'), code('H'),
                                     code('J'), code('K'), code('L'), 59 }; // 59 = ;
        for (int i = 0; i < 8; ++i)
        {
            const juce::String lbl = "step[" + juce::String(i) + "]";
            // Plain
            checkKey((lbl + " plain").toRawUTF8(),
                     stepKeys[i], false, false, false, B::Step, i);
            // Track layer (priority 1) → SelectTrack
            checkKey((lbl + " track").toRawUTF8(),
                     stepKeys[i], false, true, false, B::SelectTrack, i);
            // Mute layer (priority 2) → ToggleMute
            checkKey((lbl + " mute").toRawUTF8(),
                     stepKeys[i], false, false, true, B::ToggleMute, i);
            // Func alone doesn't remap step keys
            checkKey((lbl + " func").toRawUTF8(),
                     stepKeys[i], true, false, false, B::Step, i);
            // Track beats Mute
            checkKey((lbl + " track+mute").toRawUTF8(),
                     stepKeys[i], false, true, true, B::SelectTrack, i);
            // Track beats Func
            checkKey((lbl + " track+func").toRawUTF8(),
                     stepKeys[i], true, true, false, B::SelectTrack, i);
            // Mute beats Func
            checkKey((lbl + " mute+func").toRawUTF8(),
                     stepKeys[i], true, false, true, B::ToggleMute, i);
        }
    }

    // ── Step grid row 2: C-/, indices 8-15 ───────────────────────────────

    static void testStepGridRow2()
    {
        constexpr int stepKeys[] = { code('C'), code('V'), code('B'), code('N'),
                                     code('M'), 44, 46, 47 }; // , . /
        for (int i = 0; i < 8; ++i)
        {
            const int stepIdx = i + 8;
            const juce::String lbl = "step[" + juce::String(stepIdx) + "]";
            checkKey((lbl + " plain").toRawUTF8(),
                     stepKeys[i], false, false, false, B::Step, stepIdx);
            checkKey((lbl + " track").toRawUTF8(),
                     stepKeys[i], false, true, false, B::SelectTrack, stepIdx);
            checkKey((lbl + " mute").toRawUTF8(),
                     stepKeys[i], false, false, true, B::ToggleMute, stepIdx);
            checkKey((lbl + " track+mute").toRawUTF8(),
                     stepKeys[i], false, true, true, B::SelectTrack, stepIdx);
            checkKey((lbl + " mute+func").toRawUTF8(),
                     stepKeys[i], true, false, true, B::ToggleMute, stepIdx);
        }
    }

    // ── Unmapped keys return Button::None ─────────────────────────────────

    static void testUnmappedKeys()
    {
        QwertyOverlay ov;
        // Key codes that don't appear in any table.
        CHECK(ov.resolve(code('!'), false, false, false).button == B::None,
              "unmapped key '!' -> None");
        CHECK(ov.resolve(0, false, false, false).button == B::None,
              "unmapped keycode 0 -> None");
    }

    void runLayerResolveTests()
    {
        testModifierClusterKeys();
        testUtilityKeys();
        testSectionKeys();
        testNavKeys();
        testVerbKeys();
        testStepGridRow1();
        testStepGridRow2();
        testUnmappedKeys();
    }
}
