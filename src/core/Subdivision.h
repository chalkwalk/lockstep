#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Subdivision — musical note-value subdivision math.  JUCE-free, header-only.
// Single source of truth for divPpq so every site (engine, UI, serializer) agrees.

namespace lockstep
{
    // Base note values: 4/1 (longa) down to 1/64.
    enum class DivBase : uint8_t
    {
        D4_1, D2_1, D1_1, D1_2, D1_4, D1_8, D1_16, D1_32, D1_64
    };
    inline constexpr int kNumDivBases = 9;

    // Flavours: Straight / Dotted (×1.5) / Triplet (×2/3).
    enum class DivFlavour : uint8_t { Straight, Dotted, Triplet };
    inline constexpr int kNumDivFlavours = 3;

    // Combined index in [0, 26]: base * 3 + flavour.
    // Default 18 = D1_16 * 3 + Straight (the old divider=1 at 0.25 PPQ).
    inline constexpr int kSubdivDefault = 18;
    inline constexpr int kSubdivMin     = 0;
    inline constexpr int kSubdivMax     = 26;

    // Base note value in quarter-note PPQ (quarter = 1.0 PPQ).
    inline constexpr std::array<double, kNumDivBases> kBaseQuarters =
        { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };

    // Flavour multiplier.
    inline constexpr std::array<double, kNumDivFlavours> kFlavourMul =
        { 1.0, 1.5, 2.0 / 3.0 };

    [[nodiscard]] inline DivBase    baseFromIndex   (int idx) noexcept { return static_cast<DivBase>   (idx / 3); }
    [[nodiscard]] inline DivFlavour flavourFromIndex(int idx) noexcept { return static_cast<DivFlavour>(idx % 3); }
    [[nodiscard]] inline int        indexFromParts  (DivBase b, DivFlavour f) noexcept
        { return (static_cast<int>(b) * 3) + static_cast<int>(f); }

    // PPQ duration for the given combined index.
    [[nodiscard]] inline double subdivisionPpqFromIndex(int idx) noexcept
    {
        const auto b = static_cast<int>(baseFromIndex(idx));
        const auto f = static_cast<int>(flavourFromIndex(idx));
        return kBaseQuarters[static_cast<std::size_t>(b)]
             * kFlavourMul  [static_cast<std::size_t>(f)];
    }

    [[nodiscard]] inline double subdivisionPpq(DivBase b, DivFlavour f) noexcept
    {
        return kBaseQuarters[static_cast<std::size_t>(b)]
             * kFlavourMul  [static_cast<std::size_t>(f)];
    }

    // Nearest combined index by PPQ — used by the v19->v20 serializer remap.
    [[nodiscard]] inline int nearestSubdivIndex(double ppq) noexcept
    {
        int best = kSubdivDefault;
        double bestDiff = 1e18;
        for (int i = kSubdivMin; i <= kSubdivMax; ++i)
        {
            const double diff = std::abs(subdivisionPpqFromIndex(i) - ppq);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        return best;
    }

    // Human-readable base label (ASCII only -- CLAUDE.md Unicode rule).
    [[nodiscard]] inline const char* baseLabel(DivBase b) noexcept
    {
        switch (b)
        {
            case DivBase::D4_1:  return "4/1";
            case DivBase::D2_1:  return "2/1";
            case DivBase::D1_1:  return "1/1";
            case DivBase::D1_2:  return "1/2";
            case DivBase::D1_4:  return "1/4";
            case DivBase::D1_8:  return "1/8";
            case DivBase::D1_16: return "1/16";
            case DivBase::D1_32: return "1/32";
            case DivBase::D1_64: return "1/64";
        }
        return "1/16";
    }

    // Flavour suffix: "", ".", "T" (dotted / triplet).
    [[nodiscard]] inline const char* flavourSuffix(DivFlavour f) noexcept
    {
        switch (f)
        {
            case DivFlavour::Straight: return "";
            case DivFlavour::Dotted:   return ".";
            case DivFlavour::Triplet:  return "T";
        }
        return "";
    }

} // namespace lockstep
