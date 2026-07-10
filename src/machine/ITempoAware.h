#pragma once

namespace lockstep
{
    // Per-block transport snapshot pushed to tempo-aware machines (Player, Loop,
    // Record) before process(). This keeps the (MidiBuffer, ParamFrame,
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
        // THE absolute position (PRINCIPLES §25.1, DESIGN §40.2): how far into the
        // performance we are, in samples from a zero, at this block's start.
        // Hosted, it is the host's play head — the host timeline IS the tape
        // timeline — and standalone it is Lockstep's own clock. It is one more
        // field on the one transport authority, not a rival to it. A locate moves
        // it; nothing else does, and no state travels with it (fence #1).
        double transportPhaseSamples = 0.0;
        bool running = false;                // transport advancing this block
        // 9.17: the looper's edge timing (record/play/overdub) resolves against
        // the one shared launch-quantize grid, not a private looper grid. The
        // processor fills these per-track from the authority (LaunchQuant); the
        // machine arms its edges to launchQuantPeriodSamples. 0 = fire instantly
        // (Instant grid / stopped). launchQuantPhaseOffsetSamples is the track
        // anchor in samples so PhraseEnd edges align after a relaunch. loop_sync
        // now selects loop *length* only. (Appended fields — struct growth only.)
        double launchQuantPeriodSamples = 0.0;
        double launchQuantPhaseOffsetSamples = 0.0;
    };

    // Optional mix-in for machines that need project tempo (stretch-tracking
    // Player, varispeed Loop, bar-stamping Record). The processor dynamic_casts
    // each machine once per block and, if it implements this, calls setTransport()
    // before process(). Audio thread; the implementation must be a cheap store.
    class ITempoAware
    {
    public:
        virtual ~ITempoAware() = default;
        virtual void setTransport(const TransportInfo& t) noexcept = 0;
    };
}
