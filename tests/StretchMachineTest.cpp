// StretchMachineTest -- the Flex-analog Player (C3): independent pitch + tempo via
// the Bungee stretch engine (9.23). Verifies the decoupling that distinguishes it
// from the rate-based Sample:
//   - transposing (note up an octave) does NOT shorten the output (pitch != speed).
//   - timestretch=Tempo stretches the buffer to the project tempo (duration tracks).
//   - tune=+50c shifts pitch half a semitone (fine-tune independent of transpose).
//   - reverse plays the amplitude envelope mirrored.
//   - a v29-shaped (4-slot) frame loads tune=0 / loop=Off / rev=Fwd defaults.

#include "TestHarness.h"
#include "../src/machine/StretchMachine.h"
#include "../src/machine/SamplePool.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        constexpr double kSr = 48000.0;

        // Build a volatile pool entry holding a sine, stamped with sourceBars.
        int makeSine(SamplePool& pool, int len, double freq, double bars)
        {
            const int idx = pool.addVolatile();
            pool.prepareVolatile(kSr, 1, len);
            auto* pcm = pool.mutableVolatilePcm(idx);
            for (int i = 0; i < len; ++i)
                pcm->setSample(0, i, static_cast<float>(
                    std::sin(2.0 * juce::MathConstants<double>::pi * freq
                             * static_cast<double>(i) / kSr)));
            pool.setSourceBars(idx, bars);
            return idx;
        }

        // Count non-silent output samples while holding `note`, over up to maxLen.
        int activeSamples(StretchMachine& p, const ParamFrame& params, int note, int maxLen)
        {
            juce::AudioBuffer<float> blk(1, 256);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            juce::MidiBuffer none;
            int total = 0, count = 0;
            bool nan = false;
            for (int b = 0; total < maxLen; ++b)
            {
                blk.clear();
                p.process(b == 0 ? on : none, params, blk);
                for (int i = 0; i < 256; ++i)
                {
                    const float x = blk.getSample(0, i);
                    if (!std::isfinite(x)) nan = true;
                    if (std::abs(x) > 1.0e-3f) ++count;
                    ++total;
                }
            }
            CHECK(!nan, "Player: no NaN/Inf in output");
            return count;
        }

        ParamFrame playerFrame(int sampleId, float pitch, float tsMode)
        {
            // sample_id, pitch, timestretch, start (v29 shape — 4 slots)
            return ParamFrame{ static_cast<float>(sampleId), pitch, tsMode, 0.0f };
        }

        // Full 7-slot frame: sample_id, pitch, ts, start, tune, loop, reverse.
        ParamFrame playerFrameEx(int sampleId, float pitch, float tsMode,
                                 float tuneCents, float loop, float reverse)
        {
            return ParamFrame{ static_cast<float>(sampleId), pitch, tsMode, 0.0f,
                               tuneCents, loop, reverse };
        }

        // Collect held-note output into a flat vector (channel 0), then trim the
        // trailing silence so the result is just the one-shot's actual playback.
        std::vector<float> collectOut(StretchMachine& p, const ParamFrame& params,
                                      int note, int maxLen)
        {
            juce::AudioBuffer<float> blk(1, 256);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
            juce::MidiBuffer none;
            std::vector<float> v;
            for (int b = 0; static_cast<int>(v.size()) < maxLen; ++b)
            {
                blk.clear();
                p.process(b == 0 ? on : none, params, blk);
                for (int i = 0; i < 256; ++i) v.push_back(blk.getSample(0, i));
            }
            while (!v.empty() && std::abs(v.back()) < 1.0e-4f) v.pop_back();
            return v;
        }

        double dominantFreq(const std::vector<float>& v)
        {
            const int n = static_cast<int>(v.size());
            if (n < 8) return 0.0;
            const int lo = n / 4, hi = (3 * n) / 4;
            int crossings = 0;
            for (int i = lo + 1; i < hi; ++i)
                if ((v[static_cast<std::size_t>(i - 1)] <= 0.0f)
                    != (v[static_cast<std::size_t>(i)] <= 0.0f))
                    ++crossings;
            const double dur = static_cast<double>(hi - lo) / kSr;
            return (dur > 0.0) ? (static_cast<double>(crossings) / 2.0) / dur : 0.0;
        }
    }

    void runStretchMachineTests()
    {
        const int srcLen = 24000;  // 0.5 s
        const int cap = srcLen * 4;

        // Off mode at root: non-silent, active ~ source length.
        SamplePool pool;
        const int idx = makeSine(pool, srcLen, 440.0, /*bars*/ 1.0);

        // 9.18: a Flex-style PCM player resolves resident PCM, not disk streams.
        {
            StretchMachine p(pool);
            CHECK(p.sampleClass() == IMachine::SampleClass::Pcm,
                  "StretchMachine is a Pcm sample class");
        }

        int activeRoot = 0;
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            auto fr = playerFrame(idx, 0.0f, 0.0f);  // Off
            activeRoot = activeSamples(p, fr, 60, cap);
            CHECK(activeRoot > srcLen / 2,
                  "Player: root note produces audio (active=" + juce::String(activeRoot) + ")");
        }

        // Off mode, octave up: duration must NOT shrink (pitch decoupled from speed).
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            auto fr = playerFrame(idx, 0.0f, 0.0f);  // Off
            const int activeOct = activeSamples(p, fr, 72, cap);
            const double ratio = static_cast<double>(activeOct) / std::max(1, activeRoot);
            CHECK(ratio > 0.7 && ratio < 1.4,
                  "Player: transposing an octave keeps the duration (ratio=" + juce::String(ratio) + ")");
        }

        // Tempo mode with a doubled bar length: output stretched ~2x.
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            TransportInfo tr;
            tr.samplesPerBar = 2.0 * static_cast<double>(srcLen);  // project bar = 2x the capture bar
            tr.running = true;
            p.setTransport(tr);
            auto fr = playerFrame(idx, 0.0f, 1.0f);  // Tempo
            const int activeTempo = activeSamples(p, fr, 60, srcLen * 6);
            const double ratio = static_cast<double>(activeTempo) / std::max(1, activeRoot);
            CHECK(ratio > 1.5 && ratio < 2.6,
                  "Player: Tempo mode stretches to the project tempo (~2x, ratio=" + juce::String(ratio) + ")");
        }

        // Param defaults: a v29-shaped 4-slot frame loads tune=0/loop=Off/rev=Fwd.
        {
            StretchMachine p(pool);
            CHECK(p.numParams() == 7, "Player: 7 slots after the tune/loop/reverse append");
            const auto tuneSpec = p.paramSpec(4);
            const auto loopSpec = p.paramSpec(5);
            const auto revSpec  = p.paramSpec(6);
            CHECK(tuneSpec.id == "player_tune" && feq(tuneSpec.defaultValue, 0.0f),
                  "Player: slot 4 = player_tune default 0");
            CHECK(loopSpec.id == "player_loop" && feq(loopSpec.defaultValue, 0.0f),
                  "Player: slot 5 = player_loop default Off");
            CHECK(revSpec.id == "player_reverse" && feq(revSpec.defaultValue, 0.0f),
                  "Player: slot 6 = player_reverse default Fwd");
        }

        // Fine-tune: +50 cents shifts pitch half a semitone (2^(50/1200) ~= 1.029).
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            auto root = playerFrame(idx, 0.0f, 0.0f);              // Off, no tune
            auto tuned = playerFrameEx(idx, 0.0f, 0.0f, 50.0f, 0.0f, 0.0f);
            const double fRoot = dominantFreq(collectOut(p, root, 60, srcLen * 2));
            StretchMachine p2(pool);
            p2.prepare(kSr, 256);
            const double fTuned = dominantFreq(collectOut(p2, tuned, 60, srcLen * 2));
            const double r = fTuned / std::max(1.0, fRoot);
            CHECK(r > 1.012 && r < 1.048,
                  "Player: tune=+50c is half a semitone up (ratio=" + juce::String(r) + ")");
        }

        // Tempo+Loop (S4): a held note loops seamlessly — it stays non-silent well
        // past one buffer length (unlike a one-shot), has no long silent gap at the
        // seam, and survives a mid-run samplesPerBar change (phase-lock by period-
        // matching: per-block setRatios re-derives the period, never a reset).
        {
            StretchMachine p(pool);
            p.prepare(kSr, 256);
            TransportInfo tr;
            tr.samplesPerBar = static_cast<double>(srcLen);
            tr.running = true;
            p.setTransport(tr);
            auto fr = playerFrameEx(idx, 0.0f, 1.0f /*Tempo*/, 0.0f, 1.0f /*loop*/, 0.0f);

            juce::AudioBuffer<float> blk(1, 256);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            juce::MidiBuffer none;

            const int totalBlocks = (srcLen * 4) / 256;
            std::vector<float> tail;   // last-half samples (steady state)
            int maxSilentRun = 0, silentRun = 0;
            bool nan = false;
            int produced = 0;
            for (int b = 0; b < totalBlocks; ++b)
            {
                if (b == totalBlocks / 2)  // mid-run tempo change
                {
                    tr.samplesPerBar = 1.5 * static_cast<double>(srcLen);
                    p.setTransport(tr);
                }
                blk.clear();
                p.process(b == 0 ? on : none, fr, blk);
                for (int i = 0; i < 256; ++i, ++produced)
                {
                    const float x = blk.getSample(0, i);
                    if (!std::isfinite(x)) nan = true;
                    if (std::abs(x) < 1.0e-4f) { ++silentRun; maxSilentRun = std::max(maxSilentRun, silentRun); }
                    else silentRun = 0;
                    if (produced >= srcLen * 3) tail.push_back(x);
                }
            }
            CHECK(!nan, "Loop: no NaN/Inf across the tempo change");
            CHECK(maxSilentRun < srcLen / 4,
                  "Loop: no long silent gap at the seam (max silent run "
                  + juce::String(maxSilentRun) + ")");
            double tailEnergy = 0.0;
            for (float x : tail) tailEnergy += static_cast<double>(x) * x;
            CHECK(tailEnergy > 1.0,
                  "Loop: still sounding after 3 buffer lengths (one-shot would be silent)");
        }

        // Reverse: the amplitude envelope plays mirrored. Build a fade-in sine so
        // forward output rises in energy and reverse output falls.
        {
            const int rl = 24000;
            const int ridx = pool.addVolatile();
            pool.prepareVolatile(kSr, 1, rl);
            auto* rp = pool.mutableVolatilePcm(ridx);
            for (int i = 0; i < rl; ++i)
            {
                const double env = static_cast<double>(i) / rl;   // 0 -> 1 ramp
                rp->setSample(0, i, static_cast<float>(env * std::sin(
                    2.0 * juce::MathConstants<double>::pi * 440.0
                    * static_cast<double>(i) / kSr)));
            }
            pool.setSourceBars(ridx, 1.0);

            auto energyHalves = [](const std::vector<float>& v) {
                double e1 = 0.0, e2 = 0.0;
                const std::size_t half = v.size() / 2;
                for (std::size_t i = 0; i < v.size(); ++i)
                    (i < half ? e1 : e2) += static_cast<double>(v[i]) * v[i];
                return std::pair<double, double>{ e1, e2 };
            };

            StretchMachine pf(pool);
            pf.prepare(kSr, 256);
            auto fwd = energyHalves(collectOut(pf, playerFrame(ridx, 0.0f, 0.0f), 60, rl * 2));

            StretchMachine pr(pool);
            pr.prepare(kSr, 256);
            auto rev = energyHalves(collectOut(pr, playerFrameEx(ridx, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 60, rl * 2));

            CHECK(fwd.second > fwd.first,
                  "Player: forward fade-in has rising energy (first=" + juce::String(fwd.first)
                  + " second=" + juce::String(fwd.second) + ")");
            CHECK(rev.first > rev.second,
                  "Player: reverse mirrors the envelope — falling energy (first=" + juce::String(rev.first)
                  + " second=" + juce::String(rev.second) + ")");
        }
    }
}
