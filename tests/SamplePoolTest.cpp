// SamplePoolTest -- volatile (RAM-only) REC buffer support (Part A, DESIGN §28).
//
// Covers the pool-level capability the RecorderMachine (6.2) builds on:
//   - addVolatile() appends an entry flagged isVolatile, no file backing.
//   - prepareVolatile() sizes volatile buffers to a capacity; a writer can then
//     shrink the reported length with avoidReallocating (so a Sampler reading the
//     entry plays exactly the captured region) without reallocating.
//   - mutableVolatilePcm() returns a writable handle for volatile entries only.

#include "TestHarness.h"
#include "../src/machine/SamplePool.h"

namespace lockstep
{
    void runSamplePoolTests()
    {
        // addVolatile + flags ------------------------------------------------
        {
            SamplePool pool;
            const int v0 = pool.addVolatile();
            const int v1 = pool.addVolatile();
            CHECK(v0 == 0 && v1 == 1, "volatile entries append in order");
            CHECK(pool.isVolatileIndex(v0) && pool.isVolatileIndex(v1),
                  "isVolatileIndex true for volatile entries");
            CHECK(!pool.isVolatileIndex(-1) && !pool.isVolatileIndex(99),
                  "isVolatileIndex false out of range");

            const Sample* s = pool.get(v0);
            CHECK(s != nullptr && s->isVolatile, "Sample flagged volatile");
            CHECK(s != nullptr && s->ref.path.empty() && s->ref.hashXX32 == 0,
                  "volatile entry has no file backing");
        }

        // prepareVolatile sizes to capacity; mutable handle works -------------
        {
            SamplePool pool;
            const int v = pool.addVolatile();
            const int cap = 4096;
            pool.prepareVolatile(48000.0, 2, cap);

            const Sample* s = pool.get(v);
            CHECK(s != nullptr && s->pcm.getNumChannels() == 2, "prepared to 2 ch");
            CHECK(s != nullptr && s->pcm.getNumSamples() == cap, "prepared to capacity");
            CHECK(s != nullptr && s->sampleRate == 48000.0, "prepared sample rate set");

            auto* buf = pool.mutableVolatilePcm(v);
            CHECK(buf != nullptr, "mutable handle for volatile entry");

            // Shrink to a captured length without reallocating, then verify the
            // data pointer is unchanged (capacity preserved) and the reported
            // length now matches the capture.
            const float* before = buf->getReadPointer(0);
            buf->setSize(2, 1000, false, false, /*avoidReallocating*/ true);
            CHECK(buf->getNumSamples() == 1000, "shrunk to captured length");
            CHECK(buf->getReadPointer(0) == before, "no reallocation on shrink");
        }

        // nthVolatileIndex addresses REC slots by ordinal, robust to shifts ---
        {
            SamplePool pool;
            // A non-volatile placeholder at index 0, then two REC slots above it.
            const int file0 = pool.addMissing(SampleRef{});
            const int r0 = pool.addVolatile();
            const int r1 = pool.addVolatile();
            CHECK(file0 == 0 && r0 == 1 && r1 == 2, "layout: file then two REC");
            CHECK(pool.nthVolatileIndex(0) == 1 && pool.nthVolatileIndex(1) == 2,
                  "nthVolatileIndex maps ordinals to absolute indices");
            CHECK(pool.nthVolatileIndex(2) == -1, "no third REC slot");

            // Removing the file below shifts the REC entries down; ordinals hold.
            pool.remove(file0);
            CHECK(pool.nthVolatileIndex(0) == 0 && pool.nthVolatileIndex(1) == 1,
                  "ordinals survive a file removal that shifts absolute indices");
        }

        // mutableVolatilePcm refuses non-volatile / out of range -------------
        {
            SamplePool pool;
            const int v = pool.addVolatile();
            CHECK(pool.mutableVolatilePcm(v) != nullptr, "volatile handle ok");
            CHECK(pool.mutableVolatilePcm(-1) == nullptr, "no handle out of range");
            CHECK(pool.mutableVolatilePcm(99) == nullptr, "no handle out of range hi");
        }

        // sourceBars stamp/read on volatile entries --------------------------
        {
            SamplePool pool;
            const int file0 = pool.addMissing(SampleRef{});  // non-volatile
            const int v = pool.addVolatile();
            CHECK(feq(static_cast<float>(pool.sourceBars(v)), 0.0f),
                  "sourceBars defaults to 0 (unknown)");

            pool.setSourceBars(v, 3.5);  // a free-length loop: 3.5 bars
            CHECK(feq(static_cast<float>(pool.sourceBars(v)), 3.5f),
                  "sourceBars round-trips on a volatile entry");

            // Non-volatile and out-of-range writes/reads are no-ops returning 0.
            pool.setSourceBars(file0, 2.0);
            CHECK(feq(static_cast<float>(pool.sourceBars(file0)), 0.0f),
                  "sourceBars is volatile-only (non-volatile stays 0)");
            CHECK(feq(static_cast<float>(pool.sourceBars(99)), 0.0f),
                  "sourceBars out of range returns 0");
        }
    }
}
