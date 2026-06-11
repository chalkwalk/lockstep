#include "ButtonLayers.h"

namespace lockstep
{
    ControllerEvent resolveLayer(ControllerEvent raw, const LayerContext& ctx) noexcept
    {
        for (const auto& remap : kLayerRemaps)
        {
            if (raw.button != remap.raw) continue;
            bool active = false;
            switch (remap.layer)
            {
                case LayerRemap::Layer::Track: active = ctx.trackHeld; break;
                case LayerRemap::Layer::Mute:  active = ctx.muteHeld; break;
                case LayerRemap::Layer::Func:  active = ctx.funcHeld; break;
            }
            if (active)
            {
                ControllerEvent result = raw;
                result.button = remap.effective;
                return result;
            }
        }
        return raw;
    }
}
