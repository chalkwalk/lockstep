#include "EffectFactory.h"
#include "IEffect.h"
#include "../dsp/DelayEffect.h"
#include "../dsp/ReverbEffect.h"
#include "../dsp/DistortionEffect.h"
#include "../dsp/ChorusEffect.h"
// B2: track-grade effects (8.26)
#include "../dsp/TiltEQEffect.h"
#include "../dsp/CompressorEffect.h"
#include "../dsp/BitcrusherEffect.h"
#include "../dsp/FlangerEffect.h"
#include "../dsp/PhaserEffect.h"

namespace lockstep
{
    std::unique_ptr<IEffect> makeEffectForId(const std::string& id)
    {
        if (id == "lockstep.delay.v1")       return std::make_unique<DelayEffect>();
        if (id == "lockstep.reverb.v1")      return std::make_unique<ReverbEffect>();
        if (id == "lockstep.distortion.v1")  return std::make_unique<DistortionEffect>();
        if (id == "lockstep.chorus.v1")      return std::make_unique<ChorusEffect>();
        // B2: track-grade effects
        if (id == "lockstep.tilteq.v1")      return std::make_unique<TiltEQEffect>();
        if (id == "lockstep.comp.v1")        return std::make_unique<CompressorEffect>();
        if (id == "lockstep.bitcrush.v1")    return std::make_unique<BitcrusherEffect>();
        if (id == "lockstep.flanger.v1")     return std::make_unique<FlangerEffect>();
        if (id == "lockstep.phaser.v1")      return std::make_unique<PhaserEffect>();
        return nullptr;
    }

    static const std::vector<EffectInfo> kEffects = {
        { "lockstep.delay.v1",       "Delay",      "DLY", false },
        { "lockstep.reverb.v1",      "Reverb",     "REV", false },
        { "lockstep.distortion.v1",  "Distortion", "DRV", false },
        { "lockstep.chorus.v1",      "Chorus",     "CHR", false },
        { "lockstep.tilteq.v1",      "Tilt EQ",    "TLT", false },
        { "lockstep.comp.v1",        "Compressor", "CMP", false },
        { "lockstep.bitcrush.v1",    "Bitcrush",   "BIT", false },
        { "lockstep.flanger.v1",     "Flanger",    "FLG", false },
        { "lockstep.phaser.v1",      "Phaser",     "PHA", false },
    };

    std::vector<EffectInfo> availableEffects() { return kEffects; }
    int numAvailableEffects() { return static_cast<int>(kEffects.size()); }
    EffectInfo availableEffectInfo(int i) { return kEffects[static_cast<std::size_t>(i)]; }
}
