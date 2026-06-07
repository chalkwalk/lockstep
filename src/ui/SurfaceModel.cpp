#include "SurfaceModel.h"
#include "UITheme.h"
#include "KeyLabel.h"
#include "ParamFormat.h"
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
            case CellState::StepTrigSuppressed: return 0xFF3E6B50u;  // lifted desaturated green — separate from empty slate
            case CellState::StepFillAdd:        return kStepFillAdd;
            case CellState::StepFillSuppress:   return kStepFillSuppress;
            case CellState::StepOutOfRange:     return kStepOutRange;
            case CellState::StepPlayhead:       return kStepPlayhead;
            case CellState::StepHeld:           return kStepHeld;
            case CellState::SelectorCurrent:    return 0xFFFFFFFFu;
            case CellState::SelectorOccupied:   return kScopeStep;
            case CellState::SelectorEmpty:      return 0xFF404040u;
            case CellState::SelectorOutRange:   return kStepOutRange;
            case CellState::SelectorNext:       return kScopePhrase;
            case CellState::SelectorChain:      return 0xFF3A2A78u;  // deep indigo — phrase-family but dimmer than imminent Next
            case CellState::MuteMuted:          return kScopeMute;
            case CellState::MuteAudible:        return kStepInactive;
            case CellState::MachineCurrent:     return 0xFFFFFFFFu;
            case CellState::MachineAvailable:   return kScopeMachine;
            case CellState::MachineUnavailable: return kStepOutRange;
            case CellState::NoteEditActive:     return kScopeNoteEdit;
            case CellState::NoteEditStaged:     return 0xFFDC643Cu;
            case CellState::NoteEditOther:      return 0xFF16486Eu;  // dimmed azure — note in other octave only, distinct from active
            case CellState::NoteEditResting:    return kStepOutRange;
            case CellState::ChromaticWhite:     return kScopeTrack;
            case CellState::ChromaticBlack:     return kScopeTrack;
            case CellState::LevelsCell:         return 0xFF204060u;
            case CellState::LengthInRun:        return kScopePhrase;
            case CellState::LengthBoundary:     return 0xFFCCAAFFu;  // bright purple edge
            case CellState::LengthOutRun:       return kStepOutRange;
            case CellState::SelectorDeviated:   return kScopePhrase;
            case CellState::SelectorHome:       return 0xFFFFC020u;  // amber home/global border
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
            case ControllerButton::PhraseScope: return &modifiers[2];
            case ControllerButton::SceneScope:    return &modifiers[3];
            case ControllerButton::MorphScope:   return &modifiers[4];
            case ControllerButton::SongScope:  return &modifiers[5];
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
            // functionRow[8] is the O key = CLEAR (button VerbClear). VerbStop is
            // the legacy identity for the same slot; map both so byButton(VerbClear)
            // resolves (without it the Push Clear pad / New→Clear alias stayed off).
            case ControllerButton::VerbStop:
            case ControllerButton::VerbClear:    return &functionRow[8];
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

    SurfaceModel buildSurfaceModel(const UiState&         ui,
                                   const EditContext&      ec,
                                   const PressTracker*     press,
                                   LockstepProcessor&      proc,
                                   int                     activeTrack,
                                   int                     stepPage,
                                   GridDisplayMode         /*displayMode*/,
                                   int                     slotOffset,
                                   float                   crossfaderValue,
                                   const MorphViewState&   morphView)
    {
        SurfaceModel model;

        // Per-track machine presence (drives the empty-track greying on screen and
        // a controller's track LEDs). Single source: LockstepProcessor::isTrackEmpty.
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            model.trackHasMachine[static_cast<std::size_t>(t)] = !proc.isTrackEmpty(t);

        // Per-track deviation = playing a phrase other than the scene's home/global.
        const int homePhrase = proc.section().globalPhrase;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const int cur = proc.isTrackDeviated(t)
                ? proc.deviationPhraseIdxForTrack(t)
                : proc.section().globalPhrase;
            model.trackDeviated[static_cast<std::size_t>(t)] = (cur != homePhrase);
        }

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
        // Hardware-controller presses (e.g. a Push pad) must highlight on screen
        // exactly like mouse/keyboard — they are tracked under kControllerSource.
        auto controllerDown = [&](ControllerButton btn, int idx = -1) -> bool
        {
            return press && press->isControllerHeld(btn, idx);
        };
        auto physPressed = [&](int rawCode, ControllerButton btn, int idx = -1) -> bool
        {
            return keyDown(rawCode) || mouseDown(btn, idx) || controllerDown(btn, idx);
        };

        // Compound-chord overlay condition (MHY cross-column pair)
        const bool col1any = ui.phraseScopeHeld || ui.morphHeld || ui.muteHeld;
        const bool col2any = ui.trackHeld || ui.sceneHeld || ui.songHeld || ui.fillHeld;
        const bool hasCompound = (ui.funcHeld && (col1any || col2any))
                               || (col1any && col2any);
        // (kAmberStrip lives in UITheme.h)

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
            // Func-layer = KIT (machine/Kit picker, §4.7.2); promoted when Func held.
            if (ui.funcHeld) c.primary = "KIT";
            else             c.funcHint = "KIT";
        }

        // =====================================================================
        // modifiers[4..7] — step-row modifier columns (A/S/Z/X)
        // Built here; consumed by paintStepRows from Slice 3-4 onwards.
        // =====================================================================
        {
            SurfaceCell& c = model.modifiers[4];
            c.keyHint = "A";
            fillModifier(c, ControllerButton::MorphScope, 'A', "MORPH",
                         ui.morphHeld, ui.latch.morph,
                         kScopeMorph, kScopeMorphDim,
                         hasCompound && ui.morphHeld);
        }
        {
            SurfaceCell& c = model.modifiers[5];
            c.keyHint = "S";
            fillModifier(c, ControllerButton::SongScope, 'S', "SONG",
                         ui.songHeld, ui.latch.song,
                         kScopeSong, kScopeSongDim,
                         hasCompound && ui.songHeld);
            // Func-layer = GLOBAL (Func+Song → Global/project params).
            if (ui.funcHeld) c.primary = "GLOBAL";
            else             c.funcHint = "GLOBAL";
        }
        {
            SurfaceCell& c = model.modifiers[6];
            c.keyHint = "Z";
            // Bare Mute = global mute; Scene+Mute = scene mute. While Scene is held
            // the key reads S-MUTE in the scene-mute colour, signalling the
            // scene-mute grid view (DESIGN §13/§16).
            const bool sceneMuteMode = ui.sceneHeld;
            fillModifier(c, ControllerButton::MuteScope, 'Z',
                         sceneMuteMode ? "S-MUTE" : "MUTE",
                         ui.muteHeld, ui.latch.mute,
                         sceneMuteMode ? kScopePMute : kScopeMute, kScopeMuteDim,
                         hasCompound && ui.muteHeld);
            if (ui.latch.mute) c.pip.colour = kScopeMute;
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
            // Morph: A-pole qualifier active → accent; Morph held → morph tint.
            // scopeTint drives on-screen colour (groupForCell reads it); baseColour
            // drives controller LEDs — set both so neither renderer diverges.
            if (ui.morphHeld && ui.morphNavQualifier == 1)
            {
                c.scopeTint  = juce::Colour(kScopeMorphAcc).getARGB();
                c.baseColour = juce::Colour(kScopeMorphAcc).getARGB();
            }
            else if (ui.morphHeld)
            {
                c.scopeTint  = juce::Colour(kScopeMorph).getARGB();
                c.baseColour = juce::Colour(kScopeMorph).getARGB();
            }
            else
            {
                c.scopeTint  = 0u;
                c.baseColour = kNavActive;
            }
            // Func+↑ doubles track length ONLY without Track held; with Track held
            // the nav keys cycle the track input mode, so don't advertise ×2 there.
            if (ui.funcHeld && !ui.trackHeld) { c.primary = juce::String(u8"×2"); c.funcHint = {}; }
            else if (ui.trackHeld)            { c.primary = juce::String(u8"↑");  c.funcHint = {}; }
            else if (ui.morphHeld)            { c.primary = "A";                  c.funcHint = {}; }
            else                              { c.primary = juce::String(u8"↑");  c.funcHint = juce::String(u8"×2"); }
            jassert(!c.primary.isEmpty());
        }

        // =====================================================================
        // section[0..5] — keys 5-0 (TRIG/SRC/FILTER/AMP/MOD/FX)
        // =====================================================================

        using PS = EditMode::PrimaryScope;
        PS sectionScope = PS::None;
        if      (ui.trackHeld)        sectionScope = PS::Track;
        else if (ui.phraseScopeHeld) sectionScope = PS::Phrase;
        else if (ui.sceneHeld)         sectionScope = PS::Scene;
        else if (ui.morphHeld)        sectionScope = PS::Morph;
        else if (ui.songHeld)       sectionScope = PS::Song;
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

            const bool isSrcNoteEdit = (!isScopedMode && ui.funcHeld && s == 1);  // SRC = note-edit anchor
            const bool isMasterActive = !isScopedMode && (ui.masterSection == s);
            const bool isTrackActive  = !isScopedMode && (ui.masterSection == -1
                && ui.trackSection[static_cast<std::size_t>(activeTrack)] == s);

            if (c.pressed)
                c.base = CellState::Pressed;
            else if (c.disabled)
                c.base = CellState::Disabled;
            else if (isTrackActive || isMasterActive)
                c.base = CellState::ModeActive;
            else
                c.base = CellState::Resting;

            // baseColour distinguishes special visual modes for groupForCell()
            if (isMasterActive)
                c.baseColour = 0xFF404010u;     // golden — master section active
            else if (isSrcNoteEdit)
                c.baseColour = kScopeNoteEdit;
            else
                c.baseColour = compatColour(c.base, kSecActive);

            // Scope glow (DESIGN §6.6): a section key the held scope rebinds, and
            // that has content, lights in the scope colour. Disabled scoped cells
            // stay dim (no tint). (Machine picker now re-skins the step grid via
            // Func+Part, not section[1], so SRC reads as part-base SRC under Part.)
            if (isScopedMode && !c.disabled)
                c.scopeTint = scopeColour(sectionScope).getARGB();

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
            { 'Q', u8"Q", u8"PHRASE", u8"",      ControllerButton::PhraseScope, KeyRole::Modifier  },
            { 'W', u8"W", u8"SCENE", u8"",       ControllerButton::SceneScope,    KeyRole::Modifier  },
            { 'E', u8"E", u8"←",     u8"←ROT",    ControllerButton::NavLeft,      KeyRole::Nav       },
            { 'R', u8"R", u8"↓",     u8"÷2",     ControllerButton::NavDown,      KeyRole::Nav       },
            { 'T', u8"T", u8"→",     u8"ROT→",    ControllerButton::NavRight,     KeyRole::Nav       },
            { 'Y', u8"Y", u8"SNAP",  u8"RESTORE", ControllerButton::VerbYes,      KeyRole::VerbYes   },
            { 'U', u8"U", u8"REC",   u8"",        ControllerButton::VerbRecord,   KeyRole::VerbCopy  },
            { 'I', u8"I", u8"PLAY",  u8"",        ControllerButton::VerbPlay,     KeyRole::VerbPaste },
            { 'O', u8"O", u8"CLEAR", u8"DEL",     ControllerButton::VerbClear,    KeyRole::VerbClear },
            { 'P', u8"P", u8"YES",   u8"NO",      ControllerButton::VerbNo,       KeyRole::VerbNo    },
        }};

        const bool sectionScopeHeld = ui.trackHeld || ui.phraseScopeHeld
                                   || ui.sceneHeld || ui.morphHeld || ui.songHeld;

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
            const bool isPatHeld  = (def.keyCode == 'Q') && ui.phraseScopeHeld;
            const bool isPrtHeld  = (def.keyCode == 'W') && ui.sceneHeld;
            const bool isModeActive = isArmed || isPlaying || isPatHeld || isPrtHeld;

            juce::String displayPrimary { def.natural };
            juce::String displayHint    { def.funcLayer };

            // Live relabels (PAUSE / OD) — override after resolver.
            // (The machine/Kit picker label KIT lives on the Track modifier cell, key 2.)
            if (def.keyCode == 'I' && isPlaying && !sectionScopeHeld && !ui.stepHeld)
                displayPrimary = "PAUSE";
            if (isOverdub)
                displayPrimary = "OD";

            // Morph-held: nav ^ = A pole, nav v = B pole.
            if (ui.morphHeld && def.keyCode == 'R') { displayPrimary = "B"; displayHint = {}; }

            // CPC relabel: when a section-suite scope is held, the verb primaries
            // show COPY/PASTE/CLEAR so the scope+verb grammar is immediately readable.
            if (sectionScopeHeld)
            {
                // Under Morph scope only CLEAR changes meaning; don't relabel REC/PLAY.
                if (def.role == KeyRole::VerbCopy  && !ui.morphHeld) displayPrimary = "COPY";
                if (def.role == KeyRole::VerbPaste && !ui.morphHeld) displayPrimary = "PASTE";
                if (def.role == KeyRole::VerbClear) displayPrimary = "CLEAR";
            }

            // RESTORE/NO func-layer hints are always shown (Func+Y=Restore and Func+P=No
            // are valid even under a scope, so no suppression needed).
            const bool suppressFuncLayer = false;

            // Nav keys (E/R/T) cycle track input-mode / navigate when Track is
            // held — NOT the Func length/rotate ops — so don't advertise (or
            // promote) their Func layer there. Mirrors the NavUp cell.
            const bool navUnderTrack = (def.role == KeyRole::Nav) && ui.trackHeld;
            if (navUnderTrack)
                displayHint = {};

            // Func-hint promotion: when Func held and key has a Func-layer variant,
            // funcLayer IS the live function — show it as primary, clear hint.
            const bool hasFuncLayer = (def.funcLayer[0] != static_cast<char8_t>(0));
            if (ui.funcHeld && hasFuncLayer && !isModeActive && !suppressFuncLayer && !navUnderTrack)
            {
                displayPrimary = juce::String(def.funcLayer);
                displayHint    = {};
            }

            c.primary  = displayPrimary;
            c.funcHint = displayHint;

            // Compound overlay on Q (Pattern) and W (Part)
            c.strip.present = hasCompound
                && ((def.keyCode == 'Q' && ui.phraseScopeHeld)
                 || (def.keyCode == 'W' && ui.sceneHeld));
            c.strip.colour  = kAmberStrip;

            // Latch pips: Pattern (Q) and Part (W)
            if (def.keyCode == 'Q' && ui.latch.phrase)
                { c.pip.present = true; c.pip.colour = kScopePhrase; }
            else if (def.keyCode == 'W' && ui.latch.scene)
                { c.pip.present = true; c.pip.colour = kScopeScene; }

            // Cell state
            if (c.pressed)         c.base = CellState::Pressed;
            else if (isModeActive) c.base = CellState::ModeActive;
            else                   c.base = CellState::Resting;

            // baseColour for controller feedback and groupForCell()
            if (isOverdub)
                c.baseColour = kVerbODActive;                               // amber for OD
            else if (def.keyCode == 'Q')
                c.baseColour = ui.phraseScopeHeld ? kScopePhrase : kScopePhraseDim;
            else if (def.keyCode == 'W')
                c.baseColour = ui.sceneHeld ? kScopeScene : kScopeSceneDim;
            else if (def.keyCode == 'R' && ui.morphHeld && ui.morphNavQualifier == 2)
            {
                c.scopeTint  = juce::Colour(kScopeMorphAcc).getARGB();
                c.baseColour = juce::Colour(kScopeMorphAcc).getARGB();
            }
            else if (def.keyCode == 'R' && ui.morphHeld)
            {
                c.scopeTint  = juce::Colour(kScopeMorph).getARGB();
                c.baseColour = juce::Colour(kScopeMorph).getARGB();
            }
            else
                c.baseColour = compatColour(c.base, 0xFF404040u);

            // Hybrid pass-through + scope glow (DESIGN §6.6): verbs are
            // scope-combining. Under a section-suite scope, Y/U/I/O (Snapshot/Copy/
            // Paste/Clear) all participate in the scope grammar and glow in the scope
            // colour. P (Yes/confirm) is the confirm/cancel channel — reserved/dim
            // under scope, but still fires for pending-confirm resolution.
            // Nav/TAP are ambient and untouched.
            if (sectionScopeHeld)
            {
                if (def.role == KeyRole::VerbNo)
                    c.disabled = true;
                else if (def.role == KeyRole::VerbYes
                      || (def.role == KeyRole::VerbCopy  && !ui.morphHeld)
                      || (def.role == KeyRole::VerbPaste && !ui.morphHeld)
                      || def.role == KeyRole::VerbClear)
                    c.scopeTint = scopeColour(sectionScope).getARGB();
            }

            // Invariant: every function row cell has a non-empty primary label.
            jassert(!c.primary.isEmpty());
        }

        // Mirror Q and W into modifiers[2/3] for byButton() lookup.
        // Screen renders Q/W from functionRow[0/1]; controllers look up via modifiers.
        model.modifiers[2]        = model.functionRow[0];
        model.modifiers[2].button = ControllerButton::PhraseScope;
        model.modifiers[3]        = model.functionRow[1];
        model.modifiers[3].button = ControllerButton::SceneScope;

        // =====================================================================
        // step[0..15] — step grid cells (Slices 2–5)
        //
        // Priority order mirrors paintStepRows early-returns:
        //   funcTrackHeld → noteEdit → pLockClear → Chromatic → Levels
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

            if (ui.funcTrackHeld)
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
                // Bare Mute = global mute view; Scene+Mute = scene-mute view (the
                // scene's active-mask), in a distinct colour.
                const bool sceneMute   = ui.sceneHeld;
                const uint32_t muteCol = sceneMute ? kScopePMute : kScopeMute;

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

                    const bool muted = sceneMute
                        ? proc.getPatternMute(i) : proc.getGlobalMute(i);

                    c.base = muted ? CellState::MuteMuted : CellState::MuteAudible;
                    const juce::Colour muteJCol  { muteCol };
                    const juce::Colour audibleCol = juce::Colour(kStepInactive)
                                                     .interpolatedWith(muteJCol, 0.5f);
                    c.baseColour = muted
                        ? muteJCol.withAlpha(0.80f).getARGB()
                        : audibleCol.getARGB();
                }
            }
            // ── Phase 7 / DESIGN §34.4: Phrase-length authoring re-skin ─────────
            // Pattern+Func (focused track, purple) or Scene+Func (all tracks, orange).
            // Momentary: active exactly as long as the modifiers are held.
            else if ((ui.phraseScopeHeld || ui.morphHeld) && ui.funcHeld
                     && !ui.funcTrackHeld)
            {
                const bool broadcastMode = ui.morphHeld && ui.funcHeld;
                const juce::Colour tint  = broadcastMode
                    ? juce::Colour(kScopeMorph)
                    : juce::Colour(kScopePhrase);

                // Resolve length from the focused track's *working* sequence
                // (same source as numPages()/the LEN encoder) so a live
                // Phrase+Func+step / Morph+Func+step edit shows immediately,
                // before the next phrase/scene switch writes it back.
                const int lenTrack = (activeTrack >= 0) ? activeTrack : 0;
                const int phraseLen =
                    proc.sequence().tracks[static_cast<std::size_t>(lenTrack)].length;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button  = ControllerButton::Step;
                    c.index   = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int absIdx = stepPage * 16 + i;
                    (void)broadcastMode;  // both modes use focused track for ref currently

                    c.base = lengthEditCellState(absIdx, phraseLen);
                    const float alpha = (c.base == CellState::LengthInRun)    ? 0.35f
                                      : (c.base == CellState::LengthBoundary) ? 0.85f
                                                                              : 0.04f;
                    c.baseColour = tint.withAlpha(alpha).getARGB();
                    if (c.pressed) c.baseColour = 0xFFFFFFFFu;
                }
            }
            // ── End length-edit re-skin ──────────────────────────────────────────

            // ── 5.2 Morph step view ───────────────────────────────────────────────
            // Morph held (no Func) → step grid shows A/B pole states per MZ slot.
            // Row 0 (D-;, steps 0-7) = A poles; Row 1 (C-/, steps 8-15) = B poles.
            // Three visual states: active (lit magenta), dormant (dim), dark (none).
            else if (ui.morphHeld && !ui.funcHeld)
            {
                const int nmp = (activeTrack >= 0) ? proc.numParams(activeTrack) : 0;
                const int anchorSec = (slotOffset < nmp && activeTrack >= 0)
                    ? proc.paramSpec(activeTrack, slotOffset).sectionIndex : -1;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c  = model.step[static_cast<std::size_t>(i)];
                    c.button   = ControllerButton::Step;
                    c.index    = i;
                    c.keyHint  = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed  = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int mzLocal  = i % 8;             // slot index within MZ page
                    const int absSlot  = slotOffset + mzLocal;
                    const bool poleA   = (i < 8);           // top row = A, bottom = B

                    // Blank out-of-schema and cross-section slots.
                    const bool outOfSchema = absSlot >= nmp;
                    const bool outOfSec    = !outOfSchema && activeTrack >= 0
                        && proc.paramSpec(activeTrack, absSlot).sectionIndex != anchorSec;
                    if (outOfSchema || outOfSec)
                    {
                        c.base      = CellState::MorphPoleDark;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const auto& ms   = morphView.slots[static_cast<std::size_t>(mzLocal)];
                    const auto  pole = poleA ? ms.a : ms.b;
                    using MPS = MorphViewState::PoleState;

                    if (pole == MPS::Active)
                    {
                        c.base      = CellState::MorphPoleActive;
                        c.baseColour = c.pressed
                            ? juce::Colours::white.withAlpha(0.90f).getARGB()
                            : juce::Colour(kScopeMorph).withAlpha(0.80f).getARGB();
                    }
                    else if (pole == MPS::Dormant)
                    {
                        c.base      = CellState::MorphPoleDormant;
                        c.baseColour = c.pressed
                            ? juce::Colour(kScopeMorph).withAlpha(0.50f).getARGB()
                            : juce::Colour(kScopeMorphDim).withAlpha(0.70f).getARGB();
                    }
                    else
                    {
                        c.base      = CellState::MorphPoleDark;
                        c.baseColour = c.pressed
                            ? juce::Colour(kScopeMorphDim).withAlpha(0.40f).getARGB()
                            : juce::Colour(kStepInactive).withAlpha(0.35f).getARGB();
                    }

                    // Store the param label for the inline screen residual.
                    c.primary = (activeTrack >= 0)
                        ? juce::String(proc.paramSpec(activeTrack, absSlot).label) : juce::String{};
                }
            }
            // ── End morph step view ──────────────────────────────────────────────

            else if (ui.trackHeld || ui.phraseScopeHeld || ui.sceneHeld)
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
                else if (ui.phraseScopeHeld)
                {
                    // Phase 7: show the focused track's phrase pool (16 phrases).
                    // The fill marks the CURRENT playing phrase (a live deviation if
                    // one is active, else the floor); the home border (below) marks
                    // the scene's global phrase, so deviation reads as fill ≠ border.
                    maxAvail  = kPhrasesPerTrack;
                    const int at = activeTrack >= 0 ? activeTrack : 0;
                    activeIdx = proc.isTrackDeviated(at)
                        ? proc.deviationPhraseIdxForTrack(at)
                        : proc.section().globalPhrase;
                }
                else // sceneHeld
                {
                    // Phase 7: show sections within the active Song.
                    maxAvail  = kScenesPerSong;
                    activeIdx = proc.activeSectionIdx();
                }

                // Phase 7: Section queue indicator (replaces old pattern chain).
                std::array<int, 16> sectionQueuePos{};
                if (ui.sceneHeld && proc.hasQueuedScene())
                {
                    const int qi = proc.queuedSectionIdx();
                    if (qi >= 0 && qi < kScenesPerSong)
                        sectionQueuePos[static_cast<std::size_t>(qi)] = 1;
                }

                // Phase 7: per-phrase deviation badge for patternScope view.
                const int devTrack = (activeTrack >= 0 && ui.phraseScopeHeld) ? activeTrack : -1;
                // Dual-marker selector (DESIGN §4.7): the scene's global/home phrase.
                const int globalIdx = ui.phraseScopeHeld ? proc.section().globalPhrase : -1;

                std::array<bool, 16> slotEmpty{};
                for (int i = 0; i < maxAvail; ++i)
                {
                    if (ui.trackHeld)
                        slotEmpty[static_cast<std::size_t>(i)] = proc.isTrackEmpty(i);
                    else
                        slotEmpty[static_cast<std::size_t>(i)] = false;  // Phase 7: all phrases/sections exist
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
                    // Phase 7: section queue badge (Part scope) or deviation badge (Pattern scope).
                    const int  cpos = (ui.sceneHeld && avail)
                                      ? sectionQueuePos[static_cast<std::size_t>(i)] : 0;
                    const bool isDeviated = ui.phraseScopeHeld && avail
                                         && devTrack >= 0
                                         && proc.isTrackDeviated(devTrack)
                                         && i == proc.deviationPhraseIdxForTrack(devTrack);
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
                    else if (isDeviated)
                    {
                        // Phrase currently playing due to a live track deviation.
                        c.base      = CellState::SelectorDeviated;
                        c.baseColour = scopeTint.withAlpha(0.70f).getARGB();
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

                    // Dual-marker (DESIGN §4.7): border the scene's global/home
                    // phrase, so a deviation reads as fill (current) ≠ border (home).
                    if (ui.phraseScopeHeld && avail && i == globalIdx)
                    {
                        c.border.present = true;
                        c.border.token   = CellState::SelectorHome;
                        c.border.colour  = kHomeAmber;
                    }

                    // level encodes queue position for badge rendering in paintStepRows
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
            // Morph has no per-step action, so exclude it from step tinting.
            const bool morphOnlyScope = ui.morphHeld && !ui.trackHeld
                                     && !ui.phraseScopeHeld && !ui.sceneHeld && !ui.songHeld;
            const juce::Colour scopeBodyCol = fillOn
                ? juce::Colour(kScopeFill)
                : (morphOnlyScope ? juce::Colour(kScopeStep) : scopeColourFromState(ui));
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

        // =====================================================================
        // Manipulation-zone encoder slots (§35.8.5)
        // Fills model.slots[0..7] for the 8 encoders at slotOffset..slotOffset+7.
        // Uses Override-ELSE-Base resolution mirroring ManipulationZone::refreshSliders.
        // =====================================================================
        model.crossfader = crossfaderValue;
        {
            const bool stepHeld = ec.isActiveForEditing()
                               && ec.heldTrackIndex() == activeTrack;
            const int  heldStep = ec.heldStepIndex();
            const bool fillHeld = proc.fillActive();
            const bool fillEdit = stepHeld && fillHeld && heldStep >= 0;

            const int numParams = (activeTrack >= 0) ? proc.numParams(activeTrack) : 0;

            for (int i = 0; i < 8; ++i)
            {
                const int absSlot = slotOffset + i;
                auto& slot = model.slots[static_cast<std::size_t>(i)];

                if (activeTrack < 0 || absSlot >= numParams)
                {
                    slot.inRange = false;
                    continue;
                }

                const auto spec = proc.paramSpec(activeTrack, absSlot);
                slot.inRange = true;
                slot.label   = spec.label.isEmpty() ? juce::String(absSlot) : spec.label;

                // Owning section name (machine label, falling back to canonical) —
                // used as a header line on the Push display.
                {
                    const int secIdx = juce::jlimit(0, IMachine::kMaxSections - 1,
                                                    spec.sectionIndex);
                    const auto& secInfo = proc.section(activeTrack, secIdx);
                    slot.sectionLabel = secInfo.label.isNotEmpty()
                        ? secInfo.label
                        : juce::String(IMachine::kCanonicalSectionNames[
                              static_cast<std::size_t>(secIdx)]);
                }

                // Override-ELSE-Base resolution
                float value = proc.baseParamValue(activeTrack, absSlot);
                bool  hasLock = false;

                if (stepHeld && heldStep >= 0)
                {
                    const auto& s = proc.sequence().tracks[static_cast<std::size_t>(activeTrack)]
                                        .steps[static_cast<std::size_t>(heldStep)];
                    if (fillEdit)
                    {
                        if (s.fillOverrides.has(absSlot))
                        {
                            value   = s.fillOverrides.get(absSlot, value);
                            hasLock = true;
                        }
                        else
                        {
                            value = s.overrides.get(absSlot, value);
                        }
                    }
                    else
                    {
                        value   = s.overrides.get(absSlot, value);
                        hasLock = s.overrides.has(absSlot);
                    }
                }

                slot.hasOverride = hasLock;
                slot.valueText   = formatParamValue(value, spec);
                if (hasLock)
                    slot.valueText += " *";

                // Normalised position for ring LEDs
                const float range = spec.maxValue - spec.minValue;
                slot.position = (range > 0.0f)
                    ? juce::jlimit(0.0f, 1.0f, (value - spec.minValue) / range)
                    : 0.0f;

                if (!spec.valueLabels.empty() || spec.isStepped)
                    slot.ringMode = RingMode::Dot;
                else if (spec.minValue < 0.0f)
                    slot.ringMode = RingMode::BipolarFromCentre;
                else
                    slot.ringMode = RingMode::UnipolarFill;
            }
        }

        return model;
    }

}  // namespace lockstep
