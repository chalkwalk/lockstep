#include "TimelineModel.h"

#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace lockstep
{
    TimelineModel buildTimelineModel(const LockstepProcessor& proc,
                                     double samplesPerBar, double barPpq) noexcept
    {
        TimelineModel m;
        const int track = proc.firstTapeTrack();
        if (track < 0) return m;  // no tape → strip hidden

        const int cap = proc.tapeReelCapacity(track);
        if (cap <= 0) return m;

        m.active = true;
        m.recording = proc.tapeState(track) == 1;  // dc::DeckState::Recording

        const double pos = proc.tapePosition(track);
        const double capD = static_cast<double>(cap);
        const auto clamp01 = [](double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); };

        m.cursor01 = clamp01(pos / capD);
        m.recordedExtent01 = clamp01(static_cast<double>(proc.tapeRecordedSamples(track)) / capD);
        m.mediumFull01 = m.recordedExtent01;

        // §40.2: the reel position is musical (ppq × K), but samplesPerBar is at the
        // CURRENT tempo. The chase ratio r = K / spp(current) reconciles them, so a
        // bar reads as a bar under any BPM: bars = pos / (r × samplesPerBar).
        const double ratio = proc.tapeChaseRatio(track);
        m.chaseRatio = static_cast<float>(ratio);

        // Position caption in musical time: bar.beat (1-based bars, beats within).
        if (samplesPerBar > 0.0 && barPpq > 0.0 && ratio > 0.0)
        {
            const double bars = pos / (ratio * samplesPerBar);
            const int bar = static_cast<int>(std::floor(bars)) + 1;
            const int beat = static_cast<int>(std::floor((bars - std::floor(bars)) * barPpq)) + 1;
            m.position = juce::String(bar) + "." + juce::String(beat);
            // Surface varispeed: append "×0.50" when the tempo is off calibration.
            if (std::abs(ratio - 1.0) > 1e-3)
                m.position += " x" + juce::String(ratio, 2);
        }
        else
        {
            m.position = juce::String(pos / 48000.0, 2) + "s";  // fallback: seconds
        }

        const int n = proc.tapeMarkerCount(track);
        m.markers.reserve(static_cast<std::size_t>(n));
        double nextMarker = -1.0;  // nearest marker strictly ahead of the playhead
        for (int i = 0; i < n; ++i)
        {
            const double mp = proc.tapeMarkerPosition(track, i);
            if (mp >= 0.0)
            {
                m.markers.push_back({ clamp01(mp / capD), i });
                if (mp > pos && (nextMarker < 0.0 || mp < nextMarker)) nextMarker = mp;
            }
        }

        // §19: brighten as the playhead approaches the next marker, over a one-bar
        // window in reel samples (a musical bar is ratio × samplesPerBar under
        // chase-lock, §40.2). Display-only; the strip and a controller render it.
        const double windowSamples = ratio * samplesPerBar;
        m.markerApproach = markerApproach01(pos, nextMarker, windowSamples);
        return m;
    }
}
