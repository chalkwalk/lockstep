// SyntheticKeyboardTest -- the QWERTY path, driven the way a person drives it.
//
// Between a key going down and dispatch seeing a ControllerEvent sits a layer no
// other test touches: keyPressed() case-folds the scancode, suppresses OS
// key-repeat, drops edge keys, asks QwertyOverlay::resolve() what the key means
// under the currently-held modifiers, and registers the press. Releases are found
// by keyStateChanged diffing PressTracker against the OS key state -- there is no
// key-up callback to hook.
//
// The golden net enters BELOW all of that (DispatchProbe speaks ControllerEvents
// directly, as a MIDI controller would), so every one of those steps has been
// shipping untested. The method here is differential: drive a key on one rig,
// drive the ControllerEvent it is supposed to mean on a twin rig, and require the
// two to agree. That is the actual contract -- "the keyboard is a controller" --
// and a divergence is a real answer either way, not a harness detail.
//
// Its first job was to referee a suspected divergence: keyPressed does NOT OR the
// modifier latch into what it asks QwertyOverlay, though every other path does.
// The verdict was "no bug, for a reason worth pinning" -- see testLatchApplies-
// ToKeyboard. Which is the point of a differential test: it answers, rather than
// leaving a plausible code-reading to be argued about.
//
// HOW SHARP IS IT. Measured, not asserted: shift the controller twin's index by one
// and every one of the 22 indexed keys (6 section + 16 step) fails. It caught 19 of
// 22 before mzOff/mzOrg were added to sig() -- the three misses were the section keys
// whose only visible effect is the param page they put in the Manipulation Zone. A
// signature that cannot see a key's effect gives that key a free pass forever, and
// the test still reads as though it covers it. Re-measure this way after changing
// sig(); a number nobody has checked lately is just a story.

#include "UiDriver.h"

#include "../src/io/QwertyOverlay.h"

