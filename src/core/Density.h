#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Density — live, subtractive trig-thinning overlay (DESIGN §39).
// JUCE-free, header-only, pure — all functions are stateless and testable.
//
// Evaluation order: called AFTER TrigEvaluator::shouldFire returns true.
// Density can only silence would-fire trigs; it never re-enables a step.

namespace lockstep::Density
{
    // Durable per-song-per-track settings (serialized in TrackKit, v19).
    enum class Musicality : uint8_t { Uniform, Mixed, Metric };
    enum class DensitySelection : uint8_t { Scrub, Reroll };

    inline float musicalityM(Musicality m) noexcept
    {
        switch (m)
        {
            case Musicality::Uniform: return 0.0f;
            case Musicality::Mixed:   return 0.5f;
            case Musicality::Metric:  return 1.0f;
        }
        return 0.5f;
    }

    // Metric drop-propensity for a step at ppqInBar within a bar of barPpq length.
    // Returns 0 for the downbeat (survives longest) and 1 for the finest offbeat (dies first).
    // Quantises the phase to a 16th-grid index and uses trailing-zero-bit depth.
    inline float metricDrop(double ppqInBar, double barPpq) noexcept
    {
        if (barPpq <= 0.0) { return 0.0f; }
        const double phase = std::fmod(ppqInBar, barPpq) / barPpq;
        // Map to 16th-grid index [0..15].
        const auto k = static_cast<unsigned>(std::round(phase * 16.0)) % 16u;
        if (k == 0u) { return 0.0f; } // downbeat: always survives
        // Count trailing zeros of k as metric depth (1..3 for the 16th grid).
        unsigned depth = 0u;
        auto tmp = k;
        while ((tmp & 1u) == 0u) { ++depth; tmp >>= 1u; }
        // maxDepth for a 16-step grid is 3 (k=8 → 3 trailing zeros).
        constexpr unsigned kMaxDepth = 3u;
        return 1.0f - std::min(static_cast<float>(depth) / static_cast<float>(kMaxDepth), 1.0f);
    }

    // Deterministic hash for Scrub selection.
    // Salts intentionally differ from TrigEvaluator::deterministicPercent
    // (0x9E3779B1u / 2246822519ull / 0x45d9f3bu) so density selection does
    // not correlate with which steps barely passed the probability roll.
    inline uint32_t densityScrubHash(std::size_t trackIdx,
                                     std::int64_t stepPos,
                                     int quantizedKnobLevel) noexcept
    {
        auto h = static_cast<uint32_t>(trackIdx) * 0xd2a98b26u;
        h ^= static_cast<uint32_t>(static_cast<uint64_t>(stepPos) * 0xc2b2ae35ull);
        h ^= static_cast<uint32_t>(quantizedKnobLevel) * 0x27d4eb2fu;
        h ^= h >> 16u;
        h *= 0x85ebca6bu;
        h ^= h >> 13u;
        h *= 0xc2b2ae35u;
        h ^= h >> 16u;
        return h;
    }

    // Returns true if the trig should survive the density gate.
    //   perTrack         — per-track density amount [0.01..1.0] (default 1.0 = full)
    //   master           — signed master offset [-1.0..1.0] (default 0.0)
    //   ppqInBar         — step's PPQ position within the bar (grid PPQ, not swing-shifted)
    //   barPpq           — bar length in PPQ from the active scene's coreTime
    //   musicality       — Uniform / Mixed / Metric
    //   selection        — Scrub / Reroll
    //   trackIdx         — track index (for hash decoration)
    //   stepPos          — track-local absolute step counter (for hash)
    //   quantizedKnobLevel — round(effective * 100), pre-computed by caller so
    //                        turning the knob reshuffles the Scrub selection
    //   rerollR          — caller-supplied RNG value in [0,1) for Reroll mode
    inline bool densitySurvives(float perTrack, float master,
                                double ppqInBar, double barPpq,
                                Musicality musicality, DensitySelection selection,
                                std::size_t trackIdx, std::int64_t stepPos,
                                int quantizedKnobLevel, float rerollR) noexcept
    {
        const float effective = std::clamp(perTrack + master, 0.01f, 1.0f);
        const float m = musicalityM(musicality);

        const float dMetric = (m > 0.0f) ? metricDrop(ppqInBar, barPpq) : 0.0f;

        float r;
        if (selection == DensitySelection::Scrub)
        {
            const auto hash = densityScrubHash(trackIdx, stepPos, quantizedKnobLevel);
            r = static_cast<float>(hash % 10000u) / 10000.0f;
        }
        else
        {
            r = rerollR;
        }

        const float k = (m * dMetric) + ((1.0f - m) * r);
        return k < effective;
    }

} // namespace lockstep::Density
