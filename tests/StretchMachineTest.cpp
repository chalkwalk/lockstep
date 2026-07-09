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
            CHECK(p.numParams() == 9,
                  "Player: 9 slots after tune/loop/reverse/tune_mode/release");
            CHECK(juce::String(p.paramSpec(7).id) == "player_tune_mode"
                  && feq(p.paramSpec(7).defaultValue, 0.0f),
                  "Player: slot 7 = player_tune_mode default Auto");
            CHECK(juce::String(p.paramSpec(8).id) == "player_release"
                  && feq(p.paramSpec(8).defaultValue, 0.5f),
                  "Player: slot 8 = player_release default 0.5 (graceful-stop fade)");
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

        // A440 Auto/Raw (S7): a +30c-stamped sample plays 30c flat under Auto (the
        // detected deviation is cancelled) and unchanged under Raw.
        {
            pool.setUserTuningCents(idx, 30.0, true);  // effective tuning = +30 cents
            StretchMachine pa(pool);
            pa.prepare(kSr, 256);
            StretchMachine pr(pool);
            pr.prepare(kSr, 256);
            // slot 7 = tune_mode: 0 Auto, 1 Raw. Build 8-slot frames.
            ParamFrame autoFr{ static_cast<float>(idx), 0,0,0,0,0,0, 0.0f };
            ParamFrame rawFr { static_cast<float>(idx), 0,0,0,0,0,0, 1.0f };
            const double fAuto = dominantFreq(collectOut(pa, autoFr, 60, srcLen * 2));
            const double fRaw  = dominantFreq(collectOut(pr, rawFr,  60, srcLen * 2));
            const double ratio = fAuto / std::max(1.0, fRaw);
            const double expect = std::pow(2.0, -30.0 / 1200.0);  // ~0.9827
            CHECK(ratio > expect * 0.99 && ratio < expect * 1.01,
                  "A440: Auto plays 30c flat vs Raw (ratio=" + juce::String(ratio)
                  + " expect~" + juce::String(expect) + ")");
            pool.setUserTuningCents(idx, 0.0, false);  // restore for later blocks
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

        // Live loop re-latch (9.26): player_loop is latched only at note-on, but a
        // sustaining one-shot voice never re-fires (one-shots re-arm on transport /
        // scene launch), so a mid-voice toggle must be honoured live in process().
        // (a) start loop Off, flip On before the one-shot ends → still sounding well
        // past 3 buffer lengths; (b) start loop On, flip Off → silent once the pass
        // completes.
        {
            auto runReLatch = [&](float startLoop, float endLoop, int drainBlocks) {
                StretchMachine p(pool);
                p.prepare(kSr, 256);
                TransportInfo tr;
                tr.samplesPerBar = static_cast<double>(srcLen);
                tr.running = true;
                p.setTransport(tr);
                auto startFr = playerFrameEx(idx, 0.0f, 1.0f /*Tempo*/, 0.0f, startLoop, 0.0f);
                auto endFr   = playerFrameEx(idx, 0.0f, 1.0f, 0.0f, endLoop, 0.0f);

                juce::AudioBuffer<float> blk(1, 256);
                juce::MidiBuffer on;
                on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                juce::MidiBuffer none;

                const int flipBlock = (srcLen / 2) / 256;   // flip mid-voice, pre-end
                std::vector<float> tail;
                int produced = 0;
                for (int b = 0; b < drainBlocks; ++b)
                {
                    blk.clear();
                    const ParamFrame& fr = (b >= flipBlock) ? endFr : startFr;
                    p.process(b == 0 ? on : none, fr, blk);
                    for (int i = 0; i < 256; ++i, ++produced)
                        if (produced >= srcLen * 3) tail.push_back(blk.getSample(0, i));
                }
                double e = 0.0;
                for (float x : tail) e += static_cast<double>(x) * x;
                return e;
            };

            CHECK(runReLatch(0.0f, 1.0f, (srcLen * 4) / 256) > 1.0,
                  "Loop re-latch: Off→On mid-voice keeps it sounding past 3 buffers");
            CHECK(runReLatch(1.0f, 0.0f, (srcLen * 5) / 256) < 1.0e-2,
                  "Loop re-latch: On→Off mid-voice stops it after the pass completes");
        }

        // Graceful-stop release: releaseAllVoices() fades the held voice out over
        // player_release, NOT the ~5 ms note-off gate. With a long release the voice
        // is still clearly audible ~50 ms after the stop; with a short release it is
        // already silent. Loop On keeps the engine sustaining so the gain ramp is the
        // only thing changing the level.
        {
            auto runRelease = [&](float releaseVal) {
                StretchMachine p(pool);
                p.prepare(kSr, 256);
                TransportInfo tr;
                tr.samplesPerBar = static_cast<double>(srcLen);
                tr.running = true;
                p.setTransport(tr);
                // sampleId, pitch, ts(Tempo), start, tune, loop(On), reverse, tuneMode, release
                ParamFrame fr{ static_cast<float>(idx), 0.0f, 1.0f, 0.0f,
                               0.0f, 1.0f, 0.0f, 0.0f, releaseVal };
                juce::AudioBuffer<float> blk(1, 256);
                juce::MidiBuffer on;
                on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                juce::MidiBuffer none;
                p.process(on, fr, blk);
                for (int b = 0; b < 6; ++b) { blk.clear(); p.process(none, fr, blk); }
                p.releaseAllVoices();
                double e = 0.0;  // energy over ~48 ms (9 * 256 / 48k) after the stop
                for (int b = 0; b < 9; ++b)
                {
                    blk.clear();
                    p.process(none, fr, blk);
                    for (int i = 0; i < 256; ++i)
                        e += static_cast<double>(blk.getSample(0, i)) * blk.getSample(0, i);
                }
                return e;
            };
            const double longRel  = runRelease(1.0f);  // ~2 s fade
            const double shortRel = runRelease(0.0f);   // ~5 ms fade
            CHECK(longRel > shortRel * 20.0 + 1.0,
                  "graceful release: a long player_release keeps the voice sounding ~50 ms "
                  "past the stop where a short release is already silent (long "
                  + juce::String(longRel, 2) + " vs short " + juce::String(shortRel, 4) + ")");
        }

        // Hard CUT: killAllVoices() snaps the voice out over the fast ~5 ms gate even
        // with a LONG player_release set — the layered stop's track/master cut must
        // leave nothing decaying, so a resumed transport starts clean. With the SAME
        // long release, releaseAllVoices() (graceful stop) keeps ringing ~50 ms; kill
        // ignores the release param, so its post-stop energy is far lower.
        {
            // Both variants use release = 1.0 (~2 s graceful fade); `kill` selects the
            // hard cut, so the only difference is which stop method fires.
            auto runStop = [&](bool kill) {
                StretchMachine p(pool);
                p.prepare(kSr, 256);
                TransportInfo tr;
                tr.samplesPerBar = static_cast<double>(srcLen);
                tr.running = true;
                p.setTransport(tr);
                ParamFrame fr{ static_cast<float>(idx), 0.0f, 1.0f, 0.0f,
                               0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
                juce::AudioBuffer<float> blk(1, 256);
                juce::MidiBuffer on;
                on.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                juce::MidiBuffer none;
                p.process(on, fr, blk);
                for (int b = 0; b < 6; ++b) { blk.clear(); p.process(none, fr, blk); }
                if (kill) p.killAllVoices(); else p.releaseAllVoices();
                double e = 0.0;  // energy over ~48 ms after the stop
                for (int b = 0; b < 9; ++b)
                {
                    blk.clear();
                    p.process(none, fr, blk);
                    for (int i = 0; i < 256; ++i)
                        e += static_cast<double>(blk.getSample(0, i)) * blk.getSample(0, i);
                }
                return e;
            };
            const double killE    = runStop(true);
            const double gracefulE = runStop(false);
            CHECK(gracefulE > killE * 20.0 + 1.0,
                  "hard cut: killAllVoices() ignores a long player_release — its ~48 ms "
                  "post-stop energy is far below the graceful release's (kill "
                  + juce::String(killE, 4) + " vs graceful " + juce::String(gracefulE, 2) + ")");
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
