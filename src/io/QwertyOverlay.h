#pragma once

namespace lockstep
{
    // The QWERTY overlay maps physical scancodes to grid steps, page
    // selectors, and transport. M0 declares the seam; the keypress handlers
    // and editor wiring land alongside the Manipulation Zone in M6.
    class QwertyOverlay
    {
    public:
        struct Mapping
        {
            int trackIndex = -1;
            int stepIndex = -1;
            int pageIndex = -1;
            bool isTransport = false;
        };

        Mapping resolve(int scancode) const;
    };
}