#include <array>
#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // A compact signature of what a gesture did. Not a full digest (that is the
    // golden's job) -- enough state that two paths doing DIFFERENT things cannot
    // both produce this string.
    juce::String sig(UiDriver& d)
    {
        const UiState& u = d.ui();
        juce::String s;
        s << "ov=" << static_cast<int>(u.overlay)
          << " fn=" << static_cast<int>(u.funcHeld)
          << " tr=" << static_cast<int>(u.trackHeld)
          << " mu=" << static_cast<int>(u.muteHeld)
          << " cue=" << static_cast<int>(u.cueHeld)
          << " L[" << static_cast<int>(u.latch.phrase) << static_cast<int>(u.latch.morph)
          << static_cast<int>(u.latch.mute) << static_cast<int>(u.latch.track)
          << static_cast<int>(u.latch.scene) << static_cast<int>(u.latch.song)
          << static_cast<int>(u.latch.fill) << "]"
          << " at=" << d.activeTrack()
          << " pg=" << DispatchProbe::page(d.editor())
          << " msec=" << u.masterSection
          // What the section keys actually DO. A section key's effect is to put a page
          // of params in the Manipulation Zone -- that lands in the MZ, not in UiState,
          // so a UiState-only signature could see only that a key was pressed, never
          // WHICH page it chose. Both halves are needed: the slot offset says which
          // page, the origin says which scope it resolved from.
          << " mzOff=" << DispatchProbe::mzSlotOffset(d.editor())
          << " mzOrg=" << static_cast<int>(DispatchProbe::mzPageOrigin(d.editor()));

        // The trig MAP, not a count. A count is the trap: tapping step 1 instead of
        // step 2 leaves the same number of trigs, so a count-based signature calls
        // two different outcomes identical. Verified by canary -- with a count, an
        // index-shifted twin still matched on most keys.
        auto& seq = d.proc().sequence();
        s << " trigs=";
        for (std::size_t t = 0; t < 5; ++t)
        {
            for (int st = 0; st < 16; ++st)
                s << (seq.tracks[t].steps[static_cast<std::size_t>(st)].trig ? "1" : "0");
            s << "/";
        }
        return s;
    }

    // Every key the surface binds: the 8 cluster modifiers resolve() handles
    // inline, plus the 32 kPrimary entries. Listed here rather than reaching into
    // QwertyOverlay's private table on purpose -- this is an independent statement
    // of what the keyboard is, so a key silently vanishing from the map fails here.
    constexpr std::array<int, 40> kAllKeys = {
        // Cluster col 1 (Func/Phrase/Morph/Mute), col 2 (Track/Scene/Song/Fill)
        '1', 'Q', 'A', 'Z', '2', 'W', 'S', 'X',
        // Utilities + sections
        '3', '4', '5', '6', '7', '8', '9', '0',
        // Nav
        'E', 'R', 'T',
        // Verbs
        'Y', 'U', 'I', 'O', 'P',
        // Step row 1 (D..; = steps 0-7)
        'D', 'F', 'G', 'H', 'J', 'K', 'L', 59,
        // Step row 2 (C../ = steps 8-15)
        'C', 'V', 'B', 'N', 'M', 44, 46, 47,
    };

    // Each bare key, through the keyboard, must land where its ControllerEvent lands.
    void testEveryKeyMatchesControllerPath(int& failed)
    {
        auto check = [&failed](bool ok, const juce::String& what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/parity] %s\n", what.toRawUTF8()); ++failed; }
        };

        const QwertyOverlay qwerty;

        for (const int code : kAllKeys)
        {
            // What the key is supposed to mean, with nothing held.
            const auto expected = qwerty.resolve(code, false, false, false);
            check(expected.button != CB::None,
                  juce::String("key ") + juce::String(code) + " resolves to a real button");

            // Both twins get a real machine, so the section keys resolve MACHINE-owned
            // param pages as well as the scope-owned ones a bare stub track offers --
            // a richer surface to compare, closer to any real use.
            //
            // It is NOT what closed the sweep's blind spot, though it was expected to
            // be. Measured, three ways: the mzOff/mzOrg signature alone catches 22/22
            // even on the stub machine; a real machine with the old signature still
            // catches only 19. The section keys were never doing nothing -- they move
            // the MZ's page regardless of what machine the track has, because sections
            // resolve through the scope stack. The gap was never in the fixture. It was
            // that nothing was LOOKING at the MZ.
            juce::String viaKeyboard, viaController;
            {
                UiDriver d;
                installRealMachine(d.rig());
                d.keyTap(code);
                viaKeyboard = sig(d);
            }
            {
                UiDriver d;
                installRealMachine(d.rig());
                d.tap(expected.button, expected.index);
                viaController = sig(d);
            }
            check(viaKeyboard == viaController,
                  juce::String("key ") + juce::String(code) + " matches its controller event\n"
                      + "         keyboard:   " + viaKeyboard + "\n"
                      + "         controller: " + viaController);
        }
    }

    void testRepeatSuppression(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/repeat] %s\n", what); ++failed; }
        };

        // Holding a key makes the OS fire keyPressed repeatedly. A step key that
        // toggles would flicker its trig at the repeat rate; heldKeys_ is what stops
        // that, and nothing tested it.
        UiDriver d;
        d.keyDown('D');
        const bool afterFirst = d.proc().sequence().tracks[0].steps[0].trig;
        for (int i = 0; i < 5; ++i)
            d.keyDown('D');   // OS repeat: same key, no intervening release
        const bool afterRepeats = d.proc().sequence().tracks[0].steps[0].trig;
        check(afterFirst == afterRepeats, "OS key-repeat does not re-fire the press");

        d.keyUp('D');
        d.keyDown('D');   // a genuine second press DOES fire
        check(d.proc().sequence().tracks[0].steps[0].trig != afterFirst,
              "a real second press (after release) fires again");
    }

    void testReleaseDiff(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/release] %s\n", what); ++failed; }
        };

        // The modifier-held path end to end: '2' down makes Track held, so 'D'
        // resolves to SelectTrack rather than Step; releasing '2' must clear the
        // flag -- via keyStateChanged's diff against the key oracle, which is the
        // only release mechanism the product has.
        UiDriver d;
        d.keyDown('2');
        check(d.ui().trackHeld, "'2' down holds the Track scope");

        d.keyDown('D');
        check(d.activeTrack() == 0, "Track+'D' selects track 0 (not a trig)");
        d.keyUp('D');

        d.keyUp('2');
        check(!d.ui().trackHeld, "'2' up releases the Track scope");
    }

    void testEdgeKeysDoNothing(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/edge] %s\n", what); ++failed; }
        };

        // The keys immediately outside the 10x4 grid are deliberately inert: a
        // mis-hit reaching for 0 or P must not fire a neighbour's action.
        UiDriver d;
        const juce::String before = sig(d);
        for (const int code : { 96, 45, 61, 9, 91, 93, 39 })   // ` - = Tab [ ] '
            d.keyTap(code);
        check(sig(d) == before, "edge keys dispatch nothing");
    }

    // A LATCHED scope must mean the same thing to the keyboard as to everything else.
    //
    // Reading the code says it cannot: keyPressed passes uiState_.trackHeld/muteHeld
    // to QwertyOverlay::resolve WITHOUT OR-ing the latch in, while layerContext()
    // (PluginEditor.cpp:7703), KeyboardArea's mouse path and DispatchProbe all do.
    // That reading predicts a latched Track + 'D' places a trig while clicking the
    // same cell selects track 0.
    //
    // It does not, and this test is why: latch is implemented as "the held flag never
    // clears". CommandCore::handleUp skips clearing xxxHeld while the scope is latched
    // (see PluginEditor.cpp:6597 "cleared by handleUp -> not latched"), so trackHeld is
    // STILL TRUE under a latch and keyPressed's read already accounts for it. The OR
    // elsewhere is redundant, not compensatory.
    //
    // So the invariant the product actually rests on is LATCH IMPLIES HELD -- asserted
    // directly below, because it is load-bearing and invisible: the day someone makes
    // latch a pure marker that does not pin the held flag, every OR-ing path keeps
    // working and the keyboard alone silently starts placing trigs under a latched
    // scope. That is the regression this test exists to catch.
    void testLatchAppliesToKeyboard(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/latch] %s\n", what); ++failed; }
        };

        // Track latched by double-tap, then 'D' (step 0's key).
        UiDriver d;
        d.doubleTap(CB::TrackScope);
        check(d.ui().latch.track, "double-tap latches Track");
        check(d.ui().trackHeld,
              "LATCH IMPLIES HELD: a latched scope keeps its held flag set, which is "
              "the only reason keyPressed's non-OR-ing read is correct");

        const bool trigBefore = d.proc().sequence().tracks[0].steps[0].trig;
        d.keyTap('D');
        check(d.activeTrack() == 0, "latched Track + 'D' selects track 0");
        check(d.proc().sequence().tracks[0].steps[0].trig == trigBefore,
              "latched Track + 'D' does NOT place a trig");

        // The same gesture through the layer every other input path uses. This is
        // the assertion that matters: not "the keyboard does X" but "the keyboard
        // does what the rest of the instrument does".
        UiDriver ctl;
        ctl.doubleTap(CB::TrackScope);
        const juce::String viaController = sig(ctl.tap(CB::SelectTrack, 0));

        UiDriver kbd;
        kbd.doubleTap(CB::TrackScope);
        kbd.keyTap('D');
        check(sig(kbd) == viaController, "latched keyboard agrees with the controller path");

        // Mute latches the same way (the other latched layer resolve() consults).
        UiDriver m;
        m.doubleTap(CB::MuteScope);
        check(m.ui().latch.mute, "double-tap latches Mute");
        check(m.ui().muteHeld, "LATCH IMPLIES HELD holds for Mute too");
        const bool mTrigBefore = m.proc().sequence().tracks[0].steps[0].trig;
        m.keyTap('D');
        check(m.proc().sequence().tracks[0].steps[0].trig == mTrigBefore,
              "latched Mute + 'D' toggles the mute, not the trig");
    }

    void testOracleRestored(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SynthKbd/oracle] %s\n", what); ++failed; }
        };

        // The key oracle is process-global; a driver must leave it as it found it.
        // Otherwise a later test's editor would silently believe synthetic keys --
        // an action-at-a-distance bug that would look like anything but this.
        Rig rig;
        {
            UiDriver d;
            d.keyDown('D');
            check(DispatchProbe::press(d.editor()).physicallyDown('D'),
                  "inside the driver, a synthetic key reads as down");
        }
        check(!DispatchProbe::press(*rig.editor).physicallyDown('D'),
              "after the driver dies, the OS oracle is back (nothing is down)");
    }
}   // namespace

void runSyntheticKeyboardTests(int& failed)
{
    testEveryKeyMatchesControllerPath(failed);
    testRepeatSuppression(failed);
    testReleaseDiff(failed);
    testEdgeKeysDoNothing(failed);
    testLatchAppliesToKeyboard(failed);
    testOracleRestored(failed);
}
}   // namespace lockstep
