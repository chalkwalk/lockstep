#include "MetaBand.h"
#include "UITheme.h"
#include "ParamFormat.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/AccentVel.h"
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
    // Ratchet rates for per-step RTG authored field (9.10).
    // Matches the kRetrigRates table in PluginEditor.cpp; duplicated here to
    // avoid exposing PluginEditor internals to MetaBand.
    static constexpr std::array<double, 8> kRetrigRates = { {
        1.0,          // /4
        2.0 / 3.0,    // /4T
        0.5,          // /8
        1.0 / 3.0,    // /8T
        0.25,         // /16  (default)
        1.0 / 6.0,    // /16T
        0.125,        // /32
        1.0 / 12.0,   // /32T
    } };

    static constexpr const char* kRetrigRateLabels[9] = {
        "OFF", "/4", "/4T", "/8", "/8T", "/16", "/16T", "/32", "/32T"
    };

    static int retrigRateToIndex(double rate) noexcept
    {
        for (int i = 0; i < static_cast<int>(kRetrigRates.size()); ++i)
            if (std::abs(kRetrigRates[static_cast<std::size_t>(i)] - rate) < 1e-9)
                return i;
        return 4;  // default: /16
    }

    MetaBand resolveMetaBand(const UiState& ui)
    {
        // Transient overlays outrank the latched masterSection page so that
        // arming euclid, entering density/vel sticky, or holding swing while a
        // DIV/LEN page is latched immediately shows the relevant band.

        // Held-step move outranks everything: it is a transient interaction only
        // active while a step is held and being moved/micro-nudged (9.14).
        if (ui.stepMoveActive)
            return MetaBand::StepPosition;
        // Euclidean modal outranks everything else.
        if (ui.euclidHeld)
            return MetaBand::Euclidean;
        // TIME/KEY signatures band (entered via Song+TRIG or Scene+TRIG; re-press
        // TRIG cycles TIME <-> KEY; Func double-tap escapes it).
        if (ui.overlay == Overlay::Time)
            return ui.sigPage == UiState::SigPage::Key ? MetaBand::Key : MetaBand::Time;
        // Sticky density mode (entered via Func+MOD; Func double-tap escapes it).
        if (ui.overlay == Overlay::Density)
        {
            using SP = UiState::DensitySubPage;
            if (ui.densitySubPage == SP::Musicality) { return MetaBand::DensityMode; }
            if (ui.densitySubPage == SP::Selection)  { return MetaBand::DensitySelection; }
            return MetaBand::Density;
        }
        // Sticky velocity overlay mode.
        if (ui.overlay == Overlay::Vel)
        {
            using VP = UiState::VelSubPage;
            if (ui.velSubPage == VP::Center) { return MetaBand::VelCenter; }
            if (ui.velSubPage == VP::Mode)   { return MetaBand::VelMode; }
            if (ui.velSubPage == VP::Blend)  { return MetaBand::VelBlend; }
            return MetaBand::Vel;
        }
        // Song (or Scene/Track) alone → swing.
        if (swingScopeFor(ui) != 0 && !ui.swingDismissed)
            return MetaBand::Swing;
        // Density/Vel transient peeks (Func+Song and Func alone) removed in 9.10:
        // generators are entered exclusively via the generator hub on the 3 key.

        // Latched meta pages (set by scope+section chords; survive modifier release).
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
        return MetaBand::None;
    }

    int swingScopeFor(const UiState& ui)
    {
        if (ui.songHeld) return 1;
        if (ui.sceneHeld) return 2;
        if (ui.trackHeld) return 3;
        return 0;
    }

    int timeScopeFor(const UiState& ui)
    {
        if (ui.funcHeld && ui.songHeld) return 1;  // Set/global level
        if (ui.songHeld) return 2;                 // Song level
        if (ui.sceneHeld) return 3;                // Scene level
        return ui.timeEntryScope;                  // entry scope — never silently target scope 0
    }

    bool isTimeEntryChord(EditMode::PrimaryScope scope, int sectionIndex) noexcept
    {
        using PS = EditMode::PrimaryScope;
        return sectionIndex == 0 && (scope == PS::Song || scope == PS::Scene);
    }

    bool applyTimeEntry(UiState& ui) noexcept
    {
        const bool entering = (ui.overlay != Overlay::Time);
        ui.overlay = entering ? Overlay::Time : Overlay::None;
        if (entering)
        {
            ui.timeEntryScope = timeScopeFor(ui);
            ui.sigPage = UiState::SigPage::Time;  // always open on the TIME page
            ui.swingDismissed = true;
        }
        return entering;
    }

    void escapeTimeSticky(UiState& ui) noexcept
    {
        if (ui.overlay == Overlay::Time) { ui.overlay = Overlay::None; }
        ui.swingDismissed = true;
    }

    bool densityEditsMaster(const UiState& ui) noexcept
    {
        return ui.songHeld;
    }

    DensityWriteTarget densityWriteTarget(const UiState& ui, int field, int focusedTrack) noexcept
    {
        if (densityEditsMaster(ui))
            return { true, -1 };
        const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        return { false, page * 8 + field };
    }

    bool sectionSelectClearsDensitySticky(const UiState& ui, int sectionIndex) noexcept
    {
        return ui.overlay == Overlay::Density && sectionIndex != 4;
    }

    bool sectionSelectClearsVelSticky(const UiState& ui, int sectionIndex) noexcept
    {
        return ui.overlay == Overlay::Vel && sectionIndex != 3;
    }

    bool sectionSelectClearsTimeSticky(const UiState& ui, int sectionIndex) noexcept
    {
        return ui.overlay == Overlay::Time && sectionIndex != 0;
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

        // RTG field (slot 5): per-step authored ratchet rate (9.10).
        // 0 = off (hasRetrig=false); 1..8 = rate index 0..7.
        const int rtgIdx = (stepValid && trig && trig->hasRetrig)
                               ? retrigRateToIndex(trig->retrigRate) + 1
                               : 0;
        const float rtgVal = static_cast<float>(rtgIdx);

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
            { "RTG",  0.0f, 8.0f, true, stepValid },   // 9.10: authored ratchet
            { "", 0.0f, 1.0f, false, false },
            { "", 0.0f, 1.0f, false, false },
        } };
        const std::array<float, 8> vals = {
            static_cast<float>(note),
            static_cast<float>(velocity),
            static_cast<float>(static_cast<uint8_t>(gateVal)),
            noteSel,
            microVal,
            rtgVal, 0.0f, 0.0f
        };
        const std::array<bool, 8> locks = { false, hasVel, hasGate, false,
                                            stepValid, stepValid && rtgIdx > 0, false, false };
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
            else if (i == 5)
            {
                const int ri = std::clamp(static_cast<int>(vals[si]), 0, 8);
                f.valueText = juce::String(kRetrigRateLabels[ri]);
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

    // buildStepPositionBand: held-step move panel (9.14).
    // Field 0 = position (1..trackLen), field 1 = micro-time offset (±50% step).
    // Reads the moved step from ui.pLockClearTrack / ui.pLockClearStep.
    static std::array<MetaFieldView, 8> buildStepPositionBand(LockstepProcessor& proc,
                                                              const UiState& ui)
    {
        std::array<MetaFieldView, 8> result{};
        const int track = ui.pLockClearTrack;
        const int step = ui.pLockClearStep;
        if (track < 0 || track >= static_cast<int>(kNumTracks) || step < 0)
            return result;

        const auto& trk = proc.sequence().tracks[static_cast<std::size_t>(track)];
        const int trackLen = std::max(1, trk.length);
        const float micro = trk.steps[static_cast<std::size_t>(step)].microOffset;

        auto& f0 = result[0];
        f0.active    = true;
        f0.label     = "Pos";
        f0.minValue  = 1.0f;
        f0.maxValue  = static_cast<float>(trackLen);
        f0.value     = static_cast<float>(step + 1);
        f0.stepped   = true;
        f0.writable  = true;
        f0.valueText = juce::String(step + 1);
        f0.ringMode  = RingMode::Dot;

        auto& f1 = result[1];
        f1.active    = true;
        f1.label     = "Micro";
        f1.minValue  = -0.5f;
        f1.maxValue  = 0.5f;
        f1.value     = micro;
        f1.stepped   = false;
        f1.writable  = true;
        f1.valueText = juce::String(micro, 2);
        f1.ringMode  = RingMode::BipolarFromCentre;

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
            // Route through the value-text SSOT so units, enum labels and min/maxLabel
            // ("Auto") render here exactly as they do for machine params.
            v.valueText = formatParamValue(val, spec);
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

    // Curated time-signature list (DESIGN §4.8), ordered by ascending bar length
    // (num/den as a fraction of a 4/4 bar; ties broken by smaller denominator).
    // 4/4 sits at index 6 and remains the resolved default for Set scope.
    struct TsEntry { int num; int den; const char* label; };
    static constexpr TsEntry kTimeSigs[] = {
        {  3, 8,  "3/8" },  // 0.375 bars
        {  2, 4,  "2/4" },  // 0.5
        {  5, 8,  "5/8" },  // 0.625
        {  3, 4,  "3/4" },  // 0.75
        {  6, 8,  "6/8" },  // 0.75 (same bar PPQ as 3/4; differs in accent density)
        {  7, 8,  "7/8" },  // 0.875
        {  4, 4,  "4/4" },  // 1.0   — Set default
        {  9, 8,  "9/8" },  // 1.125
        {  5, 4,  "5/4" },  // 1.25
        { 11, 8, "11/8" },  // 1.375
        { 12, 8, "12/8" },  // 1.5
        {  7, 4,  "7/4" },  // 1.75
    };
    static constexpr int kNumTimeSigs = static_cast<int>(std::size(kTimeSigs));

    // Returns the index into kTimeSigs for the given time signature, or 0 (4/4) if not found.
    static int timeSigIndex(const TimeSig& ts)
    {
        for (int i = 0; i < kNumTimeSigs; ++i)
            if (kTimeSigs[i].num == ts.numerator && kTimeSigs[i].den == ts.denominator)
                return i;
        return 0;
    }

    // Band shows one stepped selector: the curated time-sig list.
    // For Song/Scene scopes, index 0 is "INHERIT" (clears the override).
    static std::array<MetaFieldView, 8> buildTimeSigBand(int tsScope, LockstepProcessor& proc)
    {
        std::array<MetaFieldView, 8> result{};

        // Resolve the parent (what the current level inherits from).
        const auto& song = proc.song();
        const auto& scene = proc.section();
        const TimeSig parentTs = [&]() -> TimeSig {
            if (tsScope == 3) // Scene inherits from Song or Set
                return song.hasTimeSig ? song.timeSig : proc.project().defaultTimeSig;
            if (tsScope == 2) // Song inherits from Set
                return proc.project().defaultTimeSig;
            return proc.project().defaultTimeSig; // Set: no parent
        }();

        // Determine the override value at the current scope (for display).
        const TimeSig overrideTs = [&]() -> TimeSig {
            if (tsScope == 3 && scene.hasTimeSig) return scene.coreTime;
            if (tsScope == 2 && song.hasTimeSig)  return song.timeSig;
            return proc.project().defaultTimeSig;  // Set scope
        }();

        const bool hasOverride = [&]() -> bool {
            if (tsScope == 3) return scene.hasTimeSig;
            if (tsScope == 2) return song.hasTimeSig;
            return false; // Set always "owns" its value
        }();

        // INHERIT is index 0 for Song/Scene scopes; for Set scope the list starts at 0.
        const bool hasInherit = (tsScope == 2 || tsScope == 3);
        // Effective displayed index: 0 = INHERIT (when applicable), 1..N = curated entries.
        auto tsToIdx = [&](const TimeSig& ts, bool inherit) -> int {
            if (hasInherit && inherit) return 0;
            const int idx = timeSigIndex(ts);
            return hasInherit ? idx + 1 : idx;
        };

        const int displayIdx = tsToIdx(overrideTs, !hasOverride && hasInherit);
        const int maxIdx = kNumTimeSigs - 1 + (hasInherit ? 1 : 0);
        const float scopeColour = (tsScope == 1)
            ? static_cast<float>(theme::kScopeSong)
            : (tsScope == 3 ? static_cast<float>(theme::kScopeScene) : static_cast<float>(theme::kScopeSong));

        const char* scopeLabel = (tsScope == 1) ? "Set" : (tsScope == 2) ? "Song" : "Scene";

        auto& f = result[0];
        f.active = true;
        f.label = scopeLabel;
        f.minValue = 0.0f;
        f.maxValue = static_cast<float>(maxIdx);
        f.value = static_cast<float>(displayIdx);
        f.stepped = true;
        f.writable = true;
        f.hasOverride = hasOverride;
        f.ringMode = RingMode::Dot;

        // Show the label: INHERIT or the curated name.
        if (hasInherit && displayIdx == 0)
            f.valueText = juce::String("INHERIT (")
                          + juce::String(parentTs.numerator) + "/"
                          + juce::String(parentTs.denominator) + ")";
        else
        {
            const int listIdx = displayIdx - (hasInherit ? 1 : 0);
            const int safeIdx = std::clamp(listIdx, 0, kNumTimeSigs - 1);
            f.valueText = juce::String(kTimeSigs[safeIdx].label);
        }

        // Reference mark: parent value (scope-coloured), shown when editing Song/Scene.
        if (hasInherit)
        {
            const int parentIdx = timeSigIndex(parentTs) + 1;  // +1 for INHERIT at 0
            const float parentNorm = (maxIdx > 0)
                ? static_cast<float>(parentIdx) / static_cast<float>(maxIdx) : 0.0f;
            f.marks[0] = ReferenceMark{ true, parentNorm, static_cast<juce::uint32>(scopeColour), 1.0f };
        }

        return result;
    }

    // ── KEY band (DESIGN §4.10) ──────────────────────────────────────────────
    // 8 slots: Root, Brightness (mode), then the six functional modifiers as
    // compatibility-gated on/off toggles. The KEY band shares the TIME band's
    // scope ladder (Set / Song / Scene) and entry; re-pressing TRIG cycles to it.
    struct KeyModEntry { NamedModifier mod; const char* label; };
    static constexpr KeyModEntry kKeyMods[] = {
        { NamedModifier::Harmonic,       "HARM" },
        { NamedModifier::Melodic,        "MEL"  },
        { NamedModifier::DoubleHarmonic, "DBLH" },
        { NamedModifier::HarmonicMajor,  "HMAJ" },
        { NamedModifier::Blues,          "BLUE" },
        { NamedModifier::Neapolitan,     "NEAP" },
    };

    static bool keyHasModifier(const KeySig& k, NamedModifier m)
    {
        for (NamedModifier x : k.modifiers)
            if (x == m) return true;
        return false;
    }

    static std::array<MetaFieldView, 8> buildKeyBand(int scope, LockstepProcessor& proc)
    {
        std::array<MetaFieldView, 8> result{};
        const bool hasInherit = (scope == 2 || scope == 3);
        const auto& song = proc.song();
        const auto& scene = proc.section();
        const bool hasOverride = (scope == 3) ? scene.hasKeySig
                               : (scope == 2) ? song.hasKeySig : false;
        // The key shown/edited: the scope's own override if present, otherwise
        // the inherited effective key (also the seed when an override is started).
        const KeySig shown = hasOverride
            ? ((scope == 3) ? scene.coreKeySig : song.keySig)
            : proc.effectiveKeySig();
        const KeySig parent = (scope == 3 && song.hasKeySig) ? song.keySig
                                                             : proc.project().defaultKeySig;

        const float scopeColour = (scope == 3) ? static_cast<float>(theme::kScopeScene)
                                               : static_cast<float>(theme::kScopeSong);

        // Field 0 — Root, stepped in circle-of-fifths order centred on D (index
        // 6). INHERIT sits at index 0 for Song/Scene, like the time-sig band; the
        // fifths order then occupies 1..12.
        {
            auto& f = result[0];
            f.active = true; f.label = "Root"; f.stepped = true; f.writable = true;
            f.ringMode = RingMode::Dot; f.hasOverride = hasOverride;
            if (hasInherit)
            {
                f.minValue = 0.0f; f.maxValue = 12.0f;
                if (hasOverride)
                {
                    f.value = static_cast<float>(fifthsIndexOfRootPc(shown.root) + 1);
                    f.valueText = pitchClassName(shown.root);
                }
                else
                {
                    f.value = 0.0f;
                    f.valueText = juce::String("INHERIT (") + pitchClassName(parent.root) + ")";
                    const int parentIdx = fifthsIndexOfRootPc(parent.root) + 1;
                    f.marks[0] = ReferenceMark{ true, static_cast<float>(parentIdx) / 12.0f,
                                                static_cast<juce::uint32>(scopeColour), 1.0f };
                }
            }
            else
            {
                f.minValue = 0.0f; f.maxValue = 11.0f;
                f.value = static_cast<float>(fifthsIndexOfRootPc(shown.root));
                f.valueText = pitchClassName(shown.root);
            }
        }

        // Field 1 — Brightness (mode). Dial ascends Locrian -> Lydian (darker->brighter).
        {
            auto& f = result[1];
            f.active = true; f.label = "Bright"; f.stepped = true; f.writable = true;
            f.ringMode = RingMode::Dot; f.hasOverride = hasOverride;
            f.minValue = 0.0f; f.maxValue = 6.0f;
            f.value = static_cast<float>(shown.brightness + 6);   // -6..0 -> 0..6
            f.valueText = modeName(shown.brightness);
        }

        // Fields 2..7 — the functional modifiers as compatibility-gated toggles.
        for (int i = 0; i < 6; ++i)
        {
            auto& f = result[static_cast<std::size_t>(2 + i)];
            const NamedModifier m = kKeyMods[i].mod;
            const bool on = keyHasModifier(shown, m);
            const bool compat = on || isCompatible(shown, m);
            f.active = true;
            f.label = kKeyMods[i].label;
            f.stepped = true;
            f.writable = compat;          // incompatible modifiers can't be turned on
            f.hasOverride = on;
            f.ringMode = RingMode::Dot;
            f.minValue = 0.0f; f.maxValue = 1.0f;
            f.value = on ? 1.0f : 0.0f;
            if (on)
                f.valueText = juce::String(degreeNameOf(shown, m));   // e.g. "7", "#5", "b5"
            else
                f.valueText = compat ? juce::String("--") : juce::String("n/a");
        }

        return result;
    }

    static std::array<MetaFieldView, 8> buildTempoBand(int tpScope, LockstepProcessor& proc)
    {
        std::array<MetaFieldView, 8> result{};

        // BPM range constants.
        constexpr float kMinBpm = 20.0f;  // minimum real BPM value (above INHERIT floor)
        constexpr float kMaxBpm = 300.0f;

        // Global root BPM (standalone localBpm or host bpm when host-synced).
        const double globalBpm = proc.clock().bpm();
        // Resolved Song-level BPM (global × song ratio).
        const auto& sg = proc.song();
        const double songBpm = globalBpm * (sg.hasTempo ? sg.tempoRatio : 1.0);
        // Effective BPM at current scene.
        const double effectiveBpm = proc.effectiveBpm();

        // Parent BPM for the current scope (what this scope inherits from).
        const double parentBpm = [&]() -> double {
            if (tpScope == 3) return songBpm;   // Scene inherits from Song
            if (tpScope == 2) return globalBpm; // Song inherits from global
            return globalBpm;                   // global has no parent
        }();

        // Current override BPM at this scope (what's stored, in absolute terms).
        const double overrideBpm = [&]() -> double {
            const auto& sc = proc.section();
            if (tpScope == 3 && sc.hasTempo) return parentBpm * sc.tempoRatio;
            if (tpScope == 2 && sg.hasTempo) return globalBpm * sg.tempoRatio;
            return (tpScope == 1) ? globalBpm : parentBpm; // global or no override
        }();

        const bool hasOverride = [&]() -> bool {
            if (tpScope == 3) return proc.section().hasTempo;
            if (tpScope == 2) return sg.hasTempo;
            return false; // global always owns its value
        }();

        // INHERIT floor: Song/Scene scopes expose an extra "floor" position below kMinBpm.
        // The range becomes [0, kMaxBpm]: 0 = INHERIT (clears override), kMinBpm..kMaxBpm = BPM.
        // Dial to 0 → label shows "INHERIT (<parent bpm>)", clears hasTempo on write.
        // Set/global scope uses the normal [kMinBpm, kMaxBpm] range (no parent to inherit from).
        const bool hasInherit = (tpScope == 2 || tpScope == 3);
        const float rangeMin = hasInherit ? 0.0f : kMinBpm;

        // Display value: 0 = INHERIT floor (no override), else the override BPM.
        const float displayBpm = hasInherit && !hasOverride
            ? 0.0f
            : std::clamp(static_cast<float>(overrideBpm), kMinBpm, kMaxBpm);

        const float scopeColourF = (tpScope == 3)
            ? static_cast<float>(theme::kScopeScene)
            : static_cast<float>(theme::kScopeSong);

        const char* scopeLabel = (tpScope == 1) ? "Set" : (tpScope == 2) ? "Song" : "Scene";

        auto& f = result[0];
        f.active = true;
        f.label = scopeLabel;
        f.minValue = rangeMin;
        f.maxValue = kMaxBpm;
        f.value = displayBpm;
        f.stepped = false;
        f.writable = true;
        f.hasOverride = hasOverride;
        f.ringMode = RingMode::UnipolarFill;

        // Value text: INHERIT (floor) or absolute BPM.
        if (hasInherit && !hasOverride)
        {
            f.valueText = juce::String("INHERIT (")
                          + juce::String(static_cast<int>(std::round(parentBpm))) + ")";
        }
        else
        {
            f.valueText = juce::String(static_cast<int>(std::round(displayBpm))) + " BPM";
        }

        // Reference mark: parent BPM position (scope-coloured) in the BPM range.
        if (hasInherit)
        {
            const float parentNorm = (kMaxBpm > kMinBpm)
                ? (static_cast<float>(parentBpm) - kMinBpm) / (kMaxBpm - kMinBpm) : 0.0f;
            f.marks[0] = ReferenceMark{ true, std::clamp(parentNorm, 0.0f, 1.0f),
                                        static_cast<juce::uint32>(scopeColourF), 1.0f };
        }

        (void)effectiveBpm;  // header readout is the global-effective indicator
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
        const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
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
            v.label    = "Trk " + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 100.0f;
            v.value    = perTrack * 100.0f;
            v.stepped  = false;
            v.writable = !exempt;
            v.hasOverride = !exempt && (perTrack != 1.0f || master != 0.0f);
            v.valueText   = exempt ? "EXEMPT"
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
        const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;

        static const char* musLabels[] = { "UNIFM", "MIX", "METRIC" };

        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int musVal = static_cast<int>(kit.densityMusicality);
            const bool exempt = (kit.densitySelection == Density::DensitySelection::Exempt);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active   = true;
            v.label    = "Trk " + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 2.0f;
            v.value    = static_cast<float>(musVal);
            v.stepped  = true;
            v.writable = !exempt;
            v.hasOverride = !exempt && (kit.densityMusicality != Density::Musicality::Mixed);
            v.valueText   = exempt ? "EXEMPT" : juce::String(musLabels[musVal]);
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
        const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;

        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int selVal = static_cast<int>(kit.densitySelection);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active   = true;
            v.label    = "Trk " + juce::String(trackIdx + 1);
            v.minValue = 0.0f;
            v.maxValue = 2.0f;
            v.value    = static_cast<float>(selVal);
            v.stepped  = true;
            v.writable = true;
            v.hasOverride = (kit.densitySelection != Density::DensitySelection::Scrub);
            switch (kit.densitySelection)
            {
                case Density::DensitySelection::Scrub:  v.valueText = "SCRUB"; break;
                case Density::DensitySelection::Reroll: v.valueText = "RE-ROLL"; break;
                case Density::DensitySelection::Exempt: v.valueText = "EXEMPT"; break;
            }
            v.ringMode = RingMode::Dot;
        }
        return result;
    }

    // =========================================================================
    // Velocity overlay bands (4 sub-pages, per-track paginated like density).
    // AMP section 3 sticky mode: double-tap AMP to enter, re-press to cycle.
    // =========================================================================

    static std::array<MetaFieldView, 8> buildVelBand(LockstepProcessor& proc, const UiState& ui,
                                                      int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;
        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active      = true;
            v.label       = "Trk " + juce::String(trackIdx + 1);
            v.minValue    = 0.0f;
            v.maxValue    = 100.0f;
            v.value       = kit.velDepth * 100.0f;
            v.stepped     = false;
            v.writable    = true;
            v.hasOverride = (kit.velDepth != 0.6f);
            v.valueText   = juce::String(juce::roundToInt(kit.velDepth * 100.0f));
            v.ringMode    = RingMode::UnipolarFill;
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildVelCenterBand(LockstepProcessor& proc,
                                                            const UiState& ui, int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;
        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active      = true;
            v.label       = "Trk " + juce::String(trackIdx + 1);
            v.minValue    = 1.0f;
            v.maxValue    = 127.0f;
            v.value       = static_cast<float>(kit.velCenter);
            v.stepped     = false;
            v.writable    = true;
            v.hasOverride = (kit.velCenter != 90);
            v.valueText   = juce::String(kit.velCenter);
            v.ringMode    = RingMode::UnipolarFill;
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildVelModeBand(LockstepProcessor& proc,
                                                          const UiState& ui, int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;
        static const char* modeLabels[] = { "OFF", "BAR", "PHRASE" };
        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int modeVal = static_cast<int>(kit.velMode);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active      = true;
            v.label       = "Trk " + juce::String(trackIdx + 1);
            v.minValue    = 0.0f;
            v.maxValue    = 2.0f;
            v.value       = static_cast<float>(modeVal);
            v.stepped     = true;
            v.writable    = true;
            v.hasOverride = (kit.velMode != VelMode::Off);
            v.valueText   = juce::String(modeLabels[modeVal]);
            v.ringMode    = RingMode::Dot;
        }
        return result;
    }

    static std::array<MetaFieldView, 8> buildVelBlendBand(LockstepProcessor& proc,
                                                           const UiState& ui, int focusedTrack)
    {
        std::array<MetaFieldView, 8> result{};
        const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((focusedTrack >= 8) ? 1 : 0);
        const int pageOffset = page * 8;
        static const char* blendLabels[] = { "REPLACE", "MIX" };
        for (int i = 0; i < 8; ++i)
        {
            const int trackIdx = pageOffset + i;
            if (trackIdx >= static_cast<int>(kNumTracks)) { break; }
            const auto& kit = proc.kit(trackIdx);
            const int blendVal = static_cast<int>(kit.velBlend);
            auto& v = result[static_cast<std::size_t>(i)];
            v.active      = true;
            v.label       = "Trk " + juce::String(trackIdx + 1);
            v.minValue    = 0.0f;
            v.maxValue    = 1.0f;
            v.value       = static_cast<float>(blendVal);
            v.stepped     = true;
            v.writable    = true;
            v.hasOverride = (kit.velBlend != VelBlend::Replace);
            v.valueText   = juce::String(blendLabels[blendVal]);
            v.ringMode    = RingMode::Dot;
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
        if (band == MetaBand::Vel)
            return buildVelBand(proc, ui, track);
        if (band == MetaBand::VelCenter)
            return buildVelCenterBand(proc, ui, track);
        if (band == MetaBand::VelMode)
            return buildVelModeBand(proc, ui, track);
        if (band == MetaBand::VelBlend)
            return buildVelBlendBand(proc, ui, track);
        if (band == MetaBand::Key)
            return buildKeyBand(timeScopeFor(ui), proc);
        if (band == MetaBand::Time)
        {
            // Field 0 = Tempo, Field 1 = Time Sig, Field 2 = CLICK (metronome, 9.10).
            const int scope = timeScopeFor(ui);
            auto result = buildTempoBand(scope, proc);
            const auto tsResult = buildTimeSigBand(scope, proc);
            result[1] = tsResult[0];  // copy the time-sig field into slot 1
            result[1].label = "Sig";  // distinguish from the tempo field label
            // CLICK field — metronome on/off toggle.
            auto& click = result[2];
            click.active = true;
            click.label = "CLICK";
            click.minValue = 0.0f;
            click.maxValue = 1.0f;
            click.stepped = true;
            click.writable = true;
            click.ringMode = RingMode::Dot;
            const bool metOn = proc.clock().isMetronomeEnabled();
            click.value = metOn ? 1.0f : 0.0f;
            click.valueText = metOn ? juce::String("ON") : juce::String("OFF");
            return result;
        }
        if (band == MetaBand::StepPosition)
            return buildStepPositionBand(proc, ui);
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

        // 9.14: Step-Position move panel — field 0 = position (sequential bubble-
        // swap, mirroring the hold-step + ←/→ key gesture), field 1 = micro-time.
        // Operates on the moved step (ui.pLockClearTrack / pLockClearStep), updating
        // pLockClearStep so the panel + highlight follow it.
        if (band == MetaBand::StepPosition)
        {
            const int mt = ui.pLockClearTrack;
            if (mt < 0 || mt >= static_cast<int>(kNumTracks) || ui.pLockClearStep < 0)
                return;
            auto& trk = proc.sequence().tracks[static_cast<std::size_t>(mt)];
            const int len = std::max(1, trk.length);
            if (field == 0)
            {
                const int target = std::clamp(
                    static_cast<int>(std::round(value)) - 1, 0, len - 1);
                // Swap-with-destination from the move anchor: the step lands at the
                // target and only the destination cell trades back — cells in between
                // stay on the beat (DESIGN; chosen over sequential reorder / overwrite).
                proc.relocateStepSwap(mt, ui.stepMoveAnchor, ui.pLockClearStep, target);
                ui.pLockClearStep = target;
            }
            else if (field == 1)
            {
                trk.steps[static_cast<std::size_t>(ui.pLockClearStep)].microOffset =
                    std::clamp(value, -0.5f, 0.5f);
            }
            ctx.markParamWritten();
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
                        case 5: {  // RTG — authored ratchet rate (9.10)
                            const int ri = std::clamp(static_cast<int>(value), 0, 8);
                            if (ri == 0)
                            {
                                trig.hasRetrig = false;
                            }
                            else
                            {
                                trig.hasRetrig = true;
                                trig.retrigRate = kRetrigRates[static_cast<std::size_t>(ri - 1)];
                            }
                            break;
                        }
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
                proc.setTrackSubdivision(track, newIdx);
                break;
            }

            case MetaBand::PhraseLen: {
                if (field != 0) return;
                if (ctx.isActiveForEditing() && ctx.heldTrackIndex() == track)
                    ctx.markParamWritten();
                // Route through setTrackLength so the working Track.length and the
                // APVTS param stay in sync. Writing the param alone left
                // tracks[].length stale, so the Euclid generator (which reads the
                // working struct) capped pulses at the old 16-step length while
                // playback used the real, longer param length.
                proc.setTrackLength(track, static_cast<int>(std::round(value)));
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
                    const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((track >= 8) ? 1 : 0);
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
                    const int page = (ui.overlay == Overlay::Density) ? ui.densityBank : ((track >= 8) ? 1 : 0);
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

            case MetaBand::Vel: {
                // Depth sub-page: value is 0-100 (normalised to 0..1).
                if (field >= 0 && field < 8)
                {
                    const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                        proc.kit(trackIdx).velDepth = juce::jlimit(0.0f, 1.0f, value / 100.0f);
                }
                break;
            }

            case MetaBand::VelCenter: {
                // Center sub-page: value is 1-127.
                if (field >= 0 && field < 8)
                {
                    const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                        proc.kit(trackIdx).velCenter = juce::jlimit(1, 127, juce::roundToInt(value));
                }
                break;
            }

            case MetaBand::VelMode: {
                // Mode sub-page: 0 = Off, 1 = Bar, 2 = Phrase.
                if (field >= 0 && field < 8)
                {
                    const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                        proc.kit(trackIdx).velMode =
                            static_cast<VelMode>(juce::jlimit(0, 2, juce::roundToInt(value)));
                }
                break;
            }

            case MetaBand::VelBlend: {
                // Blend sub-page: 0 = Replace, 1 = Mix.
                if (field >= 0 && field < 8)
                {
                    const int page = (ui.overlay == Overlay::Vel) ? ui.velBank : ((track >= 8) ? 1 : 0);
                    const int trackIdx = page * 8 + field;
                    if (trackIdx < static_cast<int>(kNumTracks))
                        proc.kit(trackIdx).velBlend =
                            static_cast<VelBlend>(juce::jlimit(0, 1, juce::roundToInt(value)));
                }
                break;
            }

            case MetaBand::Time: {
                const int scope = timeScopeFor(ui);

                if (field == 0)  // Tempo
                {
                    constexpr float kMinBpm = 20.0f;
                    constexpr float kMaxBpm = 300.0f;
                    // Values at or below the INHERIT floor (0) clear the override.
                    const bool atInheritFloor = (value < kMinBpm);
                    const double globalBpm = proc.clock().bpm();
                    const auto& sg = proc.song();
                    const double songBpm = globalBpm * (sg.hasTempo ? sg.tempoRatio : 1.0);

                    if (scope == 1)  // global/Set level — no INHERIT floor
                    {
                        const double bpmVal = static_cast<double>(
                            std::clamp(value, kMinBpm, kMaxBpm));
                        proc.clock().setLocalBpm(bpmVal);
                    }
                    else if (scope == 2)  // Song level
                    {
                        if (atInheritFloor)
                        {
                            proc.song().hasTempo = false;
                        }
                        else if (globalBpm > 0.0)
                        {
                            const double bpmVal = static_cast<double>(
                                std::clamp(value, kMinBpm, kMaxBpm));
                            proc.song().hasTempo = true;
                            proc.song().tempoRatio = bpmVal / globalBpm;
                        }
                    }
                    else  // Scene level (scope == 3)
                    {
                        if (atInheritFloor)
                        {
                            proc.section().hasTempo = false;
                        }
                        else
                        {
                            const double parentBpm = songBpm;
                            if (parentBpm > 0.0)
                            {
                                const double bpmVal = static_cast<double>(
                                    std::clamp(value, kMinBpm, kMaxBpm));
                                proc.section().hasTempo = true;
                                proc.section().tempoRatio = bpmVal / parentBpm;
                            }
                        }
                    }
                }
                else if (field == 1)  // Time Sig
                {
                    const bool hasInherit = (scope == 2 || scope == 3);
                    const int maxIdx = kNumTimeSigs - 1 + (hasInherit ? 1 : 0);
                    const int idx = std::clamp(juce::roundToInt(value), 0, maxIdx);

                    if (scope == 1)  // Set level
                    {
                        const int i = std::clamp(idx, 0, kNumTimeSigs - 1);
                        proc.project().defaultTimeSig.numerator = kTimeSigs[i].num;
                        proc.project().defaultTimeSig.denominator = kTimeSigs[i].den;
                    }
                    else if (scope == 2)  // Song level
                    {
                        if (idx == 0)
                        {
                            proc.song().hasTimeSig = false;
                        }
                        else
                        {
                            const int i = std::clamp(idx - 1, 0, kNumTimeSigs - 1);
                            proc.song().hasTimeSig = true;
                            proc.song().timeSig.numerator = kTimeSigs[i].num;
                            proc.song().timeSig.denominator = kTimeSigs[i].den;
                        }
                    }
                    else  // Scene level (scope == 3 or any other)
                    {
                        auto& scene = proc.section();
                        if (idx == 0)
                        {
                            scene.hasTimeSig = false;
                        }
                        else
                        {
                            const int i = std::clamp(idx - 1, 0, kNumTimeSigs - 1);
                            scene.hasTimeSig = true;
                            scene.coreTime.numerator = kTimeSigs[i].num;
                            scene.coreTime.denominator = kTimeSigs[i].den;
                        }
                    }
                }
                else if (field == 2)  // CLICK — metronome on/off (9.10)
                {
                    proc.clock().setMetronomeEnabled(value >= 0.5f);
                }
                break;
            }

            case MetaBand::Key: {
                const int scope = timeScopeFor(ui);

                // Resolve a writable KeySig at the scope. Editing at Song/Scene
                // enables the override (seeded from the inherited key); dialing
                // Root to the INHERIT floor (index 0) clears it.
                auto seedAndGet = [&]() -> KeySig& {
                    if (scope == 3)
                    {
                        auto& sc = proc.section();
                        if (!sc.hasKeySig) { sc.coreKeySig = proc.effectiveKeySig(); sc.hasKeySig = true; }
                        return sc.coreKeySig;
                    }
                    if (scope == 2)
                    {
                        auto& sg = proc.song();
                        if (!sg.hasKeySig) { sg.keySig = proc.effectiveKeySig(); sg.hasKeySig = true; }
                        return sg.keySig;
                    }
                    return proc.project().defaultKeySig;
                };

                if (field == 0)  // Root (INHERIT floor at 0 for Song/Scene)
                {
                    const bool hasInherit = (scope == 2 || scope == 3);
                    if (hasInherit && juce::roundToInt(value) <= 0)
                    {
                        if (scope == 3) proc.section().hasKeySig = false;
                        else            proc.song().hasKeySig = false;
                    }
                    else
                    {
                        const int idx = hasInherit ? (juce::roundToInt(value) - 1)
                                                   : juce::roundToInt(value);
                        seedAndGet().root = static_cast<uint8_t>(
                            rootPcAtFifthsIndex(std::clamp(idx, 0, 11)));
                    }
                }
                else if (field == 1)  // Brightness (0..6 -> -6..0)
                {
                    const int b = std::clamp(juce::roundToInt(value), 0, 6) - 6;
                    seedAndGet().brightness = static_cast<int8_t>(b);
                }
                else if (field >= 2 && field <= 7)  // modifier toggles
                {
                    const NamedModifier m = kKeyMods[field - 2].mod;
                    const bool turnOn = value >= 0.5f;
                    KeySig& k = seedAndGet();
                    const bool isOn = keyHasModifier(k, m);
                    if (turnOn && !isOn)
                    {
                        if (isCompatible(k, m)) k.modifiers.push_back(m);
                    }
                    else if (!turnOn && isOn)
                    {
                        std::vector<NamedModifier> kept;
                        for (NamedModifier x : k.modifiers)
                            if (x != m) kept.push_back(x);
                        k.modifiers = kept;
                    }
                }
                break;
            }

            default: break;
        }
    }

    juce::String bandTitle(MetaBand band)
    {
        switch (band)
        {
            case MetaBand::None:           return {};
            case MetaBand::Cond:           return "COND";
            case MetaBand::Trig:           return "TRIG";
            case MetaBand::Divider:        return "DIVIDER";
            case MetaBand::PhraseLen:      return "PHRASE LEN";
            case MetaBand::Global:         return "GLOBAL";
            case MetaBand::Swing:          return "SWING";
            case MetaBand::Density:        return "DENSITY";
            case MetaBand::DensityMode:    return "DENSITY / MODE";
            case MetaBand::DensitySelection: return "DENSITY / SEL";
            case MetaBand::MasterFx:       return "MASTER FX";
            case MetaBand::Euclidean:      return "EUCLID";
            case MetaBand::Transport:      return "TRANSPORT";
            case MetaBand::Vel:            return "VEL";
            case MetaBand::VelCenter:      return "VEL / CENTER";
            case MetaBand::VelMode:        return "VEL / MODE";
            case MetaBand::VelBlend:       return "VEL / BLEND";
            case MetaBand::Time:           return "TIME";
            case MetaBand::Key:            return "KEY";
            case MetaBand::StepPosition:   return "MOVE";
            default:                       return {};
        }
    }

}  // namespace lockstep
