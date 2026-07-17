#include "TimelineStrip.h"

#include <cmath>

namespace lockstep
{
    void TimelineStrip::paint(juce::Graphics& g)
    {
        // S8: the strip is ALWAYS drawn. Even with no tape it shows the bar ruler,
        // the wall-clock ruler and a live cursor driven by the transport, so you can
        // leave the tape face and still watch the position and recording time.
        auto area = getLocalBounds();
        g.setColour(juce::Colour(0xFF0E1014u));
        g.fillRect(area);

        // Left caption: bars.beats over wall-clock time.
        auto captionArea = area.removeFromLeft(72);
        g.setColour(juce::Colour(model_.recording ? 0xFFE05050u : 0xFF9AA4B0u));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(model_.position, captionArea.reduced(6, 0).removeFromTop(captionArea.getHeight() / 2 + 1),
                   juce::Justification::centredLeft, false);
        if (model_.wallTime.isNotEmpty())
        {
            g.setColour(juce::Colour(0xFF6A7480u));
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(model_.wallTime, captionArea.reduced(6, 0).removeFromBottom(captionArea.getHeight() / 2),
                       juce::Justification::centredLeft, false);
        }

        auto reel = area.reduced(4, 3);
        const int x0 = reel.getX();
        const int w = reel.getWidth();
        if (w <= 0) return;
        const auto atX = [&](float f) { return x0 + juce::roundToInt(f * static_cast<float>(w)); };

        // The reel track.
        g.setColour(juce::Colour(0xFF1A1E24u));
        g.fillRoundedRectangle(reel.toFloat(), 3.0f);

        // Recorded extent (chosen tape) — reddens in the last 10% (medium-full).
        if (model_.active && model_.recordedExtent01 > 0.0f)
        {
            auto filled = reel.withWidth(atX(model_.recordedExtent01) - x0);
            const bool nearFull = model_.mediumFull01 > 0.9f;
            g.setColour(juce::Colour(nearFull ? 0xFF7A2A2Au : 0xFF244A44u));
            g.fillRoundedRectangle(filled.toFloat(), 3.0f);
        }

        // Bar ruler: a faint tick every bar, brighter + numbered every 4 bars. The
        // domain is model_.domainBars bars wide (§40.6, transport-locked).
        if (model_.domainBars > 0)
        {
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            const int step = (model_.domainBars > 64) ? 8 : 4;  // label density
            for (int b = 0; b <= model_.domainBars; ++b)
            {
                const int bx = atX(static_cast<float>(b) / static_cast<float>(model_.domainBars));
                const bool major = (b % step == 0);
                g.setColour(juce::Colour(major ? 0xFF3A4250u : 0xFF262C34u));
                g.fillRect(bx, reel.getY(), 1, major ? reel.getHeight() : reel.getHeight() / 2);
                if (major && b < model_.domainBars)
                {
                    g.setColour(juce::Colour(0xFF55606Cu));
                    g.drawText(juce::String(b + 1), bx + 2, reel.getY(), 22, 9,
                               juce::Justification::topLeft, false);
                }
            }
        }

        // Wall-clock ruler: a small tick every whole 10 s along the bottom edge.
        if (model_.secondsPerBar > 0.0 && model_.domainBars > 0)
        {
            const double totalSecs = model_.domainBars * model_.secondsPerBar;
            const int tickSecs = (totalSecs > 600.0) ? 60 : (totalSecs > 120.0) ? 30 : 10;
            g.setColour(juce::Colour(0xFF3A4250u));
            for (int s = 0; s <= static_cast<int>(totalSecs); s += tickSecs)
            {
                const float f = static_cast<float>(s / totalSecs);
                const int sx = atX(f);
                g.fillRect(sx, reel.getBottom() - 3, 1, 3);
            }
        }

        // Tape-end lugs: every tape's recorded end, the chosen one highlighted.
        for (const auto& e : model_.tapeEnds)
        {
            const int ex = atX(e.end01);
            g.setColour(e.focused ? juce::Colour(0xFF40C0A0u) : juce::Colour(0xFF2A5850u));
            g.fillRect(ex - 1, reel.getY(), 2, reel.getHeight());
            // A small down-tab so an end reads as an end, not a marker.
            juce::Path tab;
            tab.addTriangle(static_cast<float>(ex - 3), static_cast<float>(reel.getY()),
                            static_cast<float>(ex + 3), static_cast<float>(reel.getY()),
                            static_cast<float>(ex), static_cast<float>(reel.getY() + 4));
            g.fillPath(tab);
        }

        // Markers (chosen tape) — thin ticks; the next ahead brightens (§19).
        const int cx = atX(model_.cursor01);
        for (const auto& m : model_.markers)
        {
            const int mx = atX(m.pos01);
            const bool isNext = mx > cx;
            const float glow = isNext ? model_.markerApproach : 0.0f;
            g.setColour(juce::Colour(0xFF4A78C0u).brighter(glow * 0.8f));
            g.fillRect(mx, reel.getY(), glow > 0.5f ? 2 : 1, reel.getHeight());
        }

        // Playhead cursor — bright, and pulsing while recording (sine on the editor's
        // clock, pushed in via setAnimClockMs; a paint that fetched the time itself
        // could not be rendered twice the same).
        float pulse = 1.0f;
        if (model_.recording)
        {
            const double t = animClockMs_ * 0.006;
            pulse = 0.6f + 0.4f * static_cast<float>(0.5 * (1.0 + std::sin(t)));
        }
        auto cursorCol = juce::Colour(model_.recording ? 0xFFFF5050u : 0xFFE8E8E8u);
        g.setColour(cursorCol.withMultipliedBrightness(pulse));
        g.fillRect(cx - 1, reel.getY() - 1, 2, reel.getHeight() + 2);
    }
}
