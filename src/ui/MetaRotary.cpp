#include "MetaRotary.h"
#include <cmath>

namespace lockstep
{
    void MetaRotaryLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                 int x, int y, int width, int height,
                                                 float sliderPos,
                                                 float rotaryStartAngle,
                                                 float rotaryEndAngle,
                                                 juce::Slider& slider)
    {
        auto* mr = dynamic_cast<MetaRotary*>(&slider);

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = (juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f) - 2.0f;
        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY();
        const float trackW = juce::jmax(2.0f, radius * 0.12f);

        const float valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float centreAngle = (rotaryStartAngle + rotaryEndAngle) * 0.5f;

        const RingMode mode = mr ? mr->ringMode : RingMode::UnipolarFill;

        // --- Background track ---
        {
            juce::Path bg;
            bg.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId)
                            .withAlpha(0.35f));
            g.strokePath(bg, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        }

        // --- Value fill ---
        if (mode == RingMode::Dot)
        {
            // No arc fill — indicator only.
        }
        else if (mode == RingMode::BipolarFromCentre)
        {
            const float fromAngle = centreAngle;
            const float toAngle = valueAngle;
            if (std::abs(toAngle - fromAngle) > 0.005f)
            {
                juce::Path fill;
                fill.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                   juce::jmin(fromAngle, toAngle),
                                   juce::jmax(fromAngle, toAngle), true);
                g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
                g.strokePath(fill, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }
        else  // UnipolarFill
        {
            if (sliderPos > 0.001f)
            {
                juce::Path fill;
                fill.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                   rotaryStartAngle, valueAngle, true);
                g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
                g.strokePath(fill, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }

        // --- Reference ticks (marks[0] drawn first, then marks[1] on top) ---
        if (mr)
        {
            const float tickInner = radius - trackW * 0.5f;
            const float tickOuter = radius + trackW * 1.5f;
            const float halfPi = juce::MathConstants<float>::halfPi;

            for (const auto& mark : mr->marks)
            {
                if (!mark.present) continue;

                const float markAngle = rotaryStartAngle + mark.position * (rotaryEndAngle - rotaryStartAngle);
                const float cosA = std::cos(markAngle - halfPi);
                const float sinA = std::sin(markAngle - halfPi);

                const float x1 = centreX + tickInner * cosA;
                const float y1 = centreY + tickInner * sinA;
                const float x2 = centreX + tickOuter * cosA;
                const float y2 = centreY + tickOuter * sinA;

                const auto argb = juce::Colour(mark.colour).withAlpha(mark.alpha);
                g.setColour(argb);
                g.drawLine(x1, y1, x2, y2, trackW);
            }
        }

        // --- Density cell overlay (DESIGN §39) ---
        // Drawn before the indicator dot so the dot sits on top.
        if (mr && mr->densityCell)
        {
            const float halfPi = juce::MathConstants<float>::halfPi;
            const float master = mr->densityMasterOffset;  // [-1, 1]
            const float totalAngle = rotaryEndAngle - rotaryStartAngle;

            // Master-level arc: anchored at rotaryStartAngle (absolute zero) so it
            // stays fixed as per-track changes.  Positive master → arc from start
            // forward; negative master → short backward stub before start.
            if (master > 0.005f)
            {
                const float masterAngle = rotaryStartAngle
                    + juce::jlimit(0.0f, 1.0f, master) * totalAngle;
                if (masterAngle - rotaryStartAngle > 0.005f)
                {
                    juce::Path arcPath;
                    arcPath.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                         rotaryStartAngle, masterAngle, true);
                    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    .withAlpha(0.55f));
                    g.strokePath(arcPath, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                }
            }
            else if (master < -0.005f)
            {
                // Negative master: small stub going backward from start so the
                // direction of the cut is visible even when the pointer is near zero.
                const float stubAngle = rotaryStartAngle
                    - juce::jlimit(0.0f, 1.0f, -master) * totalAngle * 0.2f;
                if (rotaryStartAngle - stubAngle > 0.005f)
                {
                    juce::Path overshoot;
                    overshoot.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                            stubAngle, rotaryStartAngle, true);
                    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    .withAlpha(0.35f));
                    g.strokePath(overshoot, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                                  juce::PathStrokeType::rounded));
                }
            }

            // Tick at the effective (clamped) position — the audible value.
            {
                const float effectiveAngle = rotaryStartAngle
                    + mr->densityEffective * totalAngle;
                const float tickInner = radius - trackW * 1.2f;
                const float tickOuter = radius + trackW * 1.2f;
                const float cosA = std::cos(effectiveAngle - halfPi);
                const float sinA = std::sin(effectiveAngle - halfPi);
                g.setColour(slider.findColour(juce::Slider::thumbColourId).withAlpha(0.6f));
                g.drawLine(centreX + tickInner * cosA, centreY + tickInner * sinA,
                           centreX + tickOuter * cosA, centreY + tickOuter * sinA, trackW);
            }
        }

        // --- Indicator dot ---
        {
            const float halfPi = juce::MathConstants<float>::halfPi;
            const float dotRadius = juce::jmax(2.5f, trackW * 0.8f);
            const float dotX = centreX + radius * std::cos(valueAngle - halfPi);
            const float dotY = centreY + radius * std::sin(valueAngle - halfPi);
            g.setColour(slider.findColour(juce::Slider::thumbColourId)
                            .withAlpha(slider.isEnabled() ? 1.0f : 0.4f));
            g.fillEllipse(dotX - dotRadius, dotY - dotRadius,
                          dotRadius * 2.0f, dotRadius * 2.0f);
        }
    }
}
