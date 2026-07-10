#pragma once

#include <array>
#include <cstddef>

namespace lockstep::routing
{
    // A2 (DESIGN §27): the inter-track output-routing graph. Each track has at
    // most ONE output edge — its CHANNEL "Out" destination — so the graph is a
    // functional graph (out-degree <= 1). dest[i] is the audio-track index that
    // track i feeds, or -1 if it goes to Master / Off / nowhere.
    //
    // Both functions are pure and allocation-free so the per-block topological
    // sort can run on the audio thread; they are unit-tested in RoutingGraphTest.

    // Processing order: every feeder appears before the bus it feeds. Sources
    // (nothing feeds them) come first, in ascending index; destinations last.
    // A cycle should be impossible (refused at assignment), but if one is
    // present the unresolved nodes are appended in ascending index so the result
    // is always a full permutation of [0, N).
    template <std::size_t N>
    std::array<int, N> computeOrder(const std::array<int, N>& dest) noexcept
    {
        std::array<int, N> indeg{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const int d = dest[i];
            if (d >= 0 && d < static_cast<int>(N) && d != static_cast<int>(i))
                ++indeg[static_cast<std::size_t>(d)];
        }

        std::array<int, N> order{};
        std::array<bool, N> emitted{};
        std::size_t head = 0;

        // Kahn's algorithm with a deterministic ascending-index scan instead of a
        // FIFO queue (N is tiny), so ties are resolved by track index.
        for (std::size_t pass = 0; pass < N; ++pass)
        {
            int pick = -1;
            for (std::size_t i = 0; i < N; ++i)
                if (!emitted[i] && indeg[i] == 0) { pick = static_cast<int>(i); break; }

            if (pick < 0)
            {
                // Cycle (or all done) — append remaining in ascending order.
                for (std::size_t i = 0; i < N; ++i)
                    if (!emitted[i]) { pick = static_cast<int>(i); break; }
            }
            if (pick < 0) break;

            const auto pi = static_cast<std::size_t>(pick);
            emitted[pi] = true;
            order[head++] = pick;
            const int d = dest[pi];
            if (d >= 0 && d < static_cast<int>(N) && d != pick && indeg[static_cast<std::size_t>(d)] > 0)
                --indeg[static_cast<std::size_t>(d)];
        }
        return order;
    }

    // A track may tap several sources at once — a four-sub-track deck reads up to
    // four (DESIGN §40.3). The tap side of the graph is therefore a fixed-width
    // table: tapSrc[i][k] is the k-th source track i reads, or -1. Out-degree was
    // never bounded on this side anyway (a track may be tapped by many), so this
    // changes the arity of the edge set, not the algorithm.
    template <std::size_t N, std::size_t K>
    using TapEdges = std::array<std::array<int, K>, N>;

    namespace detail
    {
        template <std::size_t N, std::size_t K>
        [[nodiscard]] constexpr bool tapsFrom(const TapEdges<N, K>& tapSrc,
                                              std::size_t consumer, int source) noexcept
        {
            for (std::size_t k = 0; k < K; ++k)
                if (tapSrc[consumer][k] == source) return true;
            return false;
        }

        // In-degree contribution of the tap edges feeding `consumer`. A source may
        // legitimately appear twice (two sub-tracks reading the same track); it is
        // one constraint, not two, so duplicates are counted once.
        template <std::size_t N, std::size_t K>
        [[nodiscard]] constexpr int tapIndegree(const TapEdges<N, K>& tapSrc,
                                                std::size_t consumer) noexcept
        {
            int deg = 0;
            for (std::size_t k = 0; k < K; ++k)
            {
                const int s = tapSrc[consumer][k];
                if (s < 0 || s >= static_cast<int>(N) || s == static_cast<int>(consumer))
                    continue;
                bool dup = false;
                for (std::size_t j = 0; j < k; ++j)
                    if (tapSrc[consumer][j] == s) { dup = true; break; }
                if (! dup) ++deg;
            }
            return deg;
        }

