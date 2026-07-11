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

        // Position caption in musical time: bar.beat (1-based bars, beats within).
        if (samplesPerBar > 0.0 && barPpq > 0.0)
        {
            const double bars = pos / samplesPerBar;
            const int bar = static_cast<int>(std::floor(bars)) + 1;
            const int beat = static_cast<int>(std::floor((bars - std::floor(bars)) * barPpq)) + 1;
            m.position = juce::String(bar) + "." + juce::String(beat);
        }
        else
        {
            m.position = juce::String(pos / 48000.0, 2) + "s";  // fallback: seconds
        }

        const int n = proc.tapeMarkerCount(track);
        m.markers.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const double mp = proc.tapeMarkerPosition(track, i);
            if (mp >= 0.0)
                m.markers.push_back({ clamp01(mp / capD), i });
        }
        return m;
    }
}
