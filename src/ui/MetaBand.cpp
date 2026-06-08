#include "MetaBand.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/TrigCondition.h"
#include "../core/Track.h"
#include "../core/MusicalGate.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include <algorithm>
#include <cmath>

namespace lockstep
{
    MetaBand resolveMetaBand(const UiState& ui)
    {
        switch (ui.masterSection)
        {
            case 0:  return MetaBand::Cond;
            case 1:  return MetaBand::Trig;
            case 3:  return MetaBand::Divider;
            case 4:  return MetaBand::PhraseLen;
            case 5:  return MetaBand::Global;
            default: break;
        }
        if (swingScopeFor(ui) != 0 && !ui.swingDismissed)
            return MetaBand::Swing;
        return MetaBand::None;
    }

    int swingScopeFor(const UiState& ui)
    {
        if (ui.songHeld)   return 1;
        if (ui.sceneHeld)  return 2;
        if (ui.trackHeld)  return 3;
        return 0;
    }

    // =========================================================================
    // buildMetaBand
    // =========================================================================

    static std::array<MetaFieldView, 8> buildCondBand(LockstepProcessor& proc, int track,
                                                      const EditContext& ctx)
    {
        const auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];

        const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  heldStep = ctx.heldStepIndex();

        const TrigCondition& baseCond = t.baseCond;
        const TrigCondition* stepCond = (stepHeld && heldStep >= 0)
            ? &t.steps[static_cast<std::size_t>(heldStep)].condition : nullptr;
        const TrigCondition& display  = (stepCond && !stepCond->isTrivial())
            ? *stepCond : baseCond;

        struct CondDef { const char* label; float lo, hi; bool enabled; };
        static constexpr std::array<CondDef, 8> kDefs = {{
            { "Prob",  1.0f, 100.0f, true  },
            { "m Num", 1.0f,   8.0f, true  },
            { "m Den", 1.0f,   8.0f, true  },
            { "Prev",  0.0f,   2.0f, true  },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
            { "",      0.0f,   1.0f, false },
        }};
        const std::array<float, 8> vals = {
            static_cast<float>(display.probabilityPercent),
            static_cast<float>(display.iterNumerator),
            static_cast<float>(display.iterDenominator),
            static_cast<float>(display.prevDependency),
            0.0f, 0.0f, 0.0f, 0.0f,
        };

        std::array<MetaFieldView, 8> result{};
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto& f = result[si];
            f.active   = kDefs[si].enabled;
            f.label    = kDefs[si].label;
            f.minValue = kDefs[si].lo;
            f.maxValue = kDefs[si].hi;
            f.value    = vals[si];
            f.stepped  = true;
            f.writable = kDefs[si].enabled;
            f.ringMode = RingMode::Dot;

            if (!f.active) continue;
            if (i == 0)
                f.valueText = juce::String(static_cast<int>(vals[si])) + "%";
            else if (i == 3)
            {
                const int pd = static_cast<int>(vals[si]);
                f.valueText = (pd == 0) ? "off" : (pd == 1 ? "fired" : "!fired");
            }
            else
                f.valueText = juce::String(static_cast<int>(vals[si]));
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildTrigBand(LockstepProcessor& proc, int track,
                                                      const EditContext& ctx)
    {
        const auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];

        const bool stepHeld  = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int  heldStep  = ctx.heldStepIndex();
        const bool stepValid = stepHeld && heldStep >= 0 && heldStep < kMaxStepsPerTrack;
        const auto* step = stepValid
            ? &t.steps[static_cast<std::size_t>(heldStep)] : nullptr;
        const auto* trig = step ? &step->trigOverride : nullptr;

        const bool  hasNote  = trig && trig->noteCount > 0;
        const int   note     = hasNote ? trig->notes[0] : t.trigDefaults.note;
        const int   velocity = (trig && trig->hasVelocity) ? trig->velocity : t.trigDefaults.velocity;
        const MusicalGate gateVal = (trig && trig->hasGate)
            ? trig->gateValue : t.trigDefaults.gateValue;
        const bool hasVel   = trig && trig->hasVelocity;
        const bool hasGate  = trig && trig->hasGate;
        const int  chordExtra = hasNote ? trig->noteCount - 1 : 0;
        const float microVal  = step ? step->microOffset : 0.0f;

        const float noteSel = static_cast<float>(
            t.noteSelection == NoteSelection::BottomBias ? 1 : 0);

