#pragma once

#include "CCMapping.h"
#include "../machine/IMachine.h"
#include <array>
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

        // Remove the first mapping whose (ccNumber, scope, trackIndex, slot,
        // mzPosition) all match. For Contextual scope use mzPosition; for
        // Track / SelectedTrack scope use slot; pass -1 for the unused field.
        void removeMapping(int ccNumber, CCScope scope,
                           int trackIndex, int slot, int mzPosition = -1);

        void clear();

        // Dispatch a CC event through all matching mappings.
        //
        // focusTrack: 0-7 selects a track; -1 means Global focus (SelectedTrack
        //             and Contextual scope CCs are ignored when focus is Global).
        // mzSlots:    current absolute slot index for each of the 4 MZ positions
        //             (used to resolve Contextual mappings at call time).
        //
        // getCurrentTrackValue: returns the current actual value for (track, slot).
        // getMetadata:          returns ParamMetadata for (track, slot).
        // writeTrackParam:      called with (track, slot, newActualValue).
        void dispatch(int ccNumber,
                      int rawValue,
                      int focusTrack,
                      const std::array<int, 4>&                        mzSlots,
                      const std::function<float(int, int)>&            getCurrentTrackValue,
                      const std::function<ParamMetadata(int, int)>&    getMetadata,
                      const std::function<void(int, int, float)>&      writeTrackParam);

        const std::vector<CCMapping>& mappings() const { return mappings_; }

    private:
        std::vector<CCMapping> mappings_;
    };
}
