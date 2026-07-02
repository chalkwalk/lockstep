#pragma once
#include <cstdint>

namespace lockstep
{
    // How a machine drives the always-on/on-demand step-grid console (7b).
    // A "console" repurposes the step grid as a machine-specific control surface
    // (the looper transport console is the hard-coded precedent; this generalises
    // it). Kept in its own tiny JUCE-free header so both IMachine and the
    // JUCE-light SurfaceLayer/LayerFacts can name it without pulling JUCE.
    //
    //   None     — no console; the grid is the normal sequencer/lock grid.
    //   OnDemand — a console the user opens with a gesture (long-press the
    //              machine's owning section key) and closes the same way.
    //   AlwaysOn — the console replaces the grid whenever the track is focused
    //              and nothing higher-priority is active (like the looper).
    enum class ConsoleMode : uint8_t
    {
        None,
        OnDemand,
        AlwaysOn,
    };
} // namespace lockstep
