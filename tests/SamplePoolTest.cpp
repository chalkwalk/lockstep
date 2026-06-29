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

        // mutableVolatilePcm refuses non-volatile / out of range -------------
        {
            SamplePool pool;
            const int v = pool.addVolatile();
            CHECK(pool.mutableVolatilePcm(v) != nullptr, "volatile handle ok");
            CHECK(pool.mutableVolatilePcm(-1) == nullptr, "no handle out of range");
            CHECK(pool.mutableVolatilePcm(99) == nullptr, "no handle out of range hi");
        }
    }
}
