#pragma once

namespace lockstep
{
    // Per-block transport snapshot pushed to tempo-aware machines (Player, Looper,
    // Recorder) before process(). This keeps the (MidiBuffer, ParamFrame,
    // AudioBuffer) machine boundary unchanged — the processor (which owns the
    // Clock) computes this once per block and delivers it through the optional
    // ITempoAware seam, reached by dynamic_cast (the same pattern as
    // isLooperTrack / sendLooperCommand). DESIGN §27 / §30.
    struct TransportInfo
    {
        double bpm = 120.0;
        double sampleRate = 44100.0;
        double samplesPerBar = 0.0;          // 0 = unknown (no tempo context)
        double barPpq = 4.0;                 // quarter notes per bar (time-sig; S1 loop length)
        double transportPhaseSamples = 0.0;  // song position in samples at block start
        bool running = false;                // transport advancing this block
    };

    // Optional mix-in for machines that need project tempo (stretch-tracking
    // Player, varispeed Looper, bar-stamping Recorder). The processor dynamic_casts
    // each machine once per block and, if it implements this, calls setTransport()
    // before process(). Audio thread; the implementation must be a cheap store.
    class ITempoAware
    {
    public:
        virtual ~ITempoAware() = default;
        virtual void setTransport(const TransportInfo& t) noexcept = 0;
    };
}
