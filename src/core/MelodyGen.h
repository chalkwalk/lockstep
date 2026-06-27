#pragma once

// MelodyGen.h — deterministic melodic generator (Phase 10.7 / DESIGN §39.11).
//
// A *print* tool, in the Euclid mould: it consumes the effective KeySig plus a
// small parameter set and stamps mono notes into a per-step buffer. It is pure,
// seeded and JUCE-free, so the same (key, length, params) always yields the same
// melody — preview and commit therefore agree, and a saved seed reproduces.
//
// The spine is METRIC STRENGTH. Lockstep already has two strength axes:
//   - beat strength: the metric hierarchy of a bar (downbeat > half > quarter
//     > eighth > off-beat), and
//   - note strength: the brightness-line core nesting (triad core inside
//     pentatonic core inside the full scale — coreTier()).
// This generator couples them: strong beats get strong (consonant, low-tier)
// notes and longer durations; weak beats get weaker (outer/colour) notes and
// shorter durations. Onsets are chosen strongest-beat-first, so as density rises
// notes fill in on ever-weaker beats; the gap a short weak note leaves becomes a
// REST that bridges into the next (stronger) onset. Pitch shape across the
// onsets follows a contour (rise/fall/arch/walk) with seed-driven deviation
// bounded by the step/leap parameter. Output is ordinary, hand-editable steps.

