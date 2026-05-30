#include "SurfaceModel.h"
#include "UITheme.h"
#include "KeyLabel.h"
#include "ScopedSectionMatrix.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../io/PressTracker.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/TrackInputMode.h"
#include <algorithm>
#include <set>

namespace lockstep
{
    using namespace theme;

    // =========================================================================
    // compatColour — fallback ARGB for any CellState token
    // =========================================================================

    uint32_t compatColour(CellState state, uint32_t fallback) noexcept
    {
        switch (state)
        {
            case CellState::Resting:            return kStepInactive;
            case CellState::Pressed:            return 0xFFFFFFFFu;
            case CellState::ModeActive:         return kScopeStep;
            case CellState::FuncHeld:           return kFuncActive;
            case CellState::Disabled:           return 0xFF1A1A1Au;
            case CellState::StepEmpty:          return kStepInactive;
            case CellState::StepTrigCertain:    return kStepActive;
            case CellState::StepTrigProbable:   return kStepActive;
            case CellState::StepTrigSuppressed: return 0xFF304838u;
            case CellState::StepFillAdd:        return kStepFillAdd;
            case CellState::StepFillSuppress:   return kStepFillSuppress;
            case CellState::StepOutOfRange:     return kStepOutRange;
            case CellState::StepPlayhead:       return kStepPlayhead;
            case CellState::StepHeld:           return kStepHeld;
            case CellState::SelectorCurrent:    return 0xFFFFFFFFu;
            case CellState::SelectorOccupied:   return kScopeStep;
            case CellState::SelectorEmpty:      return 0xFF404040u;
            case CellState::SelectorOutRange:   return kStepOutRange;
            case CellState::SelectorNext:       return kScopePattern;
            case CellState::SelectorChain:      return kScopePattern;
            case CellState::MuteMuted:          return kScopeMute;
            case CellState::MuteAudible:        return kStepInactive;
            case CellState::MachineCurrent:     return 0xFFFFFFFFu;
            case CellState::MachineAvailable:   return kScopeMachine;
            case CellState::MachineUnavailable: return kStepOutRange;
            case CellState::NoteEditActive:     return kScopeNoteEdit;
            case CellState::NoteEditStaged:     return 0xFFDC643Cu;
            case CellState::NoteEditOther:      return kScopeNoteEdit;
            case CellState::NoteEditResting:    return kStepOutRange;
            case CellState::ChromaticWhite:     return kScopeTrack;
            case CellState::ChromaticBlack:     return kScopeTrack;
            case CellState::LevelsCell:         return 0xFF204060u;
            default: return fallback;
        }
    }

    // =========================================================================
    // SurfaceModel::byButton — reverse lookup
    // =========================================================================

    const SurfaceCell* SurfaceModel::byButton(ControllerButton btn, int idx) const noexcept
    {
        switch (btn)
        {
            case ControllerButton::Func:         return &modifiers[0];
            case ControllerButton::TrackScope:   return &modifiers[1];
            case ControllerButton::PatternScope: return &modifiers[2];
            case ControllerButton::PartScope:    return &modifiers[3];
            case ControllerButton::SceneScope:   return &modifiers[4];
            case ControllerButton::MasterScope:  return &modifiers[5];
            case ControllerButton::MuteScope:    return &modifiers[6];
            case ControllerButton::FillScope:    return &modifiers[7];
            case ControllerButton::TapTempo:     return &tap;
            case ControllerButton::NavUp:        return &navUp;
            case ControllerButton::NavLeft:      return &functionRow[2];
            case ControllerButton::NavDown:      return &functionRow[3];
            case ControllerButton::NavRight:     return &functionRow[4];
            case ControllerButton::VerbYes:      return &functionRow[5];
            case ControllerButton::VerbRecord:   return &functionRow[6];
            case ControllerButton::VerbPlay:     return &functionRow[7];
            case ControllerButton::VerbStop:     return &functionRow[8];
            case ControllerButton::VerbNo:       return &functionRow[9];
            case ControllerButton::Section:
                if (idx >= 0 && idx < 6) return &section[static_cast<std::size_t>(idx)];
                return nullptr;
            case ControllerButton::Step:
                if (idx >= 0 && idx < 16) return &step[static_cast<std::size_t>(idx)];
                return nullptr;
            default: return nullptr;
        }
    }

    // =========================================================================
    // buildSurfaceModel — implementation
    // =========================================================================

