#pragma once

// KeyEstimate.h — key + tuning detection for a loaded sample (DESIGN §28,
// 4.9 sample-analysis metadata). Message-thread only; a pure function of the
// decoded PCM. Mirrors TempoEstimate.h in shape: header-only, no state, a
// single entry point that returns 0/-1 for "unknown" the way estimateBpm()
// returns 0.
//
// Approach: one short-time FFT sweep over the whole sample, accumulate
// magnitude only for bins whose centre frequency sits in a musical band
// (60..2000 Hz — the band limit IS the accumulation gate, no pre-filter).
// From that band-limited spectrum:
//   1. estimate the tuning reference (deviation from A440 ET) as the weighted
//      circular mean of each bin's fractional-semitone offset;
//   2. fold the tuning-corrected energy into a 12-bin chroma;
//   3. score 12 roots x 7 brightnesses with the circle-of-fifths note-strength
//      scorer already used by the melodic generator (Scale.h noteStrengthRank),
//      so key detection and key authoring share one tonal model.
// Depth is root + brightness only (no modifiers). root = -1 means "unknown"
// (silence / noise / too little tonal energy), exactly like bpm 0.
//
// Cost: 30s @ 48k is ~700 FFTs of 4096 -> a few ms at load, on the message
// thread. The caller gates length with kMaxAnalysisSeconds (TempoEstimate.h).

