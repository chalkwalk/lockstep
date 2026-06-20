#pragma once
#include <cstdint>

namespace lockstep
{
    // Gesture — the physical gesture that produces an action.
    // Shared between KeyBindings (grammar filter axis) and SurfaceCell
    // (primary-slot promotion token). Lives in its own header to avoid
    // the circular dep: KeyBindings.h → SurfaceModel.h → KeyBindings.h.
    enum class Gesture : uint8_t { Tap, Hold, DoubleTap };
} // namespace lockstep
