#pragma once

#include "PluginProcessor.h"
#include "ui/BarkSpectrumView.h"
#include "ui/HistoryGraph.h"
#include "ui/ScoreGauge.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p);
    ~AudioPluginAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateRecordingControls();
    static juce::String formatTime(double seconds);

    AudioPluginAudioProcessor& processor;

    // Declared first so it outlives every component that uses it
    ui::AcousticLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow{ this, 500 };

    // Components
    ui::ScoreGauge gauge;
    ui::MetricBar brightnessBar{ "Brightness", ui::palette::seriesBlue };
    ui::MetricBar harshnessBar{ "Harshness", ui::palette::seriesOrange };
    ui::MetricBar dynamicsBar{ "Dynamic variability", ui::palette::seriesAqua };
    ui::MetricBar unpredictabilityBar{ "Temporal unpredictability", ui::palette::seriesYellow };
    ui::StatReadout loudnessStat{ "Loudness", "sone" };
    ui::StatReadout levelStat{ "Level (RMS)", "dBFS" };
    ui::HistoryGraph history;
    ui::BarkSpectrumView spectrum;
    ui::RecordButton recordButton;
    juce::TextButton exportButton{ "Export CSV" };
    juce::Label statusLabel;

    // Panel areas, set in resized() and drawn in paint()
    juce::Rectangle<int> headerArea, gaugePanel, metricsPanel, historyPanel, spectrumPanel, disclaimerArea;

    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