#include "../core/Scale.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lockstep
{
    inline constexpr int    kKeyFftOrder   = 12;      // N = 4096
    inline constexpr int    kKeyHop        = 2048;    // 50% overlap
    inline constexpr double kKeyBandLoHz   = 60.0;
    inline constexpr double kKeyBandHiHz   = 2000.0;
    // Minimum normalised score margin (best over |best|) for the key to be
    // trusted. Tonal material clears this comfortably; noise stays below it.
    inline constexpr double kMinKeyConfidence = 0.03;
    // Minimum chroma "peakiness" (loudest pitch class relative to a flat 1/12
    // distribution) for the material to count as tonal at all. Broadband noise
    // spreads energy across every pitch class (peakiness ~1.3x) and would
    // otherwise clear the thin relative-mode confidence margin; real tonal
    // content concentrates well above this.
    inline constexpr double kMinChromaPeakiness = 1.6;
    // Extra weight given to energy sitting on a candidate's tonic. The pure
    // fifths-distance strength is symmetric (the note a fifth BELOW the tonic
    // ranks as strongly as the fifth above), so relative modes — which share an
    // identical pitch-class set — are indistinguishable without breaking that
    // symmetry. Rewarding tonic energy picks the root whose tonic actually
    // carries the weight; the pitch-class-set match then fixes the mode.
    inline constexpr double kKeyTonicBonus = 24.0;

    struct KeyEstimate
    {
        int    root       = -1;         // pitch class 0..11, -1 = unknown
        int    brightness = kAeolian;   // Scale.h Brightness; only when root >= 0
        double tuningCents = 0.0;       // deviation from A440 ET, [-50..50)
        double confidence  = 0.0;       // score margin (0 when unknown)
    };

    inline KeyEstimate estimateKey(const juce::AudioBuffer<float>& pcm, double sampleRate)
    {
        KeyEstimate out;

        constexpr int kN = 1 << kKeyFftOrder;   // 4096
        const int numSamples = pcm.getNumSamples();
        const int numChannels = pcm.getNumChannels();
        if (sampleRate <= 0.0 || numChannels < 1 || numSamples < kN)
            return out;   // too short / empty -> unknown

        juce::dsp::FFT fft(kKeyFftOrder);

        // Hann window (precomputed once).
        std::vector<float> window(static_cast<std::size_t>(kN));
        for (int i = 0; i < kN; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                                  * static_cast<double>(i)
                                                  / static_cast<double>(kN - 1));
            window[static_cast<std::size_t>(i)] = static_cast<float>(w);
        }

        // performFrequencyOnlyForwardTransform needs 2N floats of scratch.
        std::vector<float> scratch(static_cast<std::size_t>(2 * kN), 0.0f);

        // Band-limited magnitude spectrum (only the [lo,hi] Hz bins accumulate).
        const int halfN = kN / 2;
        std::vector<double> spec(static_cast<std::size_t>(halfN), 0.0);
        const int loBin = std::max(1, static_cast<int>(std::ceil(kKeyBandLoHz
                              * static_cast<double>(kN) / sampleRate)));
        const int hiBin = std::min(halfN - 1, static_cast<int>(std::floor(kKeyBandHiHz
                              * static_cast<double>(kN) / sampleRate)));
        if (hiBin <= loBin)
            return out;   // band collapses (absurd sample rate) -> unknown

        for (int start = 0; start + kN <= numSamples; start += kKeyHop)
        {
            // Mono-sum channels into the windowed frame; zero-pad the tail.
            for (int i = 0; i < kN; ++i)
            {
                double s = 0.0;
                for (int ch = 0; ch < numChannels; ++ch)
                    s += static_cast<double>(pcm.getReadPointer(ch)[start + i]);
                scratch[static_cast<std::size_t>(i)] =
                    static_cast<float>(s) * window[static_cast<std::size_t>(i)];
            }
            for (int i = kN; i < 2 * kN; ++i)
                scratch[static_cast<std::size_t>(i)] = 0.0f;

            fft.performFrequencyOnlyForwardTransform(scratch.data());

            for (int bin = loBin; bin <= hiBin; ++bin)
                spec[static_cast<std::size_t>(bin)] +=
                    static_cast<double>(scratch[static_cast<std::size_t>(bin)]);
        }

        // Spectral-peak picking with parabolic interpolation. A single FFT bin
        // is ~10 Hz wide, which is tens of cents at bass frequencies — far too
        // coarse to read tuning or even assign a pitch class directly. Instead
        // we take only local maxima that clear a floor relative to the loudest
        // bin, and refine each to sub-bin frequency via a parabola fit over its
        // three magnitudes. This gives both accurate tuning and a clean chroma;
        // broadband noise yields no dominant peaks, so its chroma stays flat and
        // the confidence gate below rejects it.
        double specMax = 0.0;
        for (int bin = loBin; bin <= hiBin; ++bin)
            specMax = std::max(specMax, spec[static_cast<std::size_t>(bin)]);
        if (specMax <= 1e-9)
            return out;   // silence -> unknown
        const double peakFloor = specMax * 0.05;

        struct Peak { double freq; double mag; };
        std::vector<Peak> peaks;
        for (int bin = loBin; bin <= hiBin; ++bin)
        {
            const double m = spec[static_cast<std::size_t>(bin)];
            if (m < peakFloor) continue;
            const double lm = spec[static_cast<std::size_t>(bin - 1)];
            const double rm = spec[static_cast<std::size_t>(bin + 1)];
            if (m < lm || m < rm) continue;   // local maximum only
            // Parabolic (log-magnitude) interpolation for the sub-bin offset.
            const double a = std::log(lm + 1e-12);
            const double bb = std::log(m + 1e-12);
            const double c = std::log(rm + 1e-12);
            const double denom = a - 2.0 * bb + c;
            double delta = 0.0;
            if (std::abs(denom) > 1e-12)
                delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
            const double refinedBin = static_cast<double>(bin) + delta;
            peaks.push_back({ refinedBin * sampleRate / static_cast<double>(kN), m });
        }
        if (peaks.empty())
            return out;   // no tonal peaks -> unknown

        // Tuning: weighted circular mean of each peak's fractional-semitone
        // offset. S/C accumulate sin/cos of one cycle (= one semitone); the
        // resolved angle -> cents deviation from A440 ET.
        double S = 0.0, C = 0.0;
        for (const auto& pk : peaks)
        {
            const double p = 69.0 + 12.0 * std::log2(pk.freq / 440.0);
            const double frac = p - std::round(p);   // [-0.5, 0.5)
            const double ang = 2.0 * juce::MathConstants<double>::pi * frac;
            S += pk.mag * std::sin(ang);
            C += pk.mag * std::cos(ang);
        }
        if (std::abs(S) < 1e-12 && std::abs(C) < 1e-12)
            return out;   // no tonal energy -> unknown
        out.tuningCents = std::atan2(S, C) / (2.0 * juce::MathConstants<double>::pi) * 100.0;

        // Tuning-corrected chroma fold. Unlike tuning (which needs clean,
        // isolated peaks) the chroma takes EVERY in-band bin above the floor,
        // not just strict local maxima: a semitone-neighbour partial whose main
        // lobe is masked by a louder neighbour still carries real energy for its
        // pitch class, and dropping it (e.g. F masked by E in a tight voicing)
        // would make two modes that differ only on that degree indistinguishable.
        std::array<double, 12> chroma { };
        double total = 0.0;
        for (int bin = loBin; bin <= hiBin; ++bin)
        {
            const double mag = spec[static_cast<std::size_t>(bin)];
            if (mag < peakFloor) continue;
            const double f = static_cast<double>(bin) * sampleRate / static_cast<double>(kN);
            const double p = 69.0 + 12.0 * std::log2(f / 440.0);
            const int pc = ((static_cast<int>(std::lround(p - out.tuningCents / 100.0)) % 12) + 12) % 12;
            chroma[static_cast<std::size_t>(pc)] += mag;
            total += mag;
        }
        if (total <= 1e-9)
            return out;   // silence -> unknown
        for (auto& c : chroma)
            c /= total;

        // Concentration gate: how peaked is the loudest pitch class against a
        // flat 1/12 spread. Rejects broadband noise before it can clear the thin
        // relative-mode confidence margin.
        const double peakiness = *std::max_element(chroma.begin(), chroma.end()) * 12.0;

        // Score 12 roots x brightness (Locrian..Lydian), Diatonic, no modifiers.
        // In-scale pcs score by note strength (stronger = higher weight, so we
        // invert the rank); out-of-scale pcs are penalised.
        double best = -1e300, secondBest = -1e300;
        int bestRoot = -1, bestBright = kAeolian;
        for (int root = 0; root < 12; ++root)
        {
            for (int b = kLocrian; b <= kLydian; ++b)
            {
                KeySig k;
                k.root = static_cast<std::uint8_t>(root);
                k.brightness = static_cast<std::int8_t>(b);
                k.scaleType = ScaleType::Diatonic;
                const std::uint16_t mask = pcMask(k);

                double score = 0.0;
                for (int pc = 0; pc < 12; ++pc)
                {
                    double w = maskHas(mask, pc)
                        ? static_cast<double>(12 - noteStrengthRank(k, pc))
                        : -6.0;
                    if (pc == root)
                        w += kKeyTonicBonus;   // break relative-mode symmetry
                    score += chroma[static_cast<std::size_t>(pc)] * w;
                }

                if (score > best)
                {
                    secondBest = best;
                    best = score;
                    bestRoot = root;
                    bestBright = b;
                }
                else if (score > secondBest)
                {
                    secondBest = score;
                }
            }
        }

        out.confidence = (best - secondBest) / std::max(std::abs(best), 1e-9);
        if (best > 0.0 && out.confidence >= kMinKeyConfidence
            && peakiness >= kMinChromaPeakiness)
        {
            out.root = bestRoot;
            out.brightness = bestBright;
        }
        return out;
    }
}
