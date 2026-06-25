#pragma once

// Scale.h — the tonal core (Phase 10.2 / DESIGN §4.10, PRINCIPLES §23).
//
// Lockstep's key model is the circle of fifths as a single bright->dark line.
// A scale is a contiguous *window* of fifths with the root at offset 0:
//
//     ...  Db  Ab  Eb  Bb   F  | C |  G   D   A   E   B   F#  ...
//   (flat / dark side)        root        (sharp / bright side)
//
//   - brightness = window POSITION (the mode). For a 7-note window the 7
//     placements that contain the root are the modes, ordered bright->dark:
//     Lydian(0) Ionian(-1) Mixolydian(-2) Dorian(-3) Aeolian(-4) Phrygian(-5)
//     Locrian(-6). `brightness` is the offset of the flat edge: window =
//     [brightness, brightness+6] in fifths relative to the root.
//   - richness/core = window SIZE: the central 5 fifths (pentatonic core),
//     the central 3 (triad core). Used by the melodic generator (DESIGN §39.11).
//   - colour/exotic = functional MODIFIERS: an Add (augment, +1 note) or an
//     Alter (Raise/Lower an existing pool note). Each is anchored to the
//     window's flat edge (edgeOffset), which is invariant across relative
//     modes (same pool) and tracks brightness across parallel modes — so the
//     blues note reads as b5 in minor and b3 in the relative major, the same
//     operation either way.
//
// This header is JUCE-free and pure: everything is derived from a KeySig at
// call time; only the semantic axes (root/brightness/modifiers) are stored.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lockstep
{
    // ---- Fifths <-> pitch-class -------------------------------------------

    // Semitone interval (0..11) of the note `f` fifths from the root.
    // A fifth is 7 semitones; interval(f) = (7*f) mod 12, normalised positive.
    [[nodiscard]] inline int fifthInterval(int f) noexcept
    {
        int v = (7 * f) % 12;
        return v < 0 ? v + 12 : v;
    }

    // ---- Mode brightness constants ----------------------------------------

    enum Brightness : int8_t
    {
        kLydian     =  0,   // brightest
        kIonian     = -1,
        kMixolydian = -2,
        kDorian     = -3,
        kAeolian    = -4,
        kPhrygian   = -5,
        kLocrian    = -6,   // darkest
    };

    [[nodiscard]] inline const char* modeName(int brightness) noexcept
    {
        switch (brightness)
        {
            case kLydian:     return "Lydian";
            case kIonian:     return "Ionian";
            case kMixolydian: return "Mixolydian";
            case kDorian:     return "Dorian";
            case kAeolian:    return "Aeolian";
            case kPhrygian:   return "Phrygian";
            case kLocrian:    return "Locrian";
            default:          return "";
        }
    }

    // ---- Functional modifiers ---------------------------------------------

    // A primitive modifier op, anchored to the window flat edge by `edgeOffset`
    // (fifths from the flat edge; window notes are 0..6, an Add can sit
    // outside). Op resolves against the current (root, brightness).
    struct ModOp
    {
        enum class Kind : uint8_t { Add, Raise, Lower };
        Kind kind;
        int8_t edgeOffset;
    };

    // The v1 named, curated modifiers (add-only ids — never renumber/reorder;
    // serialized by value). Each expands to one or more ModOps. Edge offsets
    // are derived in the modifier's natural home mode (see DESIGN §4.10) so the
    // name reads correctly there; the op is brightness-portable from there.
    enum class NamedModifier : uint8_t
    {
        Harmonic       = 0,   // raise b7->7      (home Aeolian)
        Melodic        = 1,   // raise b6->6, b7->7
        DoubleHarmonic = 2,   // raise b3->3, b7->7 (home Phrygian)
        HarmonicMajor  = 3,   // lower 6->b6      (home Ionian)
        Blues          = 4,   // add b5 (== b3 of the relative major)
        Neapolitan     = 5,   // lower 2->b2      (home major)
    };

    // Expand a named modifier into its primitive ops.
    [[nodiscard]] inline std::vector<ModOp> expandModifier(NamedModifier m)
    {
        using K = ModOp::Kind;
        switch (m)
        {
            case NamedModifier::Harmonic:       return { { K::Raise, 2 } };
            case NamedModifier::Melodic:        return { { K::Raise, 0 }, { K::Raise, 2 } };
            case NamedModifier::DoubleHarmonic: return { { K::Raise, 2 }, { K::Raise, 3 } };
            case NamedModifier::HarmonicMajor:  return { { K::Lower, 4 } };
            case NamedModifier::Blues:          return { { K::Add,  -2 } };
            case NamedModifier::Neapolitan:     return { { K::Lower, 3 } };
        }
        return {};
    }

    [[nodiscard]] inline const char* modifierName(NamedModifier m) noexcept
    {
        switch (m)
        {
            case NamedModifier::Harmonic:       return "Harmonic";
            case NamedModifier::Melodic:        return "Melodic";
            case NamedModifier::DoubleHarmonic: return "Double-harmonic";
            case NamedModifier::HarmonicMajor:  return "Harmonic-major";
            case NamedModifier::Blues:          return "Blues";
            case NamedModifier::Neapolitan:     return "Neapolitan";
        }
        return "";
    }

    // ---- KeySig -----------------------------------------------------------

    enum class Symmetric : uint8_t { None = 0, WholeTone = 1, Diminished = 2 };

    // The stored key signature: semantic axes only. Everything else (mask,
    // degrees, cores, quantize, names) is derived. Default = C Ionian.
    struct KeySig
    {
        uint8_t   root = 0;                 // 0..11 pitch class of the tonic
        int8_t    brightness = kIonian;     // window flat-edge offset (mode)
        std::vector<NamedModifier> modifiers;
        Symmetric symmetric = Symmetric::None;   // overrides brightness/modifiers when set

        bool operator==(const KeySig& o) const
        {
            return root == o.root && brightness == o.brightness
                && symmetric == o.symmetric && modifiers == o.modifiers;
        }
    };

    // ---- Derivation: pitch-class mask -------------------------------------

    [[nodiscard]] inline bool maskHas(uint16_t mask, int pc) noexcept
    {
        return (mask >> (((pc % 12) + 12) % 12)) & 1u;
    }

    [[nodiscard]] inline uint16_t setPc(uint16_t mask, int pc) noexcept
    {
        return static_cast<uint16_t>(mask | (1u << (((pc % 12) + 12) % 12)));
    }

    [[nodiscard]] inline uint16_t clearPc(uint16_t mask, int pc) noexcept
    {
        return static_cast<uint16_t>(mask & ~(1u << (((pc % 12) + 12) % 12)));
    }

    // Pitch class of the note at edgeOffset `e` from the flat edge, for a
    // given root/brightness.
    [[nodiscard]] inline int pcAtEdge(int root, int brightness, int e) noexcept
    {
        return ((root + fifthInterval(brightness + e)) % 12 + 12) % 12;
    }

    // The 7-note base diatonic window mask (no modifiers, no symmetric set).
    [[nodiscard]] inline uint16_t baseWindowMask(int root, int brightness) noexcept
    {
        uint16_t mask = 0;
        for (int e = 0; e <= 6; ++e)
            mask = setPc(mask, pcAtEdge(root, brightness, e));
        return mask;
    }

    [[nodiscard]] inline uint16_t symmetricMask(int root, Symmetric s) noexcept
    {
        uint16_t mask = 0;
        if (s == Symmetric::WholeTone)
            for (int i : { 0, 2, 4, 6, 8, 10 }) mask = setPc(mask, root + i);
        else if (s == Symmetric::Diminished)   // whole-half
            for (int i : { 0, 2, 3, 5, 6, 8, 9, 11 }) mask = setPc(mask, root + i);
        return mask;
    }

    // Apply one primitive op to a mask for the given root/brightness.
    [[nodiscard]] inline uint16_t applyModOp(uint16_t mask, int root, int brightness, ModOp op) noexcept
    {
        const int pc = pcAtEdge(root, brightness, op.edgeOffset);
        switch (op.kind)
        {
            case ModOp::Kind::Add:   return setPc(mask, pc);
            case ModOp::Kind::Raise: return setPc(clearPc(mask, pc), pc + 1);
            case ModOp::Kind::Lower: return setPc(clearPc(mask, pc), pc - 1);
        }
        return mask;
    }

    // The effective pitch-class set (after modifiers / symmetric override).
    [[nodiscard]] inline uint16_t pcMask(const KeySig& k)
    {
        if (k.symmetric != Symmetric::None)
            return symmetricMask(k.root, k.symmetric);

        uint16_t mask = baseWindowMask(k.root, k.brightness);
        for (NamedModifier m : k.modifiers)
            for (ModOp op : expandModifier(m))
                mask = applyModOp(mask, k.root, k.brightness, op);
        return mask;
    }

    // Ordered semitone offsets from the root (ascending, first entry 0).
    [[nodiscard]] inline std::vector<int> degrees(const KeySig& k)
    {
        const uint16_t mask = pcMask(k);
        std::vector<int> out;
        for (int i = 0; i < 12; ++i)
        {
            const int pc = (k.root + i) % 12;
            if (maskHas(mask, pc))
                out.push_back(i);
        }
        return out;
    }

    // ---- Compatibility -----------------------------------------------------

    // A named modifier is compatible with the current key iff every one of its
    // ops makes a real change: an Add must introduce a new pitch class (not
    // already present); a Raise/Lower must target a present note and land on a
    // pitch class not already in the set (no collision). Evaluated against the
    // base + already-applied modifiers, so a second modifier sees the first.
    [[nodiscard]] inline bool isCompatible(const KeySig& k, NamedModifier candidate)
    {
        if (k.symmetric != Symmetric::None)
            return false;   // symmetric scales sit outside the modifier system

        uint16_t mask = baseWindowMask(k.root, k.brightness);
        for (NamedModifier m : k.modifiers)
            for (ModOp op : expandModifier(m))
                mask = applyModOp(mask, k.root, k.brightness, op);

        for (ModOp op : expandModifier(candidate))
        {
            const int pc  = pcAtEdge(k.root, k.brightness, op.edgeOffset);
            const int res = (((op.kind == ModOp::Kind::Lower ? pc - 1 : pc + 1) % 12) + 12) % 12;
            switch (op.kind)
            {
                case ModOp::Kind::Add:
                    if (maskHas(mask, pc)) return false;      // redundant add
                    mask = setPc(mask, pc);
                    break;
                case ModOp::Kind::Raise:
                case ModOp::Kind::Lower:
                    if (pc == k.root) return false;           // never alter the tonic
                    if (!maskHas(mask, pc)) return false;     // nothing to move
                    if (maskHas(mask, res)) return false;     // collision
                    mask = setPc(clearPc(mask, pc), res);
                    break;
            }
        }
        return true;
    }

    // ---- Core tiers (melodic-generator weighting) -------------------------

    // The contiguous-fifths nesting of the bright/dark model: tier 0 = the
    // central 3 fifths (triad core), 1 = the central 5 (pentatonic core),
    // 2 = the outer pair of the 7-window or a modifier-added note (colour),
    // 3 = not in the scale. NB this is a fifths-arc, not necessarily the tonic
    // triad; the melodic generator may additionally emphasise the tonic.
    [[nodiscard]] inline int coreTier(const KeySig& k, int pitchClass)
    {
        const int pc = (((pitchClass % 12) + 12) % 12);
        if (!maskHas(pcMask(k), pc))
            return 3;
        if (k.symmetric != Symmetric::None)
            return 1;
        for (int e = 0; e <= 6; ++e)
        {
            if (pcAtEdge(k.root, k.brightness, e) == pc)
            {
                if (e >= 2 && e <= 4) return 0;
                if (e >= 1 && e <= 5) return 1;
                return 2;
            }
        }
        return 2;   // modifier-added / shifted-out note
    }

    // ---- Quantize ----------------------------------------------------------

    // Snap a MIDI note to the nearest pitch in the scale, preserving octave
    // proximity. Ties resolve downward (to the flatter note).
    [[nodiscard]] inline int quantize(const KeySig& k, int midiNote)
    {
        const uint16_t mask = pcMask(k);
        if (mask == 0) return midiNote;

        int best = midiNote;
        int bestDist = 1000;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (!maskHas(mask, pc)) continue;
            const int up = midiNote + (((pc - midiNote) % 12) + 12) % 12;  // nearest >= note
            const int down = up - 12;
            const int duUp = up - midiNote;        // 0..11
            const int duDown = midiNote - down;    // 1..12
            if (duDown <= duUp)                    // tie -> downward
            {
                if (duDown < bestDist) { bestDist = duDown; best = down; }
            }
            else if (duUp < bestDist) { bestDist = duUp; best = up; }
        }
        return best;
    }

    // ---- Naming ------------------------------------------------------------

    // ASCII degree name for a semitone interval from root (b-spelled). NB
    // keep this ASCII: juce::String(const char*) asserts on non-ASCII bytes.
    [[nodiscard]] inline const char* intervalDegreeName(int interval) noexcept
    {
        static constexpr std::array<const char*, 12> kNames =
            { "1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };
        return kNames[static_cast<size_t>((((interval % 12) + 12) % 12))];
    }

    // Scale-step number (1..7) of a semitone interval, and whether it is the
    // flat spelling of that step (b2/b3/b5/b6/b7).
    [[nodiscard]] inline int degreeNumber(int interval) noexcept
    {
        static constexpr std::array<int, 12> kNum = { 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7 };
        return kNum[static_cast<size_t>((((interval % 12) + 12) % 12))];
    }
    [[nodiscard]] inline bool isFlatDegree(int interval) noexcept
    {
        static constexpr std::array<bool, 12> kFlat =
            { false, true, false, true, false, false, true, false, true, false, true, false };
        return kFlat[static_cast<size_t>((((interval % 12) + 12) % 12))];
    }

    // The degree name (per current root) a named modifier reads as. Reflects
    // the op direction, so it tracks brightness/root the way the player thinks:
    // Harmonic is "7" in minor but "#5" in the relative major; Blues is "b5"
    // in minor but "b3" in the relative major. Multi-op modifiers report their
    // first op.
    [[nodiscard]] inline std::string degreeNameOf(const KeySig& k, NamedModifier m)
    {
        const auto ops = expandModifier(m);
        if (ops.empty()) return {};
        const ModOp op = ops.front();
        const int basePc = pcAtEdge(k.root, k.brightness, op.edgeOffset);
        const int baseInt = ((basePc - k.root) % 12 + 12) % 12;
        const int num = degreeNumber(baseInt);
        switch (op.kind)
        {
            case ModOp::Kind::Add:
                return intervalDegreeName(((basePc - k.root) % 12 + 12) % 12);
            case ModOp::Kind::Raise:
                // raising a flat degree restores it to natural; raising a
                // natural degree sharpens it.
                return isFlatDegree(baseInt) ? std::to_string(num)
                                             : ("#" + std::to_string(num));
            case ModOp::Kind::Lower:
                return "b" + std::to_string(num);
        }
        return {};
    }

    // Classical scale name, "" when none recognised. Plain modes resolve to
    // their mode name; symmetric scales to their name; a small set of common
    // single-modifier combinations get their textbook names.
    [[nodiscard]] inline std::string classicalName(const KeySig& k)
    {
        if (k.symmetric == Symmetric::WholeTone)  return "Whole-tone";
        if (k.symmetric == Symmetric::Diminished) return "Diminished";

        if (k.modifiers.empty())
            return modeName(k.brightness);

        if (k.modifiers.size() == 1)
        {
            switch (k.modifiers.front())
            {
                case NamedModifier::Harmonic:
                    if (k.brightness == kAeolian)  return "Harmonic minor";
                    break;
                case NamedModifier::Melodic:
                    if (k.brightness == kAeolian)  return "Melodic minor";
                    break;
                case NamedModifier::HarmonicMajor:
                    if (k.brightness == kIonian)   return "Harmonic major";
                    break;
                case NamedModifier::DoubleHarmonic:
                    if (k.brightness == kPhrygian) return "Double harmonic";
                    break;
                case NamedModifier::Neapolitan:
                    if (k.brightness == kIonian)   return "Neapolitan major";
                    if (k.brightness == kAeolian)  return "Neapolitan minor";
                    break;
                case NamedModifier::Blues:
                    if (k.brightness == kAeolian)  return "Blues (minor)";
                    break;
            }
        }
        return {};   // a valid exotic scale with no common name
    }

    // Pitch-class name (sharp-spelled, ASCII) for display of the root.
    [[nodiscard]] inline const char* pitchClassName(int pc) noexcept
    {
        static constexpr std::array<const char*, 12> kNames =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return kNames[static_cast<size_t>((((pc % 12) + 12) % 12))];
    }
}
