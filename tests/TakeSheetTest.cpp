// TakeSheetTest — the capture take sheet (11.11, DESIGN §41.1 "the morning after").
//
// The sheet is what makes a take a handoff rather than a pile of WAVs: the
// project, the tempo, the stems that survived, and where the launches were. Like
// the Tape's markers it is DUMB — a place, never a cue — and there is no method
// here that could fire one, which is the fence-#1 line expressed as an absence.

#include "TestHarness.h"
#include "../src/io/TakeSheet.h"

namespace lockstep
{
    void runTakeSheetTests()
    {
        // ── Timestamps read as wall clock, not samples ───────────────────────
        {
            CHECK(takeSheetTimestamp(0, 48000.0) == juce::String("0:00.000"),
                  "the take starts at zero");
            CHECK(takeSheetTimestamp(48000, 48000.0) == juce::String("0:01.000"),
                  "one second in");
            CHECK(takeSheetTimestamp(48000 * 75, 48000.0) == juce::String("1:15.000"),
                  "minutes roll over at sixty seconds");
            CHECK(takeSheetTimestamp(24000, 48000.0) == juce::String("0:00.500"),
                  "sub-second precision survives (a launch lands between beats)");
            CHECK(takeSheetTimestamp(1000, 0.0) == juce::String("0:00.000"),
                  "a zero sample rate degrades to zero, never divides by it");
        }

        // ── The log is bounded, ordered, and wait-free to push ───────────────
        {
            TakeEventLog log;
            CHECK(log.size() == 0, "a fresh log is empty");

            log.push(0, TakeEvent::Kind::Scene, 2);
            log.push(48000, TakeEvent::Kind::Song, 1);
            CHECK(log.size() == 2, "both launches were kept");

            const auto events = log.drain();
            CHECK(events.size() == 2, "drain returns what was pushed");
            CHECK(events[0].kind == TakeEvent::Kind::Scene && events[0].index == 2,
                  "the first launch is the scene");
            CHECK(events[1].sampleAt == 48000 && events[1].kind == TakeEvent::Kind::Song,
                  "the second is the song, at its sample position");

            log.clear();
            CHECK(log.size() == 0, "a new take starts with an empty log");

            // Overrun keeps the earliest events rather than allocating or wrapping:
            // a complete beginning beats a truncated tail.
            for (int i = 0; i < TakeEventLog::kCapacity + 32; ++i)
                log.push(i, TakeEvent::Kind::Scene, i);
            CHECK(log.size() == TakeEventLog::kCapacity,
                  "the ring is bounded — the audio thread never allocates");
            CHECK(log.drain().front().index == 0, "and it kept the take's beginning");
        }

        // ── The sheet says what the WAVs cannot ──────────────────────────────
        {
            TakeSheetInfo info;
            info.projectName   = "night-set";
            info.dateTime      = "12 Jul 2026 9:15:00pm";
            info.sampleRate    = 48000.0;
            info.bpm           = 128.0;
            info.timeSigNum    = 7;
            info.timeSigDen    = 8;
            info.lengthSamples = 48000 * 90;
            info.stems.push_back("track-01.wav  Analog");
            info.stems.push_back("track-04.wav  Route");

            std::vector<TakeEvent> events{
                TakeEvent{ 48000 * 16, TakeEvent::Kind::Scene, 1 },
                TakeEvent{ 48000 * 48, TakeEvent::Kind::Song, 2 },
            };

            const juce::String s = formatTakeSheet(info, events);

            CHECK(s.contains("night-set"), "the project is named");
            CHECK(s.contains("128.00 BPM"), "the tempo is stated for the DAW session");
            CHECK(s.contains("7/8"), "so is the time signature");
            CHECK(s.contains("1:30.000"), "and the take's length");
            CHECK(s.contains("master.wav"), "the master is always listed");
            CHECK(s.contains("track-01.wav  Analog"), "each kept stem names its machine");
            CHECK(s.contains("track-04.wav  Route"), "including a bus stem");
            CHECK(s.contains("0:16.000  Scene 2"),
                  "a scene launch is a place in the take (1-based for the surface)");
            CHECK(s.contains("0:48.000  Song  3"), "and so is a song switch");
            CHECK(s.contains("places, not cues"),
                  "the sheet says out loud that it plays nothing back");
        }

        // ── A take with no launches still produces a readable sheet ──────────
        {
            TakeSheetInfo info;
            info.sampleRate = 44100.0;
            info.bpm = 120.0;
            const juce::String s = formatTakeSheet(info, {});
            CHECK(s.contains("(unsaved)"), "an unsaved project is named as such");
            CHECK(s.contains("(none)"), "no launches reads as none, not as a missing section");
        }
    }
}
