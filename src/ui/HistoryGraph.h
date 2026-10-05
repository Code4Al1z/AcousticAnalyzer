#pragma once

#include "Theme.h"
#include <array>
#include <functional>
#include <vector>

namespace ui
{
    // Scrolling time-series graph of the index and the four metrics over the last
    // 60 seconds. One shared 0-100 axis (the metrics are 0-100%). Click a legend
    // chip to show or hide a series; hover for a crosshair and values.
    class HistoryGraph : public juce::Component
    {
    public:
        enum SeriesId { index = 0, brightness, harshness, dynamics, unpredictability, rating, numSeries };

        static constexpr int capacity = 3000;          // Samples kept: 5 minutes at 10 Hz
        static constexpr float secondsPerSample = 0.1f; // 10 Hz, so 60 seconds in total

        HistoryGraph()
        {
            series[index]            = { "Index",            palette::textPrimary };
            series[brightness]       = { "Brightness",       palette::seriesCyan };
            series[harshness]        = { "Harshness",        palette::seriesOrange };
            series[dynamics]         = { "Dynamics",         palette::seriesViolet };
            series[unpredictability] = { "Unpredictability", palette::seriesPink };
            series[rating]           = { "Rating",           palette::seriesYellow };

            setWantsKeyboardFocus(true);

            setTitle("History graph");
            setDescription("Scrolling graph of the activation index and its metrics over the last 60 seconds");
        }

        // Values are 0-100 for every series. The rating series is a rating mapped from
        // 1-7 onto 0-100, or negative when there is no rating yet.
        void push(const std::array<float, numSeries>& values, bool recording)
        {
            samples[static_cast<size_t>(head)] = values;
            recordingFlags[static_cast<size_t>(head)] = recording;
            head = (head + 1) % capacity;
            count = juce::jmin(count + 1, capacity);
            repaint();
        }

        void clear()
        {
            count = 0;
            head = 0;
            repaint();
        }

        int getNumSamples() const { return count; }

        // How much history the graph shows: 30, 60 or 300 seconds (all of it stays in the table)
        void setWindowSeconds(int seconds)
        {
            windowSeconds = seconds <= 30 ? 30 : seconds <= 60 ? 60 : 300;
            windowSamples = juce::jlimit(2, capacity, juce::roundToInt(static_cast<float>(windowSeconds) / secondsPerSample));
            hoverAge = -1;
            repaint();
        }

        int getWindowSeconds() const { return windowSeconds; }

        struct Sample
        {
            std::array<float, numSeries> values{};
            bool recording = false;
        };

        // age 0 is the newest sample
        Sample getSampleAtAge(int age) const
        {
            const auto i = static_cast<size_t>((head - 1 - age + capacity * 2) % capacity);
            return { samples[i], recordingFlags[i] };
        }

        static const char* getSeriesName(int id)
        {
            static const char* names[] = { "Index", "Brightness", "Harshness", "Dynamics", "Unpredictability", "Rating" };
            return names[id];
        }

        // Hover position in component coordinates; pass { -1, -1 } to clear
        void setHoverPosition(juce::Point<float> position)
        {
            const auto plot = getPlotArea();
            int newAge = -1;

            if (visible() > 0 && plot.expanded(0.0f, 4.0f).contains(position))
            {
                const float dx = plot.getWidth() / static_cast<float>(windowSamples - 1);
                newAge = juce::jlimit(0, visible() - 1, juce::roundToInt((plot.getRight() - position.x) / dx));
            }

            if (newAge != hoverAge)
            {
                hoverAge = newAge;
                repaint();
            }
        }

        void setSeriesVisible(SeriesId id, bool visible)
        {
            series[id].visible = visible;
            repaint();
        }

        bool isSeriesVisible(SeriesId id) const { return series[id].visible; }

        // Called after the user shows or hides a series by clicking its legend chip
        std::function<void()> onSeriesVisibilityChanged;

        void resized() override
        {
            // Legend chips along the top, left to right
            float x = getPlotArea().getX();
            for (int s = 0; s < numSeries; ++s)
            {
                const float width = 18.0f + 6.0f + static_cast<float>(textWidth(font(12.0f), series[s].name)) + 16.0f;
                chipBounds[s] = juce::Rectangle<float>(x, 0.0f, width, 22.0f);
                x += width + 8.0f;
            }
        }

