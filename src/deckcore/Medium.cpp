#include "Medium.h"

namespace dc
{
    namespace
    {
        [[nodiscard]] bool geometryOk(const Medium::Config& c) noexcept
        {
            return c.numSubTracks > 0 && c.channelsPerSubTrack > 0 && c.capacitySamples > 0;
        }
    }

    std::size_t Medium::storageSamples(const Config& c) noexcept
    {
        if (! geometryOk(c)) return 0;
        return static_cast<std::size_t>(c.numSubTracks)
             * static_cast<std::size_t>(c.channelsPerSubTrack)
             * static_cast<std::size_t>(c.capacitySamples);
    }

    void Medium::bind(const Config& c, Store store) noexcept
    {
        const std::size_t need = storageSamples(c);
        if (need == 0 || ! store.valid() || store.size() < need)
        {
            unbind();
            return;
        }

        const auto cap = static_cast<std::size_t>(c.capacitySamples);
        const int count = c.numSubTracks * c.channelsPerSubTrack;

        cfg_ = c;
        planes_.clear();
        planes_.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            planes_.push_back(store.slice(static_cast<std::size_t>(i) * cap, cap));
        used_.assign(static_cast<std::size_t>(c.numSubTracks), 0);
    }

    void Medium::bindPlanes(const Config& c, const Store* planes, int count) noexcept
    {
        const int need = geometryOk(c) ? c.numSubTracks * c.channelsPerSubTrack : 0;
        if (need == 0 || planes == nullptr || count < need)
        {
            unbind();
            return;
        }

        const auto cap = static_cast<std::size_t>(c.capacitySamples);
        for (int i = 0; i < need; ++i)
        {
            if (! planes[i].valid() || planes[i].size() < cap)
            {
                unbind();
                return;
            }
        }

        cfg_ = c;
        planes_.assign(planes, planes + need);
        used_.assign(static_cast<std::size_t>(c.numSubTracks), 0);
    }

    void Medium::unbind() noexcept
    {
        planes_.clear();
        used_.clear();
    }

    void Medium::ensureCommitted(int sub, int upTo) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;

        const int cap = cfg_.capacitySamples;
        const int target = std::min(upTo, cap);
        int& mark = used_[static_cast<std::size_t>(sub)];
        if (target <= mark) return;

        const auto count = static_cast<std::size_t>(target - mark);
        for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
            plane(sub, ch).fill(static_cast<std::size_t>(mark), count, 0.0f);

        mark = target;
    }

    void Medium::adoptUsed(int sub, int upTo) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;
        const int target = std::min(upTo, cfg_.capacitySamples);
        int& mark = used_[static_cast<std::size_t>(sub)];
        if (target > mark) mark = target;
    }

    void Medium::resetUsed(int sub) noexcept
    {
        if (sub >= 0 && sub < static_cast<int>(used_.size()))
            used_[static_cast<std::size_t>(sub)] = 0;
    }

    void Medium::resetAllUsed() noexcept
    {
        std::fill(used_.begin(), used_.end(), 0);
    }

    void Medium::clearSubTrack(int sub) noexcept
    {
        if (! bound() || sub < 0 || sub >= cfg_.numSubTracks) return;
        const int mark = used(sub);
        if (mark <= 0) return;
        for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
            plane(sub, ch).fill(0, static_cast<std::size_t>(mark), 0.0f);
    }
}
