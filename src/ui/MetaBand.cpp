#include "MetaBand.h"
#include "UITheme.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/Density.h"
#include "../core/Subdivision.h"
#include "../core/Euclidean.h"
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
            case 2:  return MetaBand::Transport;  // Func+7: output gain / sync / channel mode
            case 3:  return MetaBand::Divider;
            case 4:  return MetaBand::PhraseLen;
            case 5:  return MetaBand::Global;     // Song+FX: master insert params
            default: break;
        }
        // Euclidean modal outranks everything else; accent generator next.
        if (ui.euclidHeld)
            return MetaBand::Euclidean;
        if (ui.accentHeld)
            return MetaBand::Accent;
        // Sticky density mode (entered via double-tap Func).
        if (ui.densityStickyMode)
        {
            using SP = UiState::DensitySubPage;
            if (ui.densitySubPage == SP::Musicality) return MetaBand::DensityMode;
            if (ui.densitySubPage == SP::Selection)  return MetaBand::DensitySelection;
            return MetaBand::Density;
        }
        // Transient Func+Song → master density overlay (order-independent).
        if (ui.funcHeld && ui.songHeld)
            return MetaBand::Density;
        // Song (or Scene/Track) alone → swing. Guards after the Func+Song check above.
        if (swingScopeFor(ui) != 0 && !ui.swingDismissed)
            return MetaBand::Swing;
        // Func alone → transient per-track density peek.
        if (ui.funcHeld)
            return MetaBand::Density;
        return MetaBand::None;
    }

    int swingScopeFor(const UiState& ui)
    {
        if (ui.songHeld) return 1;
        if (ui.sceneHeld) return 2;
        if (ui.trackHeld) return 3;
        return 0;
    }

    bool densityEditsMaster(const UiState& ui) noexcept
    {
        return ui.songHeld;
    }

    DensityWriteTarget densityWriteTarget(const UiState& ui, int field, int focusedTrack) noexcept
    {
        if (densityEditsMaster(ui))
            return { true, -1 };
        const int page = ui.densityStickyMode ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        return { false, page * 8 + field };
    }

    bool sectionSelectClearsDensitySticky(const UiState& ui, int sectionIndex) noexcept
    {
        return ui.densityStickyMode && sectionIndex != 5;
    }

    // =========================================================================
    // buildMetaBand
    // =========================================================================

    static std::array<MetaFieldView, 8> buildCondBand(LockstepProcessor& proc, int track,
                                                      const EditContext& ctx)
    {
        const auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];

        const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int heldStep = ctx.heldStepIndex();

        const TrigCondition& baseCond = t.baseCond;
        const TrigCondition* stepCond = (stepHeld && heldStep >= 0)
                                            ? &t.steps[static_cast<std::size_t>(heldStep)].condition
                                            : nullptr;
        const TrigCondition& display = (stepCond && !stepCond->isTrivial())
                                           ? *stepCond
                                           : baseCond;

        struct CondDef
        {
            const char* label;
            float lo, hi;
            bool enabled;
        };
        static constexpr std::array<CondDef, 8> kDefs = { {
            { "Prob", 1.0f, 100.0f, true },
            { "m Num", 1.0f, 8.0f, true },
            { "m Den", 1.0f, 8.0f, true },
            { "Prev", 0.0f, 2.0f, true },
            { "", 0.0f, 1.0f, false },
            { "", 0.0f, 1.0f, false },
            { "", 0.0f, 1.0f, false },
            { "", 0.0f, 1.0f, false },
        } };
        const std::array<float, 8> vals = {
            static_cast<float>(display.probabilityPercent),
            static_cast<float>(display.iterNumerator),
            static_cast<float>(display.iterDenominator),
            static_cast<float>(display.prevDependency),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
        };

        std::array<MetaFieldView, 8> result{};
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto& f = result[si];
            f.active = kDefs[si].enabled;
            f.label = kDefs[si].label;
            f.minValue = kDefs[si].lo;
            f.maxValue = kDefs[si].hi;
            f.value = vals[si];
            f.stepped = true;
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

        const bool stepHeld = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
        const int heldStep = ctx.heldStepIndex();
        const bool stepValid = stepHeld && heldStep >= 0 && heldStep < kMaxStepsPerTrack;
        const auto* step = stepValid
                               ? &t.steps[static_cast<std::size_t>(heldStep)]
                               : nullptr;
        const auto* trig = step ? &step->trigOverride : nullptr;

        const bool hasNote = trig && trig->noteCount > 0;
        const int note = hasNote ? trig->notes[0] : t.trigDefaults.note;
        const int velocity = (trig && trig->hasVelocity) ? trig->velocity : t.trigDefaults.velocity;
        const MusicalGate gateVal = (trig && trig->hasGate)
                                        ? trig->gateValue
                                        : t.trigDefaults.gateValue;
        const bool hasVel = trig && trig->hasVelocity;
        const bool hasGate = trig && trig->hasGate;
        const int chordExtra = hasNote ? trig->noteCount - 1 : 0;
        const float microVal = step ? step->microOffset : 0.0f;

        const float noteSel = static_cast<float>(
            t.noteSelection == NoteSelection::BottomBias ? 1 : 0);

        struct TrigDef
        {
            const char* label;
            float lo, hi;
            bool stepped;
            bool active;
        };
        const std::array<TrigDef, 8> kDefs = { {
            { "Note", 0.0f, 127.0f, true, true },
            { "Vel", 1.0f, 127.0f, true, true },
            { "Gate", 0.0f, static_cast<float>(kMusicalGateCount - 1), true, true },
            { "Bias", 0.0f, 1.0f, true, true },
            { "Micro", -0.5f, 0.5f, false, stepValid },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
        } };
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
            f.active = kDefs[si].active;
            f.label = kDefs[si].label;
            f.minValue = kDefs[si].lo;
            f.maxValue = kDefs[si].hi;
            f.value = vals[si];
            f.stepped = kDefs[si].stepped;
            f.writable = kDefs[si].active;
            f.hasOverride = locks[si];
            f.ringMode = (kDefs[si].stepped || !kDefs[si].active) ? RingMode::Dot
                         : (kDefs[si].lo < 0.0f)                  ? RingMode::BipolarFromCentre
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
        const int idx = std::clamp(
            static_cast<int>(proc.apvts()
                                 .getRawParameterValue(ParamIDs::trackDivider(track))
                                 ->load()),
            kSubdivMin, kSubdivMax);
        const auto base    = baseFromIndex(idx);
        const auto flavour = flavourFromIndex(idx);

        std::array<MetaFieldView, 8> result{};

        auto& f0 = result[0];
        f0.active    = true;
        f0.label     = "Note";
        f0.minValue  = 0.0f;
        f0.maxValue  = static_cast<float>(kNumDivBases - 1);
        f0.value     = static_cast<float>(base);
        f0.stepped   = true;
        f0.writable  = true;
        f0.valueText = juce::String(baseLabel(base));
        f0.ringMode  = RingMode::Dot;

        auto& f1 = result[1];
        f1.active    = true;
        f1.label     = "Flavour";
        f1.minValue  = 0.0f;
        f1.maxValue  = static_cast<float>(kNumDivFlavours - 1);
        f1.value     = static_cast<float>(flavour);
        f1.stepped   = true;
        f1.writable  = true;
        switch (flavour)
        {
            case DivFlavour::Straight: f1.valueText = "STR";  break;
            case DivFlavour::Dotted:   f1.valueText = "DOT";  break;
            case DivFlavour::Triplet:  f1.valueText = "TRIP"; break;
        }
        f1.ringMode = RingMode::Dot;

        return result;
    }

    static std::array<MetaFieldView, 8> buildPhraseLenBand(LockstepProcessor& proc, int track)
    {
        const float length = proc.apvts()
                                 .getRawParameterValue(ParamIDs::trackLength(track))
                                 ->load();

        std::array<MetaFieldView, 8> result{};
        auto& f0 = result[0];
        f0.active = true;
        f0.label = "Length";
        f0.minValue = 1.0f;
        f0.maxValue = 64.0f;
        f0.value = length;
        f0.stepped = true;
        f0.writable = true;
        f0.valueText = juce::String(static_cast<int>(length));
        f0.ringMode = RingMode::Dot;
        return result;
    }

    // buildGlobalBand: Song+FX navigation.
    // When master insert slot has an effect loaded, show its params (mirrors track FX section).
    // When neither slot has an effect, show the global transport params (Gain/Sync/Chan).
    static std::array<MetaFieldView, 8> buildMasterFxBand(LockstepProcessor& proc,
                                                          const UiState& ui)
    {
        std::array<MetaFieldView, 8> result{};
        // 8.26: units 0-1 = master inserts, units 2-3 = send returns.
        const int unit = ui.masterFxInsertSlot;
        const bool isSend = (unit >= 2);
        const int slot = isSend ? unit - 2 : unit;
        const int np = isSend ? proc.masterSendNumParams(slot) : proc.masterInsertNumParams(slot);
        if (np == 0) return result;

        for (int i = 0; i < std::min(np, 8); ++i)
        {
            const auto spec = isSend ? proc.masterSendParamSpec(slot, i)
                                     : proc.masterInsertParamSpec(slot, i);
            const float val = isSend ? proc.masterSendParam(slot, i)
                                     : proc.masterInsertParam(slot, i);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active = true;
            v.label = juce::String(spec.label);
            v.minValue = spec.minValue;
            v.maxValue = spec.maxValue;
            v.value = val;
            v.stepped = spec.isStepped;
            v.writable = true;
            v.hasOverride = false;
            v.valueText = juce::String(val, 2);
            v.ringMode = RingMode::UnipolarFill;
        }
        return result;
    }

    // Song+FX (masterSection==5): master insert params only.
    static std::array<MetaFieldView, 8> buildGlobalBand(LockstepProcessor& proc,
                                                        const UiState& ui)
    {
        return buildMasterFxBand(proc, ui);
    }

    // Func+7 (masterSection==2): output gain / sync / channel mode.
    static std::array<MetaFieldView, 8> buildTransportBand(LockstepProcessor& proc)
    {
        const float gain = proc.apvts().getRawParameterValue(ParamIDs::outputGain)->load();
        const float sync = proc.apvts().getRawParameterValue(ParamIDs::syncMode)->load();
        const float chan = proc.apvts().getRawParameterValue(ParamIDs::channelMode)->load();

        struct GlobalDef
        {
            const char* label;
            float lo, hi;
            bool stepped;
            bool enabled;
        };
        static constexpr std::array<GlobalDef, 8> kDefs = { {
            { "Gain", -60.0f, 6.0f, false, true },
            { "Sync", 0.0f, 1.0f, true, true },
            { "Chan", 0.0f, 1.0f, true, true },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
        } };
        const std::array<float, 8> vals = { gain, sync, chan, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        std::array<MetaFieldView, 8> result{};
        for (int i = 0; i < 8; ++i)
        {
            const auto si = static_cast<std::size_t>(i);
            auto& f = result[si];
            f.active = kDefs[si].enabled;
            f.label = kDefs[si].label;
            f.minValue = kDefs[si].lo;
            f.maxValue = kDefs[si].hi;
            f.value = vals[si];
            f.stepped = kDefs[si].stepped;
            f.writable = kDefs[si].enabled;
            f.ringMode = kDefs[si].stepped ? RingMode::Dot : RingMode::BipolarFromCentre;

            if (!f.active) continue;
            if (i == 0)
                f.valueText = (vals[si] >= 0.0f ? "+" : "") + juce::String(vals[si], 1) + " dB";
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
        // Cumulative value at the held scope (the value this rotary edits).
        // scope 1 = songAll, scope 2 = songAll+sceneAll, scope 3 = effective (all three).
        const float value = [&]() -> float {
            switch (swingScope)
            {
                case 2:  return proc.swingSceneAllShown();
                case 3:  return (track >= 0) ? proc.swingSongTrackShown(track) : 0.0f;
                default: return proc.swingSongAll();
            }
        }();

        // Normalise ±0.5 → 0..1 for reference-mark positions.
        auto norm = [](float v) { return v + 0.5f; };

        auto fmtSwing = [](float v) -> juce::String {
            const int pct = static_cast<int>(std::round(v * 100.0f));
            return (pct >= 0 ? "+" : "") + juce::String(pct) + "%";
        };

        std::array<MetaFieldView, 8> result{};
        auto& sw = result[0];
        sw.active = true;
        sw.label = "Swing";
        sw.minValue = -0.5f;
        sw.maxValue = 0.5f;
        sw.value = value;
        sw.writable = true;
        sw.valueText = fmtSwing(value);
        sw.ringMode = RingMode::BipolarFromCentre;

        // Scope-coloured reference ticks marking the inherited floor.
        // Tick model (user spec): draw song tick first so scene covers it when sceneAll==0.
        if (swingScope == 2)
        {
            // Scene scope: one tick at the song floor, song colour.
            const float songAll = proc.swingSongAll();
            sw.marks[0] = ReferenceMark{ true, norm(songAll), theme::kScopeSong, 1.0f };
        }
        else if (swingScope == 3)
        {
            // Track scope: song floor (faint), then scene floor on top.
            const float songAll = proc.swingSongAll();
            const float sceneAll = proc.swingSceneAllShown();
            sw.marks[0] = ReferenceMark{ true, norm(songAll), theme::kScopeSong, 0.4f };
            sw.marks[1] = ReferenceMark{ true, norm(sceneAll), theme::kScopeScene, 1.0f };
        }

        return result;
    }

    static std::array<MetaFieldView, 8> buildEuclidBand(LockstepProcessor& proc,
                                                        int track,
                                                        const UiState& ui)
    {
        std::array<MetaFieldView, 8> result{};

        const int safeTrack = (track >= 0 && track < static_cast<int>(kNumTracks)) ? track : 0;
        const int phraseLen = proc.sequence().tracks[static_cast<std::size_t>(safeTrack)].length;

        auto makeField = [](const char* lbl, float lo, float hi, float val,
                            const char* txt, bool stepped) -> MetaFieldView {
            MetaFieldView v;
            v.active = true;
            v.label = lbl;
            v.minValue = lo;
            v.maxValue = hi;
            v.value = val;
            v.stepped = stepped;
            v.writable = true;
            v.valueText = txt;
            v.ringMode = RingMode::Dot;
            return v;
        };

        result[0] = makeField("PULSE",
                              0.0f, static_cast<float>(phraseLen > 0 ? phraseLen : 16),
                              static_cast<float>(ui.euclidPulses),
                              juce::String(ui.euclidPulses).toRawUTF8(), true);
        result[1] = makeField("OFSET",
                              static_cast<float>(-((phraseLen > 0 ? phraseLen : 16) + 1) / 2),
                              static_cast<float>(((phraseLen > 0 ? phraseLen : 16) + 1) / 2),
                              static_cast<float>(ui.euclidOffset),
                              juce::String(ui.euclidOffset).toRawUTF8(), true);
        result[2] = makeField("ACCNT",
                              0.0f, static_cast<float>(ui.euclidPulses),
                              static_cast<float>(ui.euclidAccents),
                              juce::String(ui.euclidAccents).toRawUTF8(), true);
        return result;
    }

    // Print-auto-velocity Accent band — Depth (field 0) and Center (field 1).
    static std::array<MetaFieldView, 8> buildAccentBand(const UiState& ui)
    {
        std::array<MetaFieldView, 8> result{};

        // Depth [0, 100] — 0 = flat (all steps get Center velocity), 100 = full swing.
        auto& depth = result[0];
        depth.active = true;
        depth.label = "DEPTH";
        depth.minValue = 0.0f;
        depth.maxValue = 100.0f;
        depth.value = ui.accentDepth * 100.0f;
        depth.stepped = false;
        depth.writable = true;
        depth.valueText = juce::String(juce::roundToInt(ui.accentDepth * 100.0f));
        depth.ringMode = RingMode::UnipolarFill;

        // Center [1, 127] — velocity applied to the bar downbeat.
        auto& center = result[1];
        center.active = true;
        center.label = "CENTR";
        center.minValue = 1.0f;
        center.maxValue = 127.0f;
        center.value = static_cast<float>(ui.accentCenter);
        center.stepped = false;
        center.writable = true;
        center.valueText = juce::String(ui.accentCenter);
        center.ringMode = RingMode::UnipolarFill;

        return result;
    }

    // §39 Density band — 16 tracks paginated (8 per page), arc/tick visual.
    // Page is derived from the focused track so the band always shows the bank
    // containing the active track (tracks 0-7 → page 0, 8-15 → page 1).
    // When Song is held all 8 slots reflect the single master-density knob.
    static std::array<MetaFieldView, 8> buildDensityBand(LockstepProcessor& proc,
                                                          const UiState& ui,
                                                          int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const float master = proc.masterDensity();

        // Per-track density, 8 per page. Bank source: sticky mode uses densityBank,
        // transient mode follows the focused track.
        const int page = ui.densityStickyMode ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;
        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const float perTrack = proc.trackDensity(trackIdx);
            const float effective = juce::jlimit(0.01f, 1.0f, perTrack + master);
            const bool exempt = (proc.kit(trackIdx).densitySelection == Density::DensitySelection::Exempt);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active   = true;
            v.label    = "T" + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 100.0f;
            v.value    = perTrack * 100.0f;
            v.stepped  = false;
            v.writable = !exempt;
            v.hasOverride = !exempt && (perTrack != 1.0f || master != 0.0f);
            v.valueText   = exempt ? "EXMT"
                                   : (juce::String(juce::roundToInt(effective * 100.0f)) + "%");
            v.ringMode    = RingMode::UnipolarFill;
            v.densityCell = !exempt;
            v.densityMasterOffset = master;
            v.densityEffective    = exempt ? 1.0f : effective;
        }
        return result;
    }

    // §39 DensityMode band — Musicality (3-state stepped) per track.
    static std::array<MetaFieldView, 8> buildDensityModeBand(LockstepProcessor& proc,
                                                              const UiState& ui,
                                                              int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = ui.densityStickyMode ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;

        static const char* musLabels[] = { "UNIF", "MIX", "MTRK" };

        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int musVal = static_cast<int>(kit.densityMusicality);
            const bool exempt = (kit.densitySelection == Density::DensitySelection::Exempt);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active   = true;
            v.label    = "T" + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 2.0f;
            v.value    = static_cast<float>(musVal);
            v.stepped  = true;
            v.writable = !exempt;
            v.hasOverride = !exempt && (kit.densityMusicality != Density::Musicality::Mixed);
            v.valueText   = exempt ? "EXMT" : juce::String(musLabels[musVal]);
            v.ringMode    = RingMode::Dot;
        }
        return result;
    }

    // §39 DensitySelection band — Scrub / Re-roll / Exempt (3-state) per track.
    static std::array<MetaFieldView, 8> buildDensitySelectionBand(LockstepProcessor& proc,
                                                                   const UiState& ui,
                                                                   int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = ui.densityStickyMode ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;

        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int selVal = static_cast<int>(kit.densitySelection);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active   = true;
            v.label    = "T" + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 2.0f;
            v.value    = static_cast<float>(selVal);
            v.stepped  = true;
            v.writable = true;
            v.hasOverride = (kit.densitySelection != Density::DensitySelection::Scrub);
            switch (kit.densitySelection)
            {
                case Density::DensitySelection::Scrub:  v.valueText = "SCRB"; break;
                case Density::DensitySelection::Reroll: v.valueText = "RROL"; break;
                case Density::DensitySelection::Exempt: v.valueText = "EXMT"; break;
            }
            v.ringMode = RingMode::Dot;
        }
        return result;
    }

    std::array<MetaFieldView, 8> buildMetaBand(MetaBand band,
                                               int swingScope,
                                               LockstepProcessor& proc,
                                               int track,
                                               const EditContext& ctx,
                                               const UiState& ui)
    {
        if (band == MetaBand::Density)
            return buildDensityBand(proc, ui, track);
        if (band == MetaBand::DensityMode)
            return buildDensityModeBand(proc, ui, track);
        if (band == MetaBand::DensitySelection)
            return buildDensitySelectionBand(proc, ui, track);
        if (band == MetaBand::Euclidean)
            return buildEuclidBand(proc, track, ui);
        if (band == MetaBand::Accent)
            return buildAccentBand(ui);

        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return {};

        switch (band)
        {
            case MetaBand::Cond:      return buildCondBand(proc, track, ctx);
            case MetaBand::Trig:      return buildTrigBand(proc, track, ctx);
            case MetaBand::Divider:   return buildDivBand(proc, track);
            case MetaBand::PhraseLen: return buildPhraseLenBand(proc, track);
            case MetaBand::Global:    return buildGlobalBand(proc, ui);
            case MetaBand::Transport: return buildTransportBand(proc);
            case MetaBand::Swing:     return buildSwingBand(swingScope, proc, track);
            default:                  return {};
        }
    }

    // =========================================================================
    // writeMetaField
    // =========================================================================

    void writeMetaField(MetaBand band,
                        int swingScope,
                        int field,
                        float value,
                        LockstepProcessor& proc,
                        int track,
                        EditContext& ctx,
                        UiState& ui)
    {
        // Accent generator params — update UiState staging area.
        if (band == MetaBand::Accent)
        {
            switch (field)
            {
                case 0: // Depth [0..100]
                    ui.accentDepth = std::clamp(value / 100.0f, 0.0f, 1.0f);
                    break;
                case 1: // Center [1..127]
                    ui.accentCenter = std::clamp(static_cast<int>(std::round(value)), 1, 127);
                    break;
                default: break;
            }
            return;
        }

        // 5.5: Euclidean params — update UiState staging area.
        if (band == MetaBand::Euclidean)
        {
            const int safeTrack = (track >= 0 && track < static_cast<int>(kNumTracks)) ? track : 0;
            const int phraseLen = proc.sequence().tracks[static_cast<std::size_t>(safeTrack)].length;
            const int maxLen = phraseLen > 0 ? phraseLen : 16;
            switch (field)
            {
                case 0:  // Pulses
                    ui.euclidPulses = std::clamp(static_cast<int>(std::round(value)), 0, maxLen);
                    ui.euclidAccents = std::min(ui.euclidAccents, ui.euclidPulses);
                    break;
                case 1:  // Offset — redundant rotations beyond ±ceil(len/2) are eliminated.
                    ui.euclidOffset = std::clamp(static_cast<int>(std::round(value)),
                                                 -(maxLen + 1) / 2, (maxLen + 1) / 2);
                    break;
                case 2:  // Accents
                    ui.euclidAccents = std::clamp(static_cast<int>(std::round(value)), 0, ui.euclidPulses);
                    break;
                default: break;
            }
            return;
        }

        if (track < 0 || track >= static_cast<int>(kNumTracks))
            return;

        const auto u8clamp = [](float v) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
        };

        switch (band)
        {
            case MetaBand::Cond: {
                const bool held = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
                const int step = ctx.heldStepIndex();

                auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];
                TrigCondition& target = (held && step >= 0)
                                            ? t.steps[static_cast<std::size_t>(step)].condition
                                            : t.baseCond;

                if (held && step >= 0)
                    ctx.markParamWritten();

                switch (field)
                {
                    case 0:  target.probabilityPercent = u8clamp(value); break;
                    case 1:  target.iterNumerator = u8clamp(value); break;
                    case 2:  target.iterDenominator = u8clamp(value); break;
                    case 3:  target.prevDependency = u8clamp(value); break;
                    default: break;
                }
                break;
            }

            case MetaBand::Trig: {
                const bool held = ctx.isActiveForEditing() && ctx.heldTrackIndex() == track;
                const int step = ctx.heldStepIndex();
                const bool sv = held && step >= 0 && step < kMaxStepsPerTrack;
                auto& t = proc.sequence().tracks[static_cast<std::size_t>(track)];

                if (sv)
                {
                    auto& stepRef = t.steps[static_cast<std::size_t>(step)];
                    auto& trig = stepRef.trigOverride;
                    switch (field)
                    {
                        case 0:
                            if (trig.noteCount == 0) trig.noteCount = 1;
                            trig.notes[0] = std::clamp(static_cast<int>(value), 0, 127);
                            break;
                        case 1:
                            trig.hasVelocity = true;
                            trig.velocity = std::clamp(static_cast<int>(value), 1, 127);
                            break;
                        case 2:
                            trig.hasGate = true;
                            trig.gateValue = static_cast<MusicalGate>(
                                std::clamp(static_cast<int>(value), 0, kMusicalGateCount - 1));
                            break;
                        case 4:  stepRef.microOffset = std::clamp(value, -0.5f, 0.5f); break;
                        default: break;
                    }
                    ctx.markParamWritten();
                }
                else
                {
                    switch (field)
                    {
                        case 0: t.trigDefaults.note = std::clamp(static_cast<int>(value), 0, 127); break;
                        case 1: t.trigDefaults.velocity = std::clamp(static_cast<int>(value), 1, 127); break;
                        case 2:
                            t.trigDefaults.gateValue = static_cast<MusicalGate>(
                                std::clamp(static_cast<int>(value), 0, kMusicalGateCount - 1));
                            break;
                        case 3:
                            t.noteSelection = (value >= 0.5f)
                                                  ? NoteSelection::BottomBias
                                                  : NoteSelection::TopBias;
                            break;
                        default: break;
                    }
                }
                break;
            }

            case MetaBand::Divider: {
                if (field > 1) return;
                if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
                    ctx.markParamWritten();
                auto* p = proc.apvts().getParameter(ParamIDs::trackDivider(track));
                if (!p) return;
                const int curIdx = std::clamp(
                    static_cast<int>(proc.apvts()
                                         .getRawParameterValue(ParamIDs::trackDivider(track))
                                         ->load()),
                    kSubdivMin, kSubdivMax);
                int baseInt    = static_cast<int>(baseFromIndex(curIdx));
                int flavourInt = static_cast<int>(flavourFromIndex(curIdx));
                if (field == 0)
                    baseInt    = std::clamp(static_cast<int>(value), 0, kNumDivBases    - 1);
                else
                    flavourInt = std::clamp(static_cast<int>(value), 0, kNumDivFlavours - 1);
                const int newIdx = (baseInt * kNumDivFlavours) + flavourInt;
                p->setValueNotifyingHost(static_cast<float>(newIdx) / static_cast<float>(kSubdivMax));
                break;
            }

            case MetaBand::PhraseLen: {
                if (field != 0) return;
                if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
                    ctx.markParamWritten();
                auto* p = proc.apvts().getParameter(ParamIDs::trackLength(track));
                if (p) p->setValueNotifyingHost(std::clamp((value - 1.0f) / 63.0f, 0.0f, 1.0f));
                break;
            }

            case MetaBand::Global: {
                // 8.26: units 0-1 = master inserts, units 2-3 = send returns.
                const int mUnit = ui.masterFxInsertSlot;
                if (mUnit >= 2)
                    proc.setMasterSendParam(mUnit - 2, field, value);
                else
                    proc.setMasterInsertParam(mUnit, field, value);
                break;
            }

            case MetaBand::Transport: {
                // Func+7: output gain / sync / channel mode.
                const auto writeApvts = [&](const juce::String& id, float v, float lo, float hi) {
                    auto* p = proc.apvts().getParameter(id);
                    if (p) p->setValueNotifyingHost(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f));
                };
                switch (field)
                {
                    case 0:  writeApvts(ParamIDs::outputGain, value, -60.0f, 6.0f); break;
                    case 1:  writeApvts(ParamIDs::syncMode, value, 0.0f, 1.0f); break;
                    case 2:  writeApvts(ParamIDs::channelMode, value, 0.0f, 1.0f); break;
                    default: break;
                }
                break;
            }

            case MetaBand::Swing: {
                if (field != 0) return;  // slot 1 (Effct) is display-only
                switch (swingScope)
                {
                    case 2: proc.setSwingSceneAll(value); break;
                    case 3:
                        if (track >= 0) proc.setSwingSongTrack(track, value);
                        break;
                    default: proc.setSwingSongAll(value); break;  // scope 1 = song-all
                }
                break;
            }

            case MetaBand::DensityMode: {
                // Musicality sub-page: value is 0/1/2 (Uniform/Mixed/Metric).
                if (field >= 0 && field < 8)
                {
                    const int page = ui.densityStickyMode ? ui.densityBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                        proc.kit(trackIdx).densityMusicality =
                            static_cast<Density::Musicality>(juce::jlimit(0, 2, juce::roundToInt(value)));
                }
                break;
            }

            case MetaBand::DensitySelection: {
                // Selection sub-page: value 0 = Scrub, 1 = Re-roll, 2 = Exempt.
                if (field >= 0 && field < 8)
                {
                    const int page = ui.densityStickyMode ? ui.densityBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                    {
                        const int clampedVal = juce::jlimit(0, 2, juce::roundToInt(value));
                        proc.kit(trackIdx).densitySelection =
                            static_cast<Density::DensitySelection>(clampedVal);
                    }
                }
                break;
            }

            case MetaBand::Density: {
                // Master writes are handled upstream (mouse incremental delta in
                // ManipulationZone, relative rawDelta in the encoder path).
                if (densityEditsMaster(ui)) break;
                if (field >= 0 && field < 8)
                {
                    const int trackIdx = densityWriteTarget(ui, field, track).trackIdx;
                    if (trackIdx >= 0 && trackIdx < static_cast<int>(kNumTracks))
                        proc.setTrackDensity(trackIdx, juce::jlimit(0.01f, 1.0f, value / 100.0f));
                }
                break;
            }

            default: break;
        }
    }

}  // namespace lockstep
