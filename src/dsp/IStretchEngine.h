#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // ── Real-time stretch/pitch engine seam (9.23) ───────────────────────────
    //
    // A pull-model boundary between a sample-playing machine and a time/pitch
    // engine. Engines are authored against this JUCE-only header; the concrete
    // engine's third-party headers (Bungee/Eigen) never leak past its .cpp so
    // the seam stays cheap to include and -Werror-clean.
    //
    // Engine hierarchy for the project:
    //   * real-time            = BungeeStretchEngine (Bungee, MPL-2.0) — pull-based
    //                            grain API gives free reverse/scrub/zero-speed,
    //                            seamless looping by source-position wrapping, and
    //                            native input↔output rate conversion.
    //   * deferred offline     = Rubber Band R3 (GPL) render-to-pool — a future
    //                            OfflineStretchEngine behind this same seam.
    //   * real-time fallback   = signalsmith-stretch (MIT, header-only) if Bungee
    //                            fails the Stage-1 ear test. All callers of this
    //                            seam are engine-agnostic, so the swap is local.
    //
    // Threading: prepare()/reset()/start() do all allocation and run on the
    // message thread or inside withQuiescedEngine only. process() is
    // allocation-free and audio-thread safe.

    // Random-access source of PCM the engine pulls from. Implementations are
    // audio-thread safe for read()/length()/numChannels()/sampleRate().
    struct IStretchSource
    {
        virtual ~IStretchSource() = default;

        // Copy `n` frames of channel `ch` starting at source frame `srcPos` into
        // `dest`. Positions outside [0, length()) are zero-filled. Returns `n`.
        virtual int read(float* dest, int ch, juce::int64 srcPos, int n) = 0;

        [[nodiscard]] virtual juce::int64 length() const = 0;
        [[nodiscard]] virtual int numChannels() const = 0;

        // Native sample rate of the source PCM, in Hz. Used for rate compensation
        // when the engine is constructed in fold mode (see prepare()).
        [[nodiscard]] virtual double sampleRate() const = 0;
    };

    class IStretchEngine
    {
    public:
        virtual ~IStretchEngine() = default;

        // Allocate for playback. `sourceRate > 0` selects native-rate mode: the
        // engine converts source↔output internally (used by Stream, whose file
        // rate is fixed per reader lifetime). `sourceRate <= 0` selects fold mode:
        // the engine is built rate-agnostic and folds each source's own
        // sampleRate() into speed/pitch per grain, so the selected sample may
        // change on the audio thread without reconstruction (used by the RAM
        // Stretch player, whose sample_id is p-lockable). Message thread only.
        virtual void prepare(double sourceRate, double outputRate,
                             int maxChannels, int maxBlock) = 0;

        // Return to the just-prepared state (no active voice). Allocation-free.
        virtual void reset() = 0;

        // Begin playback of `src` from source frame `startPos`.
        // timeRatio  = output duration / input duration (>1 = longer/slower).
        // pitchRatio = output frequency / input frequency (>1 = higher).
        // `src` is borrowed and must outlive playback. Bounded onset burst
        // (preroll + run-in discard). Audio-thread safe (allocation-free).
        virtual void start(IStretchSource* src, double startPos,
                          double timeRatio, double pitchRatio) = 0;

        // Refresh the ratios mid-flight (tempo glide, live pitch). Cheap.
        virtual void setRatios(double timeRatio, double pitchRatio) = 0;

        // Loop the source-position sequence over [loopStart, loopEnd) frames.
        // The seam is continuous by construction (position folding, no restart /
        // crossfade). loopStart == loopEnd disables looping.
        virtual void setLoop(juce::int64 loopStart, juce::int64 loopEnd) = 0;

        // Run the position sequence backwards (tape reverse). Zero engine cost.
        virtual void setReverse(bool reverse) = 0;

        // Current source frame the engine is reading around (for phase anchoring).
        [[nodiscard]] virtual double sourcePosition() const = 0;

        // Pull `n` output frames into out[ch][startSample..], overwriting.
        // Channels beyond the source's are filled by replicating channel 0.
        // Allocation-free; audio-thread only.
        virtual void process(juce::AudioBuffer<float>& out, int startSample, int n) = 0;

        [[nodiscard]] virtual bool isActive() const = 0;
        [[nodiscard]] virtual int latencySamples() const = 0;
    };
}