        struct TrigDef { const char* label; float lo, hi; bool stepped; bool active; };
        const std::array<TrigDef, 8> kDefs = {{
            { "Note",  0.0f,   127.0f,                                  true,  true      },
            { "Vel",   1.0f,   127.0f,                                  true,  true      },
            { "Gate",  0.0f,   static_cast<float>(kMusicalGateCount-1), true,  true      },
            { "Bias",  0.0f,     1.0f,                                  true,  true      },
            { "Micro", -0.5f,   0.5f,                                   false, stepValid },
            { "",      0.0f,    1.0f,                                   false, false     },
            { "",      0.0f,    1.0f,                                   false, false     },
            { "",      0.0f,    1.0f,                                   false, false     },
        }};
        const std::array<float, 8> vals = {
            static_cast<float>(note),
            static_cast<float>(velocity),
            static_cast<float>(static_cast<uint8_t>(gateVal)),
            noteSel,
            microVal,
            0.0f, 0.0f, 0.0f
        };
        const std::array<bool, 8> locks = { false, hasVel, hasGate, false,
                                            stepValid, false, false, false };
        static constexpr const char* kBiasLabels[] = { "TOP", "BOT" };

        std::array<MetaFieldView, 8> result{};
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto& f = result[si];
            f.active      = kDefs[si].active;
            f.label       = kDefs[si].label;
            f.minValue    = kDefs[si].lo;
            f.maxValue    = kDefs[si].hi;
            f.value       = vals[si];
            f.stepped     = kDefs[si].stepped;
            f.writable    = kDefs[si].active;
            f.hasOverride = locks[si];
            f.ringMode    = (kDefs[si].stepped || !kDefs[si].active) ? RingMode::Dot
                            : (kDefs[si].lo < 0.0f) ? RingMode::BipolarFromCentre
                            : RingMode::UnipolarFill;

            if (!f.active) continue;
            if (i == 2)
            {
                const auto gidx = static_cast<int>(vals[si]);
                if (gidx >= 0 && gidx < kMusicalGateCount)
                    f.valueText = juce::String(kMusicalGateLabels[gidx]);
            }
            else if (i == 3)
            {
                f.valueText = kBiasLabels[static_cast<int>(vals[si])];
            }
            else if (i == 4)
            {
                const int pct = static_cast<int>(std::round(vals[si] * 100.0f));
                f.valueText = (pct >= 0 ? "+" : "") + juce::String(pct) + "%";
            }
            else
            {
                f.valueText = juce::String(static_cast<int>(vals[si]));
            }
            if (i == 0 && chordExtra > 0)
                f.valueText += "+" + juce::String(chordExtra);
            if (locks[si])
                f.valueText += " *";
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildDivBand(LockstepProcessor& proc, int track)
    {
        const float divider = proc.apvts()
            .getRawParameterValue(ParamIDs::trackDivider(track))->load();

        std::array<MetaFieldView, 8> result{};
        auto& f0 = result[0];
        f0.active    = true;
        f0.label     = "Divider";
        f0.minValue  = 1.0f;
        f0.maxValue  = 16.0f;
        f0.value     = divider;
        f0.stepped   = true;
        f0.writable  = true;
        f0.valueText = juce::String(static_cast<int>(divider));
        f0.ringMode  = RingMode::Dot;
        return result;
    }

    static std::array<MetaFieldView, 8> buildPhraseLenBand(LockstepProcessor& proc, int track)
    {
        const float length = proc.apvts()
            .getRawParameterValue(ParamIDs::trackLength(track))->load();

        std::array<MetaFieldView, 8> result{};
        auto& f0 = result[0];
        f0.active    = true;
        f0.label     = "Length";
        f0.minValue  = 1.0f;
        f0.maxValue  = 64.0f;
        f0.value     = length;
        f0.stepped   = true;
        f0.writable  = true;
        f0.valueText = juce::String(static_cast<int>(length));
        f0.ringMode  = RingMode::Dot;
        return result;
    }

