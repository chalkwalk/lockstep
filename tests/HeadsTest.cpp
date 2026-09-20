// HeadsTest — dc::ReadHead / dc::WriteHead (DESIGN §40.10).
//
// The heads obey one signal law across the whole signed rate axis. What that
// means concretely, and what this file pins down:
//
//   * A write followed by a read at the same rate returns the signal, at the
//     same level, for rate 0.5, 1, 2 and -1. The |rate| deposit gain is why:
//     kernel density on the medium is 1/rate, so flux does not depend on
//     transport speed — as on tape.
//   * A stalled head (rate 0) writes nothing. Without the gain it would pile
//     unbounded energy onto one spot.
//   * Reading above unity band-limits: pitching a bright tone up must not fold
//     it back down (the whole reason the read is not Hermite).
//   * A medium's sample rate is its own. A 2x oversampled medium is
//     configuration, not a mode: the same head, at twice the rate, returns the
//     same signal.
//
// And the acceptance test for the interface itself: a **working tape delay**,
// built from Medium + WriteHead + ReadHead alone, with the feedback path in the
// caller. Lockstep never walks that path (its decks use one read head), so
// nothing but this test defends the partner app's echo mode.

#include "TestHarness.h"
#include "../src/deckcore/EraseHead.h"
#include "../src/deckcore/Heads.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lockstep
{
    // Defined below runHeadsTests, which calls it: the erase head is the third
    // head of the same law and belongs in this file.
    void runEraseHeadTests();

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        struct Reel
        {
            std::vector<float> storage;
            dc::Medium medium;

            Reel(dc::Topology topo, int capacity, int subTracks = 1, int chans = 1)
            {
                dc::Medium::Config cfg;
                cfg.topology = topo;
                cfg.numSubTracks = subTracks;
                cfg.channelsPerSubTrack = chans;
                cfg.capacitySamples = capacity;
                storage.assign(dc::Medium::storageSamples(cfg), 0.0f);
                medium.bind(cfg, dc::Store{storage.data(), storage.size()});
            }
        };

        float rms(const std::vector<float>& v, std::size_t from, std::size_t to)
        {
            double acc = 0.0;
            for (std::size_t i = from; i < to; ++i) acc += double(v[i]) * double(v[i]);
            const auto n = static_cast<double>(to - from);
            return static_cast<float>(std::sqrt(acc / std::max(1.0, n)));
        }

        // Write `n` engine samples of a sine into a fresh circular medium at
        // `writeRate`, then read it back at the same rate from the start; return
        // the rms of the interior of the readback (edges excluded — the kernel
        // has a 16-tap span, and the medium starts silent).
        float writeThenReadRms(double rate, double freqCyclesPerEngineSample, int n)
        {
            Reel reel{ dc::Topology::Circular, 4096 };
            dc::WriteHead w;
            w.setRate(rate);
            w.setPosition(rate >= 0.0 ? 64.0 : 3000.0);
            const double start = w.position();

            for (int i = 0; i < n; ++i)
            {
                const auto s = static_cast<float>(
                    std::sin(2.0 * kPi * freqCyclesPerEngineSample * i));
                w.writeFrame(reel.medium, 0, &s, 1);
                w.step(reel.medium);
            }

            dc::ReadHead r;
            r.setRate(rate);
            r.setPosition(start);
            std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                r.readFrame(reel.medium, 0, &out[static_cast<std::size_t>(i)], 1);
                r.step(reel.medium);
            }
            // Trim a generous kernel span from each end.
            return rms(out, 64, static_cast<std::size_t>(n) - 64);
        }
    }

    void runHeadsTests()
    {
        // ── Amplitude invariance across the signed rate axis ─────────────────
        // A 0.05 cycles/sample sine (well inside every bucket's passband) must
        // come back at its own rms (0.7071) whether the tape ran at half speed,
        // unity, double speed, or backwards.
        {
            for (const double rate : { 0.5, 1.0, 2.0, -1.0 })
            {
                const float got = writeThenReadRms(rate, 0.05, 1500);
                CHECK(std::abs(got - 0.70710678f) < 0.02f,
                      "write-then-read at the same rate is amplitude-invariant");
            }
        }

        // ── A stalled head writes nothing ────────────────────────────────────
        // Rate 0 means the medium is not moving under the head. Depositing there
        // would integrate the input into a single sample without bound; the
        // |rate| gain makes it deposit exactly zero.
        {
            Reel reel{ dc::Topology::Circular, 256 };
            dc::WriteHead w;
            w.setRate(0.0);
            w.setPosition(100.0);
            for (int i = 0; i < 1000; ++i)
            {
                constexpr float one = 1.0f;
                w.writeFrame(reel.medium, 0, &one, 1);
                w.step(reel.medium);
            }
            float peak = 0.0f;
            for (int i = 0; i < 256; ++i) peak = std::max(peak, std::abs(reel.medium.read(0, 0, i)));
            CHECK(peak < 1.0e-6f, "a stalled write head deposits nothing");
            CHECK(feq(static_cast<float>(w.position()), 100.0f),
                  "a stalled head does not move");
        }

        // ── Through zero: a scrub reversing under the head fades, never spikes ─
        {
            Reel reel{ dc::Topology::Circular, 512 };
            dc::WriteHead w;
            w.setPosition(256.0);
            for (int i = 0; i < 400; ++i)
            {
                // Sweep the rate from +1 through 0 to -1 while writing a constant.
                w.setRate(1.0 - 2.0 * (double(i) / 399.0));
                constexpr float one = 1.0f;
                w.writeFrame(reel.medium, 0, &one, 1);
                w.step(reel.medium);
            }
            float peak = 0.0f;
            for (int i = 0; i < 512; ++i) peak = std::max(peak, std::abs(reel.medium.read(0, 0, i)));
            // With the |rate| law the deposit thins out as the head slows, so the
            // turnaround piles up a bounded amount. Without it, the near-stalled
            // samples would each land whole on one spot.
            CHECK(peak < 4.0f, "a scrub through zero writes a bounded amount");
        }

        // ── Reading above unity band-limits ──────────────────────────────────
        // A 0.4 cycles/sample tone (0.8 x Nyquist) read at rate 2 would fold to
        // 0.2 under a naive interpolator. The rate-aware kernel removes it before
        // it can, so what comes back is near-silence, not a phantom tone.
        {
            Reel reel{ dc::Topology::Circular, 4096 };
            reel.medium.ensureCommitted(0, 4096);
            for (int i = 0; i < 4096; ++i)
                reel.medium.write(0, 0, i,
                                  static_cast<float>(std::sin(2.0 * kPi * 0.4 * i)));

            dc::ReadHead r;
            r.setRate(2.0);
            r.setPosition(64.0);
            std::vector<float> out(1024, 0.0f);
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                r.readFrame(reel.medium, 0, &out[i], 1);
                r.step(reel.medium);
            }
            CHECK(rms(out, 64, 960) < 0.15f,
                  "reading a near-Nyquist tone at 2x band-limits it away, not down");
        }

        // ── Medium rate is a medium property, not a mode ─────────────────────
        // Record the same engine-domain sine onto a 1x and a 2x medium (same head,
        // just a different rate), read each back at its own rate, and the engine
        // sees the same signal. That is the whole of the oversampling story.
        {
            const float oneX = writeThenReadRms(1.0, 0.05, 1500);
            const float twoX = writeThenReadRms(2.0, 0.05, 1500);
            CHECK(std::abs(oneX - twoX) < 0.02f,
                  "a 2x oversampled medium returns what a 1x medium returns");
        }

        // ── Slaved heads: a tap trails its leader ────────────────────────────
        {
            Reel reel{ dc::Topology::Circular, 1024 };
            dc::WriteHead w;
            dc::ReadHead tap;
            w.setPosition(500.0);
            tap.follow(&w, 200.0);
            CHECK(tap.slaved(), "a followed head is slaved");
            CHECK(feq(static_cast<float>(tap.effectivePosition()), 300.0f),
                  "a tap sits `offset` behind its leader");

            w.setRate(2.0);
            w.step(reel.medium);
            CHECK(feq(static_cast<float>(tap.effectivePosition()), 302.0f),
                  "the tap moves with the leader");
            CHECK(feq(static_cast<float>(tap.effectiveRate()), 2.0f),
                  "the tap reads at the leader's rate — varispeed stretches the echo");

            tap.step(reel.medium);
            CHECK(feq(static_cast<float>(tap.effectivePosition()), 302.0f),
                  "stepping a slaved head does nothing; its position is derived");

            tap.follow(nullptr, 0.0);
            CHECK(! tap.slaved() && feq(static_cast<float>(tap.position()), 302.0f),
                  "unslaving leaves the head where it was");
        }

        // ── The acceptance test: a tape delay, from the library alone ────────
        // One write head, one read tap trailing it by D, and the feedback path in
        // the caller — which is what a Space Echo is. Nothing in deck_core knows
        // the word "delay"; this is a configuration of the heads.
        {
            constexpr int kD = 1000;        // tap distance, medium samples
            constexpr float kFb = 0.5f;     // regeneration
            constexpr int kN = 3600;

            const auto runDelay = [](double rate, int n, int tapDist, float fb) {
                Reel reel{ dc::Topology::Circular, 16384 };
                dc::WriteHead w;
                dc::ReadHead tap;
                w.setRate(rate);
                w.setPosition(0.0);
                tap.follow(&w, static_cast<double>(tapDist));

                std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float dry = (i == 0) ? 1.0f : 0.0f;
                    float wet = 0.0f;
                    tap.readFrame(reel.medium, 0, &wet, 1);
                    const float in = dry + fb * wet;   // regeneration: caller-side
                    w.writeFrame(reel.medium, 0, &in, 1);
                    w.step(reel.medium);
                    out[static_cast<std::size_t>(i)] = wet;
                }
                return out;
            };

            // Peak of the window around an expected echo, and the position of it.
            const auto peakNear = [](const std::vector<float>& v, int centre, int win) {
                float best = 0.0f;
                int at = centre;
                for (int i = std::max(0, centre - win);
                     i < std::min(static_cast<int>(v.size()), centre + win); ++i)
                {
                    if (std::abs(v[static_cast<std::size_t>(i)]) > best)
                    {
                        best = std::abs(v[static_cast<std::size_t>(i)]);
                        at = i;
                    }
                }
                return std::pair<float, int>{ best, at };
            };

            {
                const auto out = runDelay(1.0, kN, kD, kFb);
                const auto [p1, at1] = peakNear(out, kD, 24);
                const auto [p2, at2] = peakNear(out, 2 * kD, 24);
                const auto [p3, at3] = peakNear(out, 3 * kD, 24);

                CHECK(std::abs(at1 - kD) <= 2 && std::abs(at2 - 2 * kD) <= 2
                          && std::abs(at3 - 3 * kD) <= 2,
                      "echoes land at D, 2D, 3D");
                CHECK(p1 > 0.9f, "the first echo returns the input");
                CHECK(std::abs(p2 - kFb * p1) < 0.05f, "the second echo is fb x the first");
                CHECK(std::abs(p3 - kFb * p2) < 0.05f, "regeneration is geometric");

                // Between the echoes the line is quiet: the tap is not smearing.
                CHECK(rms(out, 1200, 1800) < 0.02f, "the delay line is quiet between taps");
            }

            // Varispeed: the medium runs at 2x under the same tap distance, so the
            // echo spacing halves in engine samples. Level does not change, because
            // the write gain and the read kernel are the same law read twice.
            {
                const auto out = runDelay(2.0, kN, kD, kFb);
                const auto [p1, at1] = peakNear(out, kD / 2, 24);
                const auto [p2, at2] = peakNear(out, kD, 24);
                CHECK(std::abs(at1 - kD / 2) <= 3 && std::abs(at2 - kD) <= 3,
                      "at 2x the echoes land at D/2, D — varispeed sweeps the head spacing");
                CHECK(p1 > 0.7f, "the echo survives the faster tape");
                CHECK(std::abs(p2 - kFb * p1) < 0.08f, "regeneration still geometric at 2x");
            }

            // Half speed: the spacing doubles.
            {
                const auto out = runDelay(0.5, kN, kD, kFb);
                const auto [p1, at1] = peakNear(out, 2 * kD, 32);
                CHECK(std::abs(at1 - 2 * kD) <= 4, "at half speed the echo lands at 2D");
                CHECK(p1 > 0.35f, "and it is audible");
            }
        }

        runEraseHeadTests();
    }

    // ── The erase head (DESIGN §40.10) ───────────────────────────────────────
    // Replace = erase + write. The erase law is per MEDIUM sample, not per engine
    // sample, which is the whole difficulty: a head at half speed lingers over
    // each medium sample twice, and one at double speed skips every other. Erase
    // once per engine sample and you wipe twice as hard at half speed and leave
    // stripes of the old take at double.
    void runEraseHeadTests()
    {
        // Prime a medium with DC 1.0 — the "old take" every replace must remove.
        const auto primed = [](Reel& reel, int cap) {
            reel.medium.ensureCommitted(0, cap);
            for (int i = 0; i < cap; ++i) reel.medium.write(0, 0, i, 1.0f);
        };

        // Run a replace pass: erase head leading the write head, both at `rate`,
        // writing `input` per sample. Returns the medium.
        const auto replacePass =
            [](Reel& reel, double rate, double startPos, int n, float erasure, float input) {
                dc::WriteHead w;
                w.setRate(rate);
                w.setPosition(startPos);

                dc::EraseHead e;
                e.setErasure(erasure);
                e.setRate(rate);
                // `minGapFor(rate)`, not `kMinGap`. The gap that matters is the
                // half-width of the kernel THIS pass writes with; `kMinGap` is
                // the whole bank's worst case, which chalkwalk-tape's rate-sized
                // bank took from 8 to 128. Asserting against the worst case here
                // would place the erase head 120 samples further ahead than the
                // write it is clearing for, and the margins below would be
                // measuring the gap rather than the erasure.
                e.setPosition(dc::EraseHead::leadFor(w, dc::EraseHead::minGapFor(rate)));

                for (int i = 0; i < n; ++i)
                {
                    e.sweep(reel.medium, 0);          // clear the tape ahead...
                    w.writeFrame(reel.medium, 0, &input, 1);  // ...then lay the new take
                    w.step(reel.medium);
                }
            };

        // Peak magnitude over an inclusive index range of the medium.
        const auto peakOver = [](const dc::Medium& m, int lo, int hi) {
            float peak = 0.0f;
            for (int i = lo; i <= hi; ++i) peak = std::max(peak, std::abs(m.read(0, 0, i)));
            return peak;
        };

        // ── Full replace erases the old take at every rate ───────────────────
        // Forward, half speed, double speed, reverse. The swept span comes back
        // silent; nothing outside it is touched.
        {
            for (const double rate : { 0.5, 1.0, 2.0, -1.0 })
            {
                Reel reel{ dc::Topology::Circular, 4096 };
                primed(reel, 4096);

                const double start = (rate < 0.0) ? 3000.0 : 1000.0;
                constexpr int n = 800;
                replacePass(reel, rate, start, n, 1.0f, 0.0f);  // replace with silence

                const auto end = static_cast<int>(start + rate * n);
                const int lo = std::min(static_cast<int>(start), end) + 32;
                const int hi = std::max(static_cast<int>(start), end) - 32;
                CHECK(peakOver(reel.medium, lo, hi) < 1.0e-3f,
                      "a full-erasure pass wipes the old take at any rate");
                CHECK(peakOver(reel.medium, 3500, 3900) > 0.99f,
                      "and leaves the tape it never passed over alone");
            }
        }

        // ── A stalled head erases nothing ────────────────────────────────────
        // The tape is not moving under the head. Wiping here would mean a paused
        // replace slowly bores a hole in the take.
        {
            Reel reel{ dc::Topology::Circular, 512 };
            primed(reel, 512);
            replacePass(reel, 0.0, 100.0, 1000, 1.0f, 0.0f);
            CHECK(peakOver(reel.medium, 90, 110) > 0.99f, "a stalled erase head wipes nothing");
        }

        // ── Erasure is per medium sample, not per engine sample ──────────────
        // The rate-invariance that the sweep exists to provide: a half-erasure
        // pass leaves half, whether the tape crawled or flew.
        {
            for (const double rate : { 0.5, 1.0, 2.0 })
            {
                Reel reel{ dc::Topology::Circular, 4096 };
                primed(reel, 4096);
                replacePass(reel, rate, 1000.0, 600, 0.5f, 0.0f);

                const auto end = static_cast<int>(1000.0 + rate * 600);
                const float p = peakOver(reel.medium, 1100, end - 64);
                CHECK(std::abs(p - 0.5f) < 0.02f,
                      "half erasure leaves half, at every rate — one pass, one attenuation");
            }
        }

        // ── Replace preserves the NEW take's level ───────────────────────────
        // Erase to nothing, write DC 1.0 at double speed: the |rate| write gain
        // and the once-per-sample erase compose to unity. A replace that came
        // back at 2.0 or 0.5 would mean the two heads disagree about what a
        // medium sample is.
        {
            Reel reel{ dc::Topology::Circular, 4096 };
            primed(reel, 4096);
            replacePass(reel, 2.0, 1000.0, 600, 1.0f, 1.0f);
            const float p = peakOver(reel.medium, 1100, 2100);
            CHECK(std::abs(p - 1.0f) < 0.02f, "the replaced span holds the new take at unity");
        }

        // ── The gap protects the fresh deposit ───────────────────────────────
        // The erase head leads the write head by a kernel half-width. Put it
        // BEHIND instead and it eats what was just written — which is the bug the
        // gap exists to prevent, and worth stating as a test so nobody "tidies"
        // leadFor() into a no-op.
        {
            Reel reel{ dc::Topology::Circular, 4096 };
            primed(reel, 4096);

            dc::WriteHead w;
            w.setPosition(1000.0);
            dc::EraseHead e;
            e.setErasure(1.0f);
            e.setPosition(w.position() - dc::EraseHead::minGapFor(1.0));  // WRONG side

            for (int i = 0; i < 600; ++i)
            {
                e.sweep(reel.medium, 0);
                constexpr float one = 1.0f;
                w.writeFrame(reel.medium, 0, &one, 1);
                w.step(reel.medium);
            }
            CHECK(peakOver(reel.medium, 1100, 1500) < 0.5f,
                  "an erase head trailing the write head eats the take (hence leadFor)");
        }
    }
}
