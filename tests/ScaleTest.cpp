// ScaleTest — pure headless tests for the tonal core (Phase 10.2 / DESIGN §4.10).

#include "TestHarness.h"
#include "../src/core/Scale.h"

#include <algorithm>
#include <set>

namespace lockstep
{
    // Helper: collect the absolute pitch classes of a key as a sorted set.
    static std::set<int> pcsOf(const KeySig& k)
    {
        std::set<int> out;
        const uint16_t m = pcMask(k);
        for (int pc = 0; pc < 12; ++pc)
            if (maskHas(m, pc)) out.insert(pc);
        return out;
    }

    static void testBrightnessModes()
    {
        // C root, the four corner modes by brightness.
        const std::set<int> ionian   { 0, 2, 4, 5, 7, 9, 11 };   // major
        const std::set<int> lydian   { 0, 2, 4, 6, 7, 9, 11 };   // brightest
        const std::set<int> aeolian  { 0, 2, 3, 5, 7, 8, 10 };   // natural minor
        const std::set<int> locrian  { 0, 1, 3, 5, 6, 8, 10 };   // darkest

        CHECK(pcsOf({ 0, kIonian,  {}, Symmetric::None }) == ionian,  "C Ionian pcs");
        CHECK(pcsOf({ 0, kLydian,  {}, Symmetric::None }) == lydian,  "C Lydian pcs");
        CHECK(pcsOf({ 0, kAeolian, {}, Symmetric::None }) == aeolian, "C Aeolian pcs");
        CHECK(pcsOf({ 0, kLocrian, {}, Symmetric::None }) == locrian, "C Locrian pcs");

        // degrees() returns intervals from root, first entry 0, ascending.
        const auto d = degrees({ 0, kIonian, {}, Symmetric::None });
        CHECK((d == std::vector<int>{ 0, 2, 4, 5, 7, 9, 11 }), "C Ionian degrees");
    }

    static void testModifierPortability()
    {
        // A Aeolian and C Ionian are relative modes — same note pool.
        const KeySig aMinor { 9, kAeolian, {}, Symmetric::None };
        const KeySig cMajor { 0, kIonian,  {}, Symmetric::None };
        CHECK(pcMask(aMinor) == pcMask(cMajor), "relative modes share the pool");

        // The Harmonic modifier (raise b7->7) is the SAME absolute operation on
        // the shared pool (G->G#), so the resulting pitch sets are identical...
        KeySig aHarm = aMinor; aHarm.modifiers = { NamedModifier::Harmonic };
        KeySig cHarm = cMajor; cHarm.modifiers = { NamedModifier::Harmonic };
        CHECK(pcMask(aHarm) == pcMask(cHarm), "Harmonic on relative modes -> same pcs");

        // ...but it reads as a different degree per root: leading-tone (7) in
        // the minor, raised-fifth (#5) in the relative major.
        CHECK(degreeNameOf(aHarm, NamedModifier::Harmonic) == "7",  "Harmonic reads '7' in minor");
        CHECK(degreeNameOf(cHarm, NamedModifier::Harmonic) == "#5", "Harmonic reads '#5' in major");

        // Blues add: same blue note either way, named b5 in minor / b3 in major.
        CHECK(degreeNameOf(aMinor, NamedModifier::Blues) == "b5", "Blues reads 'b5' in minor");
        CHECK(degreeNameOf(cMajor, NamedModifier::Blues) == "b3", "Blues reads 'b3' in major");
    }

    static void testAddVsAlter()
    {
        const KeySig cMajor { 0, kIonian, {}, Symmetric::None };
        CHECK(degrees(cMajor).size() == 7u, "diatonic has 7 notes");

        KeySig cBlues = cMajor; cBlues.modifiers = { NamedModifier::Blues };
        CHECK(degrees(cBlues).size() == 8u, "Add (Blues) grows the set by one");

        KeySig cHarm = cMajor; cHarm.modifiers = { NamedModifier::Harmonic };
        CHECK(degrees(cHarm).size() == 7u, "Alter (Harmonic) keeps the count");

        // The Add note is the only difference.
        auto base = pcsOf(cMajor), blues = pcsOf(cBlues);
        std::vector<int> added;
        std::set_difference(blues.begin(), blues.end(), base.begin(), base.end(),
                            std::back_inserter(added));
        CHECK((added == std::vector<int>{ 3 }), "C+Blues adds Eb (b3)");
    }

