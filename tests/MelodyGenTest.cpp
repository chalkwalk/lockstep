// MelodyGenTest — pure headless tests for the melodic generator
// (Phase 10.7 / DESIGN §39.11).

#include "TestHarness.h"
#include "../src/core/MelodyGen.h"

#include <set>

namespace lockstep
{
    // D Dorian — the system default; root D = MIDI 62.
    static KeySig dDorian() { return { 2, kDorian, {}, ScaleType::Diatonic }; }
    static constexpr int kRootMidi = 62;

    static void testDeterminism()
    {
        const KeySig k = dDorian();
        MelodyParams p;
        p.density = 10; p.coreBias = 2; p.contour = 3; p.octaves = 2;
        p.stepLeap = 60; p.seed = 12345u;

        const auto a = generateMelody(k, 16, kRootMidi, p);
        const auto b = generateMelody(k, 16, kRootMidi, p);
        bool same = (a.size() == b.size());
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = (a[i].trig == b[i].trig && a[i].note == b[i].note && a[i].gate == b[i].gate);
        CHECK(same, "same seed+params => identical melody");

        // A different seed should (almost surely) change something.
        p.seed = 99u;
        const auto c = generateMelody(k, 16, kRootMidi, p);
        bool differs = false;
        for (std::size_t i = 0; i < a.size() && i < c.size(); ++i)
            if (a[i].note != c[i].note) differs = true;
        CHECK(differs, "different seed => different notes");
    }

    static void testAllNotesInScale()
    {
        const KeySig k = dDorian();
        const uint16_t mask = pcMask(k);
        for (int seed = 1; seed <= 8; ++seed)
        {
            MelodyParams p;
            p.density = 16; p.coreBias = 2; p.contour = seed % 4;
            p.octaves = 3; p.stepLeap = 50; p.seed = static_cast<uint32_t>(seed * 7 + 1);
            const auto m = generateMelody(k, 16, kRootMidi, p);
            for (const auto& s : m)
                if (s.trig)
                    CHECK(maskHas(mask, s.note % 12), "every generated note is in the scale");
        }
    }

    static void testCoreBiasNarrowsPool()
    {
        const KeySig k = dDorian();
        // coreBias = triad: every note must be in the triad core (tier 0).
        MelodyParams p;
        p.density = 16; p.coreBias = 0; p.contour = 0; p.octaves = 2;
        p.stepLeap = 80; p.seed = 4u;
        const auto m = generateMelody(k, 16, kRootMidi, p);
        bool allTriad = true;
        for (const auto& s : m)
            if (s.trig && coreTier(k, s.note % 12) != 0) allTriad = false;
        CHECK(allTriad, "coreBias=triad keeps every note in the triad core");
    }

    static void testStrongBeatsGetStrongNotes()
    {
        // With the full pool available (coreBias=full) the downbeat must still be
        // pinned to the triad core by its metric strength.
        const KeySig k = dDorian();
        for (int seed = 1; seed <= 8; ++seed)
        {
            MelodyParams p;
            p.density = 16; p.coreBias = 2; p.contour = 1; p.octaves = 2;
            p.stepLeap = 100; p.seed = static_cast<uint32_t>(seed);
            const auto m = generateMelody(k, 16, kRootMidi, p);
            CHECK(m[0].trig, "downbeat fires at full density");
            CHECK(coreTier(k, m[0].note % 12) == 0,
                  "downbeat note is in the triad core regardless of seed");
        }
    }

    static void testStrongBeatsLastLonger()
    {
        // Quarter-note density (4 onsets on a 16-step bar): onsets land on the
        // strong beats 0/4/8/12, and the downbeat sustains at least as long as a
        // weaker beat's note.
        const KeySig k = dDorian();
        MelodyParams p;
        p.density = 4; p.coreBias = 2; p.contour = 0; p.octaves = 2;
        p.stepLeap = 0; p.seed = 1u;
        const auto m = generateMelody(k, 16, kRootMidi, p);
        std::set<int> onsetSteps;
        for (int i = 0; i < 16; ++i)
            if (m[static_cast<std::size_t>(i)].trig) onsetSteps.insert(i);
        CHECK((onsetSteps == std::set<int>{ 0, 4, 8, 12 }),
              "4 onsets land on the quarter-note beats");
        // Downbeat (strength 16) sustains >= a quarter beat (strength 2).
        CHECK(static_cast<int>(m[0].gate) >= static_cast<int>(m[4].gate),
              "downbeat note lasts at least as long as a weaker-beat note");
    }

