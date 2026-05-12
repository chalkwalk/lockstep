#pragma once

#include <unordered_map>
#include "../machine/IMachine.h"

namespace lockstep
{
    // Sparse per-step parameter override. Keyed by dense slot index (runtime).
    // Absent entries fall through to the track's base value during
    // Override-ELSE-Base resolution.
    //
    // Serialization contract (applied by M8's PluginState serializer):
    //   - Write: iterate via forEach, emit (machine.idForSlot(slot), value) pairs.
    //   - Read:  for each (id, value) pair, resolve slot = machine.slotForId(id);
    //            if slot == -1, log a warning and skip (unknown id — safe to drop).
    class PLock
    {
    public:
        bool has(int slot) const { return overrides_.find(slot) != overrides_.end(); }

        float get(int slot, float fallback) const
        {
            auto it = overrides_.find(slot);
            return it != overrides_.end() ? it->second : fallback;
        }

        void set(int slot, float value) { overrides_[slot] = value; }
        void clear(int slot)            { overrides_.erase(slot); }
        void clearAll()                 { overrides_.clear(); }

        bool empty() const { return overrides_.empty(); }

        // Iterate all overrides. Callback signature: void(int slot, float value).
        // Used by the serializer to enumerate entries for id-keyed output.
        template<typename Fn>
        void forEach(Fn&& fn) const
        {
            for (const auto& [slot, value] : overrides_)
                fn(slot, value);
        }

    private:
        std::unordered_map<int, float> overrides_;
    };
}
