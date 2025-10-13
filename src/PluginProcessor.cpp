#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    fft(fftOrder),
    window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    fftData.fill(0.0f);
    rmsHistory.fill(0.0f);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    fftPos = 0;
}

void AudioPluginAudioProcessor::releaseResources() {}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet()
        && !layouts.getMainInputChannelSet().isDisabled();
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Calculate RMS
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    rmsLevel.store(rms);

    // Store RMS in history
    rmsHistory[rmsHistoryPos] = rms;
    rmsHistoryPos = (rmsHistoryPos + 1) % rmsHistorySize;

    // Collect samples for FFT (using first channel)
    auto* channelData = buffer.getReadPointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        fftData[fftPos] = channelData[i];
        fftPos++;

        if (fftPos >= fftSize)
        {
            fftPos = 0;
            performFFTAnalysis();
        }
    }
}

void AudioPluginAudioProcessor::performFFTAnalysis()
{
    // Apply windowing
    window.multiplyWithWindowingTable(fftData.data(), fftSize);

    // Perform FFT
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // Calculate metrics
    calculateSpectralCentroid();
    calculateSpectralHarshness();
    calculateDynamicVariability();
    calculateTemporalUnpredictability();
    calculateAcousticActivationScore();
}

void AudioPluginAudioProcessor::calculateSpectralCentroid()
{
    float numerator = 0.0f;
    float denominator = 0.0f;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        float magnitude = fftData[i];
        float frequency = (i * currentSampleRate) / fftSize;

        numerator += magnitude * frequency;
        denominator += magnitude;
    }

    float centroid = denominator > 0.0f ? numerator / denominator : 0.0f;

    // Normalize to 0-1 range (assuming 0-8000 Hz is relevant range)
    spectralCentroid.store(juce::jlimit(0.0f, 1.0f, centroid / 8000.0f));
}

void AudioPluginAudioProcessor::calculateSpectralHarshness()
{
    // Harshness correlates with high-frequency energy (>2kHz) and roughness
    // Simple metric: ratio of high-freq energy to total energy

    float lowFreqEnergy = 0.0f;
    float highFreqEnergy = 0.0f;
    int crossoverBin = static_cast<int>((2000.0 * fftSize) / currentSampleRate);

    for (int i = 0; i < fftSize / 2; ++i)
    {
        float magnitude = fftData[i];
        if (i < crossoverBin)
            lowFreqEnergy += magnitude;
        else
            highFreqEnergy += magnitude;
    }

    float totalEnergy = lowFreqEnergy + highFreqEnergy;
    float harshness = totalEnergy > 0.0f ? highFreqEnergy / totalEnergy : 0.0f;

    spectralHarshness.store(juce::jlimit(0.0f, 1.0f, harshness * 2.0f)); // Scale up for visibility
}

void AudioPluginAudioProcessor::calculateDynamicVariability()
{
    // Calculate standard deviation of RMS history
    float mean = 0.0f;
    for (int i = 0; i < rmsHistorySize; ++i)
        mean += rmsHistory[i];
    mean /= rmsHistorySize;

    float variance = 0.0f;
    for (int i = 0; i < rmsHistorySize; ++i)
    {
        float diff = rmsHistory[i] - mean;
        variance += diff * diff;
    }
    variance /= rmsHistorySize;

    float stdDev = std::sqrt(variance);

    // Normalize (arbitrary scaling based on typical values)
    dynamicVariability.store(juce::jlimit(0.0f, 1.0f, stdDev * 20.0f));
}

void AudioPluginAudioProcessor::calculateTemporalUnpredictability()
{
    // Simple metric: how much consecutive RMS values differ
    float totalDiff = 0.0f;
    int count = 0;

    for (int i = 1; i < rmsHistorySize; ++i)
    {
        totalDiff += std::abs(rmsHistory[i] - rmsHistory[i - 1]);
        count++;
    }

    float avgDiff = count > 0 ? totalDiff / count : 0.0f;

    // Normalize
    temporalUnpredictability.store(juce::jlimit(0.0f, 1.0f, avgDiff * 50.0f));
}

void AudioPluginAudioProcessor::calculateAcousticActivationScore()
{
    // Composite score: lower values for stress-inducing features
    // Research-based weights (these are initial estimates - refine with your research!)

    float centroidScore = (1.0f - spectralCentroid.load()) * 100.0f; // Lower centroid = calmer
    float harshnessScore = (1.0f - spectralHarshness.load()) * 100.0f; // Lower harshness = better
    float dynamicScore = (1.0f - dynamicVariability.load()) * 100.0f; // Lower variability = calmer
    float unpredictScore = (1.0f - temporalUnpredictability.load()) * 100.0f; // More predictable = calmer

    // Weighted average (adjust weights based on your research)
    float score = (centroidScore * 0.25f +
        harshnessScore * 0.35f +
        dynamicScore * 0.20f +
        unpredictScore * 0.20f);

    acousticActivationScore.store(juce::jlimit(0.0f, 100.0f, score));
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save state if needed
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore state if needed
}


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}