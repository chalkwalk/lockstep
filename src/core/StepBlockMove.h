#pragma once

#include <vector>
#include <utility>
#include <algorithm>
#include <functional>

namespace lockstep
{
    // Multi-step "block move" (Part 2): shift every index in `held` by `dir`
    // (+1 / -1) as a rigid block, clamped so the leading edge stops at the track
    // boundary [0, len) — no wrap. Returns the ordered (from, to) swap pairs to
    // apply via swapSteps(), direction-sorted (descending for +1, ascending for
    // -1) so two adjacent held steps never double-move; or an empty vector when
    // the block is already against the boundary (a no-op).
    //
    // Pure and header-only so the swap-order + clamp logic is unit-tested apart
    // from the editor's EditContext / physical-key bookkeeping.
    [[nodiscard]] inline std::vector<std::pair<int, int>>
    computeBlockMoveSwaps(std::vector<int> held, int dir, int len)
    {
        if (held.empty() || dir == 0 || len <= 1) return {};

        if (dir > 0)
        {
            const int maxS = *std::max_element(held.begin(), held.end());
            if (maxS + dir >= len) return {};                 // leading edge at end
            std::sort(held.begin(), held.end(), std::greater<int>());
        }
        else
        {
            const int minS = *std::min_element(held.begin(), held.end());
            if (minS + dir < 0) return {};                    // leading edge at start
            std::sort(held.begin(), held.end(), std::less<int>());
        }

        std::vector<std::pair<int, int>> swaps;
        swaps.reserve(held.size());
        for (const int s : held)
            swaps.emplace_back(s, s + dir);
        return swaps;
    }
}
