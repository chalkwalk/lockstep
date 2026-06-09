#pragma once

#include <algorithm>
#include <vector>

namespace lockstep
{
    // bjorklund — generate an Euclidean rhythm of `pulses` onsets distributed
    // as evenly as possible over `length` steps, with a signed `offset` rotation
    // (positive = rotate right/forward, negative = rotate left/backward).
    // Returns a vector<bool> of length `length` where true = onset.
    // Uses the Bresenham / Euclidean approach: onset at step i iff
    //   (i * pulses) % length < pulses
    inline std::vector<bool> bjorklund(int length, int pulses, int offset = 0)
    {
        if (length <= 0) return {};
        if (pulses < 0) pulses = 0;
        if (pulses > length) pulses = length;

        std::vector<bool> result(static_cast<std::size_t>(length), false);
        if (pulses == 0) return result;

        for (int i = 0; i < length; ++i)
            result[static_cast<std::size_t>(i)] = ((i * pulses) % length) < pulses;

        // Apply rotation: positive offset shifts onsets forward (right) by offset steps.
        // std::rotate(begin, begin+k, end) shifts LEFT by k, so for RIGHT shift use length-k.
        int rot = offset % length;
        if (rot < 0) rot += length;
        if (rot != 0)
        {
            const int leftShift = length - rot;
            std::rotate(result.begin(),
                        result.begin() + static_cast<std::ptrdiff_t>(leftShift),
                        result.end());
        }
        return result;
    }

    // euclideanAccents — assign velocities to an Euclidean pattern.
    // Returns a vector<int> of velocities for each step:
    //   0   = rest
    //   64  = onset, not accented
    //   100 = onset, accented
    // `accents` onset positions are themselves Euclidean-distributed over the `pulses`.
    inline std::vector<int> euclideanAccents(int length, int pulses, int offset,
                                             int accents)
    {
        const auto pattern = bjorklund(length, pulses, offset);

        // Collect onset positions.
        std::vector<int> onsetIdx;
        onsetIdx.reserve(static_cast<std::size_t>(pulses));
        for (int i = 0; i < length; ++i)
            if (pattern[static_cast<std::size_t>(i)]) onsetIdx.push_back(i);

        std::vector<int> result(static_cast<std::size_t>(length), 0);

        if (accents <= 0 || onsetIdx.empty())
        {
            for (int idx : onsetIdx)
                result[static_cast<std::size_t>(idx)] = 64;
            return result;
        }

        const int k = static_cast<int>(onsetIdx.size());
        if (accents > k) accents = k;
        const auto accentPat = bjorklund(k, accents, 0);

        for (int j = 0; j < k; ++j)
        {
            const int stepI = onsetIdx[static_cast<std::size_t>(j)];
            result[static_cast<std::size_t>(stepI)] =
                accentPat[static_cast<std::size_t>(j)] ? 100 : 64;
        }
        return result;
    }
}