        void paint(juce::Graphics& g) override
        {
            const auto plot = getPlotArea();
            drawGridAndAxes(g, plot);
            drawLegend(g);

            if (visible() < 2)
            {
                g.setColour(palette::textMuted);
                g.setFont(font(13.0f));
                g.drawText("Waiting for audio...", plot, juce::Justification::centred);
                return;
            }

            drawRecordingSpans(g, plot);

            {
                juce::Graphics::ScopedSaveState state(g);
                g.reduceClipRegion(plot.expanded(3.0f).toNearestInt());

                // Headline area wash, then the metric lines, then the index on top
                if (series[index].visible)
                    drawArea(g, plot, index);

                for (int s = numSeries - 1; s >= 0; --s)
                    if (series[s].visible)
                        drawLine(g, plot, s);
            }

            drawEndLabels(g, plot);

            if (hoverAge >= 0)
                drawHover(g, plot);
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            setHoverPosition(e.position);
            setMouseCursor(chipAt(e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseExit(const juce::MouseEvent&) override { setHoverPosition({ -1.0f, -1.0f }); }

        // Keyboard: Left/Right choose a legend chip, Space or Enter shows/hides its series
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::leftKey)
                focusedChip = (focusedChip + numSeries - 1) % numSeries;
            else if (key == juce::KeyPress::rightKey)
                focusedChip = (focusedChip + 1) % numSeries;
            else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
                toggleSeries(focusedChip);
            else
                return false;

            repaint();
            return true;
        }

        void focusGained(FocusChangeType) override { repaint(); }
        void focusLost(FocusChangeType) override { repaint(); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int chip = chipAt(e.position);

            if (chip >= 0)
            {
                focusedChip = chip;
                toggleSeries(chip);
            }
        }

    private:
        struct Series
        {
            juce::String name;
            juce::Colour colour;
            bool visible = true;
        };

        // Samples inside the displayed window
        int visible() const { return juce::jmin(count, windowSamples); }

        void toggleSeries(int id)
        {
            setSeriesVisible(static_cast<SeriesId>(id), !series[id].visible);

            if (onSeriesVisibilityChanged)
                onSeriesVisibilityChanged();
        }

        juce::Rectangle<float> getPlotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft(36.0f).withTrimmedTop(32.0f)
                                              .withTrimmedRight(56.0f).withTrimmedBottom(22.0f);
        }

        int chipAt(juce::Point<float> position) const
        {
            for (int s = 0; s < numSeries; ++s)
                if (chipBounds[s].contains(position))
                    return s;

            return -1;
        }

        float xForAge(const juce::Rectangle<float>& plot, int age) const
        {
            return plot.getRight() - static_cast<float>(age) * plot.getWidth() / static_cast<float>(windowSamples - 1);
        }

        float yForValue(const juce::Rectangle<float>& plot, float value) const
        {
            return plot.getBottom() - juce::jlimit(0.0f, 100.0f, value) / 100.0f * plot.getHeight();
        }

        float valueAt(int seriesId, int age) const
        {
            const auto i = static_cast<size_t>((head - 1 - age + capacity * 2) % capacity);
            return samples[i][static_cast<size_t>(seriesId)];
        }

        bool recordingAt(int age) const
        {
            return recordingFlags[static_cast<size_t>((head - 1 - age + capacity * 2) % capacity)];
        }

        void drawGridAndAxes(juce::Graphics& g, const juce::Rectangle<float>& plot) const
        {
            g.setFont(font(11.0f));

            for (int v : { 0, 25, 50, 75, 100 })
            {
                const float y = yForValue(plot, static_cast<float>(v));
                drawHairline(g, plot.getX(), plot.getRight(), y, palette::grid);

                g.setColour(palette::textMuted);
                g.drawText(juce::String(v), juce::Rectangle<float>(plot.getX() - 34.0f, y - 8.0f, 28.0f, 16.0f),
                           juce::Justification::centredRight);
            }

            const std::vector<int> ticks = windowSeconds == 30 ? std::vector<int>{ 30, 20, 10, 0 }
                                         : windowSeconds == 300 ? std::vector<int>{ 300, 240, 180, 120, 60, 0 }
                                                                : std::vector<int>{ 60, 45, 30, 15, 0 };

            for (int seconds : ticks)
            {
                const float x = plot.getRight() - static_cast<float>(seconds) / static_cast<float>(windowSeconds) * plot.getWidth();
                const auto label = seconds == 0 ? juce::String("now")
                                 : seconds >= 120 ? "-" + juce::String(seconds / 60) + " min"
                                                  : "-" + juce::String(seconds) + " s";

                g.setColour(palette::textMuted);
                g.drawText(label, juce::Rectangle<float>(x - 24.0f, plot.getBottom() + 4.0f, 48.0f, 14.0f),
                           juce::Justification::centred);
            }
        }

