// SeamTest — dc::spliceLoopEnd (DESIGN §40.3, deckcore/Seam.h).
//
// The one thing a loop splice must guarantee: whatever preceded the take's first
// sample in the recording also precedes it in the loop. Everything else — which
// curve, how long — is taste. So the tests state the guarantee (the wrap is
// continuous) and the price (the loop's END is altered, its HEAD never).

#include "TestHarness.h"
#include "../src/deckcore/Seam.h"

#include <array>
#include <cmath>
#include <vector>

namespace lockstep
{
    namespace
    {
        struct Buf
        {
            std::vector<float> data;
            dc::Medium medium;

            Buf(int len, dc::Topology topo)
            {
                dc::Medium::Config cfg;
                cfg.topology = topo;
                cfg.numSubTracks = 1;
                cfg.channelsPerSubTrack = 1;
                cfg.capacitySamples = len;
                data.assign(dc::Medium::storageSamples(cfg), 0.0f);
                medium.bind(cfg, dc::Store{ data.data(), data.size() });
                medium.adoptUsed(0, len);
            }
        };
    }

    void runSeamTests()
    {
        constexpr int kL = 256;   // loop length
        // The residual at the wrap is the fade's last step, cos((1 - 0.5/kX)*pi/2)
        // times the tail — smaller the longer the splice. At the looper's 5 ms
        // (220 samples @44.1k) it is ~0.004; 64 keeps this test honest and fast.
        constexpr int kX = 64;    // splice length

        // ── The wrap becomes continuous ──────────────────────────────────────
        // A loop of a rising ramp preceded by a falling one: the recording is
        // continuous across the take's start, but the loop's raw seam jumps by ~1.
        {
            Buf loop{ kL, dc::Topology::Circular };
            Buf lead{ kX, dc::Topology::Linear };

            // lead[i] approaches 0 from below; loop[i] rises 0 → 1.
            for (int i = 0; i < kX; ++i)
                lead.medium.write(0, 0, i, -0.05f * static_cast<float>(kX - i) / kX);
            for (int i = 0; i < kL; ++i)
                loop.medium.write(0, 0, i, static_cast<float>(i) / kL);

            const float rawJump =
                std::abs(loop.medium.read(0, 0, 0) - loop.medium.read(0, 0, kL - 1));
            CHECK(rawJump > 0.9f, "the raw loop seam jumps by nearly a full scale");

            dc::spliceLoopEnd(loop.medium, 0, kL, lead.medium, 0, 0, kX);

            const float spliced =
                std::abs(loop.medium.read(0, 0, 0) - loop.medium.read(0, 0, kL - 1));
            CHECK(spliced < 0.02f, "after the splice the wrap is continuous");

            // The loop's last sample is (almost) the lead's last — which is what
            // preceded loop[0] in the recording. That is the whole guarantee.
            CHECK(std::abs(loop.medium.read(0, 0, kL - 1) - lead.medium.read(0, 0, kX - 1))
                      < 0.02f,
                  "the loop ends on what preceded its head");
        }

        // ── The head is never touched ────────────────────────────────────────
        // A loop's first sample is its downbeat. A post-roll splice would blend it
        // away; this one alters only the ring-out.
        {
            Buf loop{ kL, dc::Topology::Circular };
            Buf lead{ kX, dc::Topology::Linear };
            for (int i = 0; i < kL; ++i) loop.medium.write(0, 0, i, 1.0f);
            for (int i = 0; i < kX; ++i) lead.medium.write(0, 0, i, 0.0f);

            dc::spliceLoopEnd(loop.medium, 0, kL, lead.medium, 0, 0, kX);

            CHECK(feq(loop.medium.read(0, 0, 0), 1.0f), "the downbeat survives untouched");
            CHECK(feq(loop.medium.read(0, 0, kL - kX - 1), 1.0f),
                  "and everything before the splice region");
            CHECK(loop.medium.read(0, 0, kL - 1) < 0.1f, "the loop's end fades to the pre-roll");
        }

        // ── A held drone through its own pre-roll ────────────────────────────
        // The known cost of equal-power curves: sin²+cos²=1 preserves POWER, not
        // amplitude, so a signal spliced against an identical one (a drone that was
        // already sounding when the take began) swells by up to +3 dB across the
        // splice. Equal power is right when the two moments are uncorrelated, which
        // is the usual case; linear curves would be right for the drone and would
        // dip 3 dB on everything else. Five milliseconds of either is a fair trade,
        // and this test pins which one we took.
        {
            Buf loop{ kL, dc::Topology::Circular };
            Buf lead{ kX, dc::Topology::Linear };
            for (int i = 0; i < kL; ++i) loop.medium.write(0, 0, i, 0.5f);
            for (int i = 0; i < kX; ++i) lead.medium.write(0, 0, i, 0.5f);

            dc::spliceLoopEnd(loop.medium, 0, kL, lead.medium, 0, 0, kX);

            float worst = 0.0f;
            for (int i = 0; i < kL; ++i)
                worst = std::max(worst, std::abs(loop.medium.read(0, 0, i) - 0.5f));
            CHECK(worst < 0.22f, "the swell is bounded by +3 dB (0.5 -> 0.707)");
            CHECK(feq(loop.medium.read(0, 0, kL - 1), 0.5f, 0.02f),
                  "and lands exactly on it at the wrap");
        }

        // ── Refusals ─────────────────────────────────────────────────────────
        {
            Buf loop{ kL, dc::Topology::Circular };
            Buf lead{ kX, dc::Topology::Linear };
            for (int i = 0; i < kL; ++i) loop.medium.write(0, 0, i, 1.0f);

            dc::spliceLoopEnd(loop.medium, 0, kL, lead.medium, 0, 0, 0);
            CHECK(feq(loop.medium.read(0, 0, kL - 1), 1.0f), "a zero-length splice does nothing");

            dc::spliceLoopEnd(loop.medium, 0, kL, lead.medium, 0, kX - 4, 16);
            CHECK(feq(loop.medium.read(0, 0, kL - 1), 1.0f),
                  "a splice reading past the pre-roll's end does nothing");

            dc::spliceLoopEnd(loop.medium, 0, 8, lead.medium, 0, 0, 16);
            CHECK(feq(loop.medium.read(0, 0, kL - 1), 1.0f),
                  "a splice longer than the loop does nothing");
        }
    }
}
