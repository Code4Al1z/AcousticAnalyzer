#pragma once

#include "Theme.h"

namespace ui
{
    // A labelled horizontal bar for a 0-1 metric. The coloured dot ties it to the
    // same series colour used in the history graph.
    class MetricBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MetricBar(juce::String nameToUse, juce::Colour keyColourToUse)
            : name(std::move(nameToUse)), keyColour(keyColourToUse)
        {
            setTitle(name);
        }

        void setValue(float newValue01, const juce::String& newDetail)
        {
            newValue01 = juce::jlimit(0.0f, 1.0f, newValue01);

            if (juce::approximatelyEqual(newValue01, value) && newDetail == detail)
                return;

            value = newValue01;
            detail = newDetail;
            setDescription(juce::String(juce::roundToInt(value * 100.0f)) + " percent. " + detail);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            auto textRow = bounds.removeFromTop(18.0f);
            bounds.removeFromTop(3.0f);
            auto track = bounds.removeFromTop(6.0f);

            // Dot, name, raw value, percentage
            g.setColour(keyColour);
            g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre({ textRow.getX() + 4.0f, textRow.getCentreY() }));

            auto percentArea = textRow.removeFromRight(46.0f);
            auto detailArea = textRow.removeFromRight(96.0f);
            textRow.removeFromLeft(16.0f);

            g.setColour(palette::textPrimary);
            g.setFont(font(14.0f));
            g.drawText(name, textRow, juce::Justification::centredLeft);

            g.setColour(palette::textMuted);
            g.setFont(font(12.0f));
            g.drawText(detail, detailArea, juce::Justification::centredRight);

            g.setColour(palette::textPrimary);
            g.setFont(font(14.0f, true));
            g.drawText(juce::String(juce::roundToInt(value * 100.0f)) + "%", percentArea, juce::Justification::centredRight);

            // Track and fill: square at the baseline, rounded at the data end
            g.setColour(palette::surfaceRaised);
            g.fillRoundedRectangle(track, 3.0f);

            const float fillWidth = track.getWidth() * value;
            if (fillWidth > 0.5f)
            {
                const float corner = juce::jmin(3.0f, fillWidth * 0.5f);
                juce::Path fill;
                fill.addRoundedRectangle(track.getX(), track.getY(), fillWidth, track.getHeight(),
                                         corner, corner, false, true, false, true);
                g.setColour(keyColour);
                g.fillPath(fill);
            }
        }

    private:
        juce::String name, detail;
        juce::Colour keyColour;
        float value = 0.0f;
    };

    // A small "label / value unit" readout
    class StatReadout : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        StatReadout(juce::String nameToUse, juce::String unitToUse)
            : name(std::move(nameToUse)), unit(std::move(unitToUse))
        {
            setTitle(name);
        }

        void setValue(const juce::String& newText)
        {
            if (newText == text)
                return;

            text = newText;
            setDescription(text + " " + unit);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            auto labelRow = bounds.removeFromTop(16.0f);

            g.setColour(palette::textMuted);
            g.setFont(font(12.0f));
            g.drawText(name, labelRow, juce::Justification::centredLeft);

            const auto valueFont = font(22.0f, true);
            g.setColour(palette::textPrimary);
            g.setFont(valueFont);
            g.drawText(text, bounds, juce::Justification::centredLeft);

            g.setColour(palette::textMuted);
            g.setFont(font(12.0f));
            g.drawText(unit, bounds.withTrimmedLeft(static_cast<float>(textWidth(valueFont, text)) + 6.0f),
                       juce::Justification::centredLeft);
        }

    private:
        juce::String name, unit, text;
    };

    // Record / stop toggle. The toggle state means "recording".
    class RecordButton : public juce::Button
    {
    public:
        RecordButton() : juce::Button("Record") {}

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            const bool recording = getToggleState();
            auto bounds = getLocalBounds().toFloat().reduced(0.5f);

            auto fill = recording ? palette::critical : palette::surfaceRaised;
            if (down)
                fill = fill.darker(0.2f);
            else if (highlighted)
                fill = fill.brighter(0.15f);

            g.setColour(fill);
            g.fillRoundedRectangle(bounds, 8.0f);
            g.setColour(palette::outline);
            g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

            // Icon: red dot to start, white square to stop
            auto icon = juce::Rectangle<float>(12.0f, 12.0f).withCentre({ bounds.getX() + 22.0f, bounds.getCentreY() });
            if (recording)
            {
                g.setColour(palette::textPrimary);
                g.fillRoundedRectangle(icon, 2.0f);
            }
            else
            {
                g.setColour(palette::critical);
                g.fillEllipse(icon);
            }

            g.setColour(palette::textPrimary);
            g.setFont(font(14.0f, true));
            g.drawText(recording ? "Stop" : "Record", bounds.withTrimmedLeft(38.0f), juce::Justification::centredLeft);
        }
    };
}