    static std::array<MetaFieldView, 8> buildGlobalBand(LockstepProcessor& proc)
    {
        const float gain = proc.apvts().getRawParameterValue(ParamIDs::outputGain)->load();
        const float sync = proc.apvts().getRawParameterValue(ParamIDs::syncMode)->load();
        const float chan = proc.apvts().getRawParameterValue(ParamIDs::channelMode)->load();

        struct GlobalDef { const char* label; float lo, hi; bool stepped; bool enabled; };
        static constexpr std::array<GlobalDef, 8> kDefs = {{
            { "Gain", -60.0f, 6.0f, false, true  },
            { "Sync",   0.0f, 1.0f, true,  true  },
            { "Chan",   0.0f, 1.0f, true,  true  },
            { "",       0.0f, 1.0f, false, false },
            { "",       0.0f, 1.0f, false, false },
            { "",       0.0f, 1.0f, false, false },
            { "",       0.0f, 1.0f, false, false },
            { "",       0.0f, 1.0f, false, false },
        }};
        const std::array<float, 8> vals = { gain, sync, chan, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        std::array<MetaFieldView, 8> result{};
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto& f = result[si];
            f.active   = kDefs[si].enabled;
            f.label    = kDefs[si].label;
            f.minValue = kDefs[si].lo;
            f.maxValue = kDefs[si].hi;
            f.value    = vals[si];
            f.stepped  = kDefs[si].stepped;
            f.writable = kDefs[si].enabled;
            f.ringMode = kDefs[si].stepped ? RingMode::Dot : RingMode::BipolarFromCentre;

            if (!f.active) continue;
            if (i == 0)
                f.valueText = (vals[si] >= 0.0f ? "+" : "")
                              + juce::String(vals[si], 1) + " dB";
            else if (i == 1)
                f.valueText = (static_cast<int>(vals[si]) == 0) ? "Locked" : "Auto";
            else if (i == 2)
                f.valueText = (static_cast<int>(vals[si]) == 0) ? "Omni" : "Per-Trk";
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildSwingBand(int swingScope, LockstepProcessor& proc,
                                                       int track)
    {
        const float swingShown = [&]() -> float {
            switch (swingScope)
            {
                case 2:  return proc.swingSceneAllShown();
                case 3:  return (track >= 0) ? proc.swingSongTrackShown(track) : 0.0f;
                default: return proc.swingSongAll();  // scope 1 = song-all
            }
        }();
        const float effSw = (track >= 0) ? proc.swingEffective(track) : 0.0f;

        const char* swingLabel = (swingScope == 2) ? "SwScn"
                               : (swingScope == 3) ? "SwTrk"
                                                   : "Swing";
        const bool isRoot = (swingScope == 1);

        auto fmtSwing = [](float v, bool delta) -> juce::String {
            const int pct = static_cast<int>(std::round(v * 100.0f));
            juce::String s = (pct >= 0 ? "+" : "") + juce::String(pct) + "%";
            if (delta) s += " (D)";
            return s;
        };

        std::array<MetaFieldView, 8> result{};
        auto& sw = result[0];
        sw.active    = true;
        sw.label     = swingLabel;
        sw.minValue  = -0.5f;
        sw.maxValue  =  0.5f;
        sw.value     = swingShown;
        sw.writable  = true;
        sw.valueText = fmtSwing(swingShown, !isRoot);
        sw.ringMode  = RingMode::BipolarFromCentre;

        auto& ef = result[1];
        ef.active    = true;
        ef.label     = "Effct";
        ef.minValue  = -0.5f;
        ef.maxValue  =  0.5f;
        ef.value     = effSw;
        ef.writable  = false;
        ef.valueText = fmtSwing(effSw, false);
        ef.ringMode  = RingMode::BipolarFromCentre;

        return result;
    }

    std::array<MetaFieldView, 8> buildMetaBand(MetaBand           band,
                                               int                swingScope,
                                               LockstepProcessor& proc,
                                               int                track,
                                               const EditContext& ctx)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};

        switch (band)
        {
            case MetaBand::Cond:      return buildCondBand(proc, track, ctx);
            case MetaBand::Trig:      return buildTrigBand(proc, track, ctx);
            case MetaBand::Divider:   return buildDivBand(proc, track);
            case MetaBand::PhraseLen: return buildPhraseLenBand(proc, track);
            case MetaBand::Global:    return buildGlobalBand(proc);
            case MetaBand::Swing:     return buildSwingBand(swingScope, proc, track);
            default:                  return {};
        }
    }

    // =========================================================================
    // writeMetaField
    // =========================================================================

