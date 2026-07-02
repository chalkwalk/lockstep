#pragma once

#include "TimeSig.h"
#include <cmath>

namespace lockstep
{
    // ── LaunchQuant — the single launch-quantize authority (PRINCIPLES §25) ──
    //
    // One grid every deferrable action resolves against: Scene launch, Song
    // switch, Phrase deviation, mute/unmute, per-track phase-reset, and the
    // looper's record/play/overdub edges. There is no second grid.
    //
    // The Set-level grid selects one of {Instant, Beat, Bar, Bars2, Bars4,
    // Bars8}. PhraseEnd is a seventh value reachable ONLY as a per-track
    // override (a band-wide phrase-end has no single boundary — coprime-length
    // tracks would each fire at a different time). See DESIGN §4.8.
    //
    // Pure and JUCE-free by design (unit-tested in tests/LaunchQuantTest.cpp).
    enum class LaunchQuant : int
    {
        Instant = 0,
        Beat,
        Bar,
        Bars2,
        Bars4,
        Bars8,
        PhraseEnd
    };

    // Per-track override sentinel: "follow the Set-level grid".
    inline constexpr int kFollowGlobal = -1;

    // Resolve a per-track override against the Set-level grid. A track whose
    // override is kFollowGlobal inherits setGrid; any concrete value wins.
    [[nodiscard]] inline LaunchQuant resolveTrackQuant(LaunchQuant setGrid, int trackOverride)
    {
        if (trackOverride == kFollowGlobal)
            return setGrid;
        return static_cast<LaunchQuant>(trackOverride);
    }

    // Legacy pre-v25 on-disk mapping: launchQuant was serialized as a bar
    // count (1/2/4/8, default 1). Map to/from the enum for back-compat — used
    // both by the behaviour-identical commit-1 read/write and the v24→v25
    // serializer upgrade.
    [[nodiscard]] inline LaunchQuant legacyBarsToQuant(int bars)
    {
        switch (bars)
        {
            case 2:  return LaunchQuant::Bars2;
            case 4:  return LaunchQuant::Bars4;
            case 8:  return LaunchQuant::Bars8;
            default: return LaunchQuant::Bar;   // 1 (and anything unexpected)
        }
    }

    [[nodiscard]] inline int quantToLegacyBars(LaunchQuant q)
    {
        switch (q)
        {
            case LaunchQuant::Bars2: return 2;
            case LaunchQuant::Bars4: return 4;
            case LaunchQuant::Bars8: return 8;
            default:                 return 1;  // Bar/Beat/Instant/PhraseEnd → nearest legacy fit
        }
    }

    // The grid period in quarter-note PPQ. Instant → 0 (no deferral). Bar-family
    // = n × barPpq; Beat = 4/denominator; PhraseEnd uses the caller-supplied
    // phrase cycle (trackLen × divPpq); a zero-or-negative cycle degrades to 0
    // (⇒ Instant at the boundary helpers).
    [[nodiscard]] inline double gridPpq(LaunchQuant q, const TimeSig& ts,
                                        double phraseCyclePpq = 0.0)
    {
        const double beatPpq = 4.0 / static_cast<double>(ts.denominator);
        switch (q)
        {
            case LaunchQuant::Instant:   return 0.0;
            case LaunchQuant::Beat:      return beatPpq;
            case LaunchQuant::Bar:       return ts.barPpq();
            case LaunchQuant::Bars2:     return ts.barPpq() * 2.0;
            case LaunchQuant::Bars4:     return ts.barPpq() * 4.0;
            case LaunchQuant::Bars8:     return ts.barPpq() * 8.0;
            case LaunchQuant::PhraseEnd: return phraseCyclePpq > 0.0 ? phraseCyclePpq : 0.0;
        }
        return 0.0;
    }

    // The next grid boundary at or after blockStartPpq. Instant (or any grid
    // whose period degrades to 0) returns blockStartPpq — i.e. "fire now".
    // Callers pass whatever PPQ frame they already work in; for a per-track
    // PhraseEnd the caller subtracts the track anchor first (DESIGN §13.4).
    [[nodiscard]] inline double nextBoundaryPpq(double blockStartPpq, LaunchQuant q,
                                                const TimeSig& ts, double phraseCyclePpq = 0.0)
    {
        const double period = gridPpq(q, ts, phraseCyclePpq);
        if (period <= 0.0)
            return blockStartPpq;
        return std::ceil(blockStartPpq / period) * period;
    }

    // True iff the next boundary falls within [blockStart, blockEnd) — the
    // per-block "did we cross a launch boundary this block?" test. On success
    // outBoundary carries the boundary PPQ. Matches the legacy scene-launch
    // semantics (boundary < blockEnd, boundary >= blockStart by construction).
    [[nodiscard]] inline bool boundaryInBlock(double blockStart, double blockEnd,
                                              LaunchQuant q, const TimeSig& ts,
                                              double& outBoundary, double phraseCyclePpq = 0.0)
    {
        const double boundary = nextBoundaryPpq(blockStart, q, ts, phraseCyclePpq);
        outBoundary = boundary;
        return boundary < blockEnd;
    }
}
