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

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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
    // serialized by value). A modifier IS its list of fifths-offset ops; that is
    // the whole definition. The common names ("harmonic", "blues") and the
    // "reads as ... in <mode>" notes are only there to bridge to how musicians
    // talk — no logic anywhere consults a mode. Brightness slides the window, the
    // op rides along, and its cross-mode behaviour (degree + availability) is
    // inferred from the geometry, never tabulated.
    enum class NamedModifier : uint8_t
    {
        Harmonic       = 0,   // raise b7->7      (home Aeolian)
        Melodic        = 1,   // raise b6->6, b7->7
        DoubleHarmonic = 2,   // raise b3->3, b7->7 (home Phrygian)
        HarmonicMajor  = 3,   // lower 6->b6      (home Ionian)
        Blues          = 4,   // add the blue note (one fifths-anchored Add) —
                              //   b7 Lydian / b3 Ionian / b6 Mixolydian /
                              //   b2 Dorian / b5 Aeolian / b1 Phrygian / b4
                              //   Locrian. Valid in all 7 modes of the full
                              //   scale; pentatonic/triad cores narrow it
                              //   (blueNoteFitsCore), a generator-time concern.
        Neapolitan     = 5,   // lower 2->b2      (home major)
    };

    // Canonical apply order (DESIGN §4.10): modifiers are always applied in this
    // fixed order regardless of selection order, so any chosen set is
    // order-independent. Lower rank applies first. Conflicts (a later op whose
    // target was already moved) leave the later modifier dormant.
    // THE single source of truth for modifier order: this list is both the
    // canonical apply precedence (lower index applies first) AND the left-to-right
    // order of the editor's modifier buttons, so the two can never diverge.
    // Single-note alterations (by commonality) first, then two-note, then the
    // Blues add last — so in a (force-selected) conflict the simpler change
    // survives and the decorative blue note yields to the structural alterations.
    inline constexpr std::array<NamedModifier, 6> kModifierOrder = {
        NamedModifier::Harmonic,        // 1 note
        NamedModifier::Neapolitan,      // 1 note
        NamedModifier::HarmonicMajor,   // 1 note
        NamedModifier::Melodic,         // 2 notes
        NamedModifier::DoubleHarmonic,  // 2 notes
        NamedModifier::Blues,           // the add, last
    };

    [[nodiscard]] inline int modifierRank(NamedModifier m) noexcept
    {
        for (int i = 0; i < static_cast<int>(kModifierOrder.size()); ++i)
            if (kModifierOrder[static_cast<size_t>(i)] == m) return i;
        return 0;
    }

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

    // The scale type IS the note count: Triad(3) / Pentatonic(5) / Diatonic(7)
    // are the central-N fifths of the brightness window (they use brightness +
    // modifiers); WholeTone(6) / Diminished(8) are symmetric (brightness and
    // modifiers do not apply). Add-only ids — never renumber. Default Diatonic.
    enum class ScaleType : uint8_t
    {
        Diatonic   = 0,   // 7 notes
        Pentatonic = 1,   // 5 notes (central 5 fifths)
        Triad      = 2,   // 3 notes (central 3 fifths)
        WholeTone  = 3,   // 6 notes, symmetric
        Diminished = 4,   // 8 notes, symmetric (whole-half)
        Chromatic  = 5,   // all 12 notes — deliberate "no scale" (no quantize)
    };

    [[nodiscard]] inline int noteCountOf(ScaleType t) noexcept
    {
        switch (t)
        {
            case ScaleType::Triad:      return 3;
            case ScaleType::Pentatonic: return 5;
            case ScaleType::WholeTone:  return 6;
            case ScaleType::Diatonic:   return 7;
            case ScaleType::Diminished: return 8;
            case ScaleType::Chromatic:  return 12;
        }
        return 7;
    }
    [[nodiscard]] inline bool isSymmetric(ScaleType t) noexcept
    {
        return t == ScaleType::WholeTone || t == ScaleType::Diminished;
    }
    // The fifths scales (Triad/Penta/Diatonic) are the only ones that use the
    // brightness window + modifiers; symmetric and chromatic do not.
    [[nodiscard]] inline bool hasFifthsWindow(ScaleType t) noexcept
    {
        return t == ScaleType::Triad || t == ScaleType::Pentatonic || t == ScaleType::Diatonic;
    }
    [[nodiscard]] inline const char* scaleTypeName(ScaleType t) noexcept
    {
        switch (t)
        {
            case ScaleType::Triad:      return "Triad";
            case ScaleType::Pentatonic: return "Penta";
            case ScaleType::Diatonic:   return "Diatonic";
            case ScaleType::WholeTone:  return "Whole-tone";
            case ScaleType::Diminished: return "Dimin";
            case ScaleType::Chromatic:  return "Chromatic";
        }
        return "Diatonic";
    }

    // The stored key signature: semantic axes only. Everything else (mask,
    // degrees, cores, quantize, names) is derived. Default = D Dorian — the
    // symmetric centre of the system: D is the centre of the circle of fifths
    // and Dorian is the centre of the bright/dark axis (its interval pattern is
    // a palindrome), so the default leans neither sharp nor flat.
    struct KeySig
    {
        uint8_t   root = 2;                 // D — centre of the circle of fifths
        int8_t    brightness = kDorian;     // Dorian — centre of the bright/dark axis
        std::vector<NamedModifier> modifiers;
        ScaleType scaleType = ScaleType::Diatonic;   // note count / family

        bool operator==(const KeySig& o) const
        {
            return root == o.root && brightness == o.brightness
                && scaleType == o.scaleType && modifiers == o.modifiers;
        }
    };

    [[nodiscard]] inline bool hasModifier(const KeySig& k, NamedModifier m) noexcept
    {
        for (NamedModifier x : k.modifiers)
            if (x == m) return true;
        return false;
    }

    [[nodiscard]] inline uint16_t pcMask(const KeySig& k);   // defined below

    // Whether a set modifier currently has an effect (bright) vs is dormant
    // (grey): removing it changes the scale iff it is applying right now.
    [[nodiscard]] inline bool modifierApplies(const KeySig& k, NamedModifier m)
    {
        if (!hasModifier(k, m)) return false;
        KeySig without = k;
        without.modifiers.erase(
            std::remove(without.modifiers.begin(), without.modifiers.end(), m),
            without.modifiers.end());
        return pcMask(k) != pcMask(without);
    }

    // Serialization helpers: the modifier list is a set of named modifiers, so
    // a 6-bit mask round-trips it (compatibility forbids duplicates, and valid
    // combinations are order-independent). NamedModifier ids are add-only.
    [[nodiscard]] inline uint8_t packModifiers(const std::vector<NamedModifier>& mods) noexcept
    {
        uint8_t bits = 0;
        for (NamedModifier m : mods)
            bits = static_cast<uint8_t>(bits | (1u << static_cast<uint8_t>(m)));
        return bits;
    }

    [[nodiscard]] inline std::vector<NamedModifier> unpackModifiers(uint8_t bits)
    {
        std::vector<NamedModifier> out;
        for (uint8_t i = 0; i < 6; ++i)
            if (bits & (1u << i))
                out.push_back(static_cast<NamedModifier>(i));
        return out;
    }

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

    // The central `size` fifths of the brightness window (size 3/5/7). For 7 it
    // is the full diatonic window; for 5 it is the pentatonic, for 3 the triad
    // core — the same nesting used everywhere else.
    [[nodiscard]] inline uint16_t coreWindowMask(int root, int brightness, int size) noexcept
    {
        uint16_t mask = 0;
        const int pad = (7 - size) / 2;
        for (int e = pad; e < pad + size; ++e)
            mask = setPc(mask, pcAtEdge(root, brightness, e));
        return mask;
    }

    // The 7-note base diatonic window mask (no modifiers).
    [[nodiscard]] inline uint16_t baseWindowMask(int root, int brightness) noexcept
    {
        return coreWindowMask(root, brightness, 7);
    }

    [[nodiscard]] inline uint16_t symmetricMask(int root, ScaleType s) noexcept
    {
        uint16_t mask = 0;
        if (s == ScaleType::WholeTone)
            for (int i : { 0, 2, 4, 6, 8, 10 }) mask = setPc(mask, root + i);
        else if (s == ScaleType::Diminished)   // whole-half
            for (int i : { 0, 2, 3, 5, 6, 8, 9, 11 }) mask = setPc(mask, root + i);
        return mask;
    }

    // Apply one primitive op to a mask, but ONLY when it makes a valid change.
    // An Add of a note already present, or a Raise/Lower that would move the
    // tonic, collide with an existing note, or has nothing to move, is a no-op.
    // This makes pcMask robust to *dormant* modifiers: a modifier set in one
    // tonality stays in the list but simply does nothing where it does not apply
    // (it greys out in the editor instead of corrupting the scale).
    [[nodiscard]] inline uint16_t applyModOp(uint16_t mask, int root, int brightness, ModOp op) noexcept
    {
        const int pc = pcAtEdge(root, brightness, op.edgeOffset);
        if (op.kind == ModOp::Kind::Add)
            return maskHas(mask, pc) ? mask : setPc(mask, pc);
        const int res = (op.kind == ModOp::Kind::Lower ? pc - 1 : pc + 1);
        if (pc == static_cast<int>(root) || !maskHas(mask, pc) || maskHas(mask, res))
            return mask;
        return setPc(clearPc(mask, pc), res);
    }

    [[nodiscard]] inline bool blueNoteFitsCore(const KeySig& k, int coreSize);

    // Apply ALL of a modifier's ops atomically. Returns the new mask and `true`
    // only when every op makes a valid change against the running mask; otherwise
    // the mask is unchanged and `false` (the whole modifier is dormant). This is
    // why a multi-note modifier is all-or-nothing — half-applying corrupted both
    // the scale and the editor highlighting.
    [[nodiscard]] inline std::pair<uint16_t, bool>
    tryApplyModifier(uint16_t mask, const KeySig& k, NamedModifier m, int size)
    {
        uint16_t work = mask;
        for (ModOp op : expandModifier(m))
        {
            if (op.kind == ModOp::Kind::Add && !blueNoteFitsCore(k, size))
                return { mask, false };
            const uint16_t next = applyModOp(work, k.root, k.brightness, op);
            if (next == work)
                return { mask, false };   // an op had no valid effect → reject all
            work = next;
        }
        return { work, true };
    }

    // The base scale (no modifiers) for the central-N window, or the symmetric /
    // chromatic set. Modifiers are layered on by pcMask.
    [[nodiscard]] inline uint16_t baseMask(const KeySig& k)
    {
        if (k.scaleType == ScaleType::Chromatic)
            return 0x0FFFu;   // all 12 notes
        if (isSymmetric(k.scaleType))
            return symmetricMask(k.root, k.scaleType);
        return coreWindowMask(k.root, k.brightness, noteCountOf(k.scaleType));
    }

    // The effective pitch-class set. Symmetric/chromatic ignore modifiers; the
    // fifths types apply modifiers in the canonical order (modifierRank), each
    // atomically — a modifier that does not fully apply in context is dormant.
    [[nodiscard]] inline uint16_t pcMask(const KeySig& k)
    {
        uint16_t mask = baseMask(k);
        if (!hasFifthsWindow(k.scaleType))
            return mask;

        const int size = noteCountOf(k.scaleType);
        std::vector<NamedModifier> ordered = k.modifiers;
        std::sort(ordered.begin(), ordered.end(),
                  [](NamedModifier a, NamedModifier b) { return modifierRank(a) < modifierRank(b); });
        for (NamedModifier m : ordered)
            mask = tryApplyModifier(mask, k, m, size).first;
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
    // already present); a Raise/Lower must target a present note, not the tonic,
    // and land on a pitch class not already in the set (no collision). Evaluated
    // against the base + already-applied modifiers, so a second modifier sees the
    // first. For the v1 modifiers the tonic guard is the only thing that bars a
    // modifier from a mode (a Raise/Lower at edge offset e hits the root when
    // brightness == -e) — so availability is purely a fifths fact, not a table.
    // Whether `candidate` can be added on top of the key's already-selected
    // modifiers (cumulative): apply the current set in canonical order, then test
    // whether the candidate applies atomically. So once a conflicting modifier is
    // selected, the candidate correctly reports unavailable.
    [[nodiscard]] inline bool isCompatible(const KeySig& k, NamedModifier candidate)
    {
        if (!hasFifthsWindow(k.scaleType))
            return false;   // symmetric / chromatic scales take no modifiers

        const int size = noteCountOf(k.scaleType);
        uint16_t mask = baseMask(k);
        std::vector<NamedModifier> ordered = k.modifiers;
        std::sort(ordered.begin(), ordered.end(),
                  [](NamedModifier a, NamedModifier b) { return modifierRank(a) < modifierRank(b); });
        for (NamedModifier m : ordered)
            mask = tryApplyModifier(mask, k, m, size).first;
        // If the candidate is already applied above, re-testing it yields no
        // effect (false) — a present modifier is not "available to add".
        return tryApplyModifier(mask, k, candidate, size).second;
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
        if (!hasFifthsWindow(k.scaleType))
            return 1;   // symmetric / chromatic — no fifths nesting
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

    // Whether an added blue/colour note belongs at a given core size — pure
    // circle-of-fifths, no scale or mode names. The size-N core is the central N
    // fifths of the 7-note window, so its flat edge sits (7-N)/2 fifths sharp of
    // the full-scale flat edge (which is at `brightness`). The blue note belongs
    // to that core exactly when the tonic — fifths offset 0 — lies inside it.
    // The exclusions are inferred, not enumerated: 7 = always; 5 drops the two
    // window-edge roots (the "Lydian/Locrian" cases); 3 drops the next pair too.
    // Generators gate by the core they work in (DESIGN §39.11); the key editor
    // edits the full scale, so it never restricts.
    [[nodiscard]] inline bool blueNoteFitsCore(const KeySig& k, int coreSize)
    {
        if (!hasFifthsWindow(k.scaleType)) return false;   // no fifths window
        const int flat = k.brightness + (7 - coreSize) / 2;  // core flat edge, in fifths
        return flat <= 0 && 0 <= flat + coreSize - 1;        // tonic inside the core
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
    [[nodiscard]] inline std::string degreeNameOfOp(const KeySig& k, ModOp op)
    {
        const int basePc = pcAtEdge(k.root, k.brightness, op.edgeOffset);
        const int baseInt = ((basePc - k.root) % 12 + 12) % 12;
        const int num = degreeNumber(baseInt);
        switch (op.kind)
        {
            case ModOp::Kind::Add:
                // An added "blue" note is named as the flat of the degree a
                // semitone ABOVE it, so the same fifths-anchored operation reads
                // b3 in Ionian, b2 in Dorian, b1 in Phrygian, b6 in Mixolydian,
                // b5 in Aeolian — one operation, mode-specific name.
                return "b" + std::to_string(degreeNumber((baseInt + 1) % 12));
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

    // The degree name(s) a named modifier reads as, per the current root —
    // reflecting op direction (Harmonic "7" in minor / "#5" in the relative
    // major; Blues "b5"/"b3"). Multi-note modifiers list every altered degree
    // (e.g. Melodic "6 7", Double-harmonic "3 7").
    [[nodiscard]] inline std::string degreeNameOf(const KeySig& k, NamedModifier m)
    {
        std::string out;
        for (ModOp op : expandModifier(m))
        {
            if (!out.empty()) out += " ";
            out += degreeNameOfOp(k, op);
        }
        return out;
    }

    // The valid brightness range for a scale type. The fifths scales of size N
    // only span the N modes whose root stays inside the central-N window: 7 ->
    // all (Lydian..Locrian), 5 -> drops Lydian/Locrian, 3 -> Mixolydian/Dorian/
    // Aeolian. Symmetric/chromatic have no brightness.
    [[nodiscard]] inline std::pair<int, int> brightnessRange(ScaleType t) noexcept
    {
        if (!hasFifthsWindow(t)) return { kDorian, kDorian };
        const int n = noteCountOf(t);
        const int bMax = -(7 - n) / 2;
        return { bMax - (n - 1), bMax };
    }
    [[nodiscard]] inline int clampBrightness(ScaleType t, int b) noexcept
    {
        const auto [lo, hi] = brightnessRange(t);
        return std::clamp(b, lo, hi);
    }

    // Classical scale name, "" when none recognised. Plain modes resolve to
    // their mode name; symmetric scales to their name; a small set of common
    // single-modifier combinations get their textbook names.
    [[nodiscard]] inline std::string classicalName(const KeySig& k)
    {
        if (k.scaleType == ScaleType::WholeTone)  return "Whole-tone";
        if (k.scaleType == ScaleType::Diminished) return "Diminished";
        // Reduced-size scales read as "<mode> penta/triad".
        if (k.scaleType == ScaleType::Pentatonic)
            return std::string(modeName(k.brightness)) + " penta";
        if (k.scaleType == ScaleType::Triad)
            return std::string(modeName(k.brightness)) + " triad";

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

    // The root selector steps through pitch classes in circle-of-fifths order,
    // centred on D (index 6), so Root and Brightness both move along the fifths
    // line: turning sharp-ward goes D A E B F# C#, flat-ward D G C F Bb Eb Ab.
    [[nodiscard]] inline int rootPcAtFifthsIndex(int idx) noexcept
    {
        static constexpr std::array<int, 12> kOrder =
            { 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1 };   // Ab Eb Bb F C G D A E B F# C#
        return kOrder[static_cast<size_t>(((idx % 12) + 12) % 12)];
    }
    [[nodiscard]] inline int fifthsIndexOfRootPc(int pc) noexcept
    {
        const int p = ((pc % 12) + 12) % 12;
        for (int i = 0; i < 12; ++i)
            if (rootPcAtFifthsIndex(i) == p) return i;
        return 6;   // D
    }

    // ---- Per-track Scale stage (pitch conform; DESIGN §4.10) --------------

    // A per-track output stage that conforms notes to the effective key. "Scale"
    // (not "quantize", which is time). Off = passthrough; Snap = move to the
    // nearest in-key note; Filter = drop out-of-key notes.
    enum class ScaleMode : uint8_t { Off = 0, Snap = 1, Filter = 2 };

    [[nodiscard]] inline const char* scaleModeName(ScaleMode m) noexcept
    {
        switch (m)
        {
            case ScaleMode::Off:    return "Off";
            case ScaleMode::Snap:   return "Snap";
            case ScaleMode::Filter: return "Filter";
        }
        return "Off";
    }

    // Apply the Scale stage to one note for the given key. Returns the output note,
    // or nullopt when Filter drops an out-of-key note. Deterministic, so a note-on
    // and its matching note-off transform identically.
    [[nodiscard]] inline std::optional<int> applyScaleMode(const KeySig& k, ScaleMode mode, int midiNote)
    {
        if (mode == ScaleMode::Off) return midiNote;
        const bool inKey = maskHas(pcMask(k), ((midiNote % 12) + 12) % 12);
        if (mode == ScaleMode::Filter)
            return inKey ? std::optional<int>(midiNote) : std::nullopt;
        return quantize(k, midiNote);   // Snap
    }

    // Pitch-class name (sharp-spelled, ASCII) for display of the root.
    [[nodiscard]] inline const char* pitchClassName(int pc) noexcept
    {
        static constexpr std::array<const char*, 12> kNames =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return kNames[static_cast<size_t>((((pc % 12) + 12) % 12))];
    }
}
