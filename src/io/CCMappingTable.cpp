#include "CCMappingTable.h"
#include <algorithm>

namespace lockstep
{
    void CCMappingTable::addMapping(CCMapping m)
    {
        mappings_.push_back(std::move(m));
    }

    void CCMappingTable::removeMapping(int ccNumber, CCScope scope,
                                       int trackIndex, int slot)
    {
        mappings_.erase(
            std::remove_if(mappings_.begin(), mappings_.end(),
                [&](const CCMapping& m) {
                    return m.ccNumber == ccNumber
                        && m.scope == scope
                        && m.trackIndex == trackIndex
                        && m.slot == slot;
                }),
            mappings_.end());
    }

    void CCMappingTable::clear() { mappings_.clear(); }

    void CCMappingTable::dispatch(
        int ccNumber,
        int rawValue,
        int focusTrack,
        const std::function<float(int, int)>&         getCurrentTrackValue,
        const std::function<ParamMetadata(int, int)>& getMetadata,
        const std::function<void(int, int, float)>&   writeTrackParam)
    {
        const float incomingNorm = static_cast<float>(rawValue) / 127.0f;

        for (auto& m : mappings_)
        {
            if (m.ccNumber != ccNumber)
                continue;

            if (m.scope == CCScope::Global)
                continue; // APVTS write path wired in M5.4

            int targetTrack = -1;
            if (m.scope == CCScope::Track)
                targetTrack = m.trackIndex;
            else if (m.scope == CCScope::SelectedTrack)
            {
                if (focusTrack < 0)
                    continue; // Global focus — no-op for SelectedTrack scope
                targetTrack = focusTrack;
            }

            if (targetTrack < 0 || m.slot < 0)
                continue;

            const auto meta = getMetadata(targetTrack, m.slot);
            const float range = meta.maxValue - meta.minValue;
            const float currentActual = getCurrentTrackValue(targetTrack, m.slot);
            const float currentNorm = (range > 0.0f)
                ? (currentActual - meta.minValue) / range
                : 0.0f;

            const float resultNorm   = m.router.route(currentNorm, incomingNorm);
            const float resultActual = meta.minValue + resultNorm * range;
            writeTrackParam(targetTrack, m.slot, resultActual);
        }
    }
}
