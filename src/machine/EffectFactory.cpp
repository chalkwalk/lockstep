#include "EffectFactory.h"
#include "IEffect.h"
#include "../dsp/DelayEffect.h"
#include "../dsp/ReverbEffect.h"
#include "../dsp/DistortionEffect.h"
#include "../dsp/ChorusEffect.h"

namespace lockstep
{
    std::unique_ptr<IEffect> makeEffectForId(const std::string& id)
    {
        if (id == "lockstep.delay.v1")      return std::make_unique<DelayEffect>();
        if (id == "lockstep.reverb.v1")     return std::make_unique<ReverbEffect>();
        if (id == "lockstep.distortion.v1") return std::make_unique<DistortionEffect>();
        if (id == "lockstep.chorus.v1")     return std::make_unique<ChorusEffect>();
        return nullptr;
    }

    static const std::vector<EffectInfo> kEffects = {
        { "lockstep.delay.v1",      "Delay",      "DLY" },
        { "lockstep.reverb.v1",     "Reverb",     "REV" },
        { "lockstep.distortion.v1", "Distortion", "DRV" },
        { "lockstep.chorus.v1",     "Chorus",     "CHR" },
    };

    std::vector<EffectInfo> availableEffects()        { return kEffects; }
    int                     numAvailableEffects()     { return static_cast<int>(kEffects.size()); }
    EffectInfo              availableEffectInfo(int i){ return kEffects[static_cast<std::size_t>(i)]; }
}
