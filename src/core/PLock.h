#pragma once

#include <unordered_map>
#include "../machine/IMachine.h"

namespace lockstep
{
    // Sparse per-step parameter override. Keyed by slot index (0..47).
    // Absent entries fall through to the track's base value during
    // Override-ELSE-Base resolution.
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

    private:
        std::unordered_map<int, float> overrides_;
    };
}
