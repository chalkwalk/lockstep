#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dc
{
    // Markers are dumb (DESIGN §40.4).
    //
    // A marker is a NAVIGATION point on the timeline: a position, a stable ordinal,
    // and an optional label token the host maps to text. Nothing more. Markers are
    // dropped manually (a console cell) or automatically at a Scene/Song switch
    // while a deck records, they survive every audio operation, and they are
    // removed only by an explicit delete.
    //
    // What a marker is NOT: a trigger. A marker never recalls a scene and a deck
    // never emits a launch during playback — that is precisely where fence #1
    // sits (replaying position-keyed launches would be the stored arrangement the
    // instrument has twice refused). A marker tells you where the chorus was; you
    // still launch the chorus. So this type has `nearest` / `next` / `prev` for
    // cueing and NOTHING that fires anything.
    //
    // Pure and JUCE-free: the lane is kept sorted by position, ordinals are a
    // monotonic drop counter (stable across sorts and deletes), and every query is
    // a const scan. The host owns cue timing (a quantized locate) and label text.
    struct Marker
    {
        double positionSamples = 0.0;
        int ordinal = 0;   // stable id = drop order; never reused
        int labelId = 0;   // 0 = no label; host maps the rest to text
    };

    class MarkerLane
    {
    public:
        // Drop a marker at `pos`. Returns its ordinal. A drop within `mergeEps`
        // samples of an existing marker replaces that marker's label rather than
        // stacking a second one on the same spot (two markers a sample apart are a
        // navigation annoyance, not two places).
        int drop(double pos, int labelId = 0, double mergeEps = 1.0) noexcept
        {
            for (auto& m : markers_)
            {
                if (std::abs(m.positionSamples - pos) <= mergeEps)
                {
                    m.labelId = labelId;
                    return m.ordinal;
                }
            }
            const int ord = nextOrdinal_++;
            markers_.push_back({ pos, ord, labelId });
            sort();
            return ord;
        }

        // Remove the marker with this ordinal. Returns true if one was removed.
        bool remove(int ordinal) noexcept
        {
            const auto before = markers_.size();
            markers_.erase(std::remove_if(markers_.begin(), markers_.end(),
                                          [ordinal](const Marker& m) { return m.ordinal == ordinal; }),
                           markers_.end());
            return markers_.size() != before;
        }

        void clear() noexcept { markers_.clear(); }

        [[nodiscard]] int count() const noexcept { return static_cast<int>(markers_.size()); }
        [[nodiscard]] const Marker& at(int i) const noexcept
        {
            return markers_[static_cast<std::size_t>(i)];
        }
        [[nodiscard]] const std::vector<Marker>& all() const noexcept { return markers_; }

        // The marker nearest `pos` (either side), or -1 if the lane is empty. The
        // index into the sorted lane, for a "cue to the closest mark" gesture.
        [[nodiscard]] int nearest(double pos) const noexcept
        {
            int best = -1;
            double bestD = 0.0;
            for (int i = 0; i < count(); ++i)
            {
                const double d = std::abs(markers_[static_cast<std::size_t>(i)].positionSamples - pos);
                if (best < 0 || d < bestD) { best = i; bestD = d; }
            }
            return best;
        }

        // The first marker strictly after / before `pos` (fast-forward / rewind to
        // the next mark), or -1 if there is none in that direction.
        [[nodiscard]] int next(double pos) const noexcept
        {
            for (int i = 0; i < count(); ++i)
                if (markers_[static_cast<std::size_t>(i)].positionSamples > pos) return i;
            return -1;
        }
        [[nodiscard]] int prev(double pos) const noexcept
        {
            for (int i = count() - 1; i >= 0; --i)
                if (markers_[static_cast<std::size_t>(i)].positionSamples < pos) return i;
            return -1;
        }

        // Restore a serialized lane (host-side, §40.8). Keeps ordinals so a saved
        // reference to a marker survives a reload; the next drop continues past the
        // highest.
        void load(std::vector<Marker> markers) noexcept
        {
            markers_ = std::move(markers);
            sort();
            nextOrdinal_ = 0;
            for (const auto& m : markers_) nextOrdinal_ = std::max(nextOrdinal_, m.ordinal + 1);
        }

    private:
        void sort() noexcept
        {
            std::sort(markers_.begin(), markers_.end(),
                      [](const Marker& a, const Marker& b)
                      { return a.positionSamples < b.positionSamples; });
        }

        std::vector<Marker> markers_;
        int nextOrdinal_ = 0;
    };
}
