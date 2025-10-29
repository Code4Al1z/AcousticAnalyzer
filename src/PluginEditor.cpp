#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(600, 520);

    // Start Recording Button
    startRecordingButton.setButtonText("Start Recording");
    startRecordingButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4CAF50));
    startRecordingButton.onClick = [this]()
        {
            processor.startLogging();
            startRecordingButton.setEnabled(false);
            stopRecordingButton.setEnabled(true);
            exportButton.setEnabled(false);
        };
    addAndMakeVisible(startRecordingButton);

    // Stop Recording Button
    stopRecordingButton.setButtonText("Stop Recording");
    stopRecordingButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffF44336));
    stopRecordingButton.onClick = [this]()
        {
            processor.stopLogging();
            startRecordingButton.setEnabled(true);
            stopRecordingButton.setEnabled(false);
            exportButton.setEnabled(true);
        };
    stopRecordingButton.setEnabled(false);
    addAndMakeVisible(stopRecordingButton);

    // Export Button
    exportButton.setButtonText("Export CSV");
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2196F3));
    exportButton.onClick = [this]()
        {
            processor.exportToCSV();
        };
    exportButton.setEnabled(false);
    addAndMakeVisible(exportButton);

    startTimerHz(30); // Update UI at 30 Hz
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(22.0f);
    g.drawText("Acoustic Environment Research Tool", 20, 15, getWidth() - 40, 25, juce::Justification::centred);

    // Version and beta label
    g.setFont(12.0f);
    g.setColour(juce::Colours::orange);
    g.drawText("BETA v0.1", 20, 40, getWidth() - 40, 15, juce::Justification::centred);

    // Recording status and time
    if (processor.isCurrentlyLogging())
    {
        g.setFont(14.0f);
        g.setColour(juce::Colour(0xffFF5252));
        juce::String statusText = "RECORDING - " + formatTime(processor.getRecordingTime());
        g.drawText(statusText, 20, 430, 200, 20, juce::Justification::left);

        // Data point counter
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        juce::String pointsText = "Data points: " + juce::String(processor.getDataPointCount());
        g.drawText(pointsText, 20, 450, 200, 20, juce::Justification::left);
    }
    else if (processor.getDataPointCount() > 0)
    {
        g.setFont(12.0f);
        g.setColour(juce::Colour(0xff4CAF50));
        juce::String statusText = "Ready to export (" + juce::String(processor.getDataPointCount()) + " points)";
        g.drawText(statusText, 20, 440, 200, 20, juce::Justification::left);
    }

    // Acoustic Activation Score (main display)
    float score = processor.getAcousticActivationScore();
    juce::Colour scoreColour = getScoreColour(score);

    g.setColour(scoreColour);
    g.setFont(48.0f);
    juce::String scoreText = juce::String(score, 1);
    g.drawText(scoreText, 20, 70, getWidth() - 40, 60, juce::Justification::centred);

    g.setFont(16.0f);
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Acoustic Activation Index (0-100)", 20, 130, getWidth() - 40, 20, juce::Justification::centred);

    // Interpretation text
    g.setFont(14.0f);
    g.setColour(scoreColour);
    g.drawText(getInterpretationText(score), 20, 150, getWidth() - 40, 20, juce::Justification::centred);

    // Individual metrics
    int yPos = 190;
    int barHeight = 30;
    int spacing = 50;

    drawMetricBar(g, "Spectral Brightness", processor.getSpectralCentroid(), yPos);
    yPos += spacing;

    drawMetricBar(g, "Spectral Harshness", processor.getSpectralHarshness(), yPos);
    yPos += spacing;

    drawMetricBar(g, "Dynamic Variability", processor.getDynamicVariability(), yPos);
    yPos += spacing;

    drawMetricBar(g, "Temporal Unpredictability", processor.getTemporalUnpredictability(), yPos);

    // Disclaimer at bottom
    g.setFont(10.0f);
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Research tool in development - Measures acoustic activation potential",
        20, getHeight() - 25, getWidth() - 40, 20, juce::Justification::centred);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // Position buttons at the bottom
    int buttonWidth = 180;
    int buttonHeight = 30;
    int buttonY = 430;
    int spacing = 10;

    int totalWidth = (buttonWidth * 3) + (spacing * 2);
    int startX = (getWidth() - totalWidth) / 2;

    startRecordingButton.setBounds(startX, buttonY, buttonWidth, buttonHeight);
    stopRecordingButton.setBounds(startX + buttonWidth + spacing, buttonY, buttonWidth, buttonHeight);
    exportButton.setBounds(startX + (buttonWidth + spacing) * 2, buttonY, buttonWidth, buttonHeight);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::timerCallback()
{
    repaint();
}

juce::Colour AudioPluginAudioProcessorEditor::getScoreColour(float score)
{
    if (score > 70.0f) return juce::Colour(0xff4CAF50); // Green
    if (score > 40.0f) return juce::Colour(0xffFFC107); // Amber
    return juce::Colour(0xffF44336); // Red
}

juce::String AudioPluginAudioProcessorEditor::getInterpretationText(float score)
{
    if (score > 70.0f)
        return "Low Activation - Calming";
    else if (score > 40.0f)
        return "Medium Activation - Neutral";
    else
        return "High Activation - Stimulating";
}

juce::String AudioPluginAudioProcessorEditor::formatTime(double seconds)
{
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 10);

    return juce::String::formatted("%02d:%02d.%01d", mins, secs, millis);
}

void AudioPluginAudioProcessorEditor::drawMetricBar(juce::Graphics& g, const juce::String& label, float value, int y)
{
    int barX = 190;
    int barWidth = getWidth() - barX - 50;
    int barHeight = 20;

    // Label
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(label, 20, y, 160, barHeight, juce::Justification::left);

    // Background bar
    g.setColour(juce::Colour(0xff333333));
    g.fillRect(barX, y, barWidth, barHeight);

    // Value bar
    juce::Colour barColour = juce::Colour(0xff2196F3);
    g.setColour(barColour);
    g.fillRect(barX, y, static_cast<int>(barWidth * value), barHeight);

    // Value text
    g.setColour(juce::Colours::white);
    g.setFont(12.0f);
    juce::String valueText = juce::String(value * 100.0f, 0) + "%";
    g.drawText(valueText, barX + barWidth + 10, y, 50, barHeight, juce::Justification::left);
}