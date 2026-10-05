#pragma once

#include "Theme.h"
#include <array>

namespace ui
{
    // Specific loudness (sone per Bark) for each of the 24 critical bands, with a
    // smooth fall-off and peak-hold ticks. The axis range follows the signal.
    class BarkSpectrumView : public juce::Component
    {
    public:
        static constexpr int numBands = 24;

        BarkSpectrumView()
        {
            setTitle("Loudness by critical band");
            setDescription("Bar display of loudness in each of the 24 Bark bands");
        }

        void setBandLayout(const std::array<float, numBands + 1>& edges, const std::array<float, numBands>& centres)
        {
            edgesHz = edges;
            centresHz = centres;
            repaint();
        }

        // Marks the bands behind the "presence" bracket (inclusive)
        void setPresenceBands(int first, int last)
        {
            presenceFirst = first;
            presenceLast = last;
            repaint();
        }

        // Call at the UI rate with the latest specific loudness values
        void update(const std::array<float, numBands>& specificLoudness)
        {
            bool changed = false;
            float highestPeak = 0.0f;

            for (size_t i = 0; i < numBands; ++i)
            {
                const float v = juce::jlimit(0.0f, 100.0f, specificLoudness[i]);

                // Instant attack, smooth release
                const float newDisplay = v > display[i] ? v : display[i] + (v - display[i]) * 0.2f;

                // Peak hold for about a second, then fall slowly
                float newPeak = peak[i];
                if (v >= peak[i])
                {
                    newPeak = v;
                    hold[i] = 30;
                }
                else if (hold[i] > 0)
                {
                    --hold[i];
                }
                else
                {
                    newPeak = juce::jmax(newDisplay, peak[i] * 0.97f);
                }

                changed = changed || std::abs(newDisplay - display[i]) > 0.002f || std::abs(newPeak - peak[i]) > 0.002f;
                display[i] = newDisplay;
                peak[i] = newPeak;
                highestPeak = juce::jmax(highestPeak, newPeak);
            }

            const float target = juce::jmax(1.0f, highestPeak * 1.15f);
            const float newScale = scaleMax + (target - scaleMax) * (target > scaleMax ? 0.3f : 0.03f);
            changed = changed || std::abs(newScale - scaleMax) > 0.002f;
            scaleMax = newScale;

            if (changed)
                repaint();
        }

        void setHoverPosition(juce::Point<float> position)
        {
            const auto plot = getPlotArea();
            int newBand = -1;

            if (plot.contains(position))
                newBand = juce::jlimit(0, numBands - 1, static_cast<int>((position.x - plot.getX()) / (plot.getWidth() / numBands)));

            if (newBand != hoverBand)
            {
                hoverBand = newBand;
                repaint();
            }
        }

        void mouseMove(const juce::MouseEvent& e) override { setHoverPosition(e.position); }
        void mouseExit(const juce::MouseEvent&) override { setHoverPosition({ -1.0f, -1.0f }); }

