#include "KeyBindings.h"
#include "../state/UiState.h"
#include "../io/EditMode.h"
#include <bit>      // std::popcount (C++20)

namespace lockstep
{
    using CB  = ControllerButton;
    using AId = ActionId;
    using SL  = SurfaceLayer;
    using CS  = CellState;

    // =========================================================================
    // kKeyBindingsData — the canonical per-key binding table.
    //
    // Row ordering within a group: most-specific (highest requiredMods popcount)
    // first for readability. resolveBinding() uses popcount, not order.
    //
    // Sections (5-0): carry ActionId only; labels deferred to ScopedSectionMatrix.
    //
    // Nav combos with Morph+Func: explicit combined rows (popcount 2) override
    // the ambiguous single-mod tie and preserve exact current behaviour.
    // =========================================================================
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
    static const KeyBinding kKeyBindingsData[] = {

        // ── Func modifier (key 1) ───────────────────────────────────────────
        { CB::Func,        -1, kModNone,              SL::Base, AId::HoldFuncScope,      u8"FUNC",    u8"",       CS::Resting },

        // ── Track modifier (key 2) ──────────────────────────────────────────
        { CB::TrackScope,  -1, kModFunc,               SL::Base, AId::OpenMachinePicker, u8"KIT",     u8"",       CS::FuncHeld },
        { CB::TrackScope,  -1, kModNone,               SL::Base, AId::HoldTrackScope,   u8"TRACK",   u8"KIT",    CS::Resting  },

        // ── Phrase scope (key Q) ─────────────────────────────────────────────
        { CB::PhraseScope, -1, kModNone,               SL::Base, AId::HoldPhraseScope,  u8"PHRASE",  u8"",       CS::Resting  },

        // ── Scene scope (key W) ──────────────────────────────────────────────
        { CB::SceneScope,  -1, kModNone,               SL::Base, AId::HoldSceneScope,   u8"SCENE",   u8"",       CS::Resting  },

        // ── Morph scope (key A) ──────────────────────────────────────────────
        { CB::MorphScope,  -1, kModNone,               SL::Base, AId::HoldMorphScope,   u8"MORPH",   u8"",       CS::Resting  },

        // ── Song scope (key S) ───────────────────────────────────────────────
        { CB::SongScope,   -1, kModFunc,               SL::Base, AId::FocusGlobal,      u8"GLOBAL",  u8"",       CS::FuncHeld },
        { CB::SongScope,   -1, kModNone,               SL::Base, AId::HoldSongScope,    u8"SONG",    u8"GLOBAL", CS::Resting  },

        // ── Mute scope (key Z) ───────────────────────────────────────────────
        // Scene+Mute = scene-mute grid view (S-MUTE). Bare Mute = track/fill mute.
        { CB::MuteScope,   -1, kModScene,              SL::Base, AId::HoldSceneMuteView, u8"S-MUTE", u8"",       CS::Resting  },
        { CB::MuteScope,   -1, kModNone,               SL::Base, AId::HoldMuteScope,    u8"MUTE",    u8"",       CS::Resting  },

        // ── Fill scope (key X) ───────────────────────────────────────────────
        { CB::FillScope,   -1, kModNone,               SL::Base, AId::HoldFillScope,    u8"FILL",    u8"",       CS::Resting  },

        // ── TAP (key 3) ──────────────────────────────────────────────────────
        { CB::TapTempo,    -1, kModFunc,               SL::Base, AId::MetronomeToggle,  u8"MET",     u8"",       CS::Resting  },
        { CB::TapTempo,    -1, kModNone,               SL::Base, AId::TapTempo,         u8"TAP",     u8"MET",    CS::Resting  },

        // ── NavUp / ↑ (key 4) ─────────────────────────────────────────────────
        // Func+Track (popcount 2) = cycle input mode. Explicit row beats any tie.
        // Func+Morph (popcount 2) = LengthDouble — preserves current dispatch order
        // where funcHeld&&!trackHeld shows ×2 even when morphHeld is also true.
        { CB::NavUp,       -1, kModFunc | kModTrack,   SL::Base, AId::CycleInputModeUp, u8"↑",  u8"",   CS::Resting },
        { CB::NavUp,       -1, kModFunc | kModMorph,   SL::Base, AId::LengthDouble,     u8"×2", u8"",   CS::Resting },
        { CB::NavUp,       -1, kModFunc,               SL::Base, AId::LengthDouble,     u8"×2", u8"",   CS::Resting },
        { CB::NavUp,       -1, kModTrack,              SL::Base, AId::CycleInputModeUp, u8"↑",  u8"",   CS::Resting },
        { CB::NavUp,       -1, kModMorph,              SL::Base, AId::MorphPickPoleA,   u8"A",  u8"",   CS::Resting },
        { CB::NavUp,       -1, kModNone,               SL::Base, AId::NavTrackUp,       u8"↑",  u8"×2", CS::Resting },

        // ── NavLeft / ← (key E) ───────────────────────────────────────────────
        { CB::NavLeft,     -1, kModFunc | kModTrack,   SL::Base, AId::CycleInputModeLeft,  u8"←",    u8"",     CS::Resting },
        { CB::NavLeft,     -1, kModFunc,               SL::Base, AId::RotateLeft,           u8"←ROT", u8"",     CS::Resting },
        { CB::NavLeft,     -1, kModTrack,              SL::Base, AId::CycleInputModeLeft,   u8"←",    u8"",     CS::Resting },
        { CB::NavLeft,     -1, kModNone,               SL::Base, AId::NavPageLeft,          u8"←",    u8"←ROT", CS::Resting },

        // ── NavDown / ↓ (key R) ───────────────────────────────────────────────
        // Func+Morph = LengthHalve — same pattern as NavUp: funcHeld wins over morphHeld.
        { CB::NavDown,     -1, kModFunc | kModTrack,   SL::Base, AId::CycleInputModeDown,  u8"↓",  u8"",   CS::Resting },
        { CB::NavDown,     -1, kModFunc | kModMorph,   SL::Base, AId::LengthHalve,         u8"÷2", u8"",   CS::Resting },
        { CB::NavDown,     -1, kModFunc,               SL::Base, AId::LengthHalve,         u8"÷2", u8"",   CS::Resting },
        { CB::NavDown,     -1, kModTrack,              SL::Base, AId::CycleInputModeDown,  u8"↓",  u8"",   CS::Resting },
        { CB::NavDown,     -1, kModMorph,              SL::Base, AId::MorphPickPoleB,      u8"B",  u8"",   CS::Resting },
        { CB::NavDown,     -1, kModNone,               SL::Base, AId::NavOctaveDown,       u8"↓",  u8"÷2", CS::Resting },

        // ── NavRight / → (key T) ─────────────────────────────────────────────
        { CB::NavRight,    -1, kModFunc | kModTrack,   SL::Base, AId::CycleInputModeRight, u8"→",    u8"",     CS::Resting },
        { CB::NavRight,    -1, kModFunc,               SL::Base, AId::RotateRight,          u8"ROT→", u8"",     CS::Resting },
        { CB::NavRight,    -1, kModTrack,              SL::Base, AId::CycleInputModeRight,  u8"→",    u8"",     CS::Resting },
        { CB::NavRight,    -1, kModNone,               SL::Base, AId::NavPageRight,         u8"→",    u8"ROT→", CS::Resting },

        // ── VerbYes / SNAP (key Y) ────────────────────────────────────────────
        { CB::VerbYes,     -1, kModFunc,               SL::Base, AId::VerbRestore,    u8"RESTORE", u8"",        CS::Resting },
        { CB::VerbYes,     -1, kModNone,               SL::Base, AId::VerbSnapshot,   u8"SNAP",    u8"RESTORE", CS::Resting },

        // ── VerbRecord / REC (key U) ──────────────────────────────────────────
        // Scope+VerbRecord = COPY for all section-suite scopes except Morph.
        { CB::VerbRecord,  -1, kModTrack,              SL::Base, AId::VerbCopy,       u8"COPY",  u8"",   CS::Resting },
        { CB::VerbRecord,  -1, kModPhrase,             SL::Base, AId::VerbCopy,       u8"COPY",  u8"",   CS::Resting },
        { CB::VerbRecord,  -1, kModScene,              SL::Base, AId::VerbCopy,       u8"COPY",  u8"",   CS::Resting },
        { CB::VerbRecord,  -1, kModSong,               SL::Base, AId::VerbCopy,       u8"COPY",  u8"",   CS::Resting },
        { CB::VerbRecord,  -1, kModNone,               SL::Base, AId::VerbRecord,     u8"REC",   u8"",   CS::Resting },

        // ── VerbPlay / PLAY (key I) ───────────────────────────────────────────
        // Scope+VerbPlay = PASTE for all section-suite scopes except Morph.
        { CB::VerbPlay,    -1, kModTrack,              SL::Base, AId::VerbPaste,      u8"PASTE", u8"",   CS::Resting },
        { CB::VerbPlay,    -1, kModPhrase,             SL::Base, AId::VerbPaste,      u8"PASTE", u8"",   CS::Resting },
        { CB::VerbPlay,    -1, kModScene,              SL::Base, AId::VerbPaste,      u8"PASTE", u8"",   CS::Resting },
        { CB::VerbPlay,    -1, kModSong,               SL::Base, AId::VerbPaste,      u8"PASTE", u8"",   CS::Resting },
        { CB::VerbPlay,    -1, kModNone,               SL::Base, AId::VerbPlay,       u8"PLAY",  u8"",   CS::Resting },

        // ── VerbClear / CLEAR (key O) ─────────────────────────────────────────
        // Func+Clear = Delete. Scope+Clear = CLEAR for ALL scopes incl. Morph.
        { CB::VerbClear,   -1, kModFunc,               SL::Base, AId::VerbDelete,     u8"DEL",   u8"",    CS::Resting },
        { CB::VerbClear,   -1, kModTrack,              SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"",   CS::Resting },
        { CB::VerbClear,   -1, kModPhrase,             SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"",   CS::Resting },
        { CB::VerbClear,   -1, kModScene,              SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"",   CS::Resting },
        { CB::VerbClear,   -1, kModMorph,              SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"",   CS::Resting },
        { CB::VerbClear,   -1, kModSong,               SL::Base, AId::VerbScopedClear, u8"CLEAR", u8"",   CS::Resting },
        { CB::VerbClear,   -1, kModNone,               SL::Base, AId::VerbClear,      u8"CLEAR", u8"DEL", CS::Resting },

        // ── VerbNo / YES (key P) ─────────────────────────────────────────────
        { CB::VerbNo,      -1, kModFunc,               SL::Base, AId::VerbCancel,     u8"NO",  u8"",   CS::Resting },
        { CB::VerbNo,      -1, kModNone,               SL::Base, AId::VerbConfirm,    u8"YES", u8"NO", CS::Resting },

        // ── Section keys (5-0): ActionId only; labels from ScopedSectionMatrix ──
        { CB::Section,      0, kModFunc,               SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section,      1, kModFunc,               SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section,      2, kModFunc,               SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section,      3, kModFunc,               SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section,      4, kModFunc,               SL::Base, AId::SelectMetaSection, u8"", u8"", CS::Resting },
        { CB::Section,      5, kModFunc,               SL::Base, AId::OpenTrackFxPicker, u8"", u8"", CS::Resting },
        { CB::Section,      0, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
        { CB::Section,      1, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
        { CB::Section,      2, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
        { CB::Section,      3, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
        { CB::Section,      4, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
        { CB::Section,      5, kModNone,               SL::Base, AId::SelectSection,     u8"", u8"", CS::Resting },
    };
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays)

    const std::span<const KeyBinding> kKeyBindings { kKeyBindingsData };

    // =========================================================================
    // heldModsFromUiState
    // =========================================================================

    uint16_t heldModsFromUiState(const UiState& ui) noexcept
    {
        uint16_t m = kModNone;
        if (ui.funcHeld)         m |= kModFunc;
        if (ui.trackHeld)        m |= kModTrack;
        if (ui.phraseScopeHeld)  m |= kModPhrase;
        if (ui.sceneHeld)        m |= kModScene;
        if (ui.morphHeld)        m |= kModMorph;
        if (ui.songHeld)         m |= kModSong;
        if (ui.muteHeld)         m |= kModMute;
        if (ui.fillHeld)         m |= kModFill;
        return m;
    }

    // =========================================================================
    // resolveBinding
    // =========================================================================

    static const KeyBinding kNoBinding { CB::None };

    const KeyBinding& resolveBinding(ControllerButton b, int idx,
                                     uint16_t heldMods,
                                     SurfaceLayer layer) noexcept
    {
        const KeyBinding* best  = nullptr;
        int               score = -1;

        for (const auto& row : kKeyBindings)
        {
            if (row.button != b)     continue;
            if (row.layer  != layer) continue;
            // index -1 in the table matches any idx; a specific index must match exactly.
            if (row.index != -1 && row.index != idx) continue;
            // All required modifiers must be held.
            if ((heldMods & row.requiredMods) != row.requiredMods) continue;

            const int s = std::popcount(row.requiredMods);
            if (s > score)
            {
                best  = &row;
                score = s;
            }
            else if (s == score && best != nullptr)
            {
                // Tiebreak: first differing bit in kScopePriority order wins.
                // Order (high→low priority): Track, Phrase, Scene, Mute, Morph,
                //   Song, Fill, Func (weakest).
                // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
                static constexpr uint16_t kOrder[] = {
                    kModTrack, kModPhrase, kModScene, kModMute,
                    kModMorph, kModSong,   kModFill,  kModFunc,
                };
                // NOLINTEND(cppcoreguidelines-avoid-c-arrays)
                const uint16_t diffMask = row.requiredMods ^ best->requiredMods;
                for (const auto bit : kOrder)
                {
                    if ((diffMask & bit) == 0) continue;
                    if ((row.requiredMods & bit) != 0)
                        best = &row;
                    break;
                }
            }
        }

        return best ? *best : kNoBinding;
    }

} // namespace lockstep