    // Probability preview helper — mirrors computePagePreview in KeyboardArea.cpp.
    // Returns per-step firing probability for the current page.
    static std::array<float, 16>
    stepPagePreview(const Track& track, int trackLen, std::int64_t loopBase,
                    int pageBase, bool fillActive) noexcept
    {
        std::array<float, 16> out{};
        const int limit = std::min(pageBase + 16, trackLen);
        float prevProb = 0.5f;

        for (int i = 0; i < limit; ++i)
        {
            const auto& stp = track.steps[static_cast<std::size_t>(i)];
            float prob = 0.0f;
            const bool fires = [&]() -> bool {
                if (fillActive)
                {
                    if (stp.fillTrigState == FillTrigState::On)  return true;
                    if (stp.fillTrigState == FillTrigState::Off) return false;
                }
                return stp.trig;
            }();
            if (fires)
            {
                const auto& cond = stp.condition.isTrivial() ? track.baseCond : stp.condition;
                bool iterPass = true;
                if (cond.iterDenominator > 1)
                {
                    const auto len   = static_cast<std::int64_t>(std::max(trackLen, 1));
                    const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
                    const auto iter  = (loopBase + static_cast<std::int64_t>(i)) / len;
                    iterPass = (iter % denom == static_cast<std::int64_t>(cond.iterNumerator) - 1);
                }
                if (iterPass)
                {
                    prob = std::clamp(static_cast<float>(cond.probabilityPercent) / 100.0f, 0.0f, 1.0f);
                    if      (cond.prevDependency == 1) prob *= prevProb;
                    else if (cond.prevDependency == 2) prob *= (1.0f - prevProb);
                }
            }
            prevProb = prob;
            if (i >= pageBase)
                out[static_cast<std::size_t>(i - pageBase)] = prob;
        }
        return out;
    }

