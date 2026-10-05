#pragma once

#include "HistoryGraph.h"
#include "Theme.h"

namespace ui
{
    // The history graph's data as a table, newest row first: a text alternative to
    // the graph. Every row has a spoken name for screen readers, and the up and
    // down arrow keys move through the rows.
    class HistoryTable : public juce::Component, private juce::ListBoxModel
    {
    public:
        explicit HistoryTable(const HistoryGraph& sourceGraph)
            : source(sourceGraph), list("historyList", this)
        {
            setTitle("History table");

            list.setRowHeight(rowHeight);
            list.setHasFocusOutline(true);
            list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(list);
        }

        // Call after the graph has new samples
        void refresh()
        {
            list.updateContent();
            list.repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            area.removeFromTop(headerHeight);
            list.setBounds(area);
        }

        void paint(juce::Graphics& g) override
        {
            auto header = getLocalBounds().removeFromTop(headerHeight);
            g.setColour(palette::surfaceRaised);
            g.fillRoundedRectangle(header.toFloat(), 4.0f);

            g.setColour(palette::textSecondary);
            g.setFont(font(12.0f, true));

            const auto columns = columnBounds(header.getWidth() - list.getVerticalScrollBar().getWidth());
            for (int c = 0; c < numColumns; ++c)
                g.drawText(columnNames[c], columns[static_cast<size_t>(c)].withHeight(headerHeight).reduced(10, 0),
                           c == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
        }

    private:
        static constexpr int numColumns = 8;
        static constexpr int headerHeight = 26;
        static constexpr int rowHeight = 22;

        static constexpr const char* columnNames[numColumns] = { "Time", "Index", "Brightness", "Harshness", "Dynamics",
                                                                  "Unpredictability", "Rating", "Recording" };

        // Share of the width for each column
        std::array<juce::Rectangle<int>, numColumns> columnBounds(int width) const
        {
            static constexpr float shares[numColumns] = { 0.14f, 0.10f, 0.13f, 0.13f, 0.12f, 0.16f, 0.10f, 0.12f };

            std::array<juce::Rectangle<int>, numColumns> bounds;
            float x = 0.0f;
            for (int c = 0; c < numColumns; ++c)
            {
                const float w = shares[c] * static_cast<float>(width);
                bounds[static_cast<size_t>(c)] = juce::Rectangle<int>(juce::roundToInt(x), 0, juce::roundToInt(w), rowHeight);
                x += w;
            }

            return bounds;
        }

        int getNumRows() override { return source.getNumSamples(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (row >= source.getNumSamples())
                return;

            if (selected)
                g.fillAll(palette::accent.withAlpha(0.25f));
            else if (row % 2 == 1)
                g.fillAll(palette::surfaceRaised.withAlpha(0.35f));

            const auto sample = source.getSampleAtAge(row);
            const auto columns = columnBounds(width);

            g.setColour(palette::textPrimary);
            g.setFont(font(13.0f));

            for (int c = 0; c < numColumns; ++c)
                g.drawText(cellText(row, c, sample), columns[static_cast<size_t>(c)].withHeight(height).reduced(10, 0),
                           c == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
        }

        juce::String getNameForRow(int row) override
        {
            if (row >= source.getNumSamples())
                return {};

            const auto sample = source.getSampleAtAge(row);
            juce::StringArray parts;

            for (int c = 0; c < numColumns; ++c)
            {
                const auto text = cellText(row, c, sample);

                if (text.isNotEmpty())
                    parts.add(juce::String(columnNames[c]) + " " + text);
            }

            return parts.joinIntoString(", ");
        }

        static juce::String cellText(int row, int column, const HistoryGraph::Sample& sample)
        {
            switch (column)
            {
                case 0: return row == 0 ? juce::String("now") : "-" + juce::String(static_cast<float>(row) * HistoryGraph::secondsPerSample, 1) + " s";
                case 1: return juce::String(sample.values[HistoryGraph::index], 1);
                case 2: return juce::String(juce::roundToInt(sample.values[HistoryGraph::brightness])) + "%";
                case 3: return juce::String(juce::roundToInt(sample.values[HistoryGraph::harshness])) + "%";
                case 4: return juce::String(juce::roundToInt(sample.values[HistoryGraph::dynamics])) + "%";
                case 5: return juce::String(juce::roundToInt(sample.values[HistoryGraph::unpredictability])) + "%";
                case 6:
                {
                    const float value = sample.values[HistoryGraph::rating];
                    return value < 0.0f ? juce::String("-") : juce::String(juce::roundToInt(value / 100.0f * 6.0f + 1.0f)) + " / 7";
                }
                case 7: return sample.recording ? juce::String("REC") : juce::String();
                default: return {};
            }
        }

        const HistoryGraph& source;
        juce::ListBox list;
    };
}
