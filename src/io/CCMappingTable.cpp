#include "CCMappingTable.h"
#include "RelativeCC.h"
#include <algorithm>

namespace lockstep
{
    void CCMappingTable::addMapping(CCMapping m)
    {
        mappings_.push_back(std::move(m));
    }

    void CCMappingTable::removeMapping(int ccNumber, CCScope scope,
                                       int trackIndex, int slot, int mzPosition)
    {
        mappings_.erase(
            std::remove_if(mappings_.begin(), mappings_.end(),
                           [&](const CCMapping& m) {
                               if (m.ccNumber != ccNumber || m.scope != scope)
                                   return false;
                               if (scope == CCScope::Contextual)
                                   return m.mzPosition == mzPosition;
                               return m.trackIndex == trackIndex && m.slot == slot;
                           }),
            mappings_.end());
    }

    void CCMappingTable::clear() { mappings_.clear(); }

    void CCMappingTable::dispatch(
        int ccNumber,
        int rawValue,
        int focusTrack,
        const std::array<int, 4>& mzSlots,
        const std::function<float(int, int)>& getCurrentTrackValue,
        const std::function<ParamSpec(int, int)>& getMetadata,
        const std::function<void(int, int, float)>& writeTrackParam,
        const std::function<void(float)>& setCrossfaderValue)
    {
        for (auto& m : mappings_)
        {
            if (m.ccNumber != ccNumber)
                continue;

            // Crossfader scope: normalised CC → morphFader directly (no soft-takeover;
            // the fader is a performance control expected to be in sync with the CC).
            if (m.scope == CCScope::Crossfader)
            {
                if (setCrossfaderValue)
                    setCrossfaderValue(static_cast<float>(rawValue) / 127.0f);
                continue;
            }

            if (m.scope == CCScope::Global)
                continue; // APVTS write path wired in M5.4 global handling

            int targetTrack = -1;
            int targetSlot = m.slot;

            if (m.scope == CCScope::Track)
            {
                targetTrack = m.trackIndex;
            }
            else if (m.scope == CCScope::SelectedTrack)
            {
                if (focusTrack < 0)
                    continue; // Global focus — no-op
                targetTrack = focusTrack;
            }
            else if (m.scope == CCScope::Contextual)
            {
                if (focusTrack < 0 || m.mzPosition < 0 || m.mzPosition > 3)
                    continue;
                targetTrack = focusTrack;
                targetSlot = mzSlots[static_cast<std::size_t>(m.mzPosition)];
            }

            if (targetTrack < 0 || targetSlot < 0)
                continue;

            const auto meta = getMetadata(targetTrack, targetSlot);
            const float range = meta.maxValue - meta.minValue;
            const float currentActual = getCurrentTrackValue(targetTrack, targetSlot);
            const float currentNorm = (range > 0.0f)
                                          ? (currentActual - meta.minValue) / range
                                          : 0.0f;

            float resultNorm;
            if (m.isRelative)
            {
                int delta;
                if (m.encoding == RelativeCCEncoding::BinOffset)
                    delta = rawValue - 64;
                else // TwosComplement
                    delta = (rawValue > 63) ? rawValue - 128 : rawValue;

                if (delta == 0)
                    continue;

                resultNorm = RelativeCCRouter{}.apply(currentNorm, delta, m.scale);
                resultNorm = std::clamp(resultNorm, 0.0f, 1.0f);
            }
            else
            {
                const float incomingNorm = static_cast<float>(rawValue) / 127.0f;
                resultNorm = m.router.route(currentNorm, incomingNorm);
            }

            const float resultActual = meta.minValue + resultNorm * range;
            writeTrackParam(targetTrack, targetSlot, resultActual);
        }
    }
}