    SurfaceModel buildSurfaceModel(const UiState&      ui,
                                   const EditContext&  ec,
                                   const PressTracker* press,
                                   LockstepProcessor&  proc,
                                   int                 activeTrack,
                                   int                 stepPage,
                                   GridDisplayMode     /*displayMode*/)
    {
        SurfaceModel model;

        // Press-state helpers
        auto keyDown = [&](int rawCode) -> bool
        {
            return press ? press->isKeyHeld(rawCode)
                         : juce::KeyPress::isKeyCurrentlyDown(rawCode);
        };
        auto mouseDown = [&](ControllerButton btn, int idx = -1) -> bool
        {
            return press && press->isMouseHeld(btn, idx);
        };
        auto physPressed = [&](int rawCode, ControllerButton btn, int idx = -1) -> bool
        {
            return keyDown(rawCode) || mouseDown(btn, idx);
        };

        // Compound-chord overlay condition (MHY cross-column pair)
        const bool col1any = ui.patternScopeHeld || ui.sceneHeld || ui.muteHeld;
        const bool col2any = ui.trackHeld || ui.partHeld || ui.masterHeld || ui.fillHeld;
        const bool hasCompound = (ui.funcHeld && (col1any || col2any))
                               || (col1any && col2any);
        static constexpr uint32_t kAmberStrip = 0xFFD0A020u;

        // =====================================================================
        // Modifier helper: fill a step-row or number-row modifier cell.
        // scopeCol = the canonical scope colour (ModeActive bg + latch pip).
        // dimCol   = the dark inactive background (kScopeXxxDim).
        // =====================================================================
        auto fillModifier = [&](SurfaceCell& c, ControllerButton btn,
                                int rawCode, const char* label,
                                bool isHeld, bool latchActive,
                                uint32_t scopeCol, uint32_t dimCol,
                                bool stripOn) -> void
        {
            c.button  = btn;
            c.index   = -1;
            c.pressed  = physPressed(rawCode, btn);
            c.primary  = label;
            c.funcHint = {};
            c.strip.present = stripOn;
            c.strip.colour  = kAmberStrip;
            if (latchActive && scopeCol != 0)
            {
                c.pip.present = true;
                c.pip.colour  = scopeCol;
            }
            if (c.pressed)      c.base = CellState::Pressed;
            else if (isHeld)    c.base = CellState::ModeActive;
            else                c.base = CellState::Resting;
            c.baseColour = (c.base == CellState::ModeActive) ? scopeCol : dimCol;
            jassert(!c.primary.isEmpty());  // invariant: modifier cells always have a label
        };

        // =====================================================================
        // modifiers[0] — Func (key 1), amber
        // =====================================================================
        {
            SurfaceCell& c = model.modifiers[0];
            c.button  = ControllerButton::Func;
            c.keyHint = "1";
            c.pressed  = physPressed('1', ControllerButton::Func);
            c.primary  = "FUNC";
            c.funcHint = {};
            c.strip.present = hasCompound && ui.funcHeld;
            c.strip.colour  = kAmberStrip;
            c.base = c.pressed ? CellState::Pressed
                   : ui.funcHeld ? CellState::ModeActive
                   : CellState::Resting;
            c.baseColour = (c.base == CellState::ModeActive) ? kFuncActive : kFuncInactive;
        }

        // =====================================================================
        // modifiers[1] — Track (key 2), col-2 row-0
        // =====================================================================
        {
            SurfaceCell& c = model.modifiers[1];
            c.keyHint = "2";
            fillModifier(c, ControllerButton::TrackScope, '2', "TRACK",
                         ui.trackHeld, ui.latch.track,
                         kScopeTrack, kScopeTrackDim,
                         hasCompound && ui.trackHeld);
        }

        // =====================================================================
        // modifiers[4..7] — step-row modifier columns (A/S/Z/X)
        // Built here; consumed by paintStepRows from Slice 3-4 onwards.
        // =====================================================================
        {
            SurfaceCell& c = model.modifiers[4];
            c.keyHint = "A";
            fillModifier(c, ControllerButton::SceneScope, 'A', "SCENE",
                         ui.sceneHeld, ui.latch.scene,
                         kScopeScene, kScopeSceneDim,
                         hasCompound && ui.sceneHeld);
        }
        {
            SurfaceCell& c = model.modifiers[5];
            c.keyHint = "S";
            fillModifier(c, ControllerButton::MasterScope, 'S', "MASTER",
                         ui.masterHeld, ui.latch.master,
                         kScopeMaster, kScopeMasterDim,
                         hasCompound && ui.masterHeld);
        }
        {
            SurfaceCell& c = model.modifiers[6];
            c.keyHint = "Z";
            // Func+Mute activates pattern-mute (kScopePMute); latch pip stays kScopeMute.
            const uint32_t activeMuteCol = ui.funcHeld ? kScopePMute : kScopeMute;
            fillModifier(c, ControllerButton::MuteScope, 'Z', "MUTE",
                         ui.muteHeld, ui.latch.mute,
                         activeMuteCol, kScopeMuteDim,
                         hasCompound && ui.muteHeld);
            // Hint band = Func-layer only; promoted when Func held.
            if (ui.funcHeld) c.primary = "PMUTE";
            else             c.funcHint = "PMUTE";
            if (ui.latch.mute) c.pip.colour = kScopeMute;  // pip = base colour, not pattern-mute
        }
        {
            SurfaceCell& c = model.modifiers[7];
            c.keyHint = "X";
            fillModifier(c, ControllerButton::FillScope, 'X', "FILL",
                         ui.fillHeld, ui.latch.fill,
                         kScopeFill, kScopeFillDim,
                         hasCompound && ui.fillHeld);
        }

        // =====================================================================
        // TAP (key 3) — number-row utility; Func-variant = MET
        // =====================================================================
        {
            SurfaceCell& c = model.tap;
            c.button  = ControllerButton::TapTempo;
            c.keyHint = "3";
            c.pressed  = physPressed('3', ControllerButton::TapTempo);
            c.base     = c.pressed ? CellState::Pressed : CellState::Resting;
            c.baseColour = kTapActive;
            // Func-hint promotion: when Func held, MET becomes the live function.
            if (ui.funcHeld) { c.primary = "MET";  c.funcHint = {};    }
            else             { c.primary = "TAP";  c.funcHint = "MET"; }
            jassert(!c.primary.isEmpty());
        }

        // =====================================================================
        // NavUp / ^ (key 4) — number-row utility; Func-variant = POOL
        // =====================================================================
        {
            SurfaceCell& c = model.navUp;
            c.button  = ControllerButton::NavUp;
            c.keyHint = "4";
            c.pressed  = physPressed('4', ControllerButton::NavUp);
            c.base     = c.pressed ? CellState::Pressed : CellState::Resting;
            c.baseColour = kNavActive;
            // Func-hint promotion: when Func held, POOL is the live function.
            if (ui.funcHeld) { c.primary = "POOL";                c.funcHint = {};      }
            else             { c.primary = juce::String(u8"↑");  c.funcHint = "POOL"; }
            jassert(!c.primary.isEmpty());
        }

        // =====================================================================
        // section[0..5] — keys 5-0 (TRIG/SRC/FILTER/AMP/MOD/FX)
        // =====================================================================

        using PS = EditMode::PrimaryScope;
        PS sectionScope = PS::None;
        if      (ui.trackHeld)        sectionScope = PS::Track;
        else if (ui.patternScopeHeld) sectionScope = PS::Pattern;
        else if (ui.partHeld)         sectionScope = PS::Part;
        else if (ui.sceneHeld)        sectionScope = PS::Scene;
        else if (ui.masterHeld)       sectionScope = PS::Master;
        const bool isScopedMode = (sectionScope != PS::None);

        static constexpr int kSectionKeyCodes[IMachine::kMaxSections] = {
            '5', '6', '7', '8', '9', '0'
        };
        static constexpr const char* kSectionKeyHints[IMachine::kMaxSections] = {
            "5", "6", "7", "8", "9", "0"
        };
        // Meta-section secondary labels. Empty string = reserved (em-dash rendered by screen).
        static constexpr std::array<const char*, IMachine::kMaxSections> kMetaLabels = {
            "COND", "NOTE", "TRACK", "", "", "GLOBAL"
        };
        auto isReservedMeta = [](int s) -> bool
        {
            return s < 0 || s >= IMachine::kMaxSections
                || kMetaLabels[static_cast<std::size_t>(s)][0] == '\0';
        };
        auto sectionHasMachineSlot = [&](int track, int canonicalIdx) -> bool
        {
            if (proc.section(track, canonicalIdx).firstSlot >= 0)
                return true;
            const int total = proc.numSections(track);
            for (int ext = IMachine::kMaxSections; ext < total; ++ext)
            {
                const auto& info = proc.section(track, ext);
                if (info.parentCanonical == canonicalIdx && info.firstSlot >= 0)
                    return true;
            }
            return false;
        };

        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            SurfaceCell& c = model.section[static_cast<std::size_t>(s)];
            c.button  = ControllerButton::Section;
            c.index   = s;
            c.keyHint = kSectionKeyHints[s];

            const bool machineHasSection = sectionHasMachineSlot(activeTrack, s);
            const char* metaLabel = isReservedMeta(s)
                ? nullptr : kMetaLabels[static_cast<std::size_t>(s)];
            const KeyDef kd {
                KeyRole::SectionKey,
                IMachine::kCanonicalSectionNames[static_cast<std::size_t>(s)],
                (metaLabel != nullptr) ? metaLabel : "",
                s, machineHasSection
            };
            const KeyLabel kl = resolveKeyLabel(kd, ui, ec);

            c.disabled = kl.disabled;
            c.primary  = kl.primary;
            c.pressed  = physPressed(kSectionKeyCodes[s], ControllerButton::Section, s);

            // Hint band = Func-layer only. AMP/MOD have no Func action → no hint.
            // Func-promotion: when Func held, meta label becomes the live primary.
            if (isScopedMode || isReservedMeta(s))
                c.funcHint = {};
            else if (ui.funcHeld)
            {
                c.primary  = kl.hint;   // e.g. TRIG→COND, SRC→NOTE, FILTER→TRACK
                c.funcHint = {};
            }
            else
                c.funcHint = kl.hint;   // dim secondary when Func not held

            const bool isMachPicker   = (sectionScope == PS::Part && s == 1);
            const bool isTrigNoteEdit = (!isScopedMode && ui.funcHeld && s == 0);
            const bool isMasterActive = !isScopedMode && (ui.masterSection == s);
            const bool isTrackActive  = !isScopedMode && (ui.masterSection == -1
                && ui.trackSection[static_cast<std::size_t>(activeTrack)] == s);

            if (c.pressed)
                c.base = CellState::Pressed;
            else if (c.disabled)
                c.base = CellState::Disabled;
            else if (isTrackActive || isMasterActive || isMachPicker)
                c.base = CellState::ModeActive;
            else
                c.base = CellState::Resting;

            // baseColour distinguishes special visual modes for groupForCell()
            if (isMachPicker)
                c.baseColour = kScopeMachine;
            else if (isMasterActive)
                c.baseColour = 0xFF404010u;     // golden — master section active
            else if (isTrigNoteEdit)
                c.baseColour = kScopeNoteEdit;
            else
                c.baseColour = compatColour(c.base, kSecActive);

            // Invariant: non-disabled section keys always resolve to a non-empty primary.
            jassert(c.disabled || !c.primary.isEmpty());
        }

