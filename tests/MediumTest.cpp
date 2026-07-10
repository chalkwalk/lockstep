// MediumTest — dc::Medium, the tape itself (DESIGN §40.3, §40.11).
//
// The medium is geometry plus a non-owning view of caller storage, and it owns
// exactly three rules that everything above it depends on:
//
//   * **Topology** — circular indices wrap, linear indices out of range do not
//     exist. The heads do index arithmetic; the medium decides what an index
//     means.
//   * **Depth** — f32 or i16 behind one accessor, so nothing above is templated
//     on storage. An i16 medium quantises; it must not shift, clip early, or
//     lose ±1.0.
//   * **High-water (lazy commit)** — storage is never zero-filled, so a read
//     past a sub-track's mark is silence rather than whatever the allocator
//     left, and committing a span turns garbage into silence *before* a write
//     head deposits into it.

#include "TestHarness.h"
#include "../src/deckcore/Medium.h"

#include <cstdint>
#include <vector>

namespace lockstep
{
    namespace
    {
        // Storage deliberately filled with garbage: nothing may read it before a
        // commit, and every zero the tests see must come from ensureCommitted.
        template <typename T>
        std::vector<T> dirtyStorage(std::size_t n, T poison)
        {
            return std::vector<T>(n, poison);
        }
    }

    void runMediumTests()
    {
        // ── Geometry + binding ───────────────────────────────────────────────
        {
            dc::Medium::Config cfg;
            cfg.topology = dc::Topology::Circular;
            cfg.numSubTracks = 4;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 100;

            const std::size_t need = dc::Medium::storageSamples(cfg);
            CHECK(need == 800, "storageSamples = subTracks * channels * capacity");

            dc::Medium m;
            CHECK(! m.bound(), "a default medium is unbound");

            // A store too small to hold the geometry must be refused outright
            // rather than half-bound: a medium that reads past its storage is a
            // worse failure than a silent one.
            auto small = dirtyStorage<float>(need - 1, 9.0f);
            m.bind(cfg, dc::Store{small.data(), small.size()});
            CHECK(! m.bound(), "a short store leaves the medium unbound");
            CHECK(feq(m.read(0, 0, 0), 0.0f), "an unbound medium reads silence");

            auto store = dirtyStorage<float>(need, 9.0f);
            m.bind(cfg, dc::Store{store.data(), store.size()});
            CHECK(m.bound(), "a sufficient store binds");
            CHECK(m.capacity() == 100 && m.numSubTracks() == 4 && m.channels() == 2,
                  "geometry survives binding");
            CHECK(m.depth() == dc::Depth::F32, "a float store is an f32 medium");
        }

        // ── High-water: uncommitted storage is never read ────────────────────
        {
            dc::Medium::Config cfg;
            cfg.numSubTracks = 2;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 64;

            auto store = dirtyStorage<float>(dc::Medium::storageSamples(cfg), 9.0f);
            dc::Medium m;
            m.bind(cfg, dc::Store{store.data(), store.size()});

            CHECK(m.used(0) == 0 && m.used(1) == 0, "a fresh medium has recorded nothing");
            CHECK(feq(m.read(0, 0, 0), 0.0f), "an uncommitted sample reads silence, not garbage");
            CHECK(feq(m.read(0, 0, 63), 0.0f), "...at every uncommitted index");

            m.ensureCommitted(0, 32);
            CHECK(m.used(0) == 32, "committing raises the high-water mark");
            CHECK(m.used(1) == 0, "...for that sub-track only");
            CHECK(feq(m.read(0, 0, 31), 0.0f), "committed storage is zeroed");
            CHECK(feq(m.read(0, 1, 31), 0.0f), "...on every channel of the sub-track");
            CHECK(feq(m.read(0, 0, 32), 0.0f), "past the mark still reads silence");

            // The garbage beyond the mark is still there — the medium hides it,
            // and hiding it is the whole point of the mark.
            m.add(0, 0, 40, 0.0f);  // resolves, but never commits
            CHECK(feq(m.read(0, 0, 40), 0.0f), "an add past the mark does not expose garbage");

            // A commit that skips over content zeroes only what it skipped.
            m.write(0, 0, 10, 0.5f);
            m.ensureCommitted(0, 64);
            CHECK(m.used(0) == 64, "the mark extends");
            CHECK(feq(m.read(0, 0, 10), 0.5f), "content below the old mark survives a commit");
            CHECK(feq(m.read(0, 0, 50), 0.0f), "the newly committed span is silent");

            m.ensureCommitted(0, 999);
            CHECK(m.used(0) == 64, "the mark never exceeds capacity");

            // Silence is recorded content; absence is not.
            m.write(0, 0, 10, 0.5f);
            m.clearSubTrack(0);
            CHECK(feq(m.read(0, 0, 10), 0.0f) && m.used(0) == 64,
                  "clearSubTrack erases audio but keeps the mark");
            m.resetUsed(0);
            CHECK(m.used(0) == 0, "resetUsed drops the mark");
        }

        // ── Circular topology: indices wrap, in both directions ──────────────
        {
            dc::Medium::Config cfg;
            cfg.topology = dc::Topology::Circular;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;

            auto store = dirtyStorage<float>(dc::Medium::storageSamples(cfg), 9.0f);
            dc::Medium m;
            m.bind(cfg, dc::Store{store.data(), store.size()});
            m.ensureCommitted(0, 16);

            m.write(0, 0, 3, 1.0f);
            CHECK(feq(m.read(0, 0, 19), 1.0f), "index + capacity is the same sample");
            CHECK(feq(m.read(0, 0, -13), 1.0f), "a negative index wraps forward (reverse reads)");

            m.add(0, 0, -1, 0.25f);  // writes at 15
            CHECK(feq(m.read(0, 0, 15), 0.25f), "a write before zero lands at the seam");

            int k = 0;
            CHECK(m.resolve(-1, k) && k == 15, "resolve wraps a negative index");
            CHECK(m.resolve(32, k) && k == 0, "resolve wraps a multiple of capacity");
        }

        // ── Linear topology: off the reel is nowhere ─────────────────────────
        {
            dc::Medium::Config cfg;
            cfg.topology = dc::Topology::Linear;
            cfg.numSubTracks = 1;
            cfg.channelsPerSubTrack = 1;
            cfg.capacitySamples = 16;

            auto store = dirtyStorage<float>(dc::Medium::storageSamples(cfg), 9.0f);
            dc::Medium m;
            m.bind(cfg, dc::Store{store.data(), store.size()});
            m.ensureCommitted(0, 16);

            int k = 0;
            CHECK(! m.resolve(-1, k), "a linear index before the reel does not exist");
            CHECK(! m.resolve(16, k), "a linear index past the reel does not exist");
            CHECK(m.resolve(0, k) && k == 0, "the first sample exists");
            CHECK(m.resolve(15, k) && k == 15, "the last sample exists");

            // Writes off the end are dropped, not wrapped onto the start — a
            // reel that wraps is a loop, and confusing the two is the bug this
            // topology exists to prevent.
            m.write(0, 0, 0, 1.0f);
            m.write(0, 0, 16, 0.5f);
            CHECK(feq(m.read(0, 0, 0), 1.0f), "a write past the end does not wrap onto the start");
            CHECK(feq(m.read(0, 0, -1), 0.0f), "a read before the start is silence");
            CHECK(feq(m.read(0, 0, 16), 0.0f), "a read past the end is silence");
        }

        // ── i16 depth: same medium, half the RAM ─────────────────────────────
        {
            dc::Medium::Config cfg;
            cfg.topology = dc::Topology::Circular;
            cfg.numSubTracks = 2;
            cfg.channelsPerSubTrack = 2;
            cfg.capacitySamples = 32;

            auto store = dirtyStorage<std::int16_t>(dc::Medium::storageSamples(cfg),
                                                    std::int16_t{ 999 });
            dc::Medium m;
            m.bind(cfg, dc::Store{store.data(), store.size()});
            CHECK(m.bound() && m.depth() == dc::Depth::I16, "an int16 store is an i16 medium");

            m.ensureCommitted(0, 32);
            CHECK(feq(m.read(0, 0, 5), 0.0f), "committing zeroes an i16 medium too");

            // Round-trip within one quantum. 1/32768 is the step; allow one.
            constexpr float kQ = 1.0f / 32767.0f;
            for (const float v : { -1.0f, -0.5f, -0.001f, 0.0f, 0.001f, 0.25f, 0.5f, 1.0f })
            {
                m.write(0, 0, 7, v);
                CHECK(feq(m.read(0, 0, 7), v, kQ), "i16 round-trips within one quantum");
            }

            // Full scale is exact in both directions, and beyond it clips rather
            // than wrapping — an i16 tape that wrapped would fold a loud take
            // inside out.
            m.write(0, 0, 8, 1.0f);
            CHECK(feq(m.read(0, 0, 8), 1.0f, 1e-6f), "+1.0 is exactly full scale");
            m.write(0, 0, 9, 2.0f);
            CHECK(feq(m.read(0, 0, 9), 1.0f, kQ), "an over-scale write clips positive");
            m.write(0, 0, 10, -2.0f);
            CHECK(m.read(0, 0, 10) < -0.999f && m.read(0, 0, 10) >= -1.001f,
                  "an over-scale write clips negative");

            // add() accumulates in the store, so it must read-modify-write.
            m.write(0, 0, 11, 0.25f);
            m.add(0, 0, 11, 0.25f);
            CHECK(feq(m.read(0, 0, 11), 0.5f, kQ), "i16 add accumulates");

            // Sub-tracks and channels are independent planes.
            m.ensureCommitted(1, 32);
            m.write(1, 1, 7, 0.75f);
            CHECK(feq(m.read(0, 0, 7), 1.0f, kQ), "sub-track 0 is untouched");
            CHECK(feq(m.read(1, 0, 7), 0.0f), "channel 0 of sub-track 1 is untouched");
            CHECK(feq(m.read(1, 1, 7), 0.75f, kQ), "the written plane holds the value");
        }
    }
}