        void drawLegend(juce::Graphics& g) const
        {
            for (int s = 0; s < numSeries; ++s)
            {
                const auto chip = chipBounds[s];
                const bool visible = series[s].visible;

                const bool focused = hasKeyboardFocus(false) && s == focusedChip;
                g.setColour(focused ? palette::textPrimary : palette::outline);
                g.drawRoundedRectangle(chip.reduced(0.5f), 6.0f, focused ? 2.0f : 1.0f);

                // Line key in the series colour; the text stays in a text token
                g.setColour(series[s].colour.withAlpha(visible ? 1.0f : 0.3f));
                g.drawLine(chip.getX() + 8.0f, chip.getCentreY(), chip.getX() + 24.0f, chip.getCentreY(), 2.0f);

                g.setColour(visible ? palette::textSecondary : palette::textMuted);
                g.setFont(font(12.0f));
                g.drawText(series[s].name, chip.withTrimmedLeft(30.0f), juce::Justification::centredLeft);
            }
        }

        void drawRecordingSpans(juce::Graphics& g, const juce::Rectangle<float>& plot) const
        {
            int spanEnd = -1;   // Age of the newest sample in the current span
            int lastSpanStartAge = -1;
            float lastSpanX = 0.0f;

            auto flush = [&](int oldestAge)
            {
                if (spanEnd < 0)
                    return;

                const float x1 = xForAge(plot, spanEnd);
                const float x0 = xForAge(plot, oldestAge);
                g.setColour(palette::critical.withAlpha(0.10f));
                g.fillRect(juce::Rectangle<float>(x0, plot.getY(), x1 - x0, plot.getHeight()));
                g.setColour(palette::critical.withAlpha(0.7f));
                g.fillRect(juce::Rectangle<float>(x0, plot.getY(), x1 - x0, 2.0f));

                lastSpanStartAge = oldestAge;
                lastSpanX = x0;
                spanEnd = -1;
            };

            for (int age = 0; age < visible(); ++age)
            {
                if (recordingAt(age))
                {
                    if (spanEnd < 0)
                        spanEnd = age;
                }
                else
                {
                    flush(age - 1);
                }
            }
            flush(visible() - 1);

            if (lastSpanStartAge >= 0)
            {
                g.setColour(palette::textSecondary);
                g.setFont(font(10.0f, true));
                g.drawText("REC", juce::Rectangle<float>(juce::jmax(plot.getX(), lastSpanX) + 4.0f, plot.getY() + 4.0f, 30.0f, 12.0f),
                           juce::Justification::centredLeft);
            }
        }

        juce::Path makePath(const juce::Rectangle<float>& plot, int seriesId) const
        {
            juce::Path path;

            bool drawing = false;

            for (int age = visible() - 1; age >= 0; --age)
            {
                const float value = valueAt(seriesId, age);

                if (value < 0.0f) // No value (a rating not given yet): leave a gap
                {
                    drawing = false;
                    continue;
                }

                const juce::Point<float> p(xForAge(plot, age), yForValue(plot, value));

                if (drawing)
                    path.lineTo(p);
                else
                    path.startNewSubPath(p);

                drawing = true;
            }

            return path;
        }

        void drawArea(juce::Graphics& g, const juce::Rectangle<float>& plot, int seriesId) const
        {
            auto path = makePath(plot, seriesId);
            path.lineTo(xForAge(plot, 0), plot.getBottom());
            path.lineTo(xForAge(plot, visible() - 1), plot.getBottom());
            path.closeSubPath();

            g.setColour(series[seriesId].colour.withAlpha(0.10f));
            g.fillPath(path);
        }