        // =====================================================================
        // functionRow[0..9] — Q-row: Q/W/E/R/T/Y/U/I/O/P
        // =====================================================================

        // FRowDef uses char8_t* so arrow glyphs (←/↓/→) are valid UTF-8.
        struct FRowDef
        {
            int              keyCode;
            const char8_t*   keyHint;
            const char8_t*   natural;
            const char8_t*   funcLayer;   // empty = no Func-layer variant
            ControllerButton button;
            KeyRole          role;
        };

        static const std::array<FRowDef, 10> kFRowDefs = {{
            { 'Q', u8"Q", u8"PAT",   u8"",       ControllerButton::PatternScope, KeyRole::Modifier  },
            { 'W', u8"W", u8"PART",  u8"MACH",   ControllerButton::PartScope,    KeyRole::Modifier  },
            { 'E', u8"E", u8"←",     u8"RST",    ControllerButton::NavLeft,      KeyRole::Nav       },
            { 'R', u8"R", u8"↓",     u8"KEY",    ControllerButton::NavDown,      KeyRole::Nav       },
            { 'T', u8"T", u8"→",     u8"RETRIG", ControllerButton::NavRight,     KeyRole::Nav       },
            { 'Y', u8"Y", u8"YES",   u8"SNAP",   ControllerButton::VerbYes,      KeyRole::VerbYes   },
            { 'U', u8"U", u8"REC",   u8"",       ControllerButton::VerbRecord,   KeyRole::VerbCopy  },
            { 'I', u8"I", u8"PLAY",  u8"",       ControllerButton::VerbPlay,     KeyRole::VerbPaste },
            { 'O', u8"O", u8"PANIC", u8"",        ControllerButton::VerbStop,     KeyRole::VerbClear },
            { 'P', u8"P", u8"NO",    u8"POP",    ControllerButton::VerbNo,       KeyRole::VerbNo    },
        }};

        const bool sectionScopeHeld = ui.trackHeld || ui.patternScopeHeld
                                   || ui.partHeld || ui.sceneHeld || ui.masterHeld;

