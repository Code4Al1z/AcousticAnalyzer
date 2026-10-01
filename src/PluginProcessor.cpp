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
    fftInput.fill(0.0f);
    fftScratch.fill(0.0f);
    magnitudes.fill(0.0f);
    rmsHistory.fill(0.0f);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    rmsWindowSamples = juce::jmax(1, juce::roundToInt(sampleRate * rmsWindowSeconds));
    rmsSampleCount = 0;
    rmsSumOfSquares = 0.0;
    rmsHistory.fill(0.0f);
    rmsHistoryPos = 0;
    fftPos = 0;
    fftInput.fill(0.0f);
    fftScratch.fill(0.0f);
    magnitudes.fill(0.0f);
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

    // Analysis uses the first channel. Samples feed two things:
    //  - the RMS history, one entry per fixed time window (independent of block size)
    //  - the FFT input buffer, one spectrum per fftSize samples
    auto* channelData = buffer.getReadPointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float sample = channelData[i];
        ++samplesProcessed;

        // Fixed-window RMS
        rmsSumOfSquares += static_cast<double>(sample) * sample;
        if (++rmsSampleCount >= rmsWindowSamples)
        {
            const float rms = static_cast<float>(std::sqrt(rmsSumOfSquares / rmsSampleCount));
            rmsLevel.store(rms);
            rmsHistory[rmsHistoryPos] = rms;
            rmsHistoryPos = (rmsHistoryPos + 1) % rmsHistorySize;
            rmsSumOfSquares = 0.0;
            rmsSampleCount = 0;
        }

        // FFT frame collection
        fftInput[fftPos] = sample;
        if (++fftPos >= fftSize)
        {
            fftPos = 0;
            performFFTAnalysis();
        }
    }

    samplesProcessedPublished.store(samplesProcessed);
}

void AudioPluginAudioProcessor::performFFTAnalysis()
{
    // Copy the collected frame into the scratch buffer and clear the upper
    // half, which JUCE uses as working space for the transform
    std::copy(fftInput.begin(), fftInput.end(), fftScratch.begin());
    std::fill(fftScratch.begin() + fftSize, fftScratch.end(), 0.0f);

    // Apply windowing
    window.multiplyWithWindowingTable(fftScratch.data(), fftSize);

    // Perform FFT
    fft.performFrequencyOnlyForwardTransform(fftScratch.data());

    // Keep only the magnitude bins the analysis functions use
    std::copy(fftScratch.begin(), fftScratch.begin() + numBins, magnitudes.begin());

    // Calculate metrics
    calculateSpectralCentroid();
    calculateSpectralHarshness();
    calculateDynamicVariability();
    calculateTemporalUnpredictability();
    calculateAcousticActivationScore();

    // Log data if recording
    if (isLogging.load())
    {
        logDataPoint();
    }
}

