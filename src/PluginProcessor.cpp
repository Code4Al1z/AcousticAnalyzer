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
    for (auto& channel : fftInput)
        channel.fill(0.0f);
    fftScratch.fill(0.0f);
    powerSpectrum.fill(0.0f);
    rmsHistoryDb.fill(rmsFloorDb);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    // Level history
    rmsWindowSamples = juce::jmax(1, juce::roundToInt(sampleRate * rmsWindowSeconds));
    rmsSampleCount = 0;
    rmsSumOfSquares = 0.0;
    rmsHistoryDb.fill(rmsFloorDb);
    rmsHistoryPos = 0;
    rmsHistoryCount = 0;

    // FFT
    analysisChannels = juce::jlimit(1, maxAnalysisChannels, getTotalNumInputChannels());
    fftPos = 0;
    fftSamplesCollected = 0;
    samplesSinceFrame = 0;
    for (auto& channel : fftInput)
        channel.fill(0.0f);
    fftScratch.fill(0.0f);
    powerSpectrum.fill(0.0f);

    // Smoothing: convert the time constants to per-frame coefficients
    const double hopSeconds = hopSize / sampleRate;
    smoothingAttackCoeff = static_cast<float>(1.0 - std::exp(-hopSeconds / smoothingAttackSeconds));
    smoothingReleaseCoeff = static_cast<float>(1.0 - std::exp(-hopSeconds / smoothingReleaseSeconds));
    centroidSmoother.reset();
    harshnessSmoother.reset();
    dynamicSmoother.reset();
    temporalSmoother.reset();
}

void AudioPluginAudioProcessor::releaseResources() {}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();

    return layouts.getMainOutputChannelSet() == input
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Flush denormals to zero so quiet tails can't cause CPU spikes
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // The plugin only measures: audio passes through untouched
    if (buffer.getNumChannels() < analysisChannels)
        return;

    // Samples feed two things:
    //  - the level history, one entry per fixed time window (independent of block size);
    //    the level is the RMS across all analysed channels
    //  - the FFT input buffers, one spectrum per hopSize samples
    const float channelGain = 1.0f / static_cast<float>(analysisChannels);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sumOfSquares = 0.0f;

        for (int ch = 0; ch < analysisChannels; ++ch)
        {
            const float sample = buffer.getReadPointer(ch)[i];
            fftInput[static_cast<size_t>(ch)][static_cast<size_t>(fftPos)] = sample;
            sumOfSquares += sample * sample;
        }

        ++samplesProcessed;

        // Fixed-window RMS
        rmsSumOfSquares += static_cast<double>(sumOfSquares) * channelGain;
        if (++rmsSampleCount >= rmsWindowSamples)
        {
            const float rms = static_cast<float>(std::sqrt(rmsSumOfSquares / rmsSampleCount));
            rmsLevel.store(rms);

            rmsHistoryDb[static_cast<size_t>(rmsHistoryPos)] =
                juce::jmax(rmsFloorDb, juce::Decibels::gainToDecibels(rms, rmsFloorDb));
            rmsHistoryPos = (rmsHistoryPos + 1) % rmsHistorySize;
            rmsHistoryCount = juce::jmin(rmsHistoryCount + 1, rmsHistorySize);

            rmsSumOfSquares = 0.0;
            rmsSampleCount = 0;
        }

        // FFT frame collection (circular buffers, overlapping frames)
        fftPos = (fftPos + 1) & (fftSize - 1);
        fftSamplesCollected = juce::jmin(fftSamplesCollected + 1, fftSize);

        if (++samplesSinceFrame >= hopSize)
        {
            samplesSinceFrame = 0;

            if (fftSamplesCollected >= fftSize)
                performFFTAnalysis();
        }
    }

    samplesProcessedPublished.store(samplesProcessed);
}

