#include "Medium.h"

namespace dc
{
    std::size_t Medium::storageSamples(const Config& c) noexcept
    {
        if (c.numSubTracks <= 0 || c.channelsPerSubTrack <= 0 || c.capacitySamples <= 0)
            return 0;
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
        cfg_ = c;
        store_ = store;
        used_.assign(static_cast<std::size_t>(c.numSubTracks), 0);
    }

    void Medium::unbind() noexcept
    {
        store_ = Store{};
        used_.clear();
    }

    void Medium::ensureCommitted(int sub, int upTo) noexcept
    {
        if (! store_.valid() || sub < 0 || sub >= cfg_.numSubTracks) return;

        const int cap = cfg_.capacitySamples;
        const int target = std::min(upTo, cap);
        int& mark = used_[static_cast<std::size_t>(sub)];
        if (target <= mark) return;

        const std::size_t count = static_cast<std::size_t>(target - mark);
        for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
            store_.fill(offset(sub, ch, mark), count, 0.0f);

        mark = target;
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
        if (! store_.valid() || sub < 0 || sub >= cfg_.numSubTracks) return;
        const int mark = used(sub);
        if (mark <= 0) return;
        for (int ch = 0; ch < cfg_.channelsPerSubTrack; ++ch)
            store_.fill(offset(sub, ch, 0), static_cast<std::size_t>(mark), 0.0f);
    }
}