void AudioPluginAudioProcessor::calculateSpectralCentroid()
{
    float numerator = 0.0f;
    float denominator = 0.0f;

    for (int i = 0; i < numBins; ++i)
    {
        float magnitude = magnitudes[i];
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
    int crossoverBin = juce::jlimit(0, numBins, static_cast<int>((2000.0 * fftSize) / currentSampleRate));

    for (int i = 0; i < numBins; ++i)
    {
        float magnitude = magnitudes[i];
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

    // Walk the ring buffer oldest -> newest so the jump from the newest entry
    // back to the oldest one is not counted as a change
    float previous = rmsHistory[rmsHistoryPos];
    for (int i = 1; i < rmsHistorySize; ++i)
    {
        const float current = rmsHistory[(rmsHistoryPos + i) % rmsHistorySize];
        totalDiff += std::abs(current - previous);
        previous = current;
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

void AudioPluginAudioProcessor::startLogging()
{
    // Message thread. Throw away anything left over from a previous recording
    isLogging.store(false);
    drainLogFifo();
    dataLog.clear();
    droppedLogPoints.store(0);

    loggingStartSample.store(samplesProcessedPublished.load());
    isLogging.store(true);
    startTimerHz(10);
}

void AudioPluginAudioProcessor::stopLogging()
{
    isLogging.store(false);
    stopTimer();
    drainLogFifo();
}

void AudioPluginAudioProcessor::logDataPoint()
{
    // Audio thread: no locks, no allocation
    int start1, size1, start2, size2;
    logFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 == 0)
    {
        droppedLogPoints.fetch_add(1);
        return;
    }

    DataPoint& point = logFifoStorage[static_cast<size_t>(start1)];
    point.timestamp = static_cast<double>(samplesProcessed - loggingStartSample.load()) / currentSampleRate;
    point.activationScore = acousticActivationScore.load();
    point.spectralCentroid = spectralCentroid.load();
    point.spectralHarshness = spectralHarshness.load();
    point.dynamicVariability = dynamicVariability.load();
    point.temporalUnpredictability = temporalUnpredictability.load();
    point.rmsLevel = rmsLevel.load();

    logFifo.finishedWrite(1);
}

void AudioPluginAudioProcessor::drainLogFifo()
{
    // Message thread only
    int start1, size1, start2, size2;
    logFifo.prepareToRead(logFifo.getNumReady(), start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        dataLog.push_back(logFifoStorage[static_cast<size_t>(start1 + i)]);
    for (int i = 0; i < size2; ++i)
        dataLog.push_back(logFifoStorage[static_cast<size_t>(start2 + i)]);

    logFifo.finishedRead(size1 + size2);
}

double AudioPluginAudioProcessor::getRecordingTime() const
{
    if (!isLogging.load())
        return 0.0;

    return static_cast<double>(samplesProcessedPublished.load() - loggingStartSample.load()) / currentSampleRate;
}

void AudioPluginAudioProcessor::exportToCSV()
{
    drainLogFifo(); // Pick up anything the audio thread logged since the last drain

    if (dataLog.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "No Data",
            "No data to export. Please record data first.",
            "OK");
        return;
    }

    // Create CSV content first (before the async callback)
    juce::String csvContent = "Timestamp_Seconds,Activation_Score,Spectral_Centroid,Spectral_Harshness,Dynamic_Variability,Temporal_Unpredictability,RMS_Level\n";

    for (const auto& point : dataLog)
    {
        csvContent += juce::String(point.timestamp, 3) + ",";
        csvContent += juce::String(point.activationScore, 2) + ",";
        csvContent += juce::String(point.spectralCentroid, 4) + ",";
        csvContent += juce::String(point.spectralHarshness, 4) + ",";
        csvContent += juce::String(point.dynamicVariability, 4) + ",";
        csvContent += juce::String(point.temporalUnpredictability, 4) + ",";
        csvContent += juce::String(point.rmsLevel, 6) + "\n";
    }

    int totalPoints = static_cast<int>(dataLog.size());
    int droppedPoints = droppedLogPoints.load();

    // Create file chooser on the heap (it will manage its own lifetime)
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save CSV File",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("acoustic_data.csv"),
        "*.csv");

    auto flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(flags, [csvContent, totalPoints, droppedPoints, chooser](const juce::FileChooser& fc)
        {
            auto result = fc.getURLResult();
            auto outputFile = result.getLocalFile();

            if (outputFile != juce::File{})
            {
                // Write to file
                if (outputFile.replaceWithText(csvContent))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                        "Export Successful",
                        "Data exported to:\n" + outputFile.getFullPathName() +
                        "\n\nTotal data points: " + juce::String(totalPoints) +
                        (droppedPoints > 0 ? "\nDropped (buffer overflow): " + juce::String(droppedPoints) : juce::String()),
                        "OK");
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Export Failed",
                        "Failed to write file. Check permissions.",
                        "OK");
                }
            }
        });
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