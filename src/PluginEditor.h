#pragma once

#include "PluginProcessor.h"

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
    juce::Colour getScoreColour(float score);
    void drawMetricBar(juce::Graphics& g, const juce::String& label, float value, int y);
    juce::String getInterpretationText(float score);
    juce::String formatTime(double seconds);

    AudioPluginAudioProcessor& processor;

    // UI Components
    juce::TextButton startRecordingButton;
    juce::TextButton stopRecordingButton;
    juce::TextButton exportButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};