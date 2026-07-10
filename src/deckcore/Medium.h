#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dc
{
    // The tape (DESIGN §40.3, §40.10/§40.11).
    //
    // A Medium is geometry + a non-owning view of caller memory. It is NOT a
    // buffer class: the host decides where the storage lives (a volatile pool
    // slot, a lazily committed reel), and deck_core never allocates it. That is
    // what lets Lockstep's pool slots and the partner app's reels be the same
    // medium without either product's allocation policy leaking into the core.
    //
    // Two axes make it a medium rather than an array:
    //
    //  * **Topology** — Circular (a loop: indices wrap) or Linear (a reel:
    //    indices past the ends do not exist). The heads read the topology; they
    //    do not know which face of the deck they are serving.
    //  * **Its own sample rate.** `mediumRate` is decoupled from the engine rate
    //    (§40.10). The heads already read and write at arbitrary ratio, so a 2×
    //    oversampled tape is configuration, not a mode. Lockstep instantiates 1×.
    //
    // Storage is f32 or i16 (§40.3 — a 16-bit tape halves the RAM). This is the
    // ONE place the depth is branched on: `Store` below. Nothing above it is
    // templated on depth, because an engine templated on its storage would put
    // the choice in every type in the library.
    //
    // **High-water (lazy commit).** Storage is never zero-filled: each sub-track
    // records how far it has been written, and reads past that return silence.
    // An unrecorded tail therefore costs address space, not resident pages —
    // the same discipline the volatile capture slots keep.

    enum class Topology
    {
        Circular,  // a loop: index arithmetic wraps at capacity
        Linear     // a reel: index arithmetic clamps, and out-of-range is silence
    };

    enum class Depth
    {
        F32,
        I16
    };

    // A non-owning, depth-erased sample store. Scale is 32767 both ways, so
    // ±1.0 maps to full scale exactly and an i16 round-trip of an i16-representable
    // value is lossless.
    class Store
    {
    public:
        Store() = default;

        Store(float* p, std::size_t n) noexcept : f32_(p), size_(n) {}
        Store(std::int16_t* p, std::size_t n) noexcept : i16_(p), size_(n) {}

        [[nodiscard]] bool valid() const noexcept { return f32_ != nullptr || i16_ != nullptr; }
        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] Depth depth() const noexcept { return f32_ ? Depth::F32 : Depth::I16; }

        [[nodiscard]] float get(std::size_t i) const noexcept
        {
            if (f32_ != nullptr) return f32_[i];
            return static_cast<float>(i16_[i]) * (1.0f / 32767.0f);
        }

        void set(std::size_t i, float v) noexcept
        {
            if (f32_ != nullptr) { f32_[i] = v; return; }
            i16_[i] = quantise(v);
        }

        void add(std::size_t i, float v) noexcept
        {
            if (f32_ != nullptr) { f32_[i] += v; return; }
            i16_[i] = quantise(get(i) + v);
        }

        void scale(std::size_t i, float g) noexcept
        {
            if (f32_ != nullptr) { f32_[i] *= g; return; }
            i16_[i] = quantise(get(i) * g);
        }

        void fill(std::size_t begin, std::size_t count, float v) noexcept
        {
            if (f32_ != nullptr) { std::fill_n(f32_ + begin, count, v); return; }
            std::fill_n(i16_ + begin, count, quantise(v));
        }

    private:
        static std::int16_t quantise(float v) noexcept
        {
            const float s = std::round(v * 32767.0f);
            if (s >= 32767.0f) return 32767;
            if (s <= -32768.0f) return -32768;
            return static_cast<std::int16_t>(s);
        }

        float* f32_ = nullptr;
        std::int16_t* i16_ = nullptr;
        std::size_t size_ = 0;
    };

    class Medium
    {
    public:
        struct Config
        {
            Topology topology = Topology::Circular;
            double mediumRate = 48000.0;  // medium samples per second
            int numSubTracks = 1;
            int channelsPerSubTrack = 2;  // the stereo engine boundary (§40.7)
            int capacitySamples = 0;      // per channel
        };

        // Samples of storage a Config needs — the host allocates this many
        // floats or int16_ts and hands the pointer to `bind`.
        [[nodiscard]] static std::size_t storageSamples(const Config& c) noexcept;

        Medium() = default;

        // Bind caller-owned storage. `store.size()` must be >= storageSamples(c);
        // a short or absent store leaves the medium unbound (every read silent,
        // every write dropped) rather than reaching past its end.
        void bind(const Config& c, Store store) noexcept;
        void unbind() noexcept;

        [[nodiscard]] bool bound() const noexcept { return store_.valid(); }
        [[nodiscard]] const Config& config() const noexcept { return cfg_; }
        [[nodiscard]] Topology topology() const noexcept { return cfg_.topology; }
        [[nodiscard]] double mediumRate() const noexcept { return cfg_.mediumRate; }
        [[nodiscard]] int capacity() const noexcept { return cfg_.capacitySamples; }
        [[nodiscard]] int numSubTracks() const noexcept { return cfg_.numSubTracks; }
        [[nodiscard]] int channels() const noexcept { return cfg_.channelsPerSubTrack; }
        [[nodiscard]] Depth depth() const noexcept { return store_.depth(); }

        // How far this sub-track has been written (its high-water mark). Reads
        // past it are silence; nothing below it is ever uninitialised.
        [[nodiscard]] int used(int sub) const noexcept
        {
            return (sub >= 0 && sub < static_cast<int>(used_.size())) ? used_[static_cast<std::size_t>(sub)] : 0;
        }

        // Raise the high-water mark to `upTo` samples, zeroing the storage the
        // mark skipped over. Call it BEFORE writing there: uncommitted storage is
        // garbage, and this is what turns garbage into silence. A write head
        // commits the span its kernel is about to touch, which is why a scatter
        // deposit into virgin tape adds to zero rather than to whatever the
        // allocator left. Never lowers the mark — that is `resetUsed`.
        void ensureCommitted(int sub, int upTo) noexcept;
        void resetUsed(int sub) noexcept;
        void resetAllUsed() noexcept;

        // Map a signed medium index onto storage. Circular media wrap; linear
        // media reject out-of-range. Returns false when the index does not exist.
        [[nodiscard]] bool resolve(std::int64_t i, int& out) const noexcept
        {
            const std::int64_t cap = cfg_.capacitySamples;
            if (cap <= 0) return false;
            if (cfg_.topology == Topology::Circular)
            {
                std::int64_t m = i % cap;
                if (m < 0) m += cap;
                out = static_cast<int>(m);
                return true;
            }
            if (i < 0 || i >= cap) return false;
            out = static_cast<int>(i);
            return true;
        }

        // Sample access at an already-resolved index. The heads own the index
        // arithmetic (kernel taps, wrap, direction); the medium owns storage,
        // depth, and the high-water rule.
        [[nodiscard]] float read(int sub, int ch, std::int64_t i) const noexcept
        {
            int k = 0;
            if (! store_.valid() || ! resolve(i, k)) return 0.0f;
            if (k >= used(sub)) return 0.0f;
            return store_.get(offset(sub, ch, k));
        }

        void add(int sub, int ch, std::int64_t i, float v) noexcept
        {
            int k = 0;
            if (! store_.valid() || ! resolve(i, k)) return;
            store_.add(offset(sub, ch, k), v);
        }

        void write(int sub, int ch, std::int64_t i, float v) noexcept
        {
            int k = 0;
            if (! store_.valid() || ! resolve(i, k)) return;
            store_.set(offset(sub, ch, k), v);
        }

        void scale(int sub, int ch, std::int64_t i, float g) noexcept
        {
            int k = 0;
            if (! store_.valid() || ! resolve(i, k)) return;
            store_.scale(offset(sub, ch, k), g);
        }

        // Erase a sub-track's content without dropping its high-water mark
        // (silence is recorded content; absence is not).
        void clearSubTrack(int sub) noexcept;

    private:
        [[nodiscard]] std::size_t offset(int sub, int ch, int i) const noexcept
        {
            const std::size_t plane =
                static_cast<std::size_t>(sub * cfg_.channelsPerSubTrack + ch);
            return plane * static_cast<std::size_t>(cfg_.capacitySamples)
                 + static_cast<std::size_t>(i);
        }

        Config cfg_{};
        Store store_{};
        std::vector<int> used_;  // per sub-track high-water, in samples
    };
}