    void writeMetaField(MetaBand           band,
                        int                swingScope,
                        int                field,
                        float              value,
                        LockstepProcessor& proc,
                        int                track,
                        EditContext&       ctx)
    {
        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto u8clamp = [](float v) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
        };

        switch (band)
        {
            case MetaBand::Cond:
            {
                const bool held = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
                const int  step = ctx.heldStepIndex();

                auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];
                TrigCondition& target = (held && step >= 0)
                    ? t.steps[static_cast<std::size_t>(step)].condition
                    : t.baseCond;

                if (held && step >= 0)
                    ctx.markParamWritten();

                switch (field)
                {
                    case 0: target.probabilityPercent = u8clamp(value); break;
                    case 1: target.iterNumerator      = u8clamp(value); break;
                    case 2: target.iterDenominator    = u8clamp(value); break;
                    case 3: target.prevDependency     = u8clamp(value); break;
                    default: break;
                }
                break;
            }

            case MetaBand::Trig:
            {
                const bool held  = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
                const int  step  = ctx.heldStepIndex();
                const bool sv    = held && step >= 0 && step < kMaxStepsPerTrack;
                auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];

                if (sv)
                {
                    auto& stepRef = t.steps[static_cast<std::size_t>(step)];
                    auto& trig    = stepRef.trigOverride;
                    switch (field)
                    {
                        case 0: if (trig.noteCount == 0) trig.noteCount = 1;
                                trig.notes[0] = std::clamp(static_cast<int>(value), 0, 127);   break;
                        case 1: trig.hasVelocity = true;
                                trig.velocity    = std::clamp(static_cast<int>(value), 1, 127); break;
                        case 2: trig.hasGate     = true;
                                trig.gateValue   = static_cast<MusicalGate>(
                                    std::clamp(static_cast<int>(value), 0, kMusicalGateCount-1)); break;
                        case 4: stepRef.microOffset = std::clamp(value, -0.5f, 0.5f);            break;
                        default: break;
                    }
                    ctx.markParamWritten();
                }
                else
                {
                    switch (field)
                    {
                        case 0: t.trigDefaults.note      = std::clamp(static_cast<int>(value), 0, 127);  break;
                        case 1: t.trigDefaults.velocity  = std::clamp(static_cast<int>(value), 1, 127);  break;
                        case 2: t.trigDefaults.gateValue = static_cast<MusicalGate>(
                                    std::clamp(static_cast<int>(value), 0, kMusicalGateCount-1)); break;
                        case 3: t.noteSelection = (value >= 0.5f)
                                    ? NoteSelection::BottomBias : NoteSelection::TopBias; break;
                        default: break;
                    }
                }
                break;
            }

            case MetaBand::Divider:
            {
                if (field != 0) return;
                if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
                    ctx.markParamWritten();
                auto* p = proc.apvts().getParameter(ParamIDs::trackDivider(track));
                if (p) p->setValueNotifyingHost(std::clamp((value - 1.0f) / 15.0f, 0.0f, 1.0f));
                break;
            }

            case MetaBand::PhraseLen:
            {
                if (field != 0) return;
                if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
                    ctx.markParamWritten();
                auto* p = proc.apvts().getParameter(ParamIDs::trackLength(track));
                if (p) p->setValueNotifyingHost(std::clamp((value - 1.0f) / 63.0f, 0.0f, 1.0f));
                break;
            }

            case MetaBand::Global:
            {
                const auto writeApvts = [&](const juce::String& id, float v, float lo, float hi)
                {
                    auto* p = proc.apvts().getParameter(id);
                    if (p) p->setValueNotifyingHost(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f));
                };
                switch (field)
                {
                    case 0: writeApvts(ParamIDs::outputGain,  value, -60.0f, 6.0f); break;
                    case 1: writeApvts(ParamIDs::syncMode,    value,   0.0f, 1.0f); break;
                    case 2: writeApvts(ParamIDs::channelMode, value,   0.0f, 1.0f); break;
                    default: break;
                }
                break;
            }

            case MetaBand::Swing:
            {
                if (field != 0) return;  // slot 1 (Effct) is display-only
                switch (swingScope)
                {
                    case 2:  proc.setSwingSceneAll(value); break;
                    case 3:  if (track >= 0) proc.setSwingSongTrack(track, value); break;
                    default: proc.setSwingSongAll(value); break;  // scope 1 = song-all
                }
                break;
            }

            default: break;
        }
    }

}  // namespace lockstep