        for (int i = 0; i < 10; ++i)
        {
            const auto& def = kFRowDefs[static_cast<std::size_t>(i)];
            SurfaceCell& c  = model.functionRow[static_cast<std::size_t>(i)];

            c.button  = def.button;
            c.index   = -1;
            c.keyHint = juce::String(def.keyHint);
            c.pressed = physPressed(def.keyCode, def.button);

            const bool isOverdub  = (def.keyCode == 'U') && proc.clock().isOverdubArmed();
            const bool isArmed    = (def.keyCode == 'U') && proc.clock().isRecordArmed();
            const bool isPlaying  = (def.keyCode == 'I') && proc.clock().inPluginPlaying();
            const bool isPatHeld  = (def.keyCode == 'Q') && ui.patternScopeHeld;
            const bool isPrtHeld  = (def.keyCode == 'W') && ui.partHeld;
            const bool isModeActive = isArmed || isPlaying || isPatHeld || isPrtHeld;

            juce::String displayPrimary { def.natural };
            juce::String displayHint    { def.funcLayer };

            // Four live relabels (MACH / PAUSE / DEL / OD) — override after resolver.
            if (def.keyCode == 'W' && ui.funcPartHeld)
                displayPrimary = "MACH";
            if (def.keyCode == 'I' && isPlaying && !sectionScopeHeld && !ui.stepHeld)
                displayPrimary = "PAUSE";
            if (def.keyCode == 'P' && ui.trackHeld)
                displayPrimary = "DEL";
            if (isOverdub)
                displayPrimary = "OD";

            // Func-hint promotion: when Func held and key has a Func-layer variant,
            // funcLayer IS the live function — show it as primary, clear hint.
            const bool hasFuncLayer = (def.funcLayer[0] != static_cast<char8_t>(0));
            if (ui.funcHeld && hasFuncLayer && !isModeActive)
            {
                displayPrimary = juce::String(def.funcLayer);
                displayHint    = {};
            }

            c.primary  = displayPrimary;
            c.funcHint = displayHint;

            // Compound overlay on Q (Pattern) and W (Part)
            c.strip.present = hasCompound
                && ((def.keyCode == 'Q' && ui.patternScopeHeld)
                 || (def.keyCode == 'W' && ui.partHeld));
            c.strip.colour  = kAmberStrip;

            // Latch pips: Pattern (Q) and Part (W)
            if (def.keyCode == 'Q' && ui.latch.pattern)
                { c.pip.present = true; c.pip.colour = kScopePattern; }
            else if (def.keyCode == 'W' && ui.latch.part)
                { c.pip.present = true; c.pip.colour = kScopePart; }

            // Cell state
            if (c.pressed)         c.base = CellState::Pressed;
            else if (isModeActive) c.base = CellState::ModeActive;
            else                   c.base = CellState::Resting;

            // baseColour for controller feedback and groupForCell()
            if (isOverdub)
                c.baseColour = 0xFFD2821Eu;                                 // amber for OD
            else if (def.keyCode == 'Q')
                c.baseColour = ui.patternScopeHeld ? kScopePattern : kScopePatternDim;
            else if (def.keyCode == 'W')
                c.baseColour = ui.partHeld ? kScopePart : kScopePartDim;
            else
                c.baseColour = compatColour(c.base, 0xFF404040u);

            // Invariant: every function row cell has a non-empty primary label.
            jassert(!c.primary.isEmpty());
        }

        // Mirror Q and W into modifiers[2/3] for byButton() lookup.
        // Screen renders Q/W from functionRow[0/1]; controllers look up via modifiers.
        model.modifiers[2]        = model.functionRow[0];
        model.modifiers[2].button = ControllerButton::PatternScope;
        model.modifiers[3]        = model.functionRow[1];
        model.modifiers[3].button = ControllerButton::PartScope;