    static void testCompatibility()
    {
        // Plain diatonic: all the alterations apply at their home, and Blues
        // (an add of a note outside the pool) always applies.
        const KeySig cAeolian { 0, kAeolian, {}, Symmetric::None };
        CHECK(isCompatible(cAeolian, NamedModifier::Harmonic), "Harmonic ok in minor");
        CHECK(isCompatible(cAeolian, NamedModifier::Melodic),  "Melodic ok in minor");
        CHECK(isCompatible(cAeolian, NamedModifier::Blues),    "Blues ok in minor");

        // A second identical add is redundant -> incompatible.
        KeySig cBlues = cAeolian; cBlues.modifiers = { NamedModifier::Blues };
        CHECK(!isCompatible(cBlues, NamedModifier::Blues), "duplicate Blues add rejected");

        // At Mixolydian the Harmonic edge-target lands on the tonic — guarded.
        const KeySig cMixo { 0, kMixolydian, {}, Symmetric::None };
        CHECK(!isCompatible(cMixo, NamedModifier::Harmonic), "Harmonic rejected when it hits the root");

        // Harmonic at Phrygian yields Phrygian-dominant (raise b3->3): valid.
        const KeySig cPhryg { 0, kPhrygian, {}, Symmetric::None };
        CHECK(isCompatible(cPhryg, NamedModifier::Harmonic), "Harmonic ok in Phrygian");
        KeySig cPhrygDom = cPhryg; cPhrygDom.modifiers = { NamedModifier::Harmonic };
        CHECK((pcsOf(cPhrygDom) == std::set<int>{ 0, 1, 4, 5, 7, 8, 10 }), "Phrygian dominant set");

        // Symmetric scales sit outside the modifier system.
        const KeySig wt { 0, kIonian, {}, Symmetric::WholeTone };
        CHECK(!isCompatible(wt, NamedModifier::Harmonic), "no modifiers on symmetric scales");
    }

    static void testCoreNesting()
    {
        // The contiguous-fifths nesting of C major: pentatonic core (central 5)
        // = C D E G A; triad core (central 3) is a fifths arc, not the tonic
        // triad (DESIGN §39.11 note).
        const KeySig cMajor { 0, kIonian, {}, Symmetric::None };

        std::set<int> tier0, tier1;
        for (int pc = 0; pc < 12; ++pc)
        {
            const int t = coreTier(cMajor, pc);
            if (t == 0) tier0.insert(pc);
            if (t <= 1) tier1.insert(pc);
        }
        // Pentatonic core (tier <= 1): C D E G A.
        CHECK((tier1 == std::set<int>{ 0, 2, 4, 7, 9 }), "C major pentatonic core = C D E G A");
        // Triad core has exactly 3 notes and is a subset of the pentatonic core.
        CHECK(tier0.size() == 3u, "triad core has 3 notes");
        CHECK(std::includes(tier1.begin(), tier1.end(), tier0.begin(), tier0.end()),
              "triad core nests inside pentatonic core");

        // Out-of-scale notes are tier 3.
        CHECK(coreTier(cMajor, 1) == 3, "Db not in C major -> tier 3");
    }

    static void testQuantize()
    {
        const KeySig cMajor { 0, kIonian, {}, Symmetric::None };
        CHECK(quantize(cMajor, 60) == 60, "C4 in scale unchanged");
        CHECK(quantize(cMajor, 59) == 59, "B3 in scale unchanged");
        // In a gapless major every chromatic note is equidistant -> tie down.
        CHECK(quantize(cMajor, 61) == 60, "C#4 ties down to C4");
        CHECK(quantize(cMajor, 66) == 65, "F#4 ties down to F4");
        CHECK(quantize(cMajor, 70) == 69, "Bb4 ties down to A4");

        // Harmonic minor has a 3-semitone gap (F .. G#); G sits nearer G#.
        const KeySig aHarm { 9, kAeolian, { NamedModifier::Harmonic }, Symmetric::None };
        CHECK(quantize(aHarm, 67) == 68, "G4 snaps up to G#4 (nearer above) in A harmonic minor");
        CHECK(quantize(aHarm, 65) == 65, "F4 in scale unchanged");
    }

    static void testNames()
    {
        CHECK(classicalName({ 0, kIonian,  {}, Symmetric::None }) == "Ionian",  "plain mode name");
        CHECK(classicalName({ 9, kAeolian, {}, Symmetric::None }) == "Aeolian", "minor mode name");

        KeySig aHarm { 9, kAeolian, { NamedModifier::Harmonic }, Symmetric::None };
        CHECK(classicalName(aHarm) == "Harmonic minor", "Aeolian + Harmonic = Harmonic minor");

        KeySig aMel { 9, kAeolian, { NamedModifier::Melodic }, Symmetric::None };
        CHECK(classicalName(aMel) == "Melodic minor", "Aeolian + Melodic = Melodic minor");

        CHECK(classicalName({ 0, kIonian, {}, Symmetric::WholeTone }) == "Whole-tone", "whole-tone name");

        // An exotic combination with no textbook name returns empty.
        KeySig exotic { 0, kLydian, { NamedModifier::Harmonic }, Symmetric::None };
        CHECK(classicalName(exotic).empty(), "unnamed exotic scale -> empty");
    }

    void runScaleTests()
    {
        testBrightnessModes();
        testModifierPortability();
        testAddVsAlter();
        testCompatibility();
        testCoreNesting();
        testQuantize();
        testNames();
    }
}