#include "MusicalGate.h"
#include "Scale.h"
#include "Step.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lockstep
{
    // Pitch contour shapes (read-clean ordering; never persisted directly).
    enum class MelodyContour : uint8_t { Rise = 0, Fall = 1, Arch = 2, Walk = 3 };

    [[nodiscard]] inline const char* melodyContourName(int c) noexcept
    {
        switch (c)
        {
            case 0: return "Rise";
            case 1: return "Fall";
            case 2: return "Arch";
            case 3: return "Walk";
        }
        return "Rise";
    }

    // Note-pool width: the widest core tier the melody may draw from. 0 = triad
    // core (central 3 fifths), 1 = pentatonic core (central 5), 2 = full scale.
    // Mirrors coreTier(); symmetric/chromatic keys ignore it.
    [[nodiscard]] inline const char* melodyCoreName(int c) noexcept
    {
        switch (c)
        {
            case 0: return "Triad";
            case 1: return "Penta";
            case 2: return "Full";
        }
        return "Penta";
    }

    struct MelodyParams
    {
        int density  = 8;   // number of onsets (placed strongest-beat-first)
        int coreBias = 1;   // widest core tier: 0 = triad, 1 = penta, 2 = full
        int contour  = 0;   // MelodyContour
        int octaves  = 2;   // pitch span in octaves above the root (1..4)
        int stepLeap = 30;  // 0..100: deviation from the contour, in pool steps
        uint32_t seed = 1;  // deterministic seed
    };

    // One generated step: trig + the mono note + its gate (valid only when trig).
    struct MelodyStep
    {
        bool        trig = false;
        int         note = 0;
        MusicalGate gate = MusicalGate::None;
    };

    // Deterministic PRNG (xorshift32) — the generator owns its randomness and
    // never touches global RNG state.
    [[nodiscard]] inline uint32_t mgNext(uint32_t& s) noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    // Metric strength of a step within a bar of `length` steps: the downbeat is
    // strongest, then positions by their trailing power of two (half, quarter,
    // eighth...). Pure position weight — the single beat-strength source used for
    // onset choice, note tier and duration alike.
    [[nodiscard]] inline int metricStrength(int step, int length) noexcept
    {
        if (length <= 0) return 0;
        const int pos = (((step % length) + length) % length);
        if (pos == 0) return 16;            // downbeat — strongest by a wide margin
        int s = pos, z = 0;
        while ((s & 1) == 0) { s >>= 1; ++z; }
        return z;                           // 8->3, 4->2, 2->1, odd->0
    }

    // The weakest note (highest fifths-distance rank, noteStrengthRank) a beat of
    // the given strength may use: strong beats are pinned near the root (the
    // central fifths arc), mid beats may reach the pentatonic arc, weak beats take
    // any colour note in the pool. Ranks: root 0, dominant/subdominant 1-2, the
    // next fifths pair 3-4, on outward — so this is the continuous, lean-aware
    // form of the old triad/penta/full tiering.
    [[nodiscard]] inline int rankCeilForStrength(int strength) noexcept
    {
        if (strength >= 3) return 2;    // downbeat / half-bar — root + nearest fifths
        if (strength == 2) return 4;    // quarter — out to the pentatonic arc
        return 1000;                    // eighth / off-beat — any colour note
    }

    // Desired sustain (in steps) for a note on a beat of the given strength.
    // Capped later to the gap before the next onset, so what is left over is the
    // rest that bridges into that (stronger) onset.
    [[nodiscard]] inline int sustainForStrength(int strength) noexcept
    {
        if (strength >= 3) return 4;   // strong — up to a quarter
        if (strength == 2) return 3;
        if (strength == 1) return 2;
        return 1;                      // weakest — short
    }

    // Map a sustain in steps (treating a step as a 1/16th) to a MusicalGate.
    [[nodiscard]] inline MusicalGate gateForSteps(int steps) noexcept
    {
        switch (std::clamp(steps, 1, 16))
        {
            case 1:  return MusicalGate::G1_16;
            case 2:  return MusicalGate::G1_8;
            case 3:  return MusicalGate::G1_8d;   // dotted eighth = 3/16
            case 4:  return MusicalGate::G1_4;
            case 5:  return MusicalGate::G1_4;
            case 6:  return MusicalGate::G1_4d;   // dotted quarter = 6/16
            case 7:  return MusicalGate::G1_2;
            case 8:  return MusicalGate::G1_2;
            default: return MusicalGate::G1_2d;   // >= 9 — long
        }
    }

    // Build the absolute-MIDI candidate pool: every in-pool pitch class, expanded
    // across `octaves` octaves from rootMidi, sorted ascending and unique. Fifths
    // keys narrow by core tier (coreBias); symmetric/chromatic keys take the whole
    // mask (they have no fifths nesting).
    [[nodiscard]] inline std::vector<int>
    melodyCandidates(const KeySig& key, int rootMidi, int octaves, int coreBias)
    {
        const bool fifths = hasFifthsWindow(key.scaleType);
        const int maxTier = std::clamp(coreBias, 0, 2);
        const uint16_t mask = pcMask(key);
        std::vector<int> pcs;
        for (int pc = 0; pc < 12; ++pc)
            if (maskHas(mask, pc) && (!fifths || coreTier(key, pc) <= maxTier))
                pcs.push_back(pc);

        std::vector<int> cand;
        const int oct = std::clamp(octaves, 1, 4);
        for (int o = 0; o < oct; ++o)
            for (int pc : pcs)
            {
                const int base = rootMidi + o * 12;
                const int note = base + ((((pc - rootMidi) % 12) + 12) % 12);
                if (note >= 0 && note <= 127)
                    cand.push_back(note);
            }
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
        return cand;
    }

    // Nearest candidate index (outward from idx0, ties resolving downward/flatter)
    // whose strength rank is at or below `ceil`. Falls back to idx0 if none
    // qualifies (e.g. a symmetric key where rank distinctions collapse).
    [[nodiscard]] inline int
    snapToRank(const std::vector<int>& cand, const std::vector<int>& ranks, int idx0, int ceil)
    {
        const int n = static_cast<int>(cand.size());
        for (int d = 0; d < n; ++d)
        {
            const int lo = idx0 - d, hi = idx0 + d;
            if (lo >= 0 && ranks[static_cast<std::size_t>(lo)] <= ceil) return lo;
            if (hi < n && ranks[static_cast<std::size_t>(hi)] <= ceil) return hi;
        }
        return idx0;
    }

    // Generate `length` steps of mono melody. Onsets are placed strongest-beat
    // first (up to `density`), each pitched on the contour and snapped to the core
    // tier its beat strength allows, sustained for a strength-scaled gate capped to
    // the next onset. Empty pool => no notes.
    [[nodiscard]] inline std::vector<MelodyStep>
    generateMelody(const KeySig& key, int length, int rootMidi, const MelodyParams& p)
    {
        std::vector<MelodyStep> out(static_cast<std::size_t>(std::max(0, length)));
        if (length <= 0)
            return out;

        const std::vector<int> cand = melodyCandidates(key, rootMidi, p.octaves, p.coreBias);
        const int N = static_cast<int>(cand.size());
        if (N == 0)
            return out;

        std::vector<int> ranks(static_cast<std::size_t>(N));
        for (int i = 0; i < N; ++i)
            ranks[static_cast<std::size_t>(i)] =
                noteStrengthRank(key, cand[static_cast<std::size_t>(i)] % 12);

        const int density = std::clamp(p.density, 0, length);
        if (density == 0)
            return out;

        // Onset placement: rank steps by (strength desc, position asc) and take the
        // strongest `density`. Equal-strength positions are already evenly spaced
        // (multiples of one power of two), so they fill in musically.
        std::vector<int> order(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
            order[static_cast<std::size_t>(i)] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const int sa = metricStrength(a, length), sb = metricStrength(b, length);
            if (sa != sb) return sa > sb;
            return a < b;
        });
        std::vector<int> onsets(order.begin(), order.begin() + density);
        std::sort(onsets.begin(), onsets.end());   // back into time order

        uint32_t s = p.seed ? p.seed : 1u;
        const int leapSpan = 1 + (std::clamp(p.stepLeap, 0, 100) * (N - 1)) / 100;
        const auto contour = static_cast<MelodyContour>(std::clamp(p.contour, 0, 3));
        const int coreBias = std::clamp(p.coreBias, 0, 2);

        int idx = N / 2;   // running index — seeds the Walk and survives gaps
        const int onsetCount = static_cast<int>(onsets.size());
        for (int o = 0; o < onsetCount; ++o)
        {
            const int step = onsets[static_cast<std::size_t>(o)];
            const int strength = metricStrength(step, length);

            const double t = onsetCount > 1 ? static_cast<double>(o) / (onsetCount - 1) : 0.0;
            int target = idx;
            switch (contour)
            {
                case MelodyContour::Rise:
                    target = static_cast<int>(std::lround(t * (N - 1)));
                    break;
                case MelodyContour::Fall:
                    target = static_cast<int>(std::lround((1.0 - t) * (N - 1)));
                    break;
                case MelodyContour::Arch:
                {
                    const double a = 1.0 - std::abs(2.0 * t - 1.0);  // 0->1->0
                    target = static_cast<int>(std::lround(a * (N - 1)));
                    break;
                }
                case MelodyContour::Walk:
                    target = idx;   // random walk: deviate from the previous index
                    break;
            }

            int jitter = 0;
            if (leapSpan > 0)
                jitter = static_cast<int>(mgNext(s) % static_cast<uint32_t>(2 * leapSpan + 1))
                         - leapSpan;
            const int idx0 = std::clamp(target + jitter, 0, N - 1);

            // Couple note strength to beat strength: pin to the strength rank the
            // beat allows (root/nearest-fifths on strong beats, colour notes on
            // weak ones). The pool is already coreBias-narrowed.
            (void) coreBias;
            idx = snapToRank(cand, ranks, idx0, rankCeilForStrength(strength));

            // Duration: strength-scaled sustain, capped to the next onset so a weak
            // short note leaves a rest that bridges into the next (stronger) onset.
            const int nextStep = (o + 1 < onsetCount) ? onsets[static_cast<std::size_t>(o + 1)]
                                                       : length;
            const int gap = nextStep - step;
            const int sustain = std::min(sustainForStrength(strength), gap);

            auto& ms = out[static_cast<std::size_t>(step)];
            ms.trig = true;
            ms.note = cand[static_cast<std::size_t>(idx)];
            ms.gate = gateForSteps(sustain);
        }
        return out;
    }
}
