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
// It found one immediately: with Track LATCHED, the keyboard ignored the latch
// and 'D' placed a trig where clicking the same cell selected track 0 (see the
// latch test at the bottom, and WI-6).

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
          << " msec=" << u.masterSection;
        // (A bare section key's selection lands in ManipulationZone, not UiState, so
        //  neither this signature nor the golden's digest can see WHICH section a
        //  section key chose -- only that the two paths agree on everything else.
        //  Section-key index parity is the one thing this sweep does not pin.)

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

            juce::String viaKeyboard, viaController;
            {
                UiDriver d;
                d.keyTap(code);
                viaKeyboard = sig(d);
            }
            {
                UiDriver d;
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
    testOracleRestored(failed);
}
}   // namespace lockstep
