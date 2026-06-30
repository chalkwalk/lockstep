#pragma once

namespace lockstep
{
    // Optional mix-in for machines whose musical length is the *track's own* grid
    // (the Looper: its loop length = track length × step subdivision, not a
    // machine-owned param). The processor pushes the focused track's grid each
    // block right beside the ITempoAware seam, so the machine never reads the
    // sequencer's length params directly and the (MidiBuffer, ParamFrame,
    // AudioBuffer) boundary stays unchanged (protects the 6.7 module ABI).
    // DESIGN §29.2.
    //
    //   lengthSteps — the track's step count (>=1).
    //   stepPpq     — quarter-note PPQ per step (subdivisionPpqFromIndex).
    //
    // The loop's musical length is lengthSteps × stepPpq quarter notes; the
    // machine converts to samples with the transport tempo. Audio thread; the
    // implementation must be a cheap store.
    class ILoopGridAware
    {
    public:
        virtual ~ILoopGridAware() = default;
        virtual void setLoopGrid(int lengthSteps, double stepPpq) noexcept = 0;
    };
}
