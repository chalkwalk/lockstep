#include "EffectFactory.h"
#include "IEffect.h"
#include "../dsp/DelayEffect.h"
#include "../dsp/ReverbEffect.h"
#include "../dsp/DistortionEffect.h"
#include "../dsp/SaturationEffect.h"
#include "../dsp/ChorusEffect.h"
// B2: track-grade effects (8.26)
#include "../dsp/TiltEQEffect.h"
#include "../dsp/CompressorEffect.h"
#include "../dsp/BitcrusherEffect.h"
#include "../dsp/FlangerEffect.h"
#include "../dsp/PhaserEffect.h"
// B3: HQ master-only effects (8.26)
#include "../dsp/HQReverbEffect.h"
#include "../dsp/HQDelayEffect.h"
#include "../dsp/BusCompressorEffect.h"
#include "../dsp/MasterUtilityEffect.h"
// S13: new catalogue effects (9.24)
#include "../dsp/LadderFilterEffect.h"
#include "../dsp/FreqShifterEffect.h"
// S14: master-only safety limiter (9.24)
#include "../dsp/LimiterEffect.h"
// S16: convolution reverb (9.24)
#include "../dsp/ConvolutionEffect.h"

namespace lockstep
{
    std::string canonicalEffectId(const std::string& id)
    {
        // Deprecated HQ-only ids fold into their unified catalogue entry; on a
        // master slot these auto-upgrade to the HQ face (see makeEffectForId).
        if (id == "lockstep.verbhq.v1")  return "lockstep.reverb.v1";
        if (id == "lockstep.delayhq.v1") return "lockstep.delay.v1";
        return id;
    }

    std::unique_ptr<IEffect> makeEffectForId(const std::string& id, EffectTier tier)
    {
        const bool hq = (tier == EffectTier::Master);

        // Tier-aware effects: one catalogue id, LQ face on track / HQ on master.
        if (id == "lockstep.reverb.v1")
            return hq ? std::unique_ptr<IEffect>(std::make_unique<HQReverbEffect>())
                      : std::unique_ptr<IEffect>(std::make_unique<ReverbEffect>());
        if (id == "lockstep.delay.v1")
            return hq ? std::unique_ptr<IEffect>(std::make_unique<HQDelayEffect>())
                      : std::unique_ptr<IEffect>(std::make_unique<DelayEffect>());

        if (id == "lockstep.distortion.v1")  return std::make_unique<DistortionEffect>();
        if (id == "lockstep.saturation.v1")  return std::make_unique<SaturationEffect>(tier);
        if (id == "lockstep.chorus.v1")      return std::make_unique<ChorusEffect>();
        // B2: track-grade effects
        if (id == "lockstep.tilteq.v1")      return std::make_unique<TiltEQEffect>();
        if (id == "lockstep.comp.v1")        return std::make_unique<CompressorEffect>();
        if (id == "lockstep.bitcrush.v1")    return std::make_unique<BitcrusherEffect>();
        if (id == "lockstep.flanger.v1")     return std::make_unique<FlangerEffect>();
        if (id == "lockstep.phaser.v1")      return std::make_unique<PhaserEffect>();
        if (id == "lockstep.buscomp.v1")     return std::make_unique<BusCompressorEffect>();
        if (id == "lockstep.mutility.v1")    return std::make_unique<MasterUtilityEffect>();
        // S13: new effects (same face on track + master; no tier split).
        if (id == "lockstep.ladder.v1")      return std::make_unique<LadderFilterEffect>();
        if (id == "lockstep.freqshift.v1")   return std::make_unique<FreqShifterEffect>();
        // S14: master-only safety limiter.
        if (id == "lockstep.limiter.v1")     return std::make_unique<LimiterEffect>();
        // S16: convolution reverb (track + master; no tier split).
        if (id == "lockstep.conv.v1")        return std::make_unique<ConvolutionEffect>();

        // Backward-compat: deprecated HQ-only ids still resolve (always HQ face)
        // for any path that did not run them through canonicalEffectId() first.
        if (id == "lockstep.verbhq.v1")      return std::make_unique<HQReverbEffect>();
        if (id == "lockstep.delayhq.v1")     return std::make_unique<HQDelayEffect>();
        // Item 5: the External send is a routing sentinel, not a processor — it
        // has no DSP instance. A send slot holding it keeps its effectId but a null
        // live effect; processMasterChain() diverts its tap to the host Send bus.
        // (Falls through to the null return below.)
        return nullptr;
    }

    static const std::vector<EffectInfo> kEffects = {
        // Reverb/Delay are unified entries: LQ on track inserts, auto-HQ on the
        // master bus / sends (placement picks the tier). masterOnly=false so they
        // show in both pickers.
        { "lockstep.delay.v1",       "Delay",      "DLY", false },
        { "lockstep.reverb.v1",      "Reverb",     "REV", false },
        { "lockstep.distortion.v1",  "Distortion", "DRV", false },
        { "lockstep.saturation.v1",  "Saturation", "SAT", false },
        { "lockstep.chorus.v1",      "Chorus",     "CHR", false },
        { "lockstep.tilteq.v1",      "Tilt EQ",    "TLT", false },
        { "lockstep.comp.v1",        "Compressor", "CMP", false },
        { "lockstep.bitcrush.v1",    "Bitcrush",   "BIT", false },
        { "lockstep.flanger.v1",     "Flanger",    "FLG", false },
        { "lockstep.phaser.v1",      "Phaser",     "PHA", false },
        // S13: ladder filter + frequency shifter (track + master).
        { "lockstep.ladder.v1",      "Ladder",     "LDR", false },
        { "lockstep.freqshift.v1",   "FreqShift",  "FSH", false },
        // S16: convolution reverb (track + master).
        { "lockstep.conv.v1",        "Convolve",   "CNV", false },
        // Genuinely master-only effects with no LQ counterpart.
        { "lockstep.buscomp.v1",     "Bus Comp",   "BUS", true,  false },
        { "lockstep.mutility.v1",    "Utility",    "UTL", true,  false },
        // S14: master-bus safety limiter (master inserts/sends only).
        { "lockstep.limiter.v1",     "Limiter",    "LIM", true,  false },
        // Item 5: External send — master send slots only (masterOnly + sendOnly).
        // No DSP; routes the send tap to the host "Send A/B" output bus.
        { kExternalSendId,           "External",   "EXT", true,  true  },
    };

    std::vector<EffectInfo> availableEffects() { return kEffects; }
    int numAvailableEffects() { return static_cast<int>(kEffects.size()); }
    EffectInfo availableEffectInfo(int i) { return kEffects[static_cast<std::size_t>(i)]; }
}
