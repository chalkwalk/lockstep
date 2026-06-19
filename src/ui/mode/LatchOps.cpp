#include "LatchOps.h"

namespace lockstep
{
    using CB = ControllerButton;

    int latchColumn(ControllerButton cb) noexcept
    {
        switch (cb)
        {
            case CB::PhraseScope: return 0;
            case CB::MorphScope:  return 0;
            case CB::MuteScope:   return 0;
            case CB::TrackScope:  return 1;
            case CB::SceneScope:  return 1;
            case CB::SongScope:   return 1;
            case CB::FillScope:   return 1;
            default:              return -1;
        }
    }

    bool* latchBoolFor(LatchState& state, ControllerButton cb) noexcept
    {
        switch (cb)
        {
            case CB::PhraseScope: return &state.phrase;
            case CB::MorphScope:  return &state.morph;
            case CB::MuteScope:   return &state.mute;
            case CB::TrackScope:  return &state.track;
            case CB::SceneScope:  return &state.scene;
            case CB::SongScope:   return &state.song;
            case CB::FillScope:   return &state.fill;
            default:              return nullptr;
        }
    }

    const bool* latchBoolFor(const LatchState& state, ControllerButton cb) noexcept
    {
        // Delegate to the mutable overload via const_cast (no UB: we cast the
        // const away only to reuse the switch, and never write through the result).
        return latchBoolFor(const_cast<LatchState&>(state), cb);
    }

    void clearLatchColumnExcept(LatchState& state, ControllerButton cb) noexcept
    {
        const int col = latchColumn(cb);
        if (col == 0)
        {
            if (cb != CB::PhraseScope) { state.phrase = false; }
            if (cb != CB::MorphScope)  { state.morph  = false; }
            if (cb != CB::MuteScope)   { state.mute   = false; }
        }
        else if (col == 1)
        {
            if (cb != CB::TrackScope) { state.track = false; }
            if (cb != CB::SceneScope) { state.scene = false; }
            if (cb != CB::SongScope)  { state.song  = false; }
            if (cb != CB::FillScope)  { state.fill  = false; }
        }
        // col == -1: not latchable — no-op.
    }

} // namespace lockstep
