#pragma once

#include "PluginProcessor.h"
#include "Presets.h"
#include "ui/BarkSpectrumView.h"
#include "ui/HistoryGraph.h"
#include "ui/HistoryTable.h"
#include "ui/RatingStrip.h"
#include "ui/ScoreGauge.h"
#include "ui/SettingsPanel.h"
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
    bool keyPressed(const juce::KeyPress& key) override; // 1-7: listener rating while recording

private:
    void timerCallback() override;
    void updateRecordingControls();
    void showSettings(bool show);
    void setHistoryView(bool showTable);
    void updateVisibility();
    void applyScale(float scale);
    void updateRatingSummary();
    void confirmClear();
    void startExport();
    juce::ValueTree getUiState() const;
    static juce::String formatTime(double seconds);

    AudioPluginAudioProcessor& processor;

    // Declared first so it outlives every component that uses it
    ui::AcousticLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow{ this, 500 };

    // Components
    ui::ScoreGauge gauge;
    ui::MetricBar brightnessBar{ "Brightness", ui::palette::seriesCyan };
    ui::MetricBar harshnessBar{ "Harshness", ui::palette::seriesOrange };
    ui::MetricBar dynamicsBar{ "Dynamic variability", ui::palette::seriesViolet };
    ui::MetricBar unpredictabilityBar{ "Temporal unpredictability", ui::palette::seriesPink };
    ui::StatReadout loudnessStat{ "Loudness", "sone" };
    ui::StatReadout levelStat{ "Level (RMS)", "dBFS" };
    ui::StatReadout widthStat{ "Stereo width", "%" };
    ui::HistoryGraph history;
    ui::HistoryTable historyTable{ history };
    ui::RatingStrip ratingStrip;
    juce::TextButton graphButton{ "Graph" };
    juce::TextButton tableButton{ "Table" };
    ui::BarkSpectrumView spectrum;
    ui::RecordButton recordButton;
    juce::TextButton exportButton{ "Export CSV" };
    juce::TextButton settingsButton{ "Settings" };
    juce::TextButton clearButton{ "Clear" };
    juce::Label statusLabel;
    PresetStore presetStore;
    ui::SettingsPanel settingsPanel{ processor.apvts, presetStore };

    std::unique_ptr<juce::FileChooser> fileChooser; // Must outlive the async dialog

    std::atomic<float>* calmAboveValue = nullptr;
    std::atomic<float>* neutralAboveValue = nullptr;
    bool uiStateRestored = false; // Don't write the size back until it has been restored
    bool tableMode = false;       // History shown as a table instead of a graph
    bool wasLogging = false;
    int lastRatingCount = 0;
    RatingSummary ratingSummary;

    // Panel areas, set in resized() and drawn in paint()
    juce::Rectangle<int> headerArea, gaugePanel, metricsPanel, historyPanel, spectrumPanel, disclaimerArea;

    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
