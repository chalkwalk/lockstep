#include "KeyAffordances.h"
#include <array>

namespace lockstep
{
    // =========================================================================
    // Affordance table — authored in Stage 5; seeded here with the most-visible
    // entries so the 4-slot layout has real content before the full sweep.
    //
    // primaryIsHold = true → strongest action fires on hold; tap is the secondary
    // action shown in the tap slot above the primary.
    //
    // Keep entries grouped by key family (utility / modifiers / verbs / nav).
    // =========================================================================

    static constexpr std::array<KeyAffordance, 22> kAffordances = {{
        // ── Utility ─────────────────────────────────────────────────────────────
        // key 3 (TapTempo): primary = GEN HUB (hold); tap = TAP TEMPO
        { ControllerButton::TapTempo,
          u8"TAP TEMPO", u8"GEN HUB", nullptr, /*primaryIsHold=*/true },

        // ── Modifiers (8 keys) ───────────────────────────────────────────────────
        // Modifiers fire their scope action on hold; double-tap latches.
        // Func double-tap escapes the active overlay.
        { ControllerButton::Func,
          nullptr, u8"FUNC LAYER", u8"ESCAPE", false },
        { ControllerButton::TrackScope,
          nullptr, u8"TRACK SCOPE", u8"LATCH", false },
        { ControllerButton::PhraseScope,
          nullptr, u8"PHRASE SCOPE", u8"LATCH", false },
        { ControllerButton::SceneScope,
          nullptr, u8"SCENE SCOPE", u8"LATCH", false },
        { ControllerButton::MorphScope,
          nullptr, u8"MORPH SCOPE", u8"LATCH", false },
        { ControllerButton::SongScope,
          nullptr, u8"SONG SCOPE", u8"LATCH", false },
        { ControllerButton::MuteScope,
          nullptr, u8"MUTE VIEW", u8"LATCH", false },
        { ControllerButton::FillScope,
          nullptr, u8"FILL SCOPE", u8"LATCH", false },

        // ── Verb keys ────────────────────────────────────────────────────────────
        // Play: tap = PLAY; double-tap = STOP (stop + rewind).
        { ControllerButton::VerbPlay,
          u8"PLAY", nullptr, u8"STOP", false },
        // Snapshot: tap = SNAPSHOT; hold Func+Y for RESTORE.
        { ControllerButton::VerbSnapshot,
          u8"SNAPSHOT", nullptr, nullptr, false },
        { ControllerButton::VerbRecord,
          u8"RECORD", nullptr, nullptr, false },
        { ControllerButton::VerbClear,
          u8"CLEAR", nullptr, nullptr, false },
        { ControllerButton::VerbConfirm,
          u8"CONFIRM", nullptr, nullptr, false },

        // ── Nav keys ─────────────────────────────────────────────────────────────
        // NavUp (key 4): bare = track up / func = ×2 length.
        { ControllerButton::NavUp,
          u8"TRACK UP", nullptr, nullptr, false },
        { ControllerButton::NavLeft,
          u8"PAGE LEFT", nullptr, nullptr, false },
        { ControllerButton::NavDown,
          u8"TRACK DOWN", nullptr, nullptr, false },
        { ControllerButton::NavRight,
          u8"PAGE RIGHT", nullptr, nullptr, false },

        // ── Transport utility ─────────────────────────────────────────────────────
        // RecordArm (key 9): tap = arm record.
        { ControllerButton::RecordArm,
          u8"REC ARM", nullptr, nullptr, false },
        // PlayStop (key 0): tap = play/stop toggle.
        { ControllerButton::PlayStop,
          u8"PLAY/STOP", nullptr, nullptr, false },
    }};

    const KeyAffordance* findAffordance(ControllerButton button) noexcept
    {
        for (const auto& a : kAffordances)
        {
            if (a.button == button)
                return &a;
        }
        return nullptr;
    }

} // namespace lockstep
