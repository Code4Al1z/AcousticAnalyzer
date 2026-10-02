#pragma once

#include "Theme.h"

namespace ui
{
    // Arc gauge for the 0-100 Acoustic Activation Index. The zone (calming /
    // neutral / stimulating) is shown with a colour, an icon shape and a label,
    // so it never relies on colour alone.
    class ScoreGauge : public juce::Component
    {
    public:
        static constexpr float calmThreshold = 70.0f;    // Above this: low activation
        static constexpr float neutralThreshold = 40.0f; // Above this: medium activation

        ScoreGauge()
        {
            setTitle("Acoustic activation index");
            setOpaque(false);
        }

        void setScore(float newScore)
        {
            newScore = juce::jlimit(0.0f, 100.0f, newScore);

            if (std::abs(newScore - score) < 0.05f)
                return;

            score = newScore;
            setDescription(juce::String(score, 1) + " out of 100. " + zoneFor(score).label);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            auto pillArea = bounds.removeFromBottom(30.0f);
            auto gaugeArea = bounds.reduced(4.0f);

            const float diameter = juce::jmax(40.0f, juce::jmin(gaugeArea.getWidth(), gaugeArea.getHeight() * 1.12f));
            const auto centre = gaugeArea.getCentre().translated(0.0f, diameter * 0.04f);
            const float thickness = juce::jmax(8.0f, diameter * 0.07f);
            const float radius = diameter * 0.5f - thickness * 0.5f - 2.0f;

            constexpr float startAngle = -2.0f * juce::MathConstants<float>::pi / 3.0f; // 240 degree sweep, opening at the bottom
            constexpr float sweep = 4.0f * juce::MathConstants<float>::pi / 3.0f;
            const auto zone = zoneFor(score);

            // Track
            juce::Path track;
            track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, startAngle + sweep, true);
            g.setColour(palette::surfaceRaised);
            g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Value
            if (score > 0.5f)
            {
                juce::Path value;
                value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle,
                                    startAngle + sweep * score / 100.0f, true);
                g.setColour(zone.colour);
                g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Zone boundaries, drawn as gaps across the track
            g.setColour(palette::surface);
            for (float threshold : { neutralThreshold, calmThreshold })
            {
                const float angle = startAngle + sweep * threshold / 100.0f;
                const juce::Point<float> direction(std::sin(angle), -std::cos(angle));
                g.drawLine(juce::Line<float>(centre + direction * (radius - thickness * 0.6f),
                                             centre + direction * (radius + thickness * 0.6f)), 2.0f);
            }

            // Number and caption
            auto textArea = juce::Rectangle<float>(radius * 1.5f, radius * 1.1f).withCentre(centre.translated(0.0f, radius * 0.05f));
            g.setColour(palette::textPrimary);
            g.setFont(font(juce::jlimit(24.0f, 64.0f, radius * 0.62f), true));
            g.drawText(juce::String(score, 1), textArea.removeFromTop(textArea.getHeight() * 0.65f),
                       juce::Justification::centredBottom);

            g.setColour(palette::textMuted);
            g.setFont(font(juce::jlimit(10.0f, 13.0f, radius * 0.14f)));
            g.drawText("ACTIVATION INDEX", textArea, juce::Justification::centredTop);

            // Zone: icon + label
            const auto labelFont = font(14.0f, true);
            const float labelWidth = static_cast<float>(textWidth(labelFont, zone.label));
            const float totalWidth = 18.0f + 8.0f + labelWidth;
            const float left = pillArea.getCentreX() - totalWidth * 0.5f;
            drawZoneIcon(g, zone.icon, zone.colour, { left + 9.0f, pillArea.getCentreY() });

            g.setColour(palette::textPrimary);
            g.setFont(labelFont);
            g.drawText(zone.label, juce::Rectangle<float>(left + 26.0f, pillArea.getY(), labelWidth + 4.0f, pillArea.getHeight()),
                       juce::Justification::centredLeft);
        }

    private:
        enum class Icon { check, alert, cross };

        struct Zone
        {
            juce::Colour colour;
            juce::String label;
            Icon icon;
        };

        static Zone zoneFor(float s)
        {
            if (s > calmThreshold)    return { palette::good,     "Low activation - calming",       Icon::check };
            if (s > neutralThreshold) return { palette::warning,  "Medium activation - neutral",    Icon::alert };
            return                           { palette::critical, "High activation - stimulating",  Icon::cross };
        }

        // Three different shapes: circle with a check, triangle with "!", square with a cross
        static void drawZoneIcon(juce::Graphics& g, Icon icon, juce::Colour colour, juce::Point<float> c)
        {
            const float r = 9.0f;
            g.setColour(colour);

            switch (icon)
            {
                case Icon::check:
                {
                    g.fillEllipse(juce::Rectangle<float>(r * 2, r * 2).withCentre(c));
                    juce::Path tick;
                    tick.startNewSubPath(c.x - 4.0f, c.y);
                    tick.lineTo(c.x - 1.0f, c.y + 3.5f);
                    tick.lineTo(c.x + 4.5f, c.y - 3.5f);
                    g.setColour(palette::surface);
                    g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }
                case Icon::alert:
                {
                    juce::Path triangle;
                    triangle.addTriangle(c.x, c.y - r, c.x + r + 1.0f, c.y + r - 1.5f, c.x - r - 1.0f, c.y + r - 1.5f);
                    g.fillPath(triangle.createPathWithRoundedCorners(2.0f));
                    g.setColour(palette::surface);
                    g.fillRect(juce::Rectangle<float>(2.0f, 6.0f).withCentre({ c.x, c.y - 0.5f }));
                    g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre({ c.x, c.y + 5.0f }));
                    break;
                }
                case Icon::cross:
                {
                    g.fillRoundedRectangle(juce::Rectangle<float>(r * 2, r * 2).withCentre(c), 3.0f);
                    g.setColour(palette::textPrimary);
                    g.drawLine(c.x - 4.0f, c.y - 4.0f, c.x + 4.0f, c.y + 4.0f, 2.0f);
                    g.drawLine(c.x - 4.0f, c.y + 4.0f, c.x + 4.0f, c.y - 4.0f, 2.0f);
                    break;
                }
            }
        }

        float score = 0.0f;
    };
}
