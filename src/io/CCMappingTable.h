#pragma once

#include "CCMapping.h"
#include "../machine/IMachine.h"
#include <functional>
#include <vector>

namespace lockstep
{
    // Stores per-parameter CC→slot mappings and applies soft-takeover routing.
    // One AbsoluteCCRouter is held per mapping entry, so takeover state is
    // independent across mappings.
    class CCMappingTable
    {
    public:
        void addMapping(CCMapping m);

        // Remove all mappings whose (ccNumber, scope, trackIndex, slot) match.
        void removeMapping(int ccNumber, CCScope scope, int trackIndex, int slot);

        void clear();

        // Dispatch a CC event through all matching mappings.
        //
        // focusTrack: 0-7 selects a track; -1 means Global focus (SelectedTrack
        //             scope CCs are ignored when focus is Global).
        //
        // getCurrentTrackValue: returns the current actual value for (track, slot)
        //                       in the machine's native range.
        // getMetadata:          returns ParamMetadata for (track, slot).
        // writeTrackParam:      called with (track, slot, newActualValue) when a
        //                       mapping's soft-takeover threshold is crossed.
        void dispatch(int ccNumber,
                      int rawValue,
                      int focusTrack,
                      const std::function<float(int, int)>&          getCurrentTrackValue,
                      const std::function<ParamMetadata(int, int)>&  getMetadata,
                      const std::function<void(int, int, float)>&    writeTrackParam);

        const std::vector<CCMapping>& mappings() const { return mappings_; }

    private:
        std::vector<CCMapping> mappings_;
    };
}