void AudioPluginAudioProcessor::performFFTAnalysis()
{
    // Silence check on the newest fftSize samples of all channels
    double sumOfSquares = 0.0;
    for (int ch = 0; ch < analysisChannels; ++ch)
        for (float x : fftInput[static_cast<size_t>(ch)])
            sumOfSquares += static_cast<double>(x) * x;

    // Digital silence: hold the last values rather than reporting "calm"
    if (sumOfSquares / (static_cast<double>(fftSize) * analysisChannels) < silenceMeanSquare)
    {
        if (isLogging.load())
            logDataPoint();
        return;
    }

    // One FFT per channel; the power spectra are averaged
    powerSpectrum.fill(0.0f);
    const float channelGain = 1.0f / static_cast<float>(analysisChannels);

    for (int ch = 0; ch < analysisChannels; ++ch)
    {
        // Copy the latest fftSize samples, oldest first, into the scratch buffer and
        // clear the upper half, which JUCE uses as working space for the transform
        const auto& input = fftInput[static_cast<size_t>(ch)];
        for (int i = 0; i < fftSize; ++i)
            fftScratch[static_cast<size_t>(i)] = input[static_cast<size_t>((fftPos + i) & (fftSize - 1))];
        std::fill(fftScratch.begin() + fftSize, fftScratch.end(), 0.0f);

        // Apply windowing
        window.multiplyWithWindowingTable(fftScratch.data(), fftSize);

        // Perform FFT (magnitudes end up in the first half of the scratch buffer)
        fft.performFrequencyOnlyForwardTransform(fftScratch.data());

        for (int bin = 0; bin < numBins; ++bin)
        {
            const float magnitude = fftScratch[static_cast<size_t>(bin)];
            powerSpectrum[static_cast<size_t>(bin)] += magnitude * magnitude * channelGain;
        }
    }

    // Raw metrics, then smoothed so the meters are stable
    const float centroid = centroidSmoother.process(computeSpectralCentroid(),
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float harshness = harshnessSmoother.process(computeSpectralHarshness(),
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float dynamics = dynamicSmoother.process(computeDynamicVariability(),
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float unpredictability = temporalSmoother.process(computeTemporalUnpredictability(),
        smoothingAttackCoeff, smoothingReleaseCoeff);

    spectralCentroid.store(centroid);
    spectralHarshness.store(harshness);
    dynamicVariability.store(dynamics);
    temporalUnpredictability.store(unpredictability);
    acousticActivationScore.store(
        computeAcousticActivationScore(centroid, harshness, dynamics, unpredictability));

    // Log data if recording
    if (isLogging.load())
        logDataPoint();
}

float AudioPluginAudioProcessor::computeSpectralCentroid() const
{
    // Power-weighted centroid (skipping the DC bin), mapped to 0-1 on a log
    // frequency axis because pitch and brightness are perceived logarithmically
    double weightedSum = 0.0;
    double totalPower = 0.0;

    for (int i = 1; i < numBins; ++i)
    {
        const double power = powerSpectrum[static_cast<size_t>(i)];
        weightedSum += power * (i * currentSampleRate / fftSize);
        totalPower += power;
    }

    if (totalPower <= 0.0)
        return 0.0f;

    const float centroidHz = juce::jlimit(centroidMinHz, centroidMaxHz,
                                          static_cast<float>(weightedSum / totalPower));

    return std::log2(centroidHz / centroidMinHz) / std::log2(centroidMaxHz / centroidMinHz);
}

float AudioPluginAudioProcessor::computeSpectralHarshness() const
{
    // Fraction of spectral power above the crossover (0 = all low, 1 = all high)
    const int crossoverBin = juce::jlimit(1, numBins,
        juce::roundToInt(harshnessCrossoverHz * fftSize / currentSampleRate));

    double lowPower = 0.0;
    double highPower = 0.0;

    for (int i = 1; i < numBins; ++i)
    {
        const double power = powerSpectrum[static_cast<size_t>(i)];
        (i < crossoverBin ? lowPower : highPower) += power;
    }

    const double totalPower = lowPower + highPower;

    return totalPower > 0.0 ? juce::jlimit(0.0f, 1.0f, static_cast<float>(highPower / totalPower)) : 0.0f;
}

float AudioPluginAudioProcessor::computeDynamicVariability() const
{
    // Standard deviation of the level (in dB) over the history, oldest to newest
    if (rmsHistoryCount < 2)
        return 0.0f;

    const int oldest = rmsHistoryPos - rmsHistoryCount + rmsHistorySize;

    double mean = 0.0;
    for (int k = 0; k < rmsHistoryCount; ++k)
        mean += rmsHistoryDb[static_cast<size_t>((oldest + k) % rmsHistorySize)];
    mean /= rmsHistoryCount;

    double variance = 0.0;
    for (int k = 0; k < rmsHistoryCount; ++k)
    {
        const double diff = rmsHistoryDb[static_cast<size_t>((oldest + k) % rmsHistorySize)] - mean;
        variance += diff * diff;
    }
    variance /= rmsHistoryCount;

    return juce::jlimit(0.0f, 1.0f, static_cast<float>(std::sqrt(variance)) / dynamicFullScaleDb);
}

float AudioPluginAudioProcessor::computeTemporalUnpredictability() const
{
    // Mean absolute level change (in dB) between consecutive windows, oldest to
    // newest, so the jump from the newest entry back to the oldest isn't counted
    if (rmsHistoryCount < 2)
        return 0.0f;

    const int oldest = rmsHistoryPos - rmsHistoryCount + rmsHistorySize;

    double totalDiff = 0.0;
    float previous = rmsHistoryDb[static_cast<size_t>(oldest % rmsHistorySize)];
    for (int k = 1; k < rmsHistoryCount; ++k)
    {
        const float current = rmsHistoryDb[static_cast<size_t>((oldest + k) % rmsHistorySize)];
        totalDiff += std::abs(current - previous);
        previous = current;
    }

    const float meanDiff = static_cast<float>(totalDiff / (rmsHistoryCount - 1));

    return juce::jlimit(0.0f, 1.0f, meanDiff / temporalFullScaleDb);
}

float AudioPluginAudioProcessor::computeAcousticActivationScore(float centroid, float harshness,
                                                                float dynamics, float unpredictability)
{
    // Composite score: lower values for stress-inducing features
    // Research-based weights (these are initial estimates - refine with your research!)

    float centroidScore = (1.0f - centroid) * 100.0f; // Lower centroid = calmer
    float harshnessScore = (1.0f - harshness) * 100.0f; // Lower harshness = better
    float dynamicScore = (1.0f - dynamics) * 100.0f; // Lower variability = calmer
    float unpredictScore = (1.0f - unpredictability) * 100.0f; // More predictable = calmer

    // Weighted average (adjust weights based on your research)
    float score = (centroidScore * 0.25f +
        harshnessScore * 0.35f +
        dynamicScore * 0.20f +
        unpredictScore * 0.20f);

    return juce::jlimit(0.0f, 100.0f, score);
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