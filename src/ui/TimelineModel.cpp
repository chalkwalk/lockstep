#include "TimelineModel.h"

#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace lockstep
{
    TimelineModel buildTimelineModel(const LockstepProcessor& proc, double samplesPerBar,
                                     double barPpq, double sampleRate) noexcept
    {
        TimelineModel m;
        const auto clamp01 = [](double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); };
        m.secondsPerBar = (sampleRate > 0.0) ? samplesPerBar / sampleRate : 0.0;

        // Transport cursor in bars — the one thing that always exists, so the strip
        // shows time advancing even with no tape. Reel position and transport are
        // chase-locked (§40.2), so the transport bar is the tape's bar too.
        const double ppq = proc.clock().cumulativePpq();
        m.cursorBars = (barPpq > 0.0) ? ppq / barPpq : 0.0;

        // A bar in reel samples for tape `t` (chase-lock: ratio × samplesPerBar).
        const auto barsOf = [&](int t, double samples) {
            const double ratio = proc.tapeChaseRatio(t);
            const double barSamp = (ratio > 0.0 ? ratio : 1.0) * samplesPerBar;
            return (barSamp > 0.0) ? samples / barSamp : 0.0;
        };

        // First pass: the chosen tape + the longest recorded extent across all tapes.
        const int chosen = proc.firstTapeTrack();
        double longestBars = 0.0;
        struct RawEnd { int track; double endBars; };
        std::vector<RawEnd> ends;
        for (int t = 0; t < static_cast<int>(kNumTracks); ++t)
        {
            if (! proc.isTapeTrack(t) || proc.tapeReelCapacity(t) <= 0) continue;
            const double endBars = barsOf(t, static_cast<double>(proc.tapeRecordedSamples(t)));
            longestBars = std::max(longestBars, endBars);
            ends.push_back({ t, endBars });
            m.active = true;  // a tape exists
        }

        // Domain: max(32 bars, the longest tape, the current cursor) so both the
        // recorded material and the playhead always stay on-strip.
        m.domainBars = std::max({ 32,
                                  static_cast<int>(std::ceil(longestBars)),
                                  static_cast<int>(std::ceil(m.cursorBars)) + 1 });
        const double domD = static_cast<double>(m.domainBars);

        m.cursor01 = clamp01(m.cursorBars / domD);

        // Tape-end lugs (every tape), highlighting the chosen one.
        m.tapeEnds.reserve(ends.size());
        for (const auto& e : ends)
            m.tapeEnds.push_back({ clamp01(e.endBars / domD), e.track, e.track == chosen });

        // Position caption (bar.beat) + wall-clock caption, both transport-driven.
        const int bar = static_cast<int>(std::floor(m.cursorBars)) + 1;
        const double fracBar = m.cursorBars - std::floor(m.cursorBars);
        const int beat = (barPpq > 0.0)
                             ? static_cast<int>(std::floor(fracBar * barPpq)) + 1 : 1;
        m.position = juce::String(bar) + "." + juce::String(beat);
        if (m.secondsPerBar > 0.0)
        {
            const double secs = m.cursorBars * m.secondsPerBar;
            const int mins = static_cast<int>(secs) / 60;
            const int rem = static_cast<int>(secs) % 60;
            m.wallTime = juce::String(mins) + ":" + juce::String(rem).paddedLeft('0', 2);
        }

        if (! m.active || chosen < 0) return m;  // no tape → ruler + cursor only

        // The chosen tape supplies the recorded extent, markers, and warnings.
        m.recording = proc.tapeState(chosen) == 1;  // dc::DeckState::Recording
        const double ratio = proc.tapeChaseRatio(chosen);
        m.chaseRatio = static_cast<float>(ratio);
        if (std::abs(ratio - 1.0) > 1e-3)
            m.position += " x" + juce::String(ratio, 2);

        m.recordedExtent01 = clamp01(barsOf(chosen,
                              static_cast<double>(proc.tapeRecordedSamples(chosen))) / domD);
        const int cap = proc.tapeReelCapacity(chosen);
        m.mediumFull01 = (cap > 0)
            ? clamp01(static_cast<double>(proc.tapeRecordedSamples(chosen)) / cap) : 0.0f;

        const double pos = proc.tapePosition(chosen);
        const int n = proc.tapeMarkerCount(chosen);
        m.markers.reserve(static_cast<std::size_t>(n));
        double nextMarker = -1.0;  // nearest marker strictly ahead of the playhead
        for (int i = 0; i < n; ++i)
        {
            const double mp = proc.tapeMarkerPosition(chosen, i);
            if (mp >= 0.0)
            {
                m.markers.push_back({ clamp01(barsOf(chosen, mp) / domD), i });
                if (mp > pos && (nextMarker < 0.0 || mp < nextMarker)) nextMarker = mp;
            }
        }

        // §19: brighten as the playhead approaches the next marker, over a one-bar
        // window in reel samples (chase-lock, §40.2). Display-only.
        m.markerApproach = markerApproach01(pos, nextMarker, (ratio > 0.0 ? ratio : 1.0) * samplesPerBar);
        return m;
    }
}