        void paint(juce::Graphics& g) override
        {
            const auto plot = getPlotArea();
            const float slotWidth = plot.getWidth() / numBands;

            // Grid and axis labels
            g.setFont(font(11.0f));
            for (float fraction : { 0.0f, 0.5f, 1.0f })
            {
                const float y = plot.getBottom() - fraction * plot.getHeight();
                drawHairline(g, plot.getX(), plot.getRight(), y, palette::grid);

                g.setColour(palette::textMuted);
                g.drawText(juce::String(fraction * scaleMax, scaleMax < 10.0f ? 1 : 0),
                           juce::Rectangle<float>(plot.getX() - 38.0f, y - 8.0f, 32.0f, 16.0f), juce::Justification::centredRight);
            }

            g.setColour(palette::textMuted);
            g.drawText("sone/Bark", juce::Rectangle<float>(0.0f, 2.0f, 80.0f, 14.0f), juce::Justification::centredLeft);

            // Presence region (where hearing is most sensitive)
            if (presenceFirst >= 0)
            {
                auto region = juce::Rectangle<float>(plot.getX() + slotWidth * static_cast<float>(presenceFirst), plot.getY(),
                                                     slotWidth * static_cast<float>(presenceLast - presenceFirst + 1), plot.getHeight());
                g.setColour(palette::surfaceRaised.withAlpha(0.6f));
                g.fillRect(region);

                // Label above the plot, left aligned with the region, so it never covers the bars
                g.setColour(palette::textMuted);
                g.setFont(font(10.0f, true));
                g.drawText("PRESENCE " + formatHz(edgesHz[static_cast<size_t>(presenceFirst)]) + "-"
                               + formatHz(edgesHz[static_cast<size_t>(presenceLast) + 1]) + " Hz",
                           juce::Rectangle<float>(region.getX(), plot.getY() - 16.0f, 160.0f, 14.0f), juce::Justification::centredLeft);
            }

            // Bars: at most 24 px wide, square at the baseline, rounded at the top
            const float barWidth = juce::jmin(24.0f, slotWidth - 4.0f);
            for (int i = 0; i < numBands; ++i)
            {
                const float height = juce::jmin(1.0f, display[static_cast<size_t>(i)] / scaleMax) * plot.getHeight();
                const float x = plot.getX() + slotWidth * (static_cast<float>(i) + 0.5f) - barWidth * 0.5f;

                if (height >= 1.0f)
                {
                    juce::Path bar;
                    const float corner = juce::jmin(4.0f, height * 0.5f, barWidth * 0.5f);
                    bar.addRoundedRectangle(x, plot.getBottom() - height, barWidth, height, corner, corner, true, true, false, false);
                    // Brand gradient across the bands: orange at the low end, pink at the high end
                    const auto barColour = palette::brandGradient(static_cast<float>(i) / (numBands - 1));
                    g.setColour(i == hoverBand ? barColour.brighter(0.3f) : barColour);
                    g.fillPath(bar);
                }

                const float peakY = plot.getBottom() - juce::jmin(1.0f, peak[static_cast<size_t>(i)] / scaleMax) * plot.getHeight();
                if (peakY < plot.getBottom() - 2.0f)
                {
                    g.setColour(palette::textSecondary);
                    g.fillRect(juce::Rectangle<float>(x, peakY - 2.0f, barWidth, 2.0f));
                }
            }

            // Frequency labels under some of the bands
            g.setFont(font(11.0f));
            g.setColour(palette::textMuted);
            for (int i : { 1, 4, 7, 10, 13, 16, 19, 22 })
            {
                const float x = plot.getX() + slotWidth * (static_cast<float>(i) + 0.5f);
                g.drawText(formatHz(centresHz[static_cast<size_t>(i)]),
                           juce::Rectangle<float>(x - 26.0f, plot.getBottom() + 4.0f, 52.0f, 14.0f), juce::Justification::centred);
            }
            g.drawText("Hz", juce::Rectangle<float>(plot.getX() - 38.0f, plot.getBottom() + 4.0f, 32.0f, 14.0f),
                       juce::Justification::centredRight);

            // Hover readout
            if (hoverBand >= 0)
            {
                const auto b = static_cast<size_t>(hoverBand);
                g.setColour(palette::textSecondary);
                g.setFont(font(12.0f));
                g.drawText("Band " + juce::String(hoverBand + 1) + "  " + formatHz(edgesHz[b]) + "-" + formatHz(edgesHz[b + 1])
                               + " Hz   " + juce::String(display[b], 2) + " sone/Bark",
                           plot.withHeight(16.0f).translated(0.0f, -20.0f), juce::Justification::centredRight);
            }
        }

    private:
        juce::Rectangle<float> getPlotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft(42.0f).withTrimmedTop(22.0f)
                                              .withTrimmedRight(8.0f).withTrimmedBottom(22.0f);
        }

        static juce::String formatHz(float hz)
        {
            if (hz >= 1000.0f)
                return juce::String(hz / 1000.0f, hz >= 10000.0f || std::abs(hz / 1000.0f - std::round(hz / 1000.0f)) < 0.01f ? 0 : 1) + "k";

            return juce::String(juce::roundToInt(hz));
        }

        std::array<float, numBands + 1> edgesHz{};
        std::array<float, numBands> centresHz{};
        std::array<float, numBands> display{};
        std::array<float, numBands> peak{};
        std::array<int, numBands> hold{};
        float scaleMax = 1.0f;
        int presenceFirst = -1, presenceLast = -1;
        int hoverBand = -1;
    };
}
