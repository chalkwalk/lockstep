// HarmonyGenTest — pure headless tests for the harmonic voice-mover core
// (Phase 10.8 / DESIGN §39.12).

#include "TestHarness.h"
#include "../src/core/HarmonyGen.h"

namespace lockstep
{
    // D Dorian — the system default; root D, ladder floor C3 (MIDI 48).
    static KeySig dDorian() { return { 2, kDorian, {}, ScaleType::Diatonic }; }
    static int rootMidi() { return kHarmonyRootBase + 2; }  // D above C3

    static void testLadderInScale()
    {
        const KeySig k = dDorian();
        const uint16_t mask = pcMask(k);
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        CHECK(!ladder.empty(), "ladder is non-empty");
        // Ascending, unique, in-scale.
        bool ascending = true, inScale = true;
        for (std::size_t i = 0; i < ladder.size(); ++i)
        {
            if (i > 0 && ladder[i] <= ladder[i - 1]) ascending = false;
            if (!maskHas(mask, ladder[i] % 12)) inScale = false;
        }
        CHECK(ascending, "ladder is strictly ascending");
        CHECK(inScale, "every ladder pitch is in the scale");
        // 7-note scale over 3 octaves = 21 ladder positions.
        CHECK(harmonyScaleSize(k) == 7, "D Dorian has 7 scale degrees");
        CHECK(static_cast<int>(ladder.size()) == 21, "3 octaves of a 7-note scale = 21 rungs");
    }

    static void testDefaultProgressionInScale()
    {
        const KeySig k = dDorian();
        const uint16_t mask = pcMask(k);
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);

        HarmonyProgression prog;  // 4 default triads
        const auto steps = printHarmony(prog, ladder, 16);
        int chords = 0;
        for (const auto& s : steps)
        {
            if (!s.trig) continue;
            ++chords;
            CHECK(s.noteCount == 3, "default chord prints a 3-note triad");
            bool asc = true;
            for (int n = 0; n < s.noteCount; ++n)
            {
                CHECK(maskHas(mask, s.notes[static_cast<std::size_t>(n)] % 12),
                      "every chord note is in the scale");
                if (n > 0 && s.notes[static_cast<std::size_t>(n)]
                              <= s.notes[static_cast<std::size_t>(n - 1)]) asc = false;
            }
            CHECK(asc, "chord notes come out ascending");
        }
        CHECK(chords == 4, "default progression prints 4 chords");
    }

    static void testEvenPlacement()
    {
        // 4 chords over a 16-step bar land on the quarter-note beats 0/4/8/12.
        const KeySig k = dDorian();
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        HarmonyProgression prog;  // length 4
        const auto steps = printHarmony(prog, ladder, 16);
        std::array<bool, 16> onset{};
        for (int i = 0; i < 16; ++i) onset[static_cast<std::size_t>(i)] = steps[static_cast<std::size_t>(i)].trig;
        CHECK(onset[0] && onset[4] && onset[8] && onset[12], "chords on the quarter beats");
        int total = 0;
        for (bool b : onset) if (b) ++total;
        CHECK(total == 4, "exactly 4 chord onsets");
        // The chord sustains across its beat (gap = 4 steps => quarter gate).
        CHECK(steps[0].gate == MusicalGate::G1_4, "chord sustains a quarter to the next");
    }

    static void testVoiceCountAndRemoval()
    {
        const KeySig k = dDorian();
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        HarmonyProgression prog;
        prog.length = 1;
        prog.chords[0].voiceCount = 2;          // bass + one upper voice
        const auto steps = printHarmony(prog, ladder, 16);
        CHECK(steps[0].trig, "two-voice chord still fires");
        CHECK(steps[0].noteCount == 2, "two-voice chord prints two notes");

        // Zero voices => no trig.
        prog.chords[0].voiceCount = 0;
        const auto none = printHarmony(prog, ladder, 16);
        CHECK(!none[0].trig, "a voiceless chord prints no trig");
    }

    static void testDistinctSlotsPrintDistinctChords()
    {
        // Two different chords in the buffer must print their own pitches — the
        // print is per-slot, not a single broadcast chord.
        const KeySig k = dDorian();
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        HarmonyProgression prog;
        prog.length = 2;
        prog.chords[0] = HarmonyChord{ 1, { { 0, 0, 0, 0 } } };   // bass = ladder[0]
        prog.chords[1] = HarmonyChord{ 1, { { 4, 0, 0, 0 } } };   // bass = ladder[4]
        const auto steps = printHarmony(prog, ladder, 16);
        CHECK(steps[0].trig && steps[0].notes[0] == ladder[0], "slot 0 prints its own bass");
        CHECK(steps[8].trig && steps[8].notes[0] == ladder[4], "slot 1 prints its own bass");
    }

    static void testVoiceMovesStayInScale()
    {
        // Shifting voice indices up (the MOVE/voice encoders' effect) keeps every
        // note in scale and clamped to the ladder.
        const KeySig k = dDorian();
        const uint16_t mask = pcMask(k);
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        const int ladderMax = static_cast<int>(ladder.size()) - 1;
        HarmonyProgression prog;
        prog.length = 1;
        // Push all voices near the top of the ladder; printHarmony must clamp.
        for (int v = 0; v < 3; ++v) prog.chords[0].voice[static_cast<std::size_t>(v)] = ladderMax + 5;
        const auto steps = printHarmony(prog, ladder, 16);
        CHECK(steps[0].trig, "clamped chord still fires");
        for (int n = 0; n < steps[0].noteCount; ++n)
            CHECK(maskHas(mask, steps[0].notes[static_cast<std::size_t>(n)] % 12),
                  "clamped voices remain in the scale");
    }

    static void testDuplicateVoicesCollapse()
    {
        const KeySig k = dDorian();
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        HarmonyProgression prog;
        prog.length = 1;
        prog.chords[0] = HarmonyChord{ 3, { { 2, 2, 2, 0 } } };   // all the same rung
        const auto steps = printHarmony(prog, ladder, 16);
        CHECK(steps[0].trig, "unison chord fires");
        CHECK(steps[0].noteCount == 1, "duplicate voices collapse to a single note");
    }

    static void testEmptyAndDegenerate()
    {
        const KeySig k = dDorian();
        const auto ladder = harmonyLadder(k, rootMidi(), kHarmonyOctaves);
        HarmonyProgression prog;
        CHECK(printHarmony(prog, ladder, 0).empty(), "length 0 => empty");

        std::vector<int> emptyLadder;
        const auto noLadder = printHarmony(prog, emptyLadder, 16);
        bool none = true;
        for (const auto& s : noLadder) if (s.trig) none = false;
        CHECK(none, "empty ladder => no chords");

        // Chromatic key: ladder spans all 12 pcs, default triad still prints.
        const KeySig chrom{ 0, kDorian, {}, ScaleType::Chromatic };
        const auto cLadder = harmonyLadder(chrom, kHarmonyRootBase, kHarmonyOctaves);
        const auto cm = printHarmony(prog, cLadder, 16);
        int fired = 0;
        for (const auto& s : cm) if (s.trig) ++fired;
        CHECK(fired > 0, "chromatic key still prints chords");
    }

    void runHarmonyGenTests()
    {
        testLadderInScale();
        testDefaultProgressionInScale();
        testEvenPlacement();
        testVoiceCountAndRemoval();
        testDistinctSlotsPrintDistinctChords();
        testVoiceMovesStayInScale();
        testDuplicateVoicesCollapse();
        testEmptyAndDegenerate();
    }
}