        void drawLine(juce::Graphics& g, const juce::Rectangle<float>& plot, int seriesId) const
        {
            g.setColour(series[seriesId].colour);
            g.strokePath(makePath(plot, seriesId),
                         juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // A dot wherever a new rating was entered
            if (seriesId == rating)
            {
                for (int age = visible() - 1; age >= 0; --age)
                {
                    const float value = valueAt(rating, age);
                    const float before = age + 1 < visible() ? valueAt(rating, age + 1) : -1.0f;

                    if (value >= 0.0f && !juce::approximatelyEqual(value, before))
                        drawRingedDot(g, { xForAge(plot, age), yForValue(plot, value) }, 3.5f, series[rating].colour);
                }
            }
        }

        void drawEndLabels(juce::Graphics& g, const juce::Rectangle<float>& plot) const
        {
            struct Label { int seriesId; float y; };
            std::array<Label, numSeries> labels{};
            int n = 0;

            for (int s = 0; s < numSeries; ++s)
            {
                if (!series[s].visible || valueAt(s, 0) < 0.0f)
                    continue;

                const float y = yForValue(plot, valueAt(s, 0));
                drawRingedDot(g, { plot.getRight(), y }, 4.0f, series[s].colour);
                labels[static_cast<size_t>(n++)] = { s, y };
            }

            // Spread the value labels so they don't overlap
            std::sort(labels.begin(), labels.begin() + n, [](const Label& a, const Label& b) { return a.y < b.y; });

            const float minGap = 15.0f;
            for (int i = 1; i < n; ++i)
                labels[static_cast<size_t>(i)].y = juce::jmax(labels[static_cast<size_t>(i)].y, labels[static_cast<size_t>(i) - 1].y + minGap);

            const float overflow = n > 0 ? labels[static_cast<size_t>(n) - 1].y - (plot.getBottom() + 6.0f) : 0.0f;
            if (overflow > 0.0f)
                for (int i = 0; i < n; ++i)
                    labels[static_cast<size_t>(i)].y -= overflow;

            g.setFont(font(12.0f, true));
            g.setColour(palette::textPrimary);

            for (int i = 0; i < n; ++i)
            {
                const auto& label = labels[static_cast<size_t>(i)];
                g.drawText(formatValue(label.seriesId, valueAt(label.seriesId, 0)),
                           juce::Rectangle<float>(plot.getRight() + 9.0f, label.y - 8.0f, 44.0f, 16.0f),
                           juce::Justification::centredLeft);
            }
        }

        static juce::String formatValue(int seriesId, float value)
        {
            if (seriesId == rating)
                return value < 0.0f ? juce::String("-") : juce::String(juce::roundToInt(value / 100.0f * 6.0f + 1.0f)) + " / 7";

            return seriesId == index ? juce::String(value, 1) : juce::String(juce::roundToInt(value)) + "%";
        }

        void drawHover(juce::Graphics& g, const juce::Rectangle<float>& plot) const
        {
            const float x = xForAge(plot, hoverAge);

            g.setColour(palette::textMuted.withAlpha(0.6f));
            g.fillRect(juce::Rectangle<float>(std::floor(x), plot.getY(), 1.0f, plot.getHeight()));

            int rows = 0;
            for (int s = 0; s < numSeries; ++s)
            {
                if (!series[s].visible)
                    continue;

                if (valueAt(s, hoverAge) >= 0.0f)
                    drawRingedDot(g, { x, yForValue(plot, valueAt(s, hoverAge)) }, 4.0f, series[s].colour);

                ++rows;
            }

            if (rows == 0)
                return;

            // Tooltip box
            const float boxWidth = 176.0f;
            const float boxHeight = 12.0f + 18.0f * static_cast<float>(rows + 1);
            const float boxX = (x + 14.0f + boxWidth <= plot.getRight()) ? x + 14.0f : x - 14.0f - boxWidth;
            const float boxY = juce::jlimit(2.0f, juce::jmax(2.0f, static_cast<float>(getHeight()) - boxHeight - 2.0f), plot.getY() + 4.0f);
            const auto box = juce::Rectangle<float>(boxX, boxY, boxWidth, boxHeight);

            g.setColour(palette::surfaceRaised);
            g.fillRoundedRectangle(box, 6.0f);
            g.setColour(palette::outline);
            g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);

            const float seconds = static_cast<float>(hoverAge) * secondsPerSample;
            auto row = box.reduced(10.0f, 6.0f);

            g.setColour(palette::textMuted);
            g.setFont(font(12.0f));
            g.drawText(hoverAge == 0 ? juce::String("now") : "-" + juce::String(seconds, 1) + " s",
                       row.removeFromTop(18.0f), juce::Justification::centredLeft);

            for (int s = 0; s < numSeries; ++s)
            {
                if (!series[s].visible)
                    continue;

                auto line = row.removeFromTop(18.0f);

                g.setColour(series[s].colour);
                g.drawLine(line.getX(), line.getCentreY(), line.getX() + 14.0f, line.getCentreY(), 2.0f);

                g.setColour(palette::textSecondary);
                g.drawText(series[s].name, line.withTrimmedLeft(20.0f), juce::Justification::centredLeft);

                g.setColour(palette::textPrimary);
                g.setFont(font(12.0f, true));
                g.drawText(formatValue(s, valueAt(s, hoverAge)), line, juce::Justification::centredRight);
                g.setFont(font(12.0f));
            }
        }

        std::array<Series, numSeries> series;
        std::array<juce::Rectangle<float>, numSeries> chipBounds{};
        std::array<std::array<float, numSeries>, capacity> samples{};
        std::array<bool, capacity> recordingFlags{};
        int head = 0;       // Next write index
        int count = 0;      // Valid samples
        int windowSeconds = 60;
        int windowSamples = 600;
        int hoverAge = -1;  // Samples back from the newest, or -1
        int focusedChip = 0; // Legend chip the keyboard acts on
    };
}
