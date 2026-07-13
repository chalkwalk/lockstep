#pragma once
// TakeSheet — the plain-text sheet written beside a capture's WAVs (11.11).
//
// "The morning after" (DESIGN §41.1): a take's audio is only half the handoff.
// The sheet carries what the files cannot — the project, the tempo, which stems
// were kept, and *where the launches were*, so a DAW session can start without
// re-deriving the performance by ear.
//
// The launches are DUMB, exactly like the Tape's markers (§40.4, NON-GOALS #1):
// they are places, never cues. Nothing reads this file back; it never fires a
// Scene. The day it does, we have shipped the arrangement we refused.
//
// Threading: launches are stamped on the AUDIO thread (push, wait-free, bounded
// — a fixed ring, never allocating, dropping the oldest if a take somehow
// overruns it) and drained on the message thread at close. The formatter is
// pure, so it is unit-tested without touching a file.

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace lockstep
{
    // What the performer did, and where.
    struct TakeEvent
    {
        enum class Kind : std::uint8_t { Scene = 0, Song = 1 };

        std::int64_t sampleAt = 0;   // samples from the take's first written sample
        Kind         kind     = Kind::Scene;
        int          index    = 0;   // which Scene / Song was launched (0-based)
    };

    // Header facts about the take, gathered on the message thread at close.
    struct TakeSheetInfo
    {
        juce::String projectName;        // "(unsaved)" when there is no project file
        juce::String dateTime;           // human-readable local time
        double       sampleRate = 0.0;
        double       bpm        = 0.0;   // effective BPM at close
        int          timeSigNum = 4;
        int          timeSigDen = 4;
        std::int64_t lengthSamples = 0;
        // Kept stems, as "track-03.wav  Analog" — file name plus what made it.
        std::vector<juce::String> stems;
    };

    // Wall-clock position of a sample offset, as m:ss.mmm.
    inline juce::String takeSheetTimestamp(std::int64_t sampleAt, double sampleRate)
    {
        if (sampleRate <= 0.0) return "0:00.000";
        const double seconds = static_cast<double>(sampleAt) / sampleRate;
        const int totalMs = static_cast<int>(seconds * 1000.0 + 0.5);
        const int mins = totalMs / 60000;
        const int secs = (totalMs / 1000) % 60;
        const int ms   = totalMs % 1000;
        return juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2)
             + "." + juce::String(ms).paddedLeft('0', 3);
    }

    // The whole sheet as text. Pure: same inputs, same bytes.
    inline juce::String formatTakeSheet(const TakeSheetInfo& info,
                                        const std::vector<TakeEvent>& events)
    {
        juce::String s;
        s << "Lockstep take" << juce::newLine
          << "=============" << juce::newLine << juce::newLine
          << "Project     : " << (info.projectName.isNotEmpty() ? info.projectName
                                                                : juce::String("(unsaved)"))
          << juce::newLine
          << "Recorded    : " << info.dateTime << juce::newLine
          << "Length      : " << takeSheetTimestamp(info.lengthSamples, info.sampleRate)
          << juce::newLine
          << "Sample rate : " << juce::String(info.sampleRate, 0) << " Hz" << juce::newLine
          << "Tempo       : " << juce::String(info.bpm, 2) << " BPM" << juce::newLine
          << "Time sig    : " << juce::String(info.timeSigNum) << "/"
          << juce::String(info.timeSigDen) << juce::newLine << juce::newLine;

        s << "Files" << juce::newLine
          << "-----" << juce::newLine
          << "master.wav" << juce::newLine;
        for (const auto& stem : info.stems)
            s << stem << juce::newLine;
        s << juce::newLine;

        s << "Launches" << juce::newLine
          << "--------" << juce::newLine;
        if (events.empty())
        {
            s << "(none)" << juce::newLine;
        }
        else
        {
            for (const auto& e : events)
            {
                s << takeSheetTimestamp(e.sampleAt, info.sampleRate) << "  "
                  << (e.kind == TakeEvent::Kind::Scene ? "Scene " : "Song  ")
                  << juce::String(e.index + 1) << juce::newLine;
            }
        }
        s << juce::newLine
          << "These are places, not cues: nothing here plays itself back."
          << juce::newLine;
        return s;
    }

    // Fixed-capacity SPSC log: pushed from the audio thread, drained on the
    // message thread at close. Never allocates; a take that overruns the ring
    // keeps the earliest events and drops the rest (a full sheet is worth more
    // than a truncated tail, and 512 launches is far past any real set).
    class TakeEventLog
    {
    public:
        static constexpr int kCapacity = 512;

        void clear() noexcept { count_.store(0, std::memory_order_relaxed); }

        // Audio thread. Wait-free, allocation-free.
        void push(std::int64_t sampleAt, TakeEvent::Kind kind, int index) noexcept
        {
            const int n = count_.load(std::memory_order_relaxed);
            if (n >= kCapacity) return;   // full: keep what we have
            events_[static_cast<std::size_t>(n)] = TakeEvent{ sampleAt, kind, index };
            count_.store(n + 1, std::memory_order_release);
        }

        // Message thread.
        [[nodiscard]] std::vector<TakeEvent> drain() const
        {
            const int n = count_.load(std::memory_order_acquire);
            return { events_.begin(), events_.begin() + n };
        }

        [[nodiscard]] int size() const noexcept
        {
            return count_.load(std::memory_order_relaxed);
        }

    private:
        std::array<TakeEvent, kCapacity> events_{};
        std::atomic<int> count_{ 0 };
    };
}
