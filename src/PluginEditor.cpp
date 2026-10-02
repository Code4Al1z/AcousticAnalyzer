#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Metric tooltips: what each number means
    brightnessBar.setTooltip("Sharpness (Zwicker / von Bismarck): how much of the loudness sits at high frequencies. "
                             "0 to 4 acum maps to 0 to 100%.");
    harshnessBar.setTooltip("Half roughness (fast amplitude modulation around 70 Hz, in asper), half the loudness "
                            "in the 2 to 5 kHz region where hearing is most sensitive.");
    dynamicsBar.setTooltip("Spread of the level (in dB) over the last 5 seconds. 12 dB or more reads 100%.");
    unpredictabilityBar.setTooltip("Average level change between 50 ms windows over the last 5 seconds. "
                                   "6 dB or more reads 100%.");
    loudnessStat.setTooltip("Zwicker loudness in sone. Assumes 100 dB SPL at 0 dBFS RMS, so treat it as relative "
                            "unless your monitoring is calibrated to that.");
    levelStat.setTooltip("RMS level of the input, averaged over the analysed channels.");

    spectrum.setBandLayout(AudioPluginAudioProcessor::barkEdgesHz, AudioPluginAudioProcessor::barkCentresHz);
    spectrum.setPresenceBands(AudioPluginAudioProcessor::presenceFirstBand, AudioPluginAudioProcessor::presenceLastBand);

    recordButton.setTooltip("Start or stop logging the analysis (about 40 points per second) for CSV export.");
    recordButton.onClick = [this]()
        {
            if (processor.isCurrentlyLogging())
                processor.stopLogging();
            else
                processor.startLogging();

            updateRecordingControls();
        };

    exportButton.setEnabled(false);
    exportButton.onClick = [this]() { processor.exportToCSV(); };

    statusLabel.setFont(ui::font(13.0f));
    statusLabel.setColour(juce::Label::textColourId, ui::palette::textSecondary);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    for (juce::Component* c : std::initializer_list<juce::Component*>{ &gauge, &brightnessBar, &harshnessBar, &dynamicsBar,
             &unpredictabilityBar, &loudnessStat, &levelStat, &history, &spectrum, &recordButton, &exportButton, &statusLabel })
        addAndMakeVisible(c);

    setResizable(true, true);
    setResizeLimits(820, 700, 1800, 1300);
    setSize(940, 760);

    updateRecordingControls();
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::palette::window);

    // Header
    g.setColour(ui::palette::textPrimary);
    g.setFont(ui::font(20.0f, true));
    g.drawText("Acoustic Environment Research Tool", headerArea, juce::Justification::centredLeft);

    const auto betaFont = ui::font(11.0f, true);
    const auto betaWidth = static_cast<float>(ui::textWidth(betaFont, "BETA v0.1")) + 18.0f;
    const auto beta = juce::Rectangle<float>(betaWidth, 22.0f)
                          .withRightX(static_cast<float>(headerArea.getRight()))
                          .withCentre({ static_cast<float>(headerArea.getRight()) - betaWidth * 0.5f, static_cast<float>(headerArea.getCentreY()) });
    g.setColour(ui::palette::outline);
    g.drawRoundedRectangle(beta.reduced(0.5f), 11.0f, 1.0f);
    g.setColour(ui::palette::textSecondary);
    g.setFont(betaFont);
    g.drawText("BETA v0.1", beta, juce::Justification::centred);

    // Panels and their titles
    struct Titled { juce::Rectangle<int> area; const char* title; };
    for (const auto& panel : { Titled{ gaugePanel, nullptr },
                               Titled{ metricsPanel, "PERCEPTUAL METRICS" },
                               Titled{ historyPanel, "HISTORY - LAST 60 SECONDS" },
                               Titled{ spectrumPanel, "LOUDNESS PER CRITICAL BAND (BARK SCALE)" } })
    {
        ui::drawPanel(g, panel.area);

        if (panel.title != nullptr)
        {
            g.setColour(ui::palette::textMuted);
            g.setFont(ui::font(11.0f, true));
            g.drawText(panel.title, panel.area.getX() + 16, panel.area.getY() + 10, panel.area.getWidth() - 32, 16,
                       juce::Justification::centredLeft);
        }
    }

    g.setColour(ui::palette::grid);
    g.fillRect(juce::Rectangle<float>(static_cast<float>(loudnessStat.getX()) - 10.0f, static_cast<float>(metricsPanel.getY() + 40),
                                      1.0f, static_cast<float>(metricsPanel.getHeight() - 52)));

    g.setColour(ui::palette::textMuted);
    g.setFont(ui::font(11.0f));
    g.drawText("Research tool in development - measures acoustic activation potential", disclaimerArea,
               juce::Justification::centred);
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    const int gap = 12;

    headerArea = area.removeFromTop(36);
    area.removeFromTop(gap);

    disclaimerArea = area.removeFromBottom(16);
    area.removeFromBottom(6);

    auto footer = area.removeFromBottom(36);
    area.removeFromBottom(gap);

    // Top row: gauge and metrics
    auto topRow = area.removeFromTop(juce::jlimit(176, 230, juce::roundToInt(static_cast<float>(area.getHeight()) * 0.27f)));
    area.removeFromTop(gap);

    gaugePanel = topRow.removeFromLeft(juce::jlimit(250, 340, juce::roundToInt(static_cast<float>(topRow.getWidth()) * 0.32f)));
    topRow.removeFromLeft(gap);
    metricsPanel = topRow;

    // Below: history graph and band display
    historyPanel = area.removeFromTop(juce::roundToInt(static_cast<float>(area.getHeight() - gap) * 0.55f));
    area.removeFromTop(gap);
    spectrumPanel = area;

    // Children
    gauge.setBounds(gaugePanel.reduced(10));

    // Metrics: four bars on the left, the raw readouts stacked on the right
    auto metrics = metricsPanel.reduced(16, 0).withTrimmedTop(34).withTrimmedBottom(10);
    auto stats = metrics.removeFromRight(150);
    metrics.removeFromRight(20);

    const int barHeight = metrics.getHeight() / 4;
    for (auto* bar : { &brightnessBar, &harshnessBar, &dynamicsBar, &unpredictabilityBar })
        bar->setBounds(metrics.removeFromTop(barHeight).reduced(0, 3));

    loudnessStat.setBounds(stats.removeFromTop(stats.getHeight() / 2));
    levelStat.setBounds(stats);

    history.setBounds(historyPanel.withTrimmedTop(30).reduced(10, 6));
    spectrum.setBounds(spectrumPanel.withTrimmedTop(30).reduced(10, 6));

    recordButton.setBounds(footer.removeFromLeft(130));
    footer.removeFromLeft(gap);
    exportButton.setBounds(footer.removeFromLeft(130));
    footer.removeFromLeft(16);
    statusLabel.setBounds(footer);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::timerCallback()
{
    const float score = processor.getAcousticActivationScore();
    const float brightness = processor.getSpectralCentroid();
    const float harshness = processor.getSpectralHarshness();
    const float dynamics = processor.getDynamicVariability();
    const float unpredictability = processor.getTemporalUnpredictability();

    gauge.setScore(score);
    brightnessBar.setValue(brightness, juce::String(processor.getSharpnessAcum(), 2) + " acum");
    harshnessBar.setValue(harshness, juce::String(processor.getRoughnessAsper(), 2) + " asper");
    dynamicsBar.setValue(dynamics, {});
    unpredictabilityBar.setValue(unpredictability, {});

    loudnessStat.setValue(juce::String(processor.getLoudnessSones(), 1));

    const float rmsDb = juce::Decibels::gainToDecibels(processor.getRMSLevel(), -100.0f);
    levelStat.setValue(rmsDb <= -99.9f ? juce::String("-inf") : juce::String(rmsDb, 1));

    std::array<float, ui::BarkSpectrumView::numBands> bands{};
    processor.getSpecificLoudness(bands);
    spectrum.update(bands);

    // The graph keeps 10 samples per second
    if (++timerTicks % 3 == 0)
        history.push({ score, brightness * 100.0f, harshness * 100.0f, dynamics * 100.0f, unpredictability * 100.0f },
                     processor.isCurrentlyLogging());

    updateRecordingControls();
}

void AudioPluginAudioProcessorEditor::updateRecordingControls()
{
    const bool logging = processor.isCurrentlyLogging();
    const int points = processor.getDataPointCount();

    recordButton.setToggleState(logging, juce::dontSendNotification);
    exportButton.setEnabled(!logging && points > 0);

    juce::String status;
    if (logging)
        status = "Recording  " + formatTime(processor.getRecordingTime()) + "   -   " + juce::String(points) + " points";
    else if (points > 0)
        status = "Ready to export  -  " + juce::String(points) + " points";
    else
        status = "Not recording";

    if (processor.getDroppedPointCount() > 0)
        status += "   -   " + juce::String(processor.getDroppedPointCount()) + " dropped";

    statusLabel.setText(status, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, logging ? ui::palette::textPrimary : ui::palette::textSecondary);
}

juce::String AudioPluginAudioProcessorEditor::formatTime(double seconds)
{
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int tenths = static_cast<int>((seconds - static_cast<int>(seconds)) * 10);

    return juce::String::formatted("%02d:%02d.%01d", mins, secs, tenths);
}
