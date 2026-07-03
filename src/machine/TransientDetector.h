#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lockstep
{
    // Algorithm constants (not user-facing in MH.4).
    inline constexpr double kMinSliceMs = 60.0;   // minimum slice duration
    inline constexpr double kBlockMs = 5.0;    // analysis block size
    inline constexpr double kFastAttackMs = 10.0;   // fast envelope attack
    inline constexpr double kFastReleaseMs = 30.0;   // fast envelope release
    inline constexpr double kSlowReleaseMs = 200.0;  // slow envelope release
    inline constexpr float kTransAbsThreshold = 1.5f; // minimum fast/slow ratio

    // 4.9 SYNC slicing: beat-grid divisions, coarsest -> finest. A division's
    // value is beats-per-slice at the detected tempo (4/4 assumed, the same
    // assumption StretchMachine::timeRatioFor already makes). Only these seven
    // are meaningful — finer than a 16th over a musical loop exceeds the 16-slice
    // cap almost immediately.
    inline constexpr int kNumSyncDivisions = 7;
    inline constexpr double kSyncBeatsPerSlice[kNumSyncDivisions] =
        { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };  // 4bar 2bar 1bar 1/2 1/4 1/8 1/16

    // Per-block analysis cached on Sample at load time (message thread only).
    struct BlockAnalysis
    {
        std::vector<float> rms;      // RMS per 5 ms block (mono-summed)
        std::vector<float> fastEnv;  // fast follower (~10 ms attack)
        std::vector<float> slowEnv;  // slow follower (~200 ms release)
        int numBlocks = 0;
        double sampleRate = 0.0;
        int blockSize = 0;      // samples per block
    };

    // Run per-block RMS + fast/slow envelope analysis over `pcm`.
    // All channels are averaged (mono sum). Message-thread only.
    inline BlockAnalysis analyseSample(const juce::AudioBuffer<float>& pcm,
                                       double sampleRate)
    {
        BlockAnalysis ba;
        ba.sampleRate = sampleRate;
        ba.blockSize = static_cast<int>(kBlockMs * 0.001 * sampleRate + 0.5);
        if (ba.blockSize < 1) ba.blockSize = 1;

        const int numCh = pcm.getNumChannels();
        const int numSamp = pcm.getNumSamples();
        if (numCh < 1 || numSamp < 1) return ba;

        ba.numBlocks = (numSamp + ba.blockSize - 1) / ba.blockSize;
        ba.rms.resize(static_cast<std::size_t>(ba.numBlocks), 0.0f);
        ba.fastEnv.resize(static_cast<std::size_t>(ba.numBlocks), 0.0f);
        ba.slowEnv.resize(static_cast<std::size_t>(ba.numBlocks), 0.0f);

        // Envelope follower coefficients (block-rate, not sample-rate).
        const double blocksPerSec = 1000.0 / kBlockMs;
        const double fastAttCoeff = 1.0 - std::exp(-1.0 / (kFastAttackMs * 0.001 * blocksPerSec));
        const double fastRelCoeff = 1.0 - std::exp(-1.0 / (kFastReleaseMs * 0.001 * blocksPerSec));
        const double slowRelCoeff = 1.0 - std::exp(-1.0 / (kSlowReleaseMs * 0.001 * blocksPerSec));

        float fastLevel = 0.0f;
        float slowLevel = 0.0f;

        for (int b = 0; b < ba.numBlocks; ++b)
        {
            const int start = b * ba.blockSize;
            const int end = std::min(start + ba.blockSize, numSamp);
            const int count = end - start;

            // Mono-summed RMS.
            double sumSq = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* ptr = pcm.getReadPointer(ch, start);
                for (int i = 0; i < count; ++i)
                    sumSq += static_cast<double>(ptr[i]) * static_cast<double>(ptr[i]);
            }
            const float blockRms = static_cast<float>(
                std::sqrt(sumSq / static_cast<double>(count * numCh)));
            ba.rms[static_cast<std::size_t>(b)] = blockRms;

            // Fast follower: fast attack, fast release.
            if (blockRms > fastLevel)
                fastLevel += static_cast<float>(fastAttCoeff) * (blockRms - fastLevel);
            else
                fastLevel += static_cast<float>(fastRelCoeff) * (blockRms - fastLevel);

            // Slow follower: no attack, slow release (tracks loud sustain).
            slowLevel += static_cast<float>(slowRelCoeff) * (blockRms - slowLevel);

            ba.fastEnv[static_cast<std::size_t>(b)] = fastLevel;
            ba.slowEnv[static_cast<std::size_t>(b)] = slowLevel;
        }

        return ba;
    }

    // Find the zero-crossing (between consecutive samples) nearest to `targetSample`
    // within the given source channel, searching within `windowSamples` in each direction.
    // Returns `targetSample` if none found.
    inline int nearestZeroCrossing(const float* ch, int numSamples,
                                   int targetSample, int windowSamples)
    {
        const int lo = std::max(0, targetSample - windowSamples);
        const int hi = std::min(numSamples - 2, targetSample + windowSamples);

        int bestIdx = targetSample;
        double bestDist = static_cast<double>(windowSamples) + 1.0;

        for (int i = lo; i <= hi; ++i)
        {
            if ((ch[i] >= 0.0f) != (ch[i + 1] >= 0.0f))
            {
                const double dist = std::abs(static_cast<double>(i - targetSample));
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
        }
        return bestIdx;
    }

    // Place N slices using EQUAL mode (even divisions, each snapped to nearest ZC).
    // Returns absolute sample positions (first is always 0).
    inline std::vector<int> placeEqualSlices(const juce::AudioBuffer<float>& pcm,
                                             double sampleRate,
                                             int count)
    {
        const int numSamples = pcm.getNumSamples();
        count = std::clamp(count, 1,
                           std::max(1, static_cast<int>(
                                           static_cast<double>(numSamples) / (kMinSliceMs * 0.001 * sampleRate))));
        count = std::min(count, 16);

        std::vector<int> out;
        out.reserve(static_cast<std::size_t>(count));
        out.push_back(0);  // slice 0 always at position 0

        const float* ch0 = pcm.getReadPointer(0);
        const double stepF = static_cast<double>(numSamples) / static_cast<double>(count);
        const int zcWindow = static_cast<int>(kBlockMs * 0.001 * sampleRate + 0.5);

        for (int k = 1; k < count; ++k)
        {
            const int target = static_cast<int>(static_cast<double>(k) * stepF);
            out.push_back(nearestZeroCrossing(ch0, numSamples, target, zcWindow));
        }
        return out;
    }

    // Find the sample position of the first onset (attack) in `ba`, searching
    // only up to `searchLimitSamples`. An onset block has real energy AND a
    // fast/slow envelope ratio above the transient threshold — the same test the
    // transient placer uses. Returns the block-mid sample position, or 0 when no
    // onset is found in range (material that starts on the 1, or is a wash).
    inline int findFirstOnsetSample(const BlockAnalysis& ba, int searchLimitSamples)
    {
        if (ba.numBlocks < 1 || ba.blockSize < 1)
            return 0;
        for (int b = 0; b < ba.numBlocks; ++b)
        {
            const int blockMid = static_cast<int>(
                (static_cast<double>(b) + 0.5) * static_cast<double>(ba.blockSize));
            if (blockMid >= searchLimitSamples)
                break;
            const float rms = ba.rms[static_cast<std::size_t>(b)];
            const float slow = ba.slowEnv[static_cast<std::size_t>(b)];
            const float fast = ba.fastEnv[static_cast<std::size_t>(b)];
            if (rms > 1e-4f && slow > 1e-6f && fast > kTransAbsThreshold * slow)
                return blockMid;
        }
        return 0;
    }

    // Place slices on a beat grid at the detected tempo (4.9 SYNC mode). The grid
    // is anchored on the first onset — a loop may not start exactly on the 1, so
    // the slices track where the audio actually begins — with every boundary
    // snapped to the nearest zero crossing. `divisionIndex` selects the spacing
    // from kSyncBeatsPerSlice. Returns absolute sample positions (first is 0).
    // Callers handle bpm <= 0 (fall back to EQUAL) before calling; the guard here
    // is belt-and-braces.
    inline std::vector<int> placeSyncSlices(const juce::AudioBuffer<float>& pcm,
                                            double sampleRate, double bpm,
                                            int divisionIndex,
                                            const BlockAnalysis& ba)
    {
        std::vector<int> out;
        out.push_back(0);   // slice 0 always at position 0

        const int numSamples = pcm.getNumSamples();
        if (numSamples < 2 || sampleRate <= 0.0 || bpm <= 0.0)
            return out;

        divisionIndex = std::clamp(divisionIndex, 0, kNumSyncDivisions - 1);
        const double spacing = (60.0 / bpm) * sampleRate
                               * kSyncBeatsPerSlice[static_cast<std::size_t>(divisionIndex)];
        if (spacing < 1.0)
            return out;

        const int minGap = static_cast<int>(kMinSliceMs * 0.001 * sampleRate);
        const float* ch0 = pcm.getReadPointer(0);
        const int zcWindow = (ba.blockSize > 0) ? ba.blockSize
                                                : static_cast<int>(kBlockMs * 0.001 * sampleRate + 0.5);

        // Anchor the grid on the first onset (ZC-snapped). An onset within one
        // minimum-slice of the start is treated as "starts on the 1" -> anchor 0,
        // so a clean loop yields a plain grid with no duplicate boundary at 0.
        int anchor = findFirstOnsetSample(ba, std::min(numSamples, static_cast<int>(spacing)));
        anchor = nearestZeroCrossing(ch0, numSamples, anchor, zcWindow);
        if (anchor < minGap)
            anchor = 0;

        for (int k = 0; ; ++k)
        {
            const double pos = static_cast<double>(anchor) + static_cast<double>(k) * spacing;
            if (pos <= static_cast<double>(minGap))
                continue;   // skip boundaries too close to the start
            if (pos > static_cast<double>(numSamples - minGap))
                break;      // past the usable tail
            out.push_back(nearestZeroCrossing(ch0, numSamples,
                                              static_cast<int>(pos), zcWindow));
            if (static_cast<int>(out.size()) >= 16)
                break;      // 16-slice cap: head is gridded, tail lands in the last slice
        }
        return out;
    }

    // Place N slices using TRANS mode (equal-boundary seeds with transient snap).
    // ba must be the BlockAnalysis for the same pcm (from analyseSample).
    // Returns absolute sample positions (first is always 0).
    inline std::vector<int> placeTransientSlices(const juce::AudioBuffer<float>& pcm,
                                                 double sampleRate,
                                                 int count,
                                                 const BlockAnalysis& ba)
    {
        const int numSamples = pcm.getNumSamples();
        count = std::clamp(count, 1,
                           std::max(1, static_cast<int>(
                                           static_cast<double>(numSamples) / (kMinSliceMs * 0.001 * sampleRate))));
        count = std::min(count, 16);

        std::vector<int> out;
        out.reserve(static_cast<std::size_t>(count));
        out.push_back(0);

        if (ba.numBlocks < 1 || ba.blockSize < 1)
            return out;

        const float* ch0 = pcm.getReadPointer(0);
        const double sliceWidth = static_cast<double>(numSamples) / static_cast<double>(count);
        const double searchHalfWidth = sliceWidth * 0.25;  // ±¼ slice width
        const int zcWindow = ba.blockSize;

        for (int k = 1; k < count; ++k)
        {
            // Equal-boundary seed position.
            const double seedPos = static_cast<double>(k) * sliceWidth;

            // Search blocks within ±¼ slice width; triangular centre-weight.
            int bestBlock = static_cast<int>(seedPos / static_cast<double>(ba.blockSize));
            float bestWeighted = -1.0f;

            const int blockLo = static_cast<int>(
                std::max(0.0, (seedPos - searchHalfWidth) / static_cast<double>(ba.blockSize)));
            const int blockHi = static_cast<int>(std::min(
                static_cast<double>(ba.numBlocks - 1),
                (seedPos + searchHalfWidth) / static_cast<double>(ba.blockSize)));

            for (int bi = blockLo; bi <= blockHi; ++bi)
            {
                const double blockCentre = (static_cast<double>(bi) + 0.5) * static_cast<double>(ba.blockSize);
                const double dist = std::abs(blockCentre - seedPos);
                const float weight = static_cast<float>(
                    std::max(0.0, 1.0 - dist / searchHalfWidth));

                // fast/slow ratio = transient strength at this block.
                const float slow = ba.slowEnv[static_cast<std::size_t>(bi)];
                const float score = (slow > 1e-6f)
                                        ? ba.fastEnv[static_cast<std::size_t>(bi)] / slow
                                        : 1.0f;

                const float weighted = score * weight;
                if (weighted > bestWeighted)
                {
                    bestWeighted = weighted;
                    bestBlock = bi;
                }
            }

            // Fallback to equal-boundary block if weighted score is too low.
            const int equalBlock = static_cast<int>(
                seedPos / static_cast<double>(ba.blockSize));
            if (bestWeighted < kTransAbsThreshold)
                bestBlock = equalBlock;

            // Within the chosen block, find zero-crossing nearest the block midpoint.
            const int blockMid = static_cast<int>(
                (static_cast<double>(bestBlock) + 0.5) * static_cast<double>(ba.blockSize));
            out.push_back(nearestZeroCrossing(ch0, numSamples, blockMid, zcWindow));
        }
        return out;
    }
}
