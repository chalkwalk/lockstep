#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace lockstep
{
    // Sparse per-step parameter override. Keyed by dense slot index (runtime).
    // Absent entries fall through to the track's base value during
    // Override-ELSE-Base resolution.
    //
    // Storage: sorted flat vector<pair<int,float>>, kept ascending by slot.
    // Call reserve(machine.numParams()) after assigning a machine so that
    // audio-thread set() calls never allocate (size never exceeds numParams()).
    //
    // Serialization contract (applied by PluginState):
    //   - Write: iterate via forEach, emit (machine.idForSlot(slot), value) pairs.
    //   - Read:  for each (id, value) pair, resolve slot = machine.slotForId(id);
    //            if slot == -1, log a warning and skip (unknown id — safe to drop).
    class PLock
    {
    public:
        bool has(int slot) const
        {
            const auto it = find(slot);
            return it != overrides_.end() && it->first == slot;
        }

        float get(int slot, float fallback) const
        {
            const auto it = find(slot);
            return (it != overrides_.end() && it->first == slot) ? it->second : fallback;
        }

        void set(int slot, float value)
        {
            const auto it = find(slot);
            if (it != overrides_.end() && it->first == slot)
                it->second = value;
            else
                overrides_.insert(it, { slot, value });
        }

        void clear(int slot)
        {
            const auto it = find(slot);
            if (it != overrides_.end() && it->first == slot)
                overrides_.erase(it);
        }

        void clearAll() { overrides_.clear(); }

        bool empty() const { return overrides_.empty(); }

        // Reserve capacity for up to n overrides; call with numParams() after
        // machine assignment so audio-thread set() never allocates.
        void reserve(int n) { overrides_.reserve(static_cast<std::size_t>(n)); }

        // Iterate all overrides. Callback: void(int slot, float value).
        template<typename Fn>
        void forEach(Fn&& fn) const
        {
            for (const auto& [slot, value] : overrides_)
                fn(slot, value);
        }

    private:
        using Entry = std::pair<int, float>;
        std::vector<Entry> overrides_;

        std::vector<Entry>::const_iterator find(int slot) const
        {
            return std::lower_bound(overrides_.begin(), overrides_.end(),
                                    slot, [](const Entry& e, int s) { return e.first < s; });
        }

        std::vector<Entry>::iterator find(int slot)
        {
            return std::lower_bound(overrides_.begin(), overrides_.end(),
                                    slot, [](const Entry& e, int s) { return e.first < s; });
        }
    };
}
