#include "SurfaceModel.h"
#include "UITheme.h"
#include "KeyLabel.h"
#include "ScopedSectionMatrix.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../io/PressTracker.h"
#include "../PluginProcessor.h"
#include <algorithm>

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

    SurfaceModel buildSurfaceModel(const UiState&      ui,
                                   const EditContext&  ec,
                                   const PressTracker* press,
                                   LockstepProcessor&  proc,
                                   int                 activeTrack,
                                   int                 /*stepPage*/,
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

            // Secondary: scoped mode suppresses meta; reserved meta gets em-dash.
            if (isScopedMode)
                c.funcHint = {};
            else if (isReservedMeta(s))
                c.funcHint = juce::String::charToString(0x2014);  // em-dash (U+2014)
            else
                c.funcHint = kl.hint;

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
            { 'W', u8"W", u8"PART",  u8"",       ControllerButton::PartScope,    KeyRole::Modifier  },
            { 'E', u8"E", u8"←",     u8"RST",    ControllerButton::NavLeft,      KeyRole::Nav       },
            { 'R', u8"R", u8"↓",     u8"KEY",    ControllerButton::NavDown,      KeyRole::Nav       },
            { 'T', u8"T", u8"→",     u8"RETRIG", ControllerButton::NavRight,     KeyRole::Nav       },
            { 'Y', u8"Y", u8"YES",   u8"SNAP",   ControllerButton::VerbYes,      KeyRole::VerbYes   },
            { 'U', u8"U", u8"REC",   u8"",       ControllerButton::VerbRecord,   KeyRole::VerbCopy  },
            { 'I', u8"I", u8"PLAY",  u8"",       ControllerButton::VerbPlay,     KeyRole::VerbPaste },
            { 'O', u8"O", u8"PANIC", u8"RST",    ControllerButton::VerbStop,     KeyRole::VerbClear },
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

            // Base label and hint via resolveKeyLabel for CPC verb keys.
            juce::String displayPrimary { def.natural };
            juce::String displayHint    { def.funcLayer };

            if (def.role == KeyRole::VerbCopy
             || def.role == KeyRole::VerbPaste
             || def.role == KeyRole::VerbClear)
            {
                const KeyDef kd {
                    def.role,
                    reinterpret_cast<const char*>(def.natural),
                    reinterpret_cast<const char*>(def.funcLayer),
                    -1, true
                };
                const KeyLabel kl = resolveKeyLabel(kd, ui, ec);
                displayPrimary = kl.primary;
                displayHint    = kl.hint;
            }

            // Four live relabels (MACH / PAUSE / DEL / OD) — override after resolver.
            if (def.keyCode == 'W' && ui.funcPartHeld)
                displayPrimary = "MACH";
            if (def.keyCode == 'I' && isPlaying && !sectionScopeHeld && !ui.stepHeld)
                displayPrimary = "PAUSE";
            if (def.keyCode == 'P' && ui.trackHeld)
                displayPrimary = "DEL";
            if (isOverdub)
                displayPrimary = "OD";

            // Func-hint promotion (decision 1): when Func held and key has a true
            // Func-layer variant, the funcLayer IS the live function — show it as primary.
            // CPC keys (funcLayer=="") are not promoted; their always-on hint stays.
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
        }

        // Mirror Q and W into modifiers[2/3] for byButton() lookup.
        // Screen renders Q/W from functionRow[0/1]; controllers look up via modifiers.
        model.modifiers[2]        = model.functionRow[0];
        model.modifiers[2].button = ControllerButton::PatternScope;
        model.modifiers[3]        = model.functionRow[1];
        model.modifiers[3].button = ControllerButton::PartScope;

        return model;
    }

}  // namespace lockstep
