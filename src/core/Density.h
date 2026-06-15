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

    // Pure hash for Reroll cadence — Uniform musicality: one roll per step firing.
    // Each (track, absolute-step-number) pair produces the same r every time the
    // step plays in the same loop position, but evolves as stepPos increments.
    inline float rerollPerStep(std::size_t trackIdx, std::int64_t stepPos) noexcept
    {
        auto h = static_cast<uint32_t>(trackIdx) * 0xb5297a4du;
        h ^= static_cast<uint32_t>(static_cast<uint64_t>(stepPos) * 0x6c62272eull);
        h ^= h >> 16u;
        h *= 0x45d9f3bu;
        h ^= h >> 16u;
        return static_cast<float>(h % 10000u) / 10000.0f;
    }

    // Pure hash for Reroll cadence — Musical/Metric musicality: one roll per
    // (track, bar, step-in-bar) triple. Fixed within a bar, fresh each new bar.
    // stepInBar = stepNum % stepsPerBar, computed by the caller.
    inline float rerollPerBar(std::size_t trackIdx,
                              std::int64_t barIndex,
                              std::int64_t stepInBar) noexcept
    {
        auto h = static_cast<uint32_t>(trackIdx) * 0xb5297a4du;
        h ^= static_cast<uint32_t>(static_cast<uint64_t>(barIndex) * 0x1b873593ull);
        h ^= static_cast<uint32_t>(static_cast<uint64_t>(stepInBar) * 0xe654ab55ull);
        h ^= h >> 16u;
        h *= 0x45d9f3bu;
        h ^= h >> 16u;
        return static_cast<float>(h % 10000u) / 10000.0f;
    }

    // Bias constant: controls how strongly Metric musicality spreads survival
    // probabilities by importance. kBias=0.9 means at low density the downbeat
    // probability approaches 0.9 while the weakest offbeat approaches 0.01.
    // Tunable without changing the API.
    inline constexpr float kBias = 0.9f;

    // Returns true if the trig should survive the density gate.
    //
    // Model: importance sets a per-step survival probability; r (the selection
    // value) is always compared against it. This separates Scrub (deterministic
    // r) from Reroll (cadence-aware r) in all musicality modes, and produces
    // gradual metric thinning via per-step randomness rather than hard tiers.
    //
    //   effective = clamp(perTrack + master, 0.01, 1)
    //   w  = 1 - metricDrop(ppqInBar, barPpq)     // importance: downbeat=1
    //   m  = musicalityM(musicality)               // 0, 0.5, 1
    //   p  = clamp(effective + kBias * m * (2w-1) * (1-effective), 0.01, 1)
    //   survive = (r < p)
    //
    // Properties:
    //   effective=1 → p=1 for all steps (full pattern preserved).
    //   Uniform (m=0) → p=effective (pure random thinning).
    //   Metric (m=1), decreasing density → downbeats survive longest,
    //     weakest offbeats drop first. Average p ≈ effective (honest knob).
    //
    //   perTrack         — per-track density amount [0.01..1.0] (default 1.0)
    //   master           — signed master offset [-1.0..1.0] (default 0.0)
    //   ppqInBar         — step's PPQ position within the bar (grid PPQ)
    //   barPpq           — bar length in PPQ from the active scene's coreTime
    //   musicality       — Uniform / Mixed / Metric
    //   selection        — Scrub / Reroll
    //   trackIdx         — track index (for hash decoration)
    //   stepPos          — track-local absolute step counter (for Scrub hash)
    //   quantizedKnobLevel — round(effective * 100); Scrub re-scrambles on change
    //   rerollR          — caller-supplied value in [0,1) for Reroll mode
    inline bool densitySurvives(float perTrack, float master,
                                double ppqInBar, double barPpq,
                                Musicality musicality, DensitySelection selection,
                                std::size_t trackIdx, std::int64_t stepPos,
                                int quantizedKnobLevel, float rerollR) noexcept
    {
        const float effective = std::clamp(perTrack + master, 0.01f, 1.0f);
        const float m = musicalityM(musicality);
        const float w = 1.0f - ((m > 0.0f) ? metricDrop(ppqInBar, barPpq) : 0.0f);
        const float p = std::clamp(
            effective + kBias * m * (2.0f * w - 1.0f) * (1.0f - effective),
            0.01f, 1.0f);

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

        return r < p;
    }

} // namespace lockstep::Density