        // Widen a one-tap-per-track array into the general table.
        template <std::size_t N>
        [[nodiscard]] constexpr TapEdges<N, 1> widen(const std::array<int, N>& tapSrc) noexcept
        {
            TapEdges<N, 1> t{};
            for (std::size_t i = 0; i < N; ++i) t[i][0] = tapSrc[i];
            return t;
        }
    }

    // Combined order over TWO edge classes (the tap-fork extension, DESIGN §27):
    //   - mix edges:  i precedes dest[i]    (track i feeds bus dest[i])
    //   - tap edges:  tapSrc[i] precedes i  (track i reads a copy of tapSrc[i])
    // Both are "source before consumer" constraints, so a single Kahn pass over the
    // union yields an order where every feeder AND every tapped source is processed
    // before its consumer. Out-degree is no longer ≤ 1 (a track may be tapped by
    // many), so this is a general DAG topo-sort; ties resolve by ascending index and
    // a cycle (refused at assignment) still yields a full permutation. Pure,
    // allocation-free — runs per-block on the audio thread.
    template <std::size_t N, std::size_t K>
    std::array<int, N> computeOrder(const std::array<int, N>& dest,
                                    const TapEdges<N, K>& tapSrc) noexcept
    {
        std::array<int, N> indeg{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const int d = dest[i];
            if (d >= 0 && d < static_cast<int>(N) && d != static_cast<int>(i))
                ++indeg[static_cast<std::size_t>(d)];   // i precedes dest[i]
            indeg[i] += detail::tapIndegree(tapSrc, i); // every tapped source precedes i
        }

        std::array<int, N> order{};
        std::array<bool, N> emitted{};
        std::size_t head = 0;

        for (std::size_t pass = 0; pass < N; ++pass)
        {
            int pick = -1;
            for (std::size_t i = 0; i < N; ++i)
                if (!emitted[i] && indeg[i] == 0) { pick = static_cast<int>(i); break; }

            if (pick < 0)  // cycle (or done) — append remaining in ascending order
                for (std::size_t i = 0; i < N; ++i)
                    if (!emitted[i]) { pick = static_cast<int>(i); break; }
            if (pick < 0) break;

            const auto pi = static_cast<std::size_t>(pick);
            emitted[pi] = true;
            order[head++] = pick;

            // Relax successors: the mix destination, and every track that taps pick.
            const int d = dest[pi];
            if (d >= 0 && d < static_cast<int>(N) && d != pick && indeg[static_cast<std::size_t>(d)] > 0)
                --indeg[static_cast<std::size_t>(d)];
            for (std::size_t j = 0; j < N; ++j)
                if (!emitted[j] && detail::tapsFrom(tapSrc, j, pick) && indeg[j] > 0)
                    --indeg[j];
        }
        return order;
    }

    // One tap per track: the shape every machine but a multi-sub-track deck has.
    template <std::size_t N>
    std::array<int, N> computeOrder(const std::array<int, N>& dest,
                                    const std::array<int, N>& tapSrc) noexcept
    {
        return computeOrder(dest, detail::widen(tapSrc));
    }

