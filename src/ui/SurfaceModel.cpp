#include "SurfaceModel.h"
#include "MetaBand.h"
#include "../core/Subdivision.h"
#include "UITheme.h"
#include "KeyLabel.h"
#include "../command/SurfaceLayer.h"
#include "../command/KeyBindings.h"
#include "ParamFormat.h"
#include "ScopedSectionMatrix.h"
#include "../state/UiState.h"
#include "../io/EditContext.h"
#include "../io/PressTracker.h"
#include "../io/TrigGridMode.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"
#include "../core/TrackInputMode.h"
#include "../machine/ISliceable.h"
#include <algorithm>
#include <bit>
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
            case CellState::ModalEntryInert:    return 0xFF3A2400u;  // dim amber: reachable but no active content
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
            case CellState::KeyModActive:       return 0xFF50B478u;  // bright green: set + applies
            case CellState::KeyModAvailable:    return 0xFF2E5E46u;  // dim green: would apply
            case CellState::KeyModDormant:      return 0xFF6A5A2Eu;  // muted amber: set but dormant
            case CellState::KeyModUnavail:      return 0xFF262C30u;  // near-off: unavailable
            case CellState::KeySymOn:           return 0xFF7050C8u;  // violet: symmetric selected
            case CellState::KeySymOff:          return 0xFF332C50u;  // dim violet: symmetric available
            case CellState::ChromaticWhite:     return kScopeTrack;
            case CellState::ChromaticBlack:     return kScopeTrack;
            case CellState::LevelsCell:         return 0xFF204060u;
            case CellState::LengthInRun:        return kScopePhrase;
            case CellState::LengthBoundary:     return 0xFFCCAAFFu;  // bright purple edge
            case CellState::LengthOutRun:       return kStepOutRange;
            case CellState::SelectorDeviated:   return kScopePhrase;
            case CellState::SelectorHome:       return 0xFFFFC020u;  // amber home/global border
            case CellState::MorphPoleActive:    return 0xFF60C080u;
            case CellState::MorphPoleDormant:   return 0xFF305040u;
            case CellState::MorphPoleDark:      return kStepOutRange;
            case CellState::SoundPoolOccupied:  return kScopeFill;
            case CellState::SoundPoolEmpty:     return kStepOutRange;
            case CellState::SoundPoolCurrent:   return 0xFFFFFFFFu;
            case CellState::RetrigRate:         return kScopeFill;
            case CellState::RetrigSelected:     return 0xFFFFFFFFu;
            case CellState::SlicePoint:         return kScopeFill;
            case CellState::SliceSelected:      return 0xFFFFFFFFu;
            case CellState::SliceEmpty:         return kStepOutRange;
            case CellState::EffectAvailable:    return 0xFF30A030u;  // lime-green — available effect slot
            case CellState::EffectLoaded:       return 0xFFFFFFFFu;  // white — loaded/selected effect
            case CellState::EffectLoadedOther:  return 0xFF6E8E6Eu;  // muted green — cross-slot hint
            case CellState::GeneratorEuclid:    return 0xFF7050C8u;  // purple — Euclidean
            case CellState::GeneratorDensity:   return 0xFF50B478u;  // green — Density
            case CellState::GeneratorVel:       return 0xFF8898A8u;  // slate — Velocity
            default:                            return fallback;
        }
    }

    // =========================================================================
    // SurfaceModel::byButton — reverse lookup
    // =========================================================================

    const SurfaceCell* SurfaceModel::byButton(ControllerButton btn, int idx) const noexcept
    {
        switch (btn)
        {
            case ControllerButton::Func:        return &modifiers[0];
            case ControllerButton::TrackScope:  return &modifiers[1];
            case ControllerButton::PhraseScope: return &modifiers[2];
            case ControllerButton::SceneScope:  return &modifiers[3];
            case ControllerButton::MorphScope:  return &modifiers[4];
            case ControllerButton::SongScope:   return &modifiers[5];
            case ControllerButton::MuteScope:   return &modifiers[6];
            case ControllerButton::FillScope:   return &modifiers[7];
            case ControllerButton::TapTempo:    return &tap;
            case ControllerButton::NavUp:       return &navUp;
            case ControllerButton::NavLeft:     return &functionRow[2];
            case ControllerButton::NavDown:     return &functionRow[3];
            case ControllerButton::NavRight:    return &functionRow[4];
            case ControllerButton::VerbSnapshot:     return &functionRow[5];
            case ControllerButton::VerbRecord:  return &functionRow[6];
            case ControllerButton::VerbPlay:    return &functionRow[7];
            // functionRow[8] is the O key = CLEAR (button VerbClear). VerbStopLegacy is
            // the legacy identity for the same slot; map both so byButton(VerbClear)
            // resolves (without it the Push Clear pad / New→Clear alias stayed off).
            case ControllerButton::VerbStopLegacy:
            case ControllerButton::VerbClear:   return &functionRow[8];
            case ControllerButton::VerbConfirm:      return &functionRow[9];
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
                    if (stp.fillTrigState == FillTrigState::On) return true;
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
                    const auto len = static_cast<std::int64_t>(std::max(trackLen, 1));
                    const auto denom = static_cast<std::int64_t>(cond.iterDenominator);
                    const auto iter = (loopBase + static_cast<std::int64_t>(i)) / len;
                    iterPass = (iter % denom == static_cast<std::int64_t>(cond.iterNumerator) - 1);
                }
                if (iterPass)
                {
                    prob = std::clamp(static_cast<float>(cond.probabilityPercent) / 100.0f, 0.0f, 1.0f);
                    if (cond.prevDependency == 1) prob *= prevProb;
                    else if (cond.prevDependency == 2) prob *= (1.0f - prevProb);
                }
            }
            prevProb = prob;
            if (i >= pageBase)
                out[static_cast<std::size_t>(i - pageBase)] = prob;
        }
        return out;
    }

    SurfaceModel buildSurfaceModel(const UiState& ui,
                                   const EditContext& ec,
                                   const PressTracker* press,
                                   LockstepProcessor& proc,
                                   int activeTrack,
                                   int stepPage,
                                   GridDisplayMode /*displayMode*/,
                                   int slotOffset,
                                   float crossfaderValue,
                                   const MorphViewState& morphView)
    {
        SurfaceModel model;

        // Per-track machine presence (drives the empty-track greying on screen and
        // a controller's track LEDs). Single source: LockstepProcessor::isTrackEmpty.
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
            model.trackHasMachine[static_cast<std::size_t>(t)] = !proc.isTrackEmpty(t);

        // Per-track deviation = playing a phrase other than the scene's diagonal row.
        const int homePhrase = proc.activeSectionIdx();
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            const int cur = proc.isTrackDeviated(t)
                                ? proc.deviationPhraseIdxForTrack(t)
                                : proc.activeSectionIdx();
            model.trackDeviated[static_cast<std::size_t>(t)] = (cur != homePhrase);
        }

        // Press-state helpers
        auto keyDown = [&](int rawCode) -> bool {
            return press ? press->isKeyHeld(rawCode)
                         : juce::KeyPress::isKeyCurrentlyDown(rawCode);
        };
        auto mouseDown = [&](ControllerButton btn, int idx = -1) -> bool {
            return press && press->isMouseHeld(btn, idx);
        };
        // Hardware-controller presses (e.g. a Push pad) must highlight on screen
        // exactly like mouse/keyboard — they are tracked under kControllerSource.
        auto controllerDown = [&](ControllerButton btn, int idx = -1) -> bool {
            return press && press->isControllerHeld(btn, idx);
        };
        auto physPressed = [&](int rawCode, ControllerButton btn, int idx = -1) -> bool {
            return keyDown(rawCode) || mouseDown(btn, idx) || controllerDown(btn, idx);
        };

        // Compound-chord overlay condition (MHY cross-column pair)
        const bool col1any = ui.phraseScopeHeld || ui.morphHeld || ui.muteHeld;
        const bool col2any = ui.trackHeld || ui.sceneHeld || ui.songHeld || ui.fillHeld;
        const bool hasCompound = (ui.funcHeld && (col1any || col2any)) || (col1any && col2any);
        // (kAmberStrip lives in UITheme.h)

        // Held-modifier bitmask — used by resolveBinding() for the label/action table.
        const uint16_t heldMods = heldModsFromUiState(ui);

        // =====================================================================
        // Modifier helper: fill a step-row or number-row modifier cell.
        // scopeCol = the canonical scope colour (ModeActive bg + latch pip).
        // dimCol   = the dark inactive background (kScopeXxxDim).
        // =====================================================================
        auto fillModifier = [&](SurfaceCell& c, ControllerButton btn,
                                int rawCode, const char* label,
                                bool isHeld, bool latchActive,
                                uint32_t scopeCol, uint32_t dimCol,
                                bool stripOn) -> void {
            c.button = btn;
            c.index = -1;
            c.pressed = physPressed(rawCode, btn);
            c.primary = label;
            c.funcHint = {};
            c.strip.present = stripOn;
            c.strip.colour = kAmberStrip;
            if (latchActive && scopeCol != 0)
            {
                c.pip.present = true;
                c.pip.colour = scopeCol;
            }
            if (c.pressed) c.base = CellState::Pressed;
            else if (isHeld) c.base = CellState::ModeActive;
            else c.base = CellState::Resting;
            c.baseColour = (c.base == CellState::ModeActive) ? scopeCol : dimCol;
            jassert(!c.primary.isEmpty());  // invariant: modifier cells always have a label
        };

        // =====================================================================
        // modifiers[0] — Func (key 1), amber
        // =====================================================================
        {
            SurfaceCell& c = model.modifiers[0];
            c.button = ControllerButton::Func;
            c.keyHint = "1";
            c.pressed = physPressed('1', ControllerButton::Func);
            c.funcHint = {};
            // Table-driven label: "FUNC" bare (no hint); Func+Func does not produce
            // a secondary, so funcHint stays empty from the binding row.
            {
                const auto& r = resolveBinding(ControllerButton::Func, -1, heldMods, SurfaceLayer::Base);
                c.primary = juce::String(r.primary);
                c.funcHint = juce::String(r.hint);
            }
            c.strip.present = hasCompound && ui.funcHeld;
            c.strip.colour = kAmberStrip;
            c.base = c.pressed     ? CellState::Pressed
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
            // Table-driven label: "TRACK" bare, "KIT" when Func held.
            {
                const auto& r = resolveBinding(ControllerButton::TrackScope, -1, heldMods, SurfaceLayer::Base);
                c.primary = juce::String(r.primary);
                c.funcHint = juce::String(r.hint);
            }
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
            // Table-driven label: "SONG" bare, "GLOBAL" when Func held.
            {
                const auto& r = resolveBinding(ControllerButton::SongScope, -1, heldMods, SurfaceLayer::Base);
                c.primary = juce::String(r.primary);
                c.funcHint = juce::String(r.hint);
            }
        }
        {
            SurfaceCell& c = model.modifiers[6];
            c.keyHint = "Z";
            // Table-driven label: "MUTE" bare; Scene+Mute gives "S-MUTE" (scene-mute
            // grid view, DESIGN §13/§16). ActionId distinguishes the two modes.
            {
                const auto& mutRow = resolveBinding(ControllerButton::MuteScope, -1, heldMods, SurfaceLayer::Base);
                const juce::String muteLabel{ mutRow.primary };
                const bool sceneMuteMode = (mutRow.action == ActionId::HoldSceneMuteView);
                fillModifier(c, ControllerButton::MuteScope, 'Z', muteLabel.toRawUTF8(),
                             ui.muteHeld, ui.latch.mute,
                             sceneMuteMode ? kScopePMute : kScopeMute, kScopeMuteDim,
                             hasCompound && ui.muteHeld);
                if (ui.latch.mute) c.pip.colour = kScopeMute;
                if (ui.sceneHeld && !ui.muteHeld)
                {
                    c.scopeTint = kScopeScene;
                    c.baseColour = kScopeScene;
                }
            }
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
            c.button = ControllerButton::TapTempo;
            c.keyHint = "3";
            c.pressed = physPressed('3', ControllerButton::TapTempo);
            c.base = c.pressed ? CellState::Pressed : CellState::Resting;
            c.baseColour = kTapActive;
            // Table-driven label: "TAP" bare (hint "MET"); "MET" when Func held.
            {
                const auto& r = resolveBinding(ControllerButton::TapTempo, -1, heldMods, SurfaceLayer::Base);
                c.primary = juce::String(r.primary);
                c.funcHint = juce::String(r.hint);
            }
            jassert(!c.primary.isEmpty());
        }

        // =====================================================================
        // NavUp / ^ (key 4) — number-row utility; Func-variant = POOL
        // =====================================================================
        {
            SurfaceCell& c = model.navUp;
            c.button = ControllerButton::NavUp;
            c.keyHint = "4";
            c.pressed = physPressed('4', ControllerButton::NavUp);
            c.base = c.pressed ? CellState::Pressed : CellState::Resting;
            // Morph: A-pole qualifier active → accent; Morph held → morph tint.
            // Track: cycle-input-mode action is scope-specific → Track tint.
            // scopeTint drives on-screen colour (groupForCell reads it); baseColour
            // drives controller LEDs — set both so neither renderer diverges.
            if (ui.morphHeld && ui.morphNavQualifier == 1)
            {
                c.scopeTint = juce::Colour(kScopeMorphAcc).getARGB();
                c.baseColour = juce::Colour(kScopeMorphAcc).getARGB();
            }
            else if (ui.morphHeld)
            {
                c.scopeTint = juce::Colour(kScopeMorph).getARGB();
                c.baseColour = juce::Colour(kScopeMorph).getARGB();
            }
            else if (ui.trackHeld)
            {
                c.scopeTint = kScopeTrack;
                c.baseColour = kScopeTrack;
            }
            else
            {
                c.scopeTint = 0u;
                c.baseColour = kNavActive;
            }
            // Table-driven label: ↑ bare (hint ×2); ×2 when Func held (not Track);
            // A-pole when Morph held; ↑ no-hint when Track held (cycles input mode).
            // Morph+Func gives ×2 (explicit combined row in table preserves dispatch order).
            {
                const auto& r = resolveBinding(ControllerButton::NavUp, -1, heldMods, SurfaceLayer::Base);
                c.primary = juce::String(r.primary);
                c.funcHint = juce::String(r.hint);
            }
            jassert(!c.primary.isEmpty());
        }

        // =====================================================================
        // section[0..5] — keys 5-0 (TRIG/SRC/FILTER/AMP/MOD/FX)
        // =====================================================================

        using PS = EditMode::PrimaryScope;
        const PS sectionScope = firstHeldSectionSuiteScope(ui);
        const bool isScopedMode = (sectionScope != PS::None);

        static constexpr int kSectionKeyCodes[IMachine::kMaxSections] = {
            '5', '6', '7', '8', '9', '0'
        };
        static constexpr const char* kSectionKeyHints[IMachine::kMaxSections] = {
            "5", "6", "7", "8", "9", "0"
        };
        // Func-row secondary labels (the Func-held section row). Empty = no
        // secondary on that key → it dims under Func (DESIGN §6.1 rule 3).
        // TRACK (length/divider) relocated to Track+TRIG; GLOBAL (gain/sync/clock)
        // relocated to Song+FX. COND/NOTE remain Func secondaries (§6.2). FX has
        // NO Func secondary: the insert picker moved to the *hold* gesture on FX
        // (9.14 Stage 2 — "PICK FX" is surfaced as the hold-rail label via the
        // grammar row, not a Func secondary), freeing Func+FX. AMP/MOD also carry
        // no Func secondary: the velocity / density generators moved to the
        // generator hub on `3` in 9.10, freeing Func+AMP / Func+MOD. A label here
        // must correspond to a dispatchable Func+section action or the row promises
        // a panel that never opens — guarded by testSectionFuncHintsMatchDispatch.
        static constexpr std::array<const char*, IMachine::kMaxSections> kMetaLabels = {
            "COND", "NOTE", "", "", "", ""
        };
        auto isReservedMeta = [](int s) -> bool {
            return s < 0 || s >= IMachine::kMaxSections || kMetaLabels[static_cast<std::size_t>(s)][0] == '\0';
        };
        auto sectionHasMachineSlot = [&](int track, int canonicalIdx) -> bool {
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
            c.button = ControllerButton::Section;
            c.index = s;
            c.keyHint = kSectionKeyHints[s];

            const bool machineHasSection = sectionHasMachineSlot(activeTrack, s);
            const char* metaLabel = isReservedMeta(s)
                                        ? nullptr
                                        : kMetaLabels[static_cast<std::size_t>(s)];
            const KeyDef kd{
                KeyRole::SectionKey,
                IMachine::kCanonicalSectionNames[static_cast<std::size_t>(s)],
                (metaLabel != nullptr) ? metaLabel : "",
                s, machineHasSection
            };
            const KeyLabel kl = resolveKeyLabel(kd, ui, ec);

            c.disabled = kl.disabled;
            c.primary = kl.primary;
            c.pressed = physPressed(kSectionKeyCodes[s], ControllerButton::Section, s);

            // Hint band = Func-layer only. AMP/MOD have no Func action → no hint.
            // Func-promotion: when Func held, meta label becomes the live primary.
            // In scoped mode, honor per-cell funcLabel if present (e.g. Song+FX → "PICK FX").
            if (isScopedMode)
            {
                const auto scInfo = scopedCell(sectionScope, s);
                if (scInfo.funcLabel != nullptr)
                {
                    if (ui.funcHeld)
                    {
                        c.primary = scInfo.funcLabel;
                        c.funcHint = {};
                    }
                    else
                        c.funcHint = scInfo.funcLabel;
                }
                else
                    c.funcHint = {};
            }
            else if (isReservedMeta(s))
                c.funcHint = {};
            else if (ui.funcHeld && !kl.hint.isEmpty())
            {
                c.primary = kl.hint;   // e.g. TRIG→COND, SRC→NOTE, FILTER→TRACK
                c.funcHint = {};
            }
            else
                c.funcHint = kl.hint;   // dim secondary when Func not held

            // SRC glows when StepInspector is active — "tap to edit notes for this step".
            // (Previously lit for Func+Src; that gesture was retired in 9.14 Stage 3.)
            const bool isSrcNoteEdit = (!isScopedMode && ui.pLockClearMode && s == 1);
            // The key must also *say* it is the note editor in this context — not
            // advertise the retired Func access path. Relabel primary → "NOTE" and
            // drop the dim Func hint. deriveSlots leaves both untouched (SRC's tap
            // row has an empty primary; its Func row carries no label).
            if (isSrcNoteEdit)
            {
                c.primary = "NOTE";
                c.funcHint = {};
            }
            const bool isMasterActive = !isScopedMode && (ui.masterSection == s);
            const bool isTrackActive = !isScopedMode && (ui.masterSection == -1 && ui.trackSection[static_cast<std::size_t>(activeTrack)] == s);

            // Func layer (bare Func, no scope): the section row must announce its
            // secondary layer in colour, not just text (DESIGN §6.1 rule 3, §6.2).
            // Cells with a wired secondary (COND/NOTE) glow in the Func hue;
            // cells with none dim to Disabled.
            // Func+FX picker retired (9.14 Stage 2) — FX now dims under Func.
            const bool funcLayerActive = (ui.funcHeld && !isScopedMode);
            const bool hasFuncSecondary = !isReservedMeta(s);
            if (funcLayerActive)
                c.disabled = !hasFuncSecondary;
            // FX carries no Func-layer action (the picker is a non-Func gesture:
            // Track+hold / Song+hold). Under Func held it must read inert even when
            // a scope would otherwise light it (e.g. Func+Song was showing a lit
            // "FX" where the retired GLOBAL scope used to sit). (9.14)
            if (ui.funcHeld && s == proc.kFxSecIdx)
                c.disabled = true;

            // Fill layer: TRIG (0) and SRC (1) glow when Fill is held to announce
            // the Retrig and SoundPool overlays respectively.
            const bool fillLayerActive = (ui.fillHeld && !isScopedMode && !ui.funcHeld);
            const bool isFillArmed = fillLayerActive && (s == 0 || s == 1);

            // §39.10: Func+AMP is "available-but-inert" when no tracks have velMode enabled.
            const bool isVelInert = funcLayerActive && s == proc.kVelSecIdx
                && [&]() {
                    for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
                        if (proc.kit(t).velMode != VelMode::Off)
                            return false;
                    return true;
                }();

            if (c.pressed)
                c.base = CellState::Pressed;
            else if (c.disabled)
                c.base = CellState::Disabled;
            else if (isVelInert)
                c.base = CellState::ModalEntryInert;
            else if (isTrackActive || isMasterActive || isFillArmed)
                c.base = CellState::ModeActive;
            else
                c.base = CellState::Resting;

            // baseColour distinguishes special visual modes for groupForCell()
            if (isVelInert)
                c.baseColour = 0xFF3A2400u;     // dim amber — inert modal entry
            else if (funcLayerActive && hasFuncSecondary)
                c.baseColour = kScopeFunc;      // Func-secondary glow (COND / NOTE / FX)
            else if (isFillArmed)
                c.baseColour = kScopeFill;      // Fill-secondary glow (TRIG / SRC)
            else if (isMasterActive)
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
            else if (funcLayerActive && hasFuncSecondary)
                c.scopeTint = scopeColour(EditMode::PrimaryScope::Func).getARGB();
            else if (isFillArmed)
                c.scopeTint = scopeColour(EditMode::PrimaryScope::Fill).getARGB();

            // Invariant: non-disabled section keys always resolve to a non-empty primary.
            jassert(c.disabled || !c.primary.isEmpty());
        }

        // =====================================================================
        // Active-layer resolution — hoisted here so both function-row and step-grid
        // renderers can read it without re-deriving. resolveActiveLayer is the one
        // SSOT so rendering and dispatch cannot diverge.
        // =====================================================================
        const bool validStepTrackMode = activeTrack >= 0 && activeTrack < static_cast<int>(kNumTracks);
        const TrackInputMode activeTrackMode = validStepTrackMode
                                                   ? ui.trackInputMode[static_cast<std::size_t>(activeTrack)]
                                                   : TrackInputMode::Play;
        const LayerFacts layerFacts{ activeTrackMode, activeTrack };
        const SurfaceLayer activeLayer = resolveActiveLayer(ui, ec, layerFacts);

        // =====================================================================
        // functionRow[0..9] — Q-row: Q/W/E/R/T/Y/U/I/O/P
        // =====================================================================

        // FRowDef: per-key static metadata for the function row.
        // Labels are now table-driven via resolveBinding(); only keyCode, keyHint,
        // button, and role remain here for event routing and cell-state logic.
        struct FRowDef
        {
            int keyCode;
            const char8_t* keyHint;
            ControllerButton button;
            KeyRole role;
        };

        static const std::array<FRowDef, 10> kFRowDefs = { {
            { 'Q', u8"Q", ControllerButton::PhraseScope, KeyRole::Modifier },
            { 'W', u8"W", ControllerButton::SceneScope, KeyRole::Modifier },
            { 'E', u8"E", ControllerButton::NavLeft, KeyRole::Nav },
            { 'R', u8"R", ControllerButton::NavDown, KeyRole::Nav },
            { 'T', u8"T", ControllerButton::NavRight, KeyRole::Nav },
            { 'Y', u8"Y", ControllerButton::VerbSnapshot, KeyRole::VerbSnapshot },
            { 'U', u8"U", ControllerButton::VerbRecord, KeyRole::VerbCopy },
            { 'I', u8"I", ControllerButton::VerbPlay, KeyRole::VerbPaste },
            { 'O', u8"O", ControllerButton::VerbClear, KeyRole::VerbClear },
            { 'P', u8"P", ControllerButton::VerbConfirm, KeyRole::VerbConfirm },
        } };

        const bool sectionScopeHeld = (firstHeldSectionSuiteScope(ui) != PS::None);

        for (int i = 0; i < 10; ++i)
        {
            const auto& def = kFRowDefs[static_cast<std::size_t>(i)];
            SurfaceCell& c = model.functionRow[static_cast<std::size_t>(i)];

            c.button = def.button;
            c.index = -1;
            c.keyHint = juce::String(def.keyHint);
            c.pressed = physPressed(def.keyCode, def.button);

            const bool isOverdub = (def.keyCode == 'U') && proc.clock().isOverdubArmed();
            const bool isArmed = (def.keyCode == 'U') && proc.clock().isRecordArmed();
            const bool isPlaying = (def.keyCode == 'I') && proc.clock().inPluginPlaying();
            const bool isPatHeld = (def.keyCode == 'Q') && ui.phraseScopeHeld;
            const bool isPrtHeld = (def.keyCode == 'W') && ui.sceneHeld;
            const bool isModeActive = isArmed || isPlaying || isPatHeld || isPrtHeld;

            // Table-driven labels: covers Func-promotion, CPC relabels (COPY/PASTE/CLEAR
            // under scope), nav-hint suppression under Track, and Morph B-pole on NavDown.
            // Runtime states PAUSE and OD override afterwards since the table is static.
            const auto& binding = resolveBinding(def.button, -1, heldMods, SurfaceLayer::Base);
            juce::String displayPrimary{ binding.primary };
            juce::String displayHint{ binding.hint };

            // QUANT lives on the P/Confirm key under quantizing scopes (Trig held
            // steps / Track / Phrase): it zeros microOffset. The binding table can't
            // express the Trig case (Trig is not a modifier bit), so surface it here.
            // Scene/Morph/Song keep P as the dim confirm channel. (DESIGN §19.3)
            const bool quantScope = ui.stepHeld || ui.trackHeld || ui.phraseScopeHeld;

            // Runtime-only overrides (not encodable in a static table):
            if (def.keyCode == 'I' && isPlaying && !sectionScopeHeld && !ui.stepHeld)
                displayPrimary = "PAUSE";
            if (isOverdub)
                displayPrimary = "OD";
            if (def.keyCode == 'P' && quantScope && !ui.euclidHeld)
            {
                displayPrimary = "QUANT";
                displayHint = {};
            }


            c.primary = displayPrimary;
            c.funcHint = displayHint;

            // Compound overlay on Q (Pattern) and W (Part)
            c.strip.present = hasCompound && ((def.keyCode == 'Q' && ui.phraseScopeHeld) || (def.keyCode == 'W' && ui.sceneHeld));
            c.strip.colour = kAmberStrip;

            // Latch pips: Pattern (Q) and Part (W)
            if (def.keyCode == 'Q' && ui.latch.phrase)
            {
                c.pip.present = true;
                c.pip.colour = kScopePhrase;
            }
            else if (def.keyCode == 'W' && ui.latch.scene)
            {
                c.pip.present = true;
                c.pip.colour = kScopeScene;
            }

            // Cell state
            if (c.pressed) c.base = CellState::Pressed;
            else if (isModeActive) c.base = CellState::ModeActive;
            else c.base = CellState::Resting;

            // baseColour for controller feedback and groupForCell()
            if (isOverdub)
                c.baseColour = kVerbODActive;                               // amber for OD
            else if (def.keyCode == 'Q')
                c.baseColour = ui.phraseScopeHeld ? kScopePhrase : kScopePhraseDim;
            else if (def.keyCode == 'W')
                c.baseColour = ui.sceneHeld ? kScopeScene : kScopeSceneDim;
            else if (def.keyCode == 'R' && ui.morphHeld && ui.morphNavQualifier == 2)
            {
                c.scopeTint = juce::Colour(kScopeMorphAcc).getARGB();
                c.baseColour = juce::Colour(kScopeMorphAcc).getARGB();
            }
            else if (def.keyCode == 'R' && ui.morphHeld)
            {
                c.scopeTint = juce::Colour(kScopeMorph).getARGB();
                c.baseColour = juce::Colour(kScopeMorph).getARGB();
            }
            else if (def.keyCode == 'R' && ui.trackHeld)
            {
                c.scopeTint = kScopeTrack;
                c.baseColour = kScopeTrack;
            }
            else
                c.baseColour = compatColour(c.base, 0xFF404040u);

            // Hybrid pass-through + scope glow (DESIGN §6.6): verbs are
            // scope-combining. Under a section-suite scope, Y/U/I/O (Snapshot/Copy/
            // Paste/Clear) all participate in the scope grammar and glow in the scope
            // colour. P (Confirm) is the confirm/cancel channel — reserved/dim
            // under scope, but still fires for pending-confirm resolution.
            // Nav/TAP are ambient and untouched.
            if (sectionScopeHeld)
            {
                if (def.role == KeyRole::VerbConfirm)
                {
                    // QUANT under Track/Phrase is an active scoped verb — glow, don't
                    // dim. Under Scene/Morph/Song, P stays the reserved confirm channel.
                    if (quantScope)
                        c.scopeTint = scopeColour(sectionScope).getARGB();
                    else
                        c.disabled = true;
                }
                else if (def.role == KeyRole::VerbSnapshot || (def.role == KeyRole::VerbCopy && !ui.morphHeld) || (def.role == KeyRole::VerbPaste && !ui.morphHeld) || def.role == KeyRole::VerbClear)
                    c.scopeTint = scopeColour(sectionScope).getARGB();
            }

            // DeletePicker / PendingConfirm: dim all function-row keys except Func.
            // (Steps stay live for DeletePicker; PendingConfirm dims them separately.)
            if (activeLayer == SurfaceLayer::DeletePicker && def.button != ControllerButton::Func)
            {
                c.disabled = true;
                c.base = CellState::Disabled;
                c.scopeTint = 0;
            }

            // PendingConfirm: P shows live YES/NO depending on whether Func is held;
            // every other key dims. Func itself is exempt (user needs it to reach NO).
            if (activeLayer == SurfaceLayer::PendingConfirm)
            {
                if (def.role == KeyRole::VerbConfirm)
                {
                    const auto& b = resolveBinding(def.button, -1, heldMods,
                                                   SurfaceLayer::PendingConfirm);
                    c.primary = juce::String(b.primary);
                    c.funcHint = juce::String(b.hint);
                    if (!c.pressed)
                        c.base = b.state;
                    c.disabled = false;
                    c.scopeTint = 0;
                }
                else if (def.button != ControllerButton::Func)
                {
                    c.disabled = true;
                    c.base = CellState::Disabled;
                    c.scopeTint = 0;
                }
            }

            // Invariant: every function row cell has a non-empty primary label.
            jassert(!c.primary.isEmpty());
        }

        // Mirror Q and W into modifiers[2/3] for byButton() lookup.
        // Screen renders Q/W from functionRow[0/1]; controllers look up via modifiers.
        model.modifiers[2] = model.functionRow[0];
        model.modifiers[2].button = ControllerButton::PhraseScope;
        model.modifiers[3] = model.functionRow[1];
        model.modifiers[3].button = ControllerButton::SceneScope;

        // =====================================================================
        // step[0..15] — step grid cells (Slices 2–5)
        // =====================================================================
        {
            static constexpr int kStepKeyCodes[16] = {
                'D', 'F', 'G', 'H', 'J', 'K', 'L', 59,
                'C', 'V', 'B', 'N', 'M', 44, 46, 47
            };
            static constexpr const char* kStepKeyHints[16] = {
                "D", "F", "G", "H", "J", "K", "L", ";",
                "C", "V", "B", "N", "M", ",", ".", "/"
            };

            // ── Layer-keyed branches (bodies verbatim from the original cascade) ──
            if (activeLayer == SurfaceLayer::PendingConfirm)
            {
                // Dim all step cells — confirm resolves via P/Func+P only.
                for (int i = 0; i < 16; ++i)
                {
                    auto& s = model.step[static_cast<std::size_t>(i)];
                    s.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    s.base = CellState::Disabled;
                    s.disabled = true;
                }
            }
            else if (activeLayer == SurfaceLayer::DeletePicker)
            {
                // Delete-picker selector: show slots for the picker scope; tap selects target.
                const DeleteScope dpScope = ui.deletePicker.scope;
                int maxAvail = 0;
                int activeIdx = 0;
                if (dpScope == DeleteScope::Track)
                {
                    maxAvail = static_cast<int>(kNumTracks);
                    activeIdx = activeTrack;
                }
                else if (dpScope == DeleteScope::Phrase)
                {
                    maxAvail = kPhrasesPerTrack;
                    const int at = activeTrack >= 0 ? activeTrack : 0;
                    activeIdx = proc.activeSectionIdx();
                    (void)at;
                }
                else // Scene
                {
                    maxAvail = kScenesPerSong;
                    activeIdx = proc.activeSectionIdx();
                }

                const juce::Colour scopeTint{ 0xFFC03030u };  // red danger tint for delete
                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = (dpScope == DeleteScope::Track)
                                   ? ControllerButton::SelectTrack
                                   : ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i],
                                            c.button == ControllerButton::Step
                                                ? ControllerButton::Step
                                                : ControllerButton::SelectTrack,
                                            i);

                    const bool avail = i < maxAvail;
                    const bool isCurrent = avail && (i == activeIdx);

                    bool isEmpty = false;
                    if (avail)
                    {
                        if (dpScope == DeleteScope::Track)
                            isEmpty = proc.isTrackEmpty(i);
                        else if (dpScope == DeleteScope::Scene)
                            isEmpty = (i != activeIdx) && !proc.sceneSlotOccupied(i);
                    }

                    if (!avail)
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = scopeTint.withAlpha(0.04f).getARGB();
                    }
                    else if (isEmpty)
                    {
                        c.base = CellState::SelectorEmpty;
                        c.baseColour = scopeTint.withAlpha(0.09f).getARGB();
                    }
                    else if (isCurrent)
                    {
                        c.base = CellState::SelectorCurrent;
                        c.baseColour = juce::Colours::white.interpolatedWith(scopeTint, 0.30f).getARGB();
                    }
                    else
                    {
                        c.base = CellState::SelectorOccupied;
                        c.baseColour = scopeTint.withAlpha(0.18f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::SoundPool)
            {
                // Sound Pool overlay: grid cells show saved sounds for the active track.
                static constexpr std::array<double, 8> kRetrigRates = { {
                    1.0,
                    2.0 / 3.0,
                    0.5,
                    1.0 / 3.0,
                    0.25,
                    1.0 / 6.0,
                    0.125,
                    1.0 / 12.0,
                } };
                (void)kRetrigRates;
                const juce::Colour fillTint{ kScopeFill };
                const int poolSize = proc.soundPoolSize();
                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    if (i >= poolSize)
                    {
                        c.base = CellState::SoundPoolEmpty;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        c.base = CellState::SoundPoolOccupied;
                        const auto* entry = proc.soundPoolEntry(i);
                        const juce::String label = entry
                                                       ? juce::String(entry->name.c_str())
                                                       : juce::String(i + 1);
                        c.primary = label;
                        c.baseColour = c.pressed
                                           ? juce::Colours::white.withAlpha(0.22f).getARGB()
                                           : fillTint.withAlpha(0.18f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::RetrigPicker)
            {
                // Retrig overlay: check if the machine is ISliceable (slice picker)
                // or show ratchet rates.
                const auto* machine = proc.machineForTrack(activeTrack);
                const auto* sliceable = machine
                                            ? dynamic_cast<const ISliceable*>(machine)
                                            : nullptr;
                const juce::Colour fillTint{ kScopeFill };

                if (sliceable && sliceable->hasSlices())
                {
                    // Slice-point picker: one cell per slice.
                    const int numSlices = sliceable->numSlices();
                    for (int i = 0; i < 16; ++i)
                    {
                        SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                        c.button = ControllerButton::Step;
                        c.index = i;
                        c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                        c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                        if (i >= numSlices)
                        {
                            c.base = CellState::SliceEmpty;
                            c.baseColour = kStepOutRange;
                        }
                        else
                        {
                            c.base = CellState::SlicePoint;
                            c.primary = juce::String(i + 1);
                            c.baseColour = c.pressed
                                               ? juce::Colours::white.withAlpha(0.70f).getARGB()
                                               : fillTint.withAlpha(0.20f + static_cast<float>(i) / static_cast<float>(numSlices) * 0.25f).getARGB();
                        }
                    }
                }
                else
                {
                    // Ratchet-rate picker: 8 rates (cells 0-7), remainder dimmed.
                    static constexpr std::array<const char*, 8> kRateLabels = { { "/4", "/4T", "/8", "/8T", "/16", "/16T", "/32", "/32T" } };
                    for (int i = 0; i < 16; ++i)
                    {
                        SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                        c.button = ControllerButton::Step;
                        c.index = i;
                        c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                        c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                        if (i >= 8)
                        {
                            c.base = CellState::StepOutOfRange;
                            c.baseColour = kStepOutRange;
                        }
                        else
                        {
                            c.base = CellState::RetrigRate;
                            c.primary = juce::String(kRateLabels[static_cast<std::size_t>(i)]);
                            c.baseColour = c.pressed
                                               ? juce::Colours::white.withAlpha(0.70f).getARGB()
                                               : fillTint.withAlpha(0.16f + static_cast<float>(7 - i) * 0.015f).getARGB();
                        }
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::MachinePicker)
            {
                // Machine picker (MHZ.3.5): cells encode available machine slots.
                const juce::Colour machineTint{ kScopeMachine };
                const int numMachines = proc.numAvailableMachines();
                const juce::String activeMachineId = proc.getMachineId(activeTrack);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail = i < numMachines;
                    if (!avail)
                    {
                        c.base = CellState::MachineUnavailable;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const juce::String machId{ proc.availableMachineInfo(i).id };
                        const bool isCur = (machId == activeMachineId);
                        c.base = isCur ? CellState::MachineCurrent : CellState::MachineAvailable;
                        c.baseColour = isCur
                                           ? juce::Colours::white.withAlpha(0.18f).getARGB()
                                           : machineTint.withAlpha(0.12f).getARGB();
                        c.primary = juce::String(proc.availableMachineInfo(i).displayName);
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::GeneratorHub)
            {
                // Generator hub (9.10): cells 0-2 = Euclid / Density / Vel; rest dark.
                static constexpr const char* kHubLabels[3] = { "EUCLID", "DENSITY", "VEL" };
                static constexpr CellState kHubStates[3] = {
                    CellState::GeneratorEuclid,
                    CellState::GeneratorDensity,
                    CellState::GeneratorVel,
                };
                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);
                    if (i < 3)
                    {
                        c.base = kHubStates[i];
                        c.baseColour = compatColour(kHubStates[i]);
                        c.primary = juce::String(kHubLabels[i]);
                    }
                    else
                    {
                        c.base = CellState::StepEmpty;
                        c.baseColour = kStepOutRange;
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::TrackFxPicker)
            {
                // FX insert picker (6.5): cells encode available effects for the active insert slot.
                // Cross-slot: the other insert slot's loaded effect shows a dim EffectLoadedOther hint.
                const juce::Colour fxTint{ compatColour(CellState::EffectAvailable) };
                const juce::Colour otherTint{ compatColour(CellState::EffectLoadedOther) };
                const int numEffects = proc.numAvailableEffects();
                const std::string loadedId = proc.trackInsertId(activeTrack,
                                                                ui.funcFxInsertSlot);
                const std::string otherSlotId = proc.trackInsertId(activeTrack,
                                                                   1 - ui.funcFxInsertSlot);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    if (i >= numEffects)
                    {
                        c.base = CellState::MachineUnavailable;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const auto info = proc.availableEffectInfo(i);
                        const bool isCur = (info.id == loadedId);
                        const bool isOther = !isCur && !otherSlotId.empty() && (info.id == otherSlotId);
                        c.base = isCur   ? CellState::EffectLoaded
                               : isOther ? CellState::EffectLoadedOther
                                         : CellState::EffectAvailable;
                        c.primary = juce::String(info.name.c_str());
                        c.baseColour = isCur   ? juce::Colours::white.withAlpha(0.20f).getARGB()
                                     : isOther ? otherTint.withAlpha(0.14f).getARGB()
                                               : fxTint.withAlpha(0.12f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::MasterFxPicker)
            {
                // Master FX picker (6.5): cells encode available effects for the active master unit.
                // Active unit's loaded effect = EffectLoaded; other units' effects = EffectLoadedOther.
                const juce::Colour fxTint{ compatColour(CellState::EffectAvailable) };
                const juce::Colour otherTint{ compatColour(CellState::EffectLoadedOther) };
                const int numEffects = proc.numAvailableEffects();
                const int mUnit = ui.masterFxInsertSlot;
                const bool mIsSend = (mUnit >= 2);
                const int mSlot = mIsSend ? mUnit - 2 : mUnit;
                const std::string activeId = mIsSend ? proc.masterSendId(mSlot)
                                                     : proc.masterInsertId(mSlot);
                // Collect IDs from all other units for cross-slot dim hints.
                std::array<std::string, 4> allUnitIds = {
                    proc.masterInsertId(0), proc.masterInsertId(1),
                    proc.masterSendId(0),   proc.masterSendId(1)
                };

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    if (i >= numEffects)
                    {
                        c.base = CellState::MachineUnavailable;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const auto info = proc.availableEffectInfo(i);
                        const bool isCur = (info.id == activeId);
                        bool isOther = false;
                        if (!isCur && !info.id.empty())
                        {
                            for (int u = 0; u < 4; ++u)
                            {
                                if (u != mUnit && allUnitIds[static_cast<std::size_t>(u)] == info.id)
                                {
                                    isOther = true;
                                    break;
                                }
                            }
                        }
                        c.base = isCur   ? CellState::EffectLoaded
                               : isOther ? CellState::EffectLoadedOther
                                         : CellState::EffectAvailable;
                        c.primary = juce::String(info.name.c_str());
                        c.baseColour = isCur   ? juce::Colours::white.withAlpha(0.20f).getARGB()
                                     : isOther ? otherTint.withAlpha(0.14f).getARGB()
                                               : fxTint.withAlpha(0.12f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::KeyPanel)
            {
                // KEY panel (DESIGN §4.10): row 0 cells 0..5 are the functional
                // modifier checkboxes; row 1 right (cells 14/15) are the two
                // symmetric scales. Brightness of a cell = whether it applies in
                // the current tonality; mark = whether it is set.
                const KeySig shown = keyEditorShownKey(ui, proc);
                const bool symmetric = isSymmetric(shown.scaleType);

                auto stateColour = [](CellState st) -> uint32_t {
                    switch (st)
                    {
                        case CellState::KeyModActive:    return 0xFF50B478u;
                        case CellState::KeyModAvailable: return 0xFF2E5E46u;
                        case CellState::KeyModDormant:   return 0xFF6A5A2Eu;
                        case CellState::KeySymOn:         return 0xFF7050C8u;
                        case CellState::KeySymOff:        return 0xFF332C50u;
                        default:                          return 0xFF262C30u;  // KeyModUnavail
                    }
                };

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int modCount = keyModifierCount();
                    if (i < modCount)
                    {
                        const NamedModifier m = keyModifierAt(i);
                        const bool on = hasModifier(shown, m);
                        const bool applies = on ? modifierApplies(shown, m)
                                                : (!symmetric && isCompatible(shown, m));
                        c.base = symmetric          ? CellState::KeyModUnavail
                               : (on && applies)    ? CellState::KeyModActive
                               : on                 ? CellState::KeyModDormant
                               : applies            ? CellState::KeyModAvailable
                                                    : CellState::KeyModUnavail;
                        c.baseColour = stateColour(c.base);
                        c.primary = juce::String(keyModifierLabel(i));
                        if (on && applies)
                            c.primary += " " + juce::String(degreeNameOf(shown, m));
                    }
                    else if (i == 14 || i == 15)
                    {
                        const ScaleType sym = (i == 14) ? ScaleType::WholeTone : ScaleType::Diminished;
                        const bool on = (shown.scaleType == sym);
                        c.base = on ? CellState::KeySymOn : CellState::KeySymOff;
                        c.baseColour = stateColour(c.base);
                        c.primary = (i == 14) ? "WHOLE" : "DIM";
                    }
                    else
                    {
                        c.base = CellState::StepOutOfRange;
                        c.baseColour = kStepOutRange;
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::NoteEdit)
            {
                // NoteEdit overlay: cells encode semitone note state for one octave.
                // Cells 0–11 = semitones C–B; 12–15 = dead.
                static constexpr bool kNoteIsBlack[] = { false, true, false, true, false, false, true, false, true, false, true, false };

                const juce::Colour noteTint{ kScopeNoteEdit };
                const juce::Colour stageTint = juce::Colour::fromRGB(220, 100, 60);
                const int octave = ui.noteEditOctave;
                const int trackIdx = activeTrack;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    if (i >= 12)
                    {
                        c.base = CellState::StepOutOfRange;
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
                        const auto* staged = [&]() -> const std::set<int>* {
                            auto it = ui.noteEditStaged.find(stepIdx);
                            return (it != ui.noteEditStaged.end()) ? &it->second : nullptr;
                        }();
                        for (int n = 0; n < s.trigOverride.noteCount; ++n)
                        {
                            const int noteVal = s.trigOverride.notes[static_cast<std::size_t>(n)];
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
                        c.base = CellState::NoteEditStaged;
                        c.baseColour = stageTint.withAlpha(0.12f).getARGB();
                    }
                    else if (curActive)
                    {
                        c.base = CellState::NoteEditActive;
                        c.baseColour = noteTint.withAlpha(isBlack ? 0.50f : 0.65f).getARGB();
                    }
                    else if (crossOctave)
                    {
                        c.base = CellState::NoteEditOther;
                        c.baseColour = noteTint.withAlpha(0.12f).getARGB();
                    }
                    else
                    {
                        c.base = CellState::NoteEditResting;
                        c.baseColour = juce::Colour(isBlack ? 0xff202830u : 0xff2c3540u).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::StepInspector)
            {
                // StepInspector overlay: same P-lock slot view as PLockClear but
                // entered by bare step-hold (no Func). Uses pLockClearStep (not
                // ec.heldStepIndex) so the view follows the step after a bubble-swap.
                const juce::Colour clearTint{ kScopePLock };
                const int targetStep = ui.pLockClearStep;
                const auto& stepData = proc.sequence()
                                           .tracks[static_cast<std::size_t>(activeTrack)]
                                           .steps[static_cast<std::size_t>(targetStep)];
                const int numSlots = proc.numParams(activeTrack);

                std::vector<int> lockedSlots;
                const auto& tov = stepData.trigOverride;
                if (tov.hasVelocity) lockedSlots.push_back(-2);
                if (tov.hasGate) lockedSlots.push_back(-3);
                for (int s = 0; s < numSlots; ++s)
                    if (stepData.overrides.has(s))
                        lockedSlots.push_back(s);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool hasPacked = i < static_cast<int>(lockedSlots.size());
                    if (!hasPacked)
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const int slotIdx = lockedSlots[static_cast<std::size_t>(i)];
                        const bool isStaged = ui.pLockClearStaged.count(slotIdx) > 0;
                        c.base = isStaged ? CellState::SelectorEmpty
                                          : CellState::SelectorOccupied;
                        c.baseColour = isStaged
                                           ? clearTint.withAlpha(0.10f).getARGB()
                                           : clearTint.withAlpha(0.45f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::PLockClear)
            {
                // P-Lock clear overlay: cells map to packed P-locked slot list.
                const juce::Colour clearTint{ kScopePLock };
                const int targetStep = ui.pLockClearStep;
                const auto& stepData = proc.sequence()
                                           .tracks[static_cast<std::size_t>(activeTrack)]
                                           .steps[static_cast<std::size_t>(targetStep)];
                const int numSlots = proc.numParams(activeTrack);

                std::vector<int> lockedSlots;
                const auto& tov = stepData.trigOverride;
                if (tov.hasVelocity) lockedSlots.push_back(-2);
                if (tov.hasGate) lockedSlots.push_back(-3);
                for (int s = 0; s < numSlots; ++s)
                    if (stepData.overrides.has(s))
                        lockedSlots.push_back(s);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool hasPacked = i < static_cast<int>(lockedSlots.size());
                    if (!hasPacked)
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const int slotIdx = lockedSlots[static_cast<std::size_t>(i)];
                        const bool isStaged = ui.pLockClearStaged.count(slotIdx) > 0;
                        c.base = isStaged ? CellState::SelectorEmpty
                                          : CellState::SelectorOccupied;
                        c.baseColour = isStaged
                                           ? clearTint.withAlpha(0.10f).getARGB()
                                           : clearTint.withAlpha(0.45f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::ChromaticInput)
            {
                // Chromatic piano overlay: 8 white keys (bottom row) + 5 black + 3 dead (top).
                const juce::Colour whiteKey = juce::Colour(kScopeTrack).withAlpha(0.38f);
                const juce::Colour blackKey = juce::Colour(kScopeTrack).withAlpha(0.16f);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int semitone = kPianoNoteOffset[static_cast<std::size_t>(i)];
                    if (semitone < 0)
                    {
                        c.base = CellState::StepOutOfRange;
                        c.baseColour = kStepOutRange;
                    }
                    else
                    {
                        const bool isBlack = (i < 8);  // top row (0-7) = black keys
                        c.base = isBlack ? CellState::ChromaticBlack : CellState::ChromaticWhite;
                        c.baseColour = c.pressed
                                           ? juce::Colours::white.withAlpha(0.70f).getARGB()
                                           : (isBlack ? blackKey : whiteKey).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::LevelsInput)
            {
                // Levels overlay: 16 velocity buckets (1/16..16/16 of 127).
                static const juce::Colour lowCol{ 0xFF204060u };
                static const juce::Colour highCol{ 0xFFE07030u };

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const float t = static_cast<float>(i + 1) / 16.0f;
                    const juce::Colour cellCol = lowCol.interpolatedWith(highCol, t);
                    c.base = CellState::LevelsCell;
                    c.level = t;  // store gradient position for consumer outline colour
                    c.baseColour = c.pressed
                                       ? juce::Colours::white.withAlpha(0.75f).getARGB()
                                       : cellCol.withAlpha(0.55f + t * 0.30f).getARGB();
                }
            }
            else if (activeLayer == SurfaceLayer::MorphMuteView)
            {
                // Morph+Mute view: one cell per track showing whether a fluid-mute
                // morph is authored on the track's Level slot, and how blended it is
                // now (live animation — cell brightness tracks the fader position).
                // Tap = toggle: author if none, remove if present.
                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);
                    c.primary = juce::String(i + 1);

                    if (i >= static_cast<int>(kNumTracks))
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    if (proc.hasFluidMute(i))
                    {
                        // Colour = pole A (violet) or pole B (rose) based on which
                        // pole carries silence. Brightness tracks fader distance from
                        // the silence pole: full when muted, dim when playing.
                        // crossfaderValue: 1.0 = A-side, 0.0 = B-side (slider convention).
                        const int pole = proc.fluidMutePole(i);
                        const juce::Colour poleCol = (pole != 1)
                                                         ? juce::Colour(kScopeMorphA)   // A=silence → violet
                                                         : juce::Colour(kScopeMorphB);  // B=silence → rose
                        // brightness=1 when fader is at the silence pole.
                        const float brightness = (pole != 1)
                                                     ? crossfaderValue              // A-pole: full at crossfader=1 (A)
                                                     : (1.0f - crossfaderValue);    // B-pole: full at crossfader=0 (B)
                        const float alpha = 0.25f + brightness * 0.60f;
                        c.base = CellState::MorphPoleActive;
                        c.baseColour = poleCol.withAlpha(alpha).getARGB();
                    }
                    else
                    {
                        c.base = CellState::MorphPoleDark;
                        c.baseColour = c.pressed
                                           ? juce::Colour(kScopeMorphDim).withAlpha(0.40f).getARGB()
                                           : juce::Colour(kStepInactive).withAlpha(0.20f).getARGB();
                    }
                }
            }
            else if (activeLayer == SurfaceLayer::MuteView)
            {
                // Mute re-skin (Slice 3): cells encode per-track mute state so
                // paintStepRows can consume a single model path and add press feedback.
                // Bare Mute = global mute view; Scene+Mute = scene-mute view (the
                // scene's active-mask), in a distinct colour.
                const bool sceneMute = ui.sceneHeld;
                const uint32_t muteCol = sceneMute ? kScopePMute : kScopeMute;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail = i < static_cast<int>(kNumTracks);
                    if (!avail)
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const bool muted = sceneMute
                                           ? proc.getPatternMute(i)
                                           : proc.getGlobalMute(i);

                    c.base = muted ? CellState::MuteMuted : CellState::MuteAudible;
                    const juce::Colour muteJCol{ muteCol };
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
            else if (activeLayer == SurfaceLayer::LengthEdit)
            {
                const bool broadcastMode = ui.morphHeld && ui.funcHeld;
                const juce::Colour tint = broadcastMode
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
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int absIdx = stepPage * 16 + i;
                    (void)broadcastMode;  // both modes use focused track for ref currently

                    c.base = lengthEditCellState(absIdx, phraseLen);
                    const float alpha = (c.base == CellState::LengthInRun)      ? 0.35f
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
            else if (activeLayer == SurfaceLayer::MorphStepView)
            {
                const int nmp = (activeTrack >= 0) ? proc.numParams(activeTrack) : 0;
                const int anchorSec = (slotOffset < nmp && activeTrack >= 0)
                                          ? proc.paramSpec(activeTrack, slotOffset).sectionIndex
                                          : -1;

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int mzLocal = i % 8;             // slot index within MZ page
                    const int absSlot = slotOffset + mzLocal;
                    const bool poleA = (i < 8);           // top row = A, bottom = B

                    // Blank out-of-schema and cross-section slots.
                    const bool outOfSchema = absSlot >= nmp;
                    const bool outOfSec = !outOfSchema && activeTrack >= 0 && proc.paramSpec(activeTrack, absSlot).sectionIndex != anchorSec;
                    if (outOfSchema || outOfSec)
                    {
                        c.base = CellState::MorphPoleDark;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const auto& ms = morphView.slots[static_cast<std::size_t>(mzLocal)];
                    const auto pole = poleA ? ms.a : ms.b;
                    using MPS = MorphViewState::PoleState;

                    if (pole == MPS::Active)
                    {
                        c.base = CellState::MorphPoleActive;
                        c.baseColour = juce::Colour(kScopeMorph).withAlpha(0.80f).getARGB();
                    }
                    else if (pole == MPS::Dormant)
                    {
                        c.base = CellState::MorphPoleDormant;
                        c.baseColour = c.pressed
                                           ? juce::Colour(kScopeMorph).withAlpha(0.50f).getARGB()
                                           : juce::Colour(kScopeMorphDim).withAlpha(0.70f).getARGB();
                    }
                    else
                    {
                        c.base = CellState::MorphPoleDark;
                        c.baseColour = c.pressed
                                           ? juce::Colour(kScopeMorphDim).withAlpha(0.40f).getARGB()
                                           : juce::Colour(kStepInactive).withAlpha(0.35f).getARGB();
                    }

                    // Store the param label for the inline screen residual.
                    c.primary = (activeTrack >= 0)
                                    ? juce::String(proc.paramSpec(activeTrack, absSlot).label)
                                    : juce::String{};
                }
            }
            // ── End morph step view ──────────────────────────────────────────────

            else if (activeLayer == SurfaceLayer::ScopeSelector)
            {
                // Scope re-skin (Slice 4): cells encode track/pattern/part selector state.
                // fill colour + pressed → builder; border/text/badge → inline screen residuals.
                const juce::Colour scopeTint = scopeColourFromState(ui);

                int maxAvail = 0;
                int activeIdx = 0;
                if (ui.trackHeld)
                {
                    maxAvail = static_cast<int>(kNumTracks);
                    activeIdx = activeTrack;
                }
                else if (ui.phraseScopeHeld)
                {
                    // Phase 7: show the focused track's phrase pool (16 phrases).
                    // The fill marks the CURRENT playing phrase (a live deviation if
                    // one is active, else the floor); the home border (below) marks
                    // the scene's global phrase, so deviation reads as fill ≠ border.
                    maxAvail = kPhrasesPerTrack;
                    const int at = activeTrack >= 0 ? activeTrack : 0;
                    activeIdx = proc.isTrackDeviated(at)
                                    ? proc.deviationPhraseIdxForTrack(at)
                                    : proc.activeSectionIdx();
                }
                else if (ui.songHeld)
                {
                    // Phase 7: show song slots within the Set.
                    maxAvail = kNumSongs;
                    activeIdx = proc.activePieceIdx();
                }
                else // sceneHeld
                {
                    // Phase 7: show sections within the active Song.
                    maxAvail = kScenesPerSong;
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
                // Dual-marker selector (DESIGN §4.7): the scene's diagonal home row.
                const int globalIdx = ui.phraseScopeHeld ? proc.activeSectionIdx() : -1;

                std::array<bool, 16> slotEmpty{};
                for (int i = 0; i < maxAvail; ++i)
                {
                    if (ui.trackHeld)
                        slotEmpty[static_cast<std::size_t>(i)] = proc.isTrackEmpty(i);
                    else if (ui.sceneHeld)
                        slotEmpty[static_cast<std::size_t>(i)] =
                            (i != activeIdx) && !proc.sceneSlotOccupied(i);
                    else if (ui.songHeld)
                        slotEmpty[static_cast<std::size_t>(i)] = !proc.songSlotOccupied(i);
                    else
                        slotEmpty[static_cast<std::size_t>(i)] = false;  // phrases: all rows exist
                }

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const bool avail = i < maxAvail;
                    const bool isEmpty = avail && slotEmpty[static_cast<std::size_t>(i)];
                    const bool isCurrent = avail && !isEmpty && (i == activeIdx);
                    // Phase 7: section queue badge (Part scope) or deviation badge (Pattern scope).
                    const int cpos = (ui.sceneHeld && avail)
                                         ? sectionQueuePos[static_cast<std::size_t>(i)]
                                         : 0;
                    const bool isDeviated = ui.phraseScopeHeld && avail && devTrack >= 0 && proc.isTrackDeviated(devTrack) && i == proc.deviationPhraseIdxForTrack(devTrack);
                    const bool isNext = cpos == 1;
                    const bool isChain = cpos >= 2;

                    // CellState token + fill colour
                    if (!avail)
                    {
                        c.base = CellState::SelectorOutRange;
                        c.baseColour = scopeTint.withAlpha(0.04f).getARGB();
                    }
                    else if (isEmpty)
                    {
                        c.base = CellState::SelectorEmpty;
                        c.baseColour = scopeTint.withAlpha(0.09f).getARGB();
                    }
                    else if (isNext)
                    {
                        c.base = CellState::SelectorNext;
                        c.baseColour = scopeTint.withAlpha(0.80f).getARGB();
                    }
                    else if (isChain)
                    {
                        c.base = CellState::SelectorChain;
                        c.baseColour = scopeTint.withAlpha(0.42f).getARGB();
                    }
                    else if (isDeviated)
                    {
                        // Phrase currently playing due to a live track deviation.
                        c.base = CellState::SelectorDeviated;
                        c.baseColour = scopeTint.withAlpha(0.70f).getARGB();
                    }
                    else if (isCurrent)
                    {
                        c.base = CellState::SelectorCurrent;
                        c.baseColour = juce::Colours::white.interpolatedWith(scopeTint, 0.30f).getARGB();
                    }
                    else
                    {
                        c.base = CellState::SelectorOccupied;
                        c.baseColour = scopeTint.withAlpha(0.18f).getARGB();
                    }

                    // Dual-marker (DESIGN §4.7): border the scene's global/home
                    // phrase, so a deviation reads as fill (current) ≠ border (home).
                    if (ui.phraseScopeHeld && avail && i == globalIdx)
                    {
                        c.border.present = true;
                        c.border.token = CellState::SelectorHome;
                        c.border.colour = kHomeAmber;
                    }

                    // level encodes queue position for badge rendering in paintStepRows
                    c.level = static_cast<float>(cpos);
                }
            }
            else
            {

            // Track length + playhead position
                const bool validTrack = activeTrack >= 0 && activeTrack < static_cast<int>(kNumTracks);
                auto* lenP = validTrack
                                 ? proc.apvts().getRawParameterValue(ParamIDs::trackLength(activeTrack))
                                 : nullptr;
                auto* divP = validTrack
                                 ? proc.apvts().getRawParameterValue(ParamIDs::trackDivider(activeTrack))
                                 : nullptr;
                const int trackLen = lenP ? std::max(1, static_cast<int>(lenP->load())) : 16;
                const int subdivIdx = divP ? std::clamp(static_cast<int>(divP->load()),
                                                         kSubdivMin, kSubdivMax) : kSubdivDefault;
                const double divPpq = subdivisionPpqFromIndex(subdivIdx);

                int playheadAbs = -1;
                std::int64_t loopBase = 0;
                if (divPpq > 0.0 && trackLen > 0 && validTrack)
                {
                    const double cumPpq = proc.clock().cumulativePpq();
                    const auto stepNum = static_cast<std::int64_t>(cumPpq / divPpq);
                    playheadAbs = static_cast<int>(stepNum % trackLen);
                    loopBase = (stepNum / static_cast<std::int64_t>(trackLen)) * static_cast<std::int64_t>(trackLen);
                    model.playheadPhase = static_cast<float>(std::fmod(cumPpq, divPpq) / divPpq);
                }

                const int baseStep = stepPage * 16;
                const bool fillOn = proc.fillActive();
                const auto& trk = proc.sequence().tracks[static_cast<std::size_t>(
                    validTrack ? activeTrack : 0)];
                const auto preview = stepPagePreview(trk, trackLen, loopBase, baseStep, fillOn);
                const auto& ctx = ec;
                const auto& heldSteps = ctx.heldSteps();

            // Scope body colour: adopt whichever modifier is held (fixes always-green).
            // Morph has no per-step action, so exclude it from step tinting.
                const bool morphOnlyScope = ui.morphHeld && !ui.trackHeld && !ui.phraseScopeHeld && !ui.sceneHeld && !ui.songHeld;
                const juce::Colour scopeBodyCol = fillOn
                                                      ? juce::Colour(kScopeFill)
                                                      : (morphOnlyScope ? juce::Colour(kScopeStep) : scopeColourFromState(ui));
                const juce::Colour inactiveCol(kStepInactive);

                for (int i = 0; i < 16; ++i)
                {
                    SurfaceCell& c = model.step[static_cast<std::size_t>(i)];
                    c.button = ControllerButton::Step;
                    c.index = i;
                    c.keyHint = kStepKeyHints[static_cast<std::size_t>(i)];
                    c.pressed = physPressed(kStepKeyCodes[i], ControllerButton::Step, i);

                    const int absIdx = baseStep + i;
                    const bool inRange = absIdx < trackLen && validTrack;

                    if (!inRange)
                    {
                        c.base = CellState::StepOutOfRange;
                        c.baseColour = kStepOutRange;
                        continue;
                    }

                    const auto& stepRef = trk.steps[static_cast<std::size_t>(absIdx)];
                    const bool hasTrig = stepRef.trig;
                    // While moving, the physical key (in heldSteps) no longer holds the
                    // moved content after a swap — follow pLockClearStep so the moved
                    // step stays highlighted at its current slot as it hops.
                    const bool isHeld = ui.stepMoveActive
                        ? (activeTrack == ui.pLockClearTrack && absIdx == ui.pLockClearStep)
                        : (ctx.heldTrackIndex() == activeTrack && std::find(heldSteps.begin(), heldSteps.end(), absIdx) != heldSteps.end());
                    const bool hasLock = !stepRef.overrides.empty();
                    const bool hasFillLock = !stepRef.fillOverrides.empty();
                    const bool isHead = (absIdx == playheadAbs);
                    const FillTrigState fts = stepRef.fillTrigState;

                    const float prob = [&]() -> float {
                        if (!hasTrig && fts == FillTrigState::On && !fillOn)
                        {
                            const auto& cond = stepRef.condition.isTrivial()
                                                   ? trk.baseCond
                                                   : stepRef.condition;
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

                    c.level = lerpFrac;
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
                        c.border.colour = kStepPlayhead;
                        c.border.token = CellState::StepPlayhead;
                    }
                    if (hasLock)
                    {
                        c.dot.present = true;
                        c.dot.colour = kStepPLock;
                    }
                // strip: fill-mode border takes priority over fill P-Lock badge
                    if (fillOn && fts != FillTrigState::Inherit)
                    {
                        c.strip.present = true;
                        c.strip.colour = (fts == FillTrigState::On) ? kStepFillAdd : kStepFillSuppress;
                    }
                    else if (hasFillLock)
                    {
                        c.strip.present = true;
                        c.strip.colour = kStepFillPLock;
                    }
                    if (ctx.isLatched(absIdx))
                    {
                        c.pip.present = true;
                        c.pip.colour = kScopeStep;
                    }
                }

            } // end else (normal step grid)
        }

        // =====================================================================
        // Manipulation-zone encoder slots (§35.8.5)
        // Fills model.slots[0..7] for the 8 encoders at slotOffset..slotOffset+7.
        // Meta bands (resolveMetaBand) take priority over the machine-param path so
        // the controller sees exactly what the on-screen ManipulationZone shows.
        // =====================================================================
        model.crossfader = crossfaderValue;
        {
            const MetaBand band = resolveMetaBand(ui);

            if (band != MetaBand::None)
            {
                // Meta surface: fill slots from MetaFieldView.
                const int swScope = swingScopeFor(ui);
                const auto views = buildMetaBand(band, swScope, proc, activeTrack, ec, ui);

                for (int i = 0; i < 8; ++i)
                {
                    const auto vi = static_cast<std::size_t>(i);
                    auto& slot = model.slots[vi];
                    const auto& v = views[vi];
                    slot.inRange = v.active;
                    slot.label = v.label;
                    slot.sectionLabel = {};   // meta bands have no owning section
                    slot.valueText = v.valueText;
                    slot.hasOverride = v.hasOverride;
                    slot.ringMode = v.ringMode;
                    slot.marks = v.marks;
                    const float range = v.maxValue - v.minValue;
                    slot.position = (range > 0.0f && v.active)
                                        ? juce::jlimit(0.0f, 1.0f, (v.value - v.minValue) / range)
                                        : 0.0f;
                }
            }
            else
            {
                // Machine-param path: Override-ELSE-Base resolution.
                const bool stepHeld = ec.isActiveForEditing() && ec.heldTrackIndex() == activeTrack;
                const int heldStep = ec.heldStepIndex();
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
                    slot.label = spec.label.isEmpty() ? juce::String(absSlot) : spec.label;

                    // Owning section name (machine label, falling back to canonical).
                    {
                        const int secIdx = juce::jlimit(0, IMachine::kMaxSections - 1,
                                                        spec.sectionIndex);
                        const auto& secInfo = proc.section(activeTrack, secIdx);
                        slot.sectionLabel = secInfo.label.isNotEmpty()
                                                ? secInfo.label
                                                : juce::String(IMachine::kCanonicalSectionNames[static_cast<std::size_t>(secIdx)]);
                    }

                    float value = proc.baseParamValue(activeTrack, absSlot);
                    bool hasLock = false;

                    if (stepHeld && heldStep >= 0)
                    {
                        const auto& s = proc.sequence().tracks[static_cast<std::size_t>(activeTrack)].steps[static_cast<std::size_t>(heldStep)];
                        if (fillEdit)
                        {
                            if (s.fillOverrides.has(absSlot))
                            {
                                value = s.fillOverrides.get(absSlot, value);
                                hasLock = true;
                            }
                            else
                            {
                                value = s.overrides.get(absSlot, value);
                            }
                        }
                        else
                        {
                            value = s.overrides.get(absSlot, value);
                            hasLock = s.overrides.has(absSlot);
                        }
                    }

                    slot.hasOverride = hasLock;
                    slot.valueText = formatParamValue(value, spec);
                    if (hasLock)
                        slot.valueText += " *";

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
        }

        // ── gridBanner ───────────────────────────────────────────────────────
        // layerBanner() has an exhaustive switch (no default:) so adding a new
        // SurfaceLayer without wiring its banner text is a compile error.
        model.gridBanner = layerBanner(activeLayer, ui);

        // ── pageDots ─────────────────────────────────────────────────────────
        // Per-section: how many pages does the active track's section have?
        {
            const int ti = activeTrack;
            const int numSecs = proc.numSections(ti);
            for (int s = 0; s < IMachine::kMaxSections; ++s)
            {
                int totalPages = 0;
                if (s < numSecs)
                {
                    const auto info = proc.section(ti, s);
                    if (info.firstSlot >= 0)
                        totalPages = std::max(1, info.pageCount);
                }
                // Extension sections with same parentCanonical add more pages.
                for (int ex = IMachine::kMaxSections; ex < numSecs; ++ex)
                {
                    const auto info = proc.section(ti, ex);
                    if (info.parentCanonical == s && info.firstSlot >= 0)
                        totalPages += std::max(1, info.pageCount);
                }

                auto& dots = model.pageDots[static_cast<std::size_t>(s)];
                dots.count = static_cast<uint8_t>(totalPages);
                dots.active = (totalPages > 0)
                                  ? static_cast<uint8_t>(ui.trackPage
                                                             [static_cast<std::size_t>(ti)]
                                                             [static_cast<std::size_t>(s)] %
                                                         totalPages)
                                  : 0u;
            }
        }

        // 9.10 §19: F/J home-row anchor markers — present in every layer.
        // Step indices 1 (F) and 4 (J) are the keyboard home-row anchors.
        model.step[1].homeKey = true;
        model.step[4].homeKey = true;

        // 9.12 gesture-affordance pass: derive tap/hold/doubleTap affordance slots and
        // the primary-promotion token for every control cell from the grammar (SSOT).
        // Applied regardless of heldMods so context-sensitive slots are always current.
        //
        // Label authority (PRINCIPLES §20): for cells whose grammar row carries a
        // non-empty primary (modifiers, tap, nav, verbs), THIS pass is the source of
        // truth for c.primary/funcHint — the per-cell assignments earlier in this
        // function are a pre-fill it supersedes. Section cells are the exception: their
        // grammar rows have an empty primary (labels come from KeyLabel/
        // ScopedSectionMatrix in the builder), so deriveSlots leaves their c.primary
        // untouched. Do not "dedupe" by deleting the builder section labels.
        {
            // Specificity of a resolved row = popcount(requiredMods); -1 if no match.
            auto spec = [](const KeyBinding& b) {
                return (b.action == ActionId::None) ? -1
                       : static_cast<int>(std::popcount(b.requiredMods));
            };
            auto deriveSlots = [&](SurfaceCell& c) {
                const auto tap  = resolveBinding(c.button, c.index, heldMods,
                                                 SurfaceLayer::Base, Gesture::Tap);
                const auto hold = resolveBinding(c.button, c.index, heldMods,
                                                 SurfaceLayer::Base, Gesture::Hold);
                const auto dbl  = resolveBinding(c.button, c.index, heldMods,
                                                 SurfaceLayer::Base, Gesture::DoubleTap);

                // Primary = the most-specific of {tap, hold} for the CURRENT heldMods,
                // so a held modifier promotes its context action into the large slot
                // (e.g. Func+Song = GLOBAL, not the bare at-rest SONG). On a tie prefer
                // an explicit promoted row, else Hold (press-to-engage modifiers).
                const int ts = spec(tap), hs = spec(hold);
                const KeyBinding* prim = &tap;
                // Section keys carry a real tap action with an EMPTY primary — the
                // label is owned by the builder/ScopedSectionMatrix. A label-bearing
                // hold/dbl row (e.g. FX "PICK FX" / "PICK MASTER FX") must NEVER win
                // the primary slot, even when it is more specific (a held scope);
                // otherwise the section name is clobbered. Force prim=tap so the
                // builder primary survives and the hold label flows to c.holdLabel.
                const bool builderOwnsPrimary =
                    (tap.action != ActionId::None && tap.primary[0] == u8'\0');
                if (builderOwnsPrimary) prim = &tap;
                else if (hs > ts)       prim = &hold;
                else if (ts > hs)       prim = &tap;
                else if (hold.promoted) prim = &hold;
                else if (tap.promoted)  prim = &tap;
                else if (hs >= 0)       prim = &hold;
                const int primSpec = spec(*prim);

                c.primaryGesture = prim->gesture;
                if (prim->action != ActionId::None && prim->primary[0] != u8'\0')
                    c.primary = juce::String(prim->primary);

                // A secondary rail shows a gesture only when it carries a DISTINCT
                // action that is at least as specific as the primary — this drops the
                // duplicate-action phantom (a modifier's tap == its hold scope action)
                // and stops bare at-rest actions leaking into a held-modifier context.
                auto secVisible = [&](const KeyBinding& b) {
                    return b.action != ActionId::None
                        && b.action != prim->action
                        && spec(b) >= primSpec;
                };
                c.tapLabel  = (prim->gesture != Gesture::Tap  && secVisible(tap))
                              ? juce::String(tap.primary)  : juce::String();
                c.holdLabel = (prim->gesture != Gesture::Hold && secVisible(hold))
                              ? juce::String(hold.primary) : juce::String();
                c.doubleTapLabel = secVisible(dbl) ? juce::String(dbl.primary) : juce::String();

                // Under Func, section keys are remapped to the meta-section layer
                // (ButtonLayers), so a Func+section never reaches the FX picker
                // arming in the Section dispatch. Suppress the section hold/tap rails
                // so the FX cell can't promise a picker (e.g. Song+Func showing
                // "PICK MASTER FX") that the Func path won't fire. The picker is a
                // non-Func gesture: Track+hold / Song+hold.
                if ((heldMods & kModFunc) != 0 && c.button == ControllerButton::Section)
                {
                    c.holdLabel = juce::String();
                    c.tapLabel  = juce::String();
                }

                // Func-variant preview (bottom chip) — at rest only; when Func is held
                // the variant is already promoted to the primary slot above.
                if (heldMods == kModNone) {
                    const auto fn = resolveBinding(c.button, c.index, kModFunc,
                                                   SurfaceLayer::Base, Gesture::Tap);
                    // Only override when the Func row carries a real label. Section
                    // rows are action-only (empty primary) with the label supplied by
                    // the builder/ScopedSectionMatrix — don't blank it.
                    if (fn.action != ActionId::None && fn.action != tap.action
                        && fn.primary[0] != u8'\0')
                        c.funcHint = juce::String(fn.primary);
                }
            };

            for (auto& c : model.modifiers)
                deriveSlots(c);
            deriveSlots(model.tap);
            deriveSlots(model.navUp);
            for (auto& c : model.section)
                deriveSlots(c);
            for (auto& c : model.functionRow)
                deriveSlots(c);
            for (auto& c : model.step)
                deriveSlots(c);
        }

        return model;
    }

}  // namespace lockstep