    static void testRestsBridgeToStrongBeats()
    {
        // A weak short note must leave a gap before the next, stronger onset.
        // At density 4 the note on step 12 (strength 2) sustains 3 steps, leaving
        // a one-step rest bridging into the downbeat at step 0 of the next bar.
        const KeySig k = dDorian();
        MelodyParams p;
        p.density = 4; p.coreBias = 2; p.contour = 0; p.octaves = 2;
        p.stepLeap = 0; p.seed = 1u;
        const auto m = generateMelody(k, 16, kRootMidi, p);
        CHECK(m[12].trig, "step 12 fires");
        // 3-step sustain (1/4.) leaves steps 15 as the bridging rest before 0.
        CHECK(m[12].gate == MusicalGate::G1_8d || m[12].gate == MusicalGate::G1_4,
              "weak-beat note is short enough to leave a bridging rest");
    }

    static void testOnsetsPreferStrongBeats()
    {
        // Sub-bar density places onsets strongest-beat first.
        const KeySig k = dDorian();
        MelodyParams p;
        p.density = 1; p.coreBias = 2; p.contour = 0; p.octaves = 1;
        p.stepLeap = 0; p.seed = 1u;
        const auto m = generateMelody(k, 16, kRootMidi, p);
        CHECK(m[0].trig, "the single onset lands on the downbeat");
        int count = 0;
        for (const auto& s : m) if (s.trig) ++count;
        CHECK(count == 1, "density 1 => exactly one onset");
    }

    static void testEmptyAndDegenerate()
    {
        const KeySig k = dDorian();
        MelodyParams p; p.density = 8;
        CHECK(generateMelody(k, 0, kRootMidi, p).empty(), "length 0 => empty");

        MelodyParams z; z.density = 0;
        const auto m = generateMelody(k, 16, kRootMidi, z);
        bool none = true;
        for (const auto& s : m) if (s.trig) none = false;
        CHECK(none, "density 0 => no onsets");

        // Chromatic key with triad bias must still produce notes (no fifths
        // nesting => coreBias ignored, full mask used).
        const KeySig chrom { 0, kDorian, {}, ScaleType::Chromatic };
        MelodyParams c; c.density = 8; c.coreBias = 0;
        const auto cm = generateMelody(chrom, 16, kRootMidi, c);
        int fired = 0;
        for (const auto& s : cm) if (s.trig) ++fired;
        CHECK(fired > 0, "chromatic + triad bias still generates notes");
    }

    static void testKeepRhythmLocksOnsets()
    {
        // KeepRhythm: onsets come from the supplied trig set, the density param is
        // ignored, and pitches still land in the scale.
        const KeySig k = dDorian();
        const uint16_t mask = pcMask(k);

        std::vector<bool> onsets(16, false);
        onsets[0] = onsets[3] = onsets[7] = onsets[11] = true;  // an off-grid groove

        MelodyParams p;
        p.density = 16;            // would fill every step in Generate mode...
        p.coreBias = 2; p.contour = 0; p.octaves = 2; p.stepLeap = 40; p.seed = 7u;
        p.source = static_cast<int>(MelodySource::KeepRhythm);

        const auto m = generateMelody(k, 16, kRootMidi, p, &onsets);
        for (int i = 0; i < 16; ++i)
        {
            const bool want = onsets[static_cast<std::size_t>(i)];
            CHECK(m[static_cast<std::size_t>(i)].trig == want,
                  "KeepRhythm: onset set matches the supplied trigs exactly");
            if (want)
                CHECK(maskHas(mask, m[static_cast<std::size_t>(i)].note % 12),
                      "KeepRhythm: pitched note is in the scale");
        }

        // Without the fixed set, KeepRhythm falls back to Generate (no crash).
        const auto fallback = generateMelody(k, 16, kRootMidi, p, nullptr);
        int fired = 0;
        for (const auto& s : fallback) if (s.trig) ++fired;
        CHECK(fired > 0, "KeepRhythm with no onset set falls back to Generate");
    }

    void runMelodyGenTests()
    {
        testDeterminism();
        testAllNotesInScale();
        testCoreBiasNarrowsPool();
        testStrongBeatsGetStrongNotes();
        testStrongBeatsLastLonger();
        testRestsBridgeToStrongBeats();
        testOnsetsPreferStrongBeats();
        testKeepRhythmLocksOnsets();
        testEmptyAndDegenerate();
    }
}