        // =====================================================================
        // step[0..15] — step grid cells (Slices 2–5)
        //
        // Priority order mirrors paintStepRows early-returns:
        //   funcPartHeld → noteEdit → pLockClear → Chromatic → Levels
        //   → muteHeld → scope re-skin → normal step grid.
        // =====================================================================
        {
            static constexpr int kStepKeyCodes[16] = {
                'D','F','G','H','J','K','L', 59,
                'C','V','B','N','M', 44, 46, 47
            };
            static constexpr const char* kStepKeyHints[16] = {
                "D","F","G","H","J","K","L",";",
                "C","V","B","N","M",",",".","/"
            };

            // Active track input mode — needed by Chromatic/Levels branches.
            const bool validTrackMode = activeTrack >= 0
                                     && activeTrack < static_cast<int>(kNumTracks);
            const TrackInputMode activeTrackMode = validTrackMode
                ? ui.trackInputMode[static_cast<std::size_t>(activeTrack)]
                : TrackInputMode::Play;

            if (ui.funcPartHeld)
            {
                // Machine picker (MHZ.3.5): cells encode available machine slots.
                const juce::Colour machineTint { kScopeMachine };
                const int numMachines = proc.numAvailableMachines();
                const juce::String activeMachineId = proc.getMachineId(activeTrack);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail = i < numMachines;
                    if (!avail)
                    {
                        c.base      = CellState::MachineUnavailable;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const juce::String machId { proc.availableMachineInfo(i).id };
                        const bool isCur = (machId == activeMachineId);
                        c.base      = isCur ? CellState::MachineCurrent : CellState::MachineAvailable;
                        c.baseColour = isCur
                            ? juce::Colours::white.withAlpha(0.18f).getARGB()
                            : machineTint.withAlpha(0.12f).getARGB();
                    }
                }
            }
            else if (ui.noteEditMode && !ui.noteEditSteps.empty())
            {
                // NoteEdit overlay: cells encode semitone note state for one octave.
                // Cells 0–11 = semitones C–B; 12–15 = dead.
                static constexpr bool kNoteIsBlack[] =
                    { false,true,false,true,false,false,true,false,true,false,true,false };

                const juce::Colour noteTint  { kScopeNoteEdit };
                const juce::Colour stageTint = juce::Colour::fromRGB(220, 100, 60);
                const int octave    = ui.noteEditOctave;
                const int trackIdx  = activeTrack;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    if (i >= 12)
                    {
                        c.base      = CellState::StepOutOfRange;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const int semitone = i;
                    bool curActive = false, curStaged = false, crossOctave = false;

                    for (const int stepIdx : ui.noteEditSteps)
                    {
                        if (stepIdx < 0 || stepIdx >= kMaxStepsPerTrack) continue;
                        const auto& s = proc.sequence()
                            .tracks[static_cast<std::size_t>(trackIdx)]
                            .steps[static_cast<std::size_t>(stepIdx)];
                        const auto* staged = [&]() -> const std::set<int>*
                        {
                            auto it = ui.noteEditStaged.find(stepIdx);
                            return (it != ui.noteEditStaged.end()) ? &it->second : nullptr;
                        }();
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                        {
                            const int noteVal = s.trigOverride.notes[n];
                            if (noteVal % 12 != semitone) continue;
                            if (noteVal / 12 - 1 == octave)
                            {
                                curActive = true;
                                if (staged && staged->count(noteVal) > 0) curStaged = true;
                            }
                            else
                            {
                                crossOctave = true;
                            }
                        }
                    }

                    const bool isBlack = kNoteIsBlack[static_cast<std::size_t>(semitone)];
                    if (curActive && curStaged)
                    {
                        c.base      = CellState::NoteEditStaged;
                        c.baseColour = stageTint.withAlpha(0.12f).getARGB();
                    }
                    else if (curActive)
                    {
                        c.base      = CellState::NoteEditActive;
                        c.baseColour = noteTint.withAlpha(isBlack ? 0.50f : 0.65f).getARGB();
                    }
                    else if (crossOctave)
                    {
                        c.base      = CellState::NoteEditOther;
                        c.baseColour = noteTint.withAlpha(0.12f).getARGB();
                    }
                    else
                    {
                        c.base      = CellState::NoteEditResting;
                        c.baseColour = juce::Colour(isBlack ? 0xff202830u : 0xff2c3540u).getARGB();
                    }
                }
            }
            else if (ui.pLockClearMode
                  && ui.pLockClearTrack == activeTrack
                  && ui.pLockClearStep >= 0)
            {
                // P-Lock clear overlay: cells map to packed P-locked slot list.
                const juce::Colour clearTint { kScopePLock };
                const int targetStep = ui.pLockClearStep;
                const auto& stepData = proc.sequence()
                    .tracks[static_cast<std::size_t>(activeTrack)]
                    .steps[static_cast<std::size_t>(targetStep)];
                const int numSlots = proc.numParams(activeTrack);

                std::vector<int> lockedSlots;
                const auto& tov = stepData.trigOverride;
                if (tov.hasVelocity) lockedSlots.push_back(-2);
                if (tov.hasGate)     lockedSlots.push_back(-3);
                for (int s = 0; s < numSlots; ++s)
                    if (stepData.overrides.has(s))
                        lockedSlots.push_back(s);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool hasPacked = i < static_cast<int>(lockedSlots.size());
                    if (!hasPacked)
                    {
                        c.base      = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const int slotIdx  = lockedSlots[static_cast<std::size_t>(i)];
                        const bool isStaged = ui.pLockClearStaged.count(slotIdx) > 0;
                        c.base      = isStaged ? CellState::SelectorEmpty
                                               : CellState::SelectorOccupied;
                        c.baseColour = isStaged
                            ? clearTint.withAlpha(0.10f).getARGB()
                            : clearTint.withAlpha(0.45f).getARGB();
                    }
                }
            }
            else if (activeTrackMode == TrackInputMode::Chromatic)
            {
                // Chromatic piano overlay: 8 white keys (bottom row) + 5 black + 3 dead (top).
                const juce::Colour whiteKey = juce::Colour(kScopeTrack).withAlpha(0.38f);
                const juce::Colour blackKey = juce::Colour(kScopeTrack).withAlpha(0.16f);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int semitone = kPianoNoteOffset[static_cast<std::size_t>(i)];
                    if (semitone < 0)
                    {
                        c.base      = CellState::StepOutOfRange;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const bool isBlack = (i < 8);  // top row (0-7) = black keys
                        c.base      = isBlack ? CellState::ChromaticBlack : CellState::ChromaticWhite;
                        c.baseColour = c.pressed
                            ? juce::Colours::white.withAlpha(0.70f).getARGB()
                            : (isBlack ? blackKey : whiteKey).getARGB();
                    }
                }
            }
            else if (activeTrackMode == TrackInputMode::Levels)
            {
                // Levels overlay: 16 velocity buckets (1/16..16/16 of 127).
                static const juce::Colour lowCol  { 0xFF204060u };
                static const juce::Colour highCol { 0xFFE07030u };

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const float t      = static_cast<float>(i + 1) / 16.0f;
                    const juce::Colour cellCol = lowCol.interpolatedWith(highCol, t);
                    c.base  = CellState::LevelsCell;
                    c.level = t;  // store gradient position for consumer outline colour
                    c.baseColour = c.pressed
                        ? juce::Colours::white.withAlpha(0.75f).getARGB()
                        : cellCol.withAlpha(0.55f + t * 0.30f).getARGB();
                }
            }
            else if (ui.muteHeld)
            {
                // Mute re-skin (Slice 3): cells encode per-track mute state so
                // paintStepRows can consume a single model path and add press feedback.
                const bool isPatternMute = ui.funcHeld;
                const uint32_t muteCol   = isPatternMute ? kScopePMute : kScopeMute;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail = i < static_cast<int>(kNumTracks);
                    if (!avail)
                    {
                        c.base      = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const bool committed = isPatternMute
                        ? proc.getPatternMute(i) : proc.getGlobalMute(i);
                    const bool pending   = isPatternMute
                        && ui.pendingPatternMuteToggle[static_cast<std::size_t>(i)];
                    const bool muted = committed ^ pending;

                    c.base = muted ? CellState::MuteMuted : CellState::MuteAudible;
                    const juce::Colour muteJCol  { muteCol };
                    const juce::Colour audibleCol = juce::Colour(kStepInactive)
                                                     .interpolatedWith(muteJCol, 0.5f);
                    c.baseColour = muted
                        ? muteJCol.withAlpha(0.80f).getARGB()
                        : audibleCol.getARGB();
                }
            }
            else if (ui.trackHeld || ui.patternScopeHeld || ui.partHeld)
            {
                // Scope re-skin (Slice 4): cells encode track/pattern/part selector state.
                // fill colour + pressed → builder; border/text/badge → inline screen residuals.
                const juce::Colour scopeTint = scopeColourFromState(ui);

                int maxAvail = 0;
                int activeIdx = 0;
                if (ui.trackHeld)
                {
                    maxAvail  = static_cast<int>(kNumTracks);
                    activeIdx = activeTrack;
                }
                else if (ui.patternScopeHeld)
                {
                    maxAvail  = kPatternsPerBank;
                    activeIdx = proc.activePatternIdx();
                }
                else // partHeld
                {
                    maxAvail  = kPartsPerBank;
                    activeIdx = static_cast<int>(proc.activePattern().partRef);
                }

                std::array<int, kPatternsPerBank> chainPos{};
                if (ui.patternScopeHeld)
                {
                    int nextChainPos;
                    if (proc.hasQueuedPattern())
                    {
                        const int qi = proc.queuedPatternPatIdx();
                        if (qi >= 0 && qi < kPatternsPerBank && chainPos[static_cast<std::size_t>(qi)] == 0)
                            chainPos[static_cast<std::size_t>(qi)] = 1;
                        nextChainPos = 2;
                    }
                    else
                    {
                        nextChainPos = 1;
                    }
                    const int chainLen = proc.chainLength();
                    for (int ci = 0; ci < chainLen; ++ci)
                    {
                        const auto [bi, pi] = proc.chainEntry(ci);
                        (void)bi;
                        if (pi >= 0 && pi < kPatternsPerBank && chainPos[static_cast<std::size_t>(pi)] == 0)
                            chainPos[static_cast<std::size_t>(pi)] = nextChainPos + ci;
                    }
                }

                const int bankIdx = proc.activeBankIdx();
                std::array<bool, 16> slotEmpty{};
                for (int i = 0; i < maxAvail; ++i)
                {
                    if (ui.trackHeld)
                        slotEmpty[static_cast<std::size_t>(i)] = proc.isTrackEmpty(i);
                    else if (ui.patternScopeHeld)
                        slotEmpty[static_cast<std::size_t>(i)] = !proc.isPatternInitialised(bankIdx, i);
                    else
                        slotEmpty[static_cast<std::size_t>(i)] = !proc.isPartInitialised(bankIdx, i);
                }

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail   = i < maxAvail;
                    const bool isEmpty = avail && slotEmpty[static_cast<std::size_t>(i)];
                    const bool isCurrent = avail && !isEmpty && (i == activeIdx);
                    const int  cpos = (ui.patternScopeHeld && avail && !isEmpty)
                                      ? chainPos[static_cast<std::size_t>(i)] : 0;
                    const bool isNext  = cpos == 1;
                    const bool isChain = cpos >= 2;

                    // CellState token + fill colour
                    if (!avail)
                    {
                        c.base      = CellState::SelectorOutRange;
                        c.baseColour = scopeTint.withAlpha(0.04f).getARGB();
                    }
                    else if (isEmpty)
                    {
                        c.base      = CellState::SelectorEmpty;
                        c.baseColour = scopeTint.withAlpha(0.09f).getARGB();
                    }
                    else if (isNext)
                    {
                        c.base      = CellState::SelectorNext;
                        c.baseColour = scopeTint.withAlpha(0.80f).getARGB();
                    }
                    else if (isChain)
                    {
                        c.base      = CellState::SelectorChain;
                        c.baseColour = scopeTint.withAlpha(0.42f).getARGB();
                    }
                    else if (isCurrent)
                    {
                        c.base      = CellState::SelectorCurrent;
                        c.baseColour = juce::Colours::white.interpolatedWith(scopeTint, 0.30f).getARGB();
                    }
                    else
                    {
                        c.base      = CellState::SelectorOccupied;
                        c.baseColour = scopeTint.withAlpha(0.18f).getARGB();
                    }

                    // level encodes chain position for badge rendering in paintStepRows
                    c.level = static_cast<float>(cpos);
                }
            }
            else
            {

            // Track length + playhead position
            const bool validTrack = activeTrack >= 0
                                 && activeTrack < static_cast<int>(kNumTracks);
            auto* lenP = validTrack
                ? proc.apvts().getRawParameterValue(ParamIDs::trackLength(activeTrack))
                : nullptr;
            auto* divP = validTrack
                ? proc.apvts().getRawParameterValue(ParamIDs::trackDivider(activeTrack))
                : nullptr;
            const int trackLen  = lenP ? std::max(1, static_cast<int>(lenP->load())) : 16;
            const int div       = divP ? std::max(1, static_cast<int>(divP->load())) : 1;
            const double divPpq = 0.25 * static_cast<double>(div);

            int playheadAbs = -1;
            std::int64_t loopBase = 0;
            if (divPpq > 0.0 && trackLen > 0 && validTrack)
            {
                const auto stepNum = static_cast<std::int64_t>(
                    proc.clock().cumulativePpq() / divPpq);
                playheadAbs = static_cast<int>(stepNum % trackLen);
                loopBase    = (stepNum / static_cast<std::int64_t>(trackLen))
                              * static_cast<std::int64_t>(trackLen);
            }

            const int baseStep   = stepPage * 16;
            const bool fillOn    = proc.fillActive();
            const auto& trk      = proc.sequence().tracks[static_cast<std::size_t>(
                                       validTrack ? activeTrack : 0)];
            const auto  preview  = stepPagePreview(trk, trackLen, loopBase, baseStep, fillOn);
            const auto& ctx      = ec;
            const auto& heldSteps = ctx.heldSteps();

            // Scope body colour: adopt whichever modifier is held (fixes always-green).
            const juce::Colour scopeBodyCol = fillOn
                ? juce::Colour(kScopeFill)
                : scopeColourFromState(ui);
            const juce::Colour inactiveCol(kStepInactive);

            for (int i = 0; i < 16; ++i)
            {
                SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                c.button  = ControllerButton::Step;
                c.index   = i;
                c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                const int absIdx  = baseStep + i;
                const bool inRange = absIdx < trackLen && validTrack;

                if (!inRange)
                {
                    c.base      = CellState::StepOutOfRange;
                    c.baseColour = kStepOutRange;
                    continue;
                }

                const auto& stepRef  = trk.steps[static_cast<std::size_t>(absIdx)];
                const bool hasTrig   = stepRef.trig;
                const bool isHeld    = ctx.heldTrackIndex() == activeTrack
                                    && std::find(heldSteps.begin(), heldSteps.end(), absIdx)
                                       != heldSteps.end();
                const bool hasLock   = !stepRef.overrides.empty();
                const bool hasFillLock = !stepRef.fillOverrides.empty();
                const bool isHead    = (absIdx == playheadAbs);
                const FillTrigState fts = stepRef.fillTrigState;

                const float prob = [&]() -> float
                {
                    if (!hasTrig && fts == FillTrigState::On && !fillOn)
                    {
                        const auto& cond = stepRef.condition.isTrivial()
                                           ? trk.baseCond : stepRef.condition;
                        return std::clamp(
                            static_cast<float>(cond.probabilityPercent) / 100.0f, 0.0f, 1.0f);
                    }
                    return preview[static_cast<std::size_t>(i)];
                }();

                float lerpFrac = 0.0f;
                if (hasTrig)
                {
                    if (fts == FillTrigState::Off)
                        lerpFrac = juce::jlimit(0.05f, 0.33f, 0.05f + prob * 0.28f);
                    else
                        lerpFrac = juce::jlimit(0.15f, 1.0f, 0.15f + prob * 0.85f);
                }
                else if (fts == FillTrigState::On)
                {
                    lerpFrac = juce::jlimit(0.10f, 0.67f, 0.10f + prob * 0.57f);
                }

                c.level      = lerpFrac;
                c.baseColour = inactiveCol.interpolatedWith(scopeBodyCol, lerpFrac).getARGB();

                // CellState token — StepHeld overrides trig state for border rendering;
                // paintStepRows checks stepRef.trig directly for note-count colour.
                if (isHeld)
                    c.base = CellState::StepHeld;
                else if (hasTrig && fts == FillTrigState::Off)
                    c.base = CellState::StepTrigSuppressed;
                else if (hasTrig && prob < 0.999f && fts != FillTrigState::Off)
                    c.base = CellState::StepTrigProbable;
                else if (hasTrig)
                    c.base = CellState::StepTrigCertain;
                else if (fts == FillTrigState::On)
                    c.base = CellState::StepFillAdd;
                else
                    c.base = CellState::StepEmpty;

                // Decorations
                if (isHead)
                {
                    c.border.present = true;
                    c.border.colour  = kStepPlayhead;
                    c.border.token   = CellState::StepPlayhead;
                }
                if (hasLock)
                {
                    c.dot.present = true;
                    c.dot.colour  = kStepPLock;
                }
                // strip: fill-mode border takes priority over fill P-Lock badge
                if (fillOn && fts != FillTrigState::Inherit)
                {
                    c.strip.present = true;
                    c.strip.colour  = (fts == FillTrigState::On) ? kStepFillAdd : kStepFillSuppress;
                }
                else if (hasFillLock)
                {
                    c.strip.present = true;
                    c.strip.colour  = kStepFillPLock;
                }
                if (ctx.isLatched(absIdx))
                {
                    c.pip.present = true;
                    c.pip.colour  = kScopeStep;
                }
            }

            } // end else (normal step grid)
        }

        return model;
    }

}  // namespace lockstep