    // True if the union of mix edges (dest) and tap edges (tapSrc) contains a cycle.
    // Used to refuse a tap assignment that would close a loop across either edge
    // class. Pure Kahn: if fewer than N nodes drain cleanly, a cycle remains.
    template <std::size_t N, std::size_t K>
    bool hasCycle(const std::array<int, N>& dest, const TapEdges<N, K>& tapSrc) noexcept
    {
        std::array<int, N> indeg{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const int d = dest[i];
            if (d >= 0 && d < static_cast<int>(N) && d != static_cast<int>(i))
                ++indeg[static_cast<std::size_t>(d)];
            indeg[i] += detail::tapIndegree(tapSrc, i);
        }

        std::array<bool, N> emitted{};
        std::size_t drained = 0;
        for (std::size_t pass = 0; pass < N; ++pass)
        {
            int pick = -1;
            for (std::size_t i = 0; i < N; ++i)
                if (!emitted[i] && indeg[i] == 0) { pick = static_cast<int>(i); break; }
            if (pick < 0) break;  // nothing drainable — remaining nodes form a cycle

            const auto pi = static_cast<std::size_t>(pick);
            emitted[pi] = true;
            ++drained;
            const int d = dest[pi];
            if (d >= 0 && d < static_cast<int>(N) && d != pick && indeg[static_cast<std::size_t>(d)] > 0)
                --indeg[static_cast<std::size_t>(d)];
            for (std::size_t j = 0; j < N; ++j)
                if (!emitted[j] && detail::tapsFrom(tapSrc, j, pick) && indeg[j] > 0)
                    --indeg[j];
        }
        return drained < N;
    }

    template <std::size_t N>
    bool hasCycle(const std::array<int, N>& dest, const std::array<int, N>& tapSrc) noexcept
    {
        return hasCycle(dest, detail::widen(tapSrc));
    }

    // Audibility under solo, routing-aware (DESIGN §27). When any track is
    // soloed, a track is audible iff it is connected to a soloed track through
    // the routing graph — either UPSTREAM (a soloed bus needs the feeders that
    // sum into it) or DOWNSTREAM (a soloed feeder needs the bus chain that
    // carries it to master). Precisely, audible[T] iff following T's single-out
    // chain reaches a soloed track (covers T-itself + ancestors/feeders), OR T
    // lies on the downstream chain from some soloed track (descendants).
    //
    // Caller decides what to do when nothing is soloed; this returns the mask
    // unconditionally (all-false if no track is soloed). Pure, alloc-free.
    template <std::size_t N>
    std::array<bool, N> soloAudibleMask(const std::array<int, N>& dest,
                                        const std::array<bool, N>& soloed) noexcept
    {
        std::array<bool, N> audible{};

        // Upward / self: T is audible if its out-chain reaches a soloed node.
        for (std::size_t t = 0; t < N; ++t)
        {
            int cur = static_cast<int>(t);
            for (std::size_t steps = 0; steps <= N; ++steps)
            {
                if (cur < 0 || cur >= static_cast<int>(N)) break;  // Master/Off
                if (soloed[static_cast<std::size_t>(cur)]) { audible[t] = true; break; }
                cur = dest[static_cast<std::size_t>(cur)];
            }
        }

        // Downward: every descendant on a soloed node's out-chain is audible.
        for (std::size_t s = 0; s < N; ++s)
        {
            if (!soloed[s]) continue;
            int cur = dest[s];
            for (std::size_t steps = 0; steps < N; ++steps)
            {
                if (cur < 0 || cur >= static_cast<int>(N)) break;  // reached Master/Off
                audible[static_cast<std::size_t>(cur)] = true;
                cur = dest[static_cast<std::size_t>(cur)];
            }
        }

        return audible;
    }

    // True if adding the edge from -> to would create a cycle, given the current
    // single-out edges. Because out-degree is <= 1, this is just walking the
    // existing chain out of `to` and checking whether it returns to `from`.
    template <std::size_t N>
    bool wouldCreateCycle(const std::array<int, N>& dest, int from, int to) noexcept
    {
        if (from < 0 || from >= static_cast<int>(N)) return false;
        if (to < 0 || to >= static_cast<int>(N)) return false;  // Master/Off — never a cycle
        int cur = to;
        for (std::size_t steps = 0; steps <= N; ++steps)
        {
            if (cur < 0 || cur >= static_cast<int>(N)) return false;  // reached Master/Off
            if (cur == from) return true;                             // closed the loop
            cur = dest[static_cast<std::size_t>(cur)];
        }
        return true;  // never terminated => already cyclic; refuse defensively
    }
}
