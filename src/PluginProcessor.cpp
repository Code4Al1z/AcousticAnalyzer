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

    for (auto& band : sharedSpecificLoudness)
        band.store(0.0f);
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
    sharpnessSmoother.reset();
    roughnessSmoother.reset();
    presenceSmoother.reset();
    loudnessSmoother.reset();
    dynamicSmoother.reset();
    temporalSmoother.reset();

    // Bark bands: the first FFT bin of each band (DC is skipped)
    for (int b = 0; b <= numBarkBands; ++b)
        barkBinEdges[static_cast<size_t>(b)] = juce::jlimit(1, numBins,
            static_cast<int>(std::ceil(barkEdgesHz[static_cast<size_t>(b)] * fftSize / sampleRate)));

    // Threshold in quiet at each band centre (Terhardt), as intensity relative to 0 dB SPL
    for (int b = 0; b < numBarkBands; ++b)
    {
        const double fkHz = barkCentresHz[static_cast<size_t>(b)] / 1000.0;
        const double thresholdDb = 3.64 * std::pow(fkHz, -0.8)
                                 - 6.5 * std::exp(-0.6 * (fkHz - 3.3) * (fkHz - 3.3))
                                 + 1.0e-3 * std::pow(fkHz, 4.0);
        thresholdIntensity[static_cast<size_t>(b)] = static_cast<float>(std::pow(10.0, thresholdDb / 10.0));
    }

    bandSplDb.fill(-100.0f);
    specificLoudness.fill(0.0f);

    // Roughness filterbank: skip bands too close to Nyquist
    lastRoughnessBand = 0;
    while (lastRoughnessBand < numBarkBands
           && barkEdgesHz[static_cast<size_t>(lastRoughnessBand) + 1] <= 0.45 * sampleRate)
        ++lastRoughnessBand;

    roughnessMeanCoeff = 1.0 - std::exp(-1.0 / (sampleRate * roughnessMeanSeconds));
    roughnessPowerCoeff = 1.0 - std::exp(-1.0 / (sampleRate * roughnessPowerSeconds));

    for (auto& channelBands : roughnessBands)
    {
        for (int b = firstRoughnessBand; b < lastRoughnessBand; ++b)
        {
            const double low = barkEdgesHz[static_cast<size_t>(b)];
            const double high = barkEdgesHz[static_cast<size_t>(b) + 1];
            const double centre = std::sqrt(low * high);

            auto& band = channelBands[static_cast<size_t>(b)];
            band = RoughnessBand{};
            band.bandpass.setBandpass(sampleRate, centre, centre / (high - low));
            band.envLowpass.setLowpass(sampleRate, roughnessEnvelopeCutoffHz, 0.7071);
            band.modBandpass.setBandpass(sampleRate, roughnessModulationHz, roughnessModulationQ);
        }
    }

    for (auto& cross : modulationCross)
        cross.fill(0.0);
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
            processRoughnessSample(ch, sample);
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
        for (auto& band : sharedSpecificLoudness) // The band display should show the silence
            band.store(0.0f, std::memory_order_relaxed);

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

    // Psychoacoustic analysis, then smoothed so the meters are stable
    float loudness = 0.0f, sharpness = 0.0f, presenceLoudness = 0.0f;
    computeBarkAnalysis(loudness, sharpness, presenceLoudness);
    const float roughness = computeRoughness();

    for (size_t b = 0; b < sharedSpecificLoudness.size(); ++b)
        sharedSpecificLoudness[b].store(specificLoudness[b], std::memory_order_relaxed);

    const float smoothedLoudness = loudnessSmoother.process(loudness,
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float smoothedSharpness = sharpnessSmoother.process(sharpness,
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float smoothedRoughness = roughnessSmoother.process(roughness,
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float smoothedPresence = presenceSmoother.process(presenceLoudness,
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float dynamics = dynamicSmoother.process(computeDynamicVariability(),
        smoothingAttackCoeff, smoothingReleaseCoeff);
    const float unpredictability = temporalSmoother.process(computeTemporalUnpredictability(),
        smoothingAttackCoeff, smoothingReleaseCoeff);

    // Normalise to 0-1: brightness from sharpness, harshness from roughness + presence
    const float brightness = juce::jlimit(0.0f, 1.0f, smoothedSharpness / sharpnessFullScaleAcum);
    const float roughnessNorm = juce::jlimit(0.0f, 1.0f, smoothedRoughness / roughnessFullScaleAsper);
    const float presenceNorm = juce::jlimit(0.0f, 1.0f, smoothedPresence / presenceFullScaleSone);
    const float harshness = harshnessRoughnessWeight * roughnessNorm
                          + (1.0f - harshnessRoughnessWeight) * presenceNorm;

    loudnessSones.store(smoothedLoudness);
    sharpnessAcum.store(smoothedSharpness);
    roughnessAsper.store(smoothedRoughness);
    spectralCentroid.store(brightness);
    spectralHarshness.store(harshness);
    dynamicVariability.store(dynamics);
    temporalUnpredictability.store(unpredictability);
    acousticActivationScore.store(
        computeAcousticActivationScore(brightness, harshness, dynamics, unpredictability));

    // Log data if recording
    if (isLogging.load())
        logDataPoint();
}

void AudioPluginAudioProcessor::computeBarkAnalysis(float& loudness, float& sharpness, float& presenceLoudness)
{
    // Power in the FFT bins -> mean square of the signal. For a Hann window this
    // is 16 / (3 N^2) times the one-sided sum of bin powers (same for tones and noise).
    constexpr double powerToMeanSquare = 16.0 / (3.0 * static_cast<double>(fftSize) * fftSize);

    std::array<double, numBarkBands> intensity{};

    for (int b = 0; b < numBarkBands; ++b)
    {
        double power = 0.0;
        for (int bin = barkBinEdges[static_cast<size_t>(b)]; bin < barkBinEdges[static_cast<size_t>(b) + 1]; ++bin)
            power += powerSpectrum[static_cast<size_t>(bin)];

        const double meanSquare = juce::jmax(power * powerToMeanSquare, 1.0e-20);
        const double splDb = referenceSplAtFullScaleRms + 10.0 * std::log10(meanSquare);

        bandSplDb[static_cast<size_t>(b)] = static_cast<float>(splDb);
        intensity[static_cast<size_t>(b)] = std::pow(10.0, splDb / 10.0);
    }

    // Excitation: each band's energy spreads to its neighbours with Zwicker's
    // slopes. Below the band it falls 27 dB per Bark; above it the slope gets
    // shallower with level (24 + 230/f - 0.2 L dB per Bark), which is why loud
    // sounds mask upwards.
    std::array<double, numBarkBands> excitation{};
    const double lowerFactor = std::pow(10.0, -27.0 / 10.0);

    for (int source = 0; source < numBarkBands; ++source)
    {
        const double sourceIntensity = intensity[static_cast<size_t>(source)];
        const double slopeDbPerBark = juce::jmax(3.0,
            24.0 + 230.0 / barkCentresHz[static_cast<size_t>(source)] - 0.2 * bandSplDb[static_cast<size_t>(source)]);
        const double upperFactor = std::pow(10.0, -slopeDbPerBark / 10.0);

        excitation[static_cast<size_t>(source)] += sourceIntensity;

        double spread = sourceIntensity;
        for (int i = source + 1; i < numBarkBands; ++i)
            excitation[static_cast<size_t>(i)] += (spread *= upperFactor);

        spread = sourceIntensity;
        for (int i = source - 1; i >= 0; --i)
            excitation[static_cast<size_t>(i)] += (spread *= lowerFactor);
    }

    // Specific loudness (Zwicker), sone per Bark; each band is about 1 Bark wide.
    // The prefactor uses the threshold in quiet at 1 kHz (about 3.4 dB SPL) for every
    // band; only the excitation-to-threshold ratio varies with frequency.
    static const double referenceThresholdTerm = std::pow(std::pow(10.0, 0.3369), 0.23);

    double totalLoudness = 0.0;
    double sharpnessSum = 0.0;
    double presenceSum = 0.0;

    for (int i = 0; i < numBarkBands; ++i)
    {
        const double threshold = thresholdIntensity[static_cast<size_t>(i)];
        const double specific = loudnessScale * juce::jmax(0.0,
            0.08 * referenceThresholdTerm
                 * (std::pow(0.5 + 0.5 * excitation[static_cast<size_t>(i)] / threshold, 0.23) - 1.0));

        specificLoudness[static_cast<size_t>(i)] = static_cast<float>(specific);
        totalLoudness += specific;

        // Sharpness weighting grows above 15.8 Bark (von Bismarck / Zwicker)
        const double z = i + 0.5;
        const double weight = z <= 15.8 ? 1.0 : 0.15 * std::exp(0.42 * (z - 15.8)) + 0.85;
        sharpnessSum += specific * weight * z;

        if (i >= presenceFirstBand && i <= presenceLastBand)
            presenceSum += specific;
    }

    loudness = static_cast<float>(totalLoudness);
    sharpness = totalLoudness > 1.0e-6 ? static_cast<float>(0.11 * sharpnessSum / totalLoudness) : 0.0f;
    presenceLoudness = static_cast<float>(presenceSum);
}

void AudioPluginAudioProcessor::processRoughnessSample(int channel, float sample)
{
    // Audio thread, per sample: split into Bark bands, extract each band's
    // envelope, and track the envelope's modulation around 70 Hz
    auto& bands = roughnessBands[static_cast<size_t>(channel)];

    for (int b = firstRoughnessBand; b < lastRoughnessBand; ++b)
    {
        auto& band = bands[static_cast<size_t>(b)];

        const double filtered = band.bandpass.process(sample);
        const double envelope = band.envLowpass.process(std::abs(filtered));

        band.envMean += roughnessMeanCoeff * (envelope - band.envMean);
        band.lastModulation = band.modBandpass.process(envelope);
        band.modPower += roughnessPowerCoeff * (band.lastModulation * band.lastModulation - band.modPower);
    }

    // How in step neighbouring bands' modulations are (noise: not at all,
    // an amplitude-modulated tone: fully)
    auto& cross = modulationCross[static_cast<size_t>(channel)];

    for (int b = firstRoughnessBand; b + 1 < lastRoughnessBand; ++b)
        cross[static_cast<size_t>(b)] += roughnessPowerCoeff
            * (bands[static_cast<size_t>(b)].lastModulation * bands[static_cast<size_t>(b) + 1].lastModulation
               - cross[static_cast<size_t>(b)]);
}

float AudioPluginAudioProcessor::computeRoughness() const
{
    // Per band: modulation depth x neighbour correlation x audibility, summed.
    // Simplified from Daniel & Weber; calibrated by roughnessScale so that a
    // 1 kHz tone, 70 Hz AM at 100%, 60 dB SPL reads 1 asper.
    double total = 0.0;

    for (int ch = 0; ch < analysisChannels; ++ch)
    {
        const auto& bands = roughnessBands[static_cast<size_t>(ch)];
        const auto& cross = modulationCross[static_cast<size_t>(ch)];

        for (int b = firstRoughnessBand; b < lastRoughnessBand; ++b)
        {
            const auto& band = bands[static_cast<size_t>(b)];
            if (band.envMean < 1.0e-9)
                continue;

            // Modulation depth (AC rms x sqrt 2 / DC, so 100% AM reads 1)
            const double depth = juce::jmin(1.0, std::sqrt(2.0 * band.modPower) / band.envMean);

            double correlationSum = 0.0;
            int neighbours = 0;

            if (b > firstRoughnessBand)
            {
                const double denominator = std::sqrt(bands[static_cast<size_t>(b) - 1].modPower * band.modPower) + 1.0e-18;
                correlationSum += cross[static_cast<size_t>(b) - 1] / denominator;
                ++neighbours;
            }

            if (b + 1 < lastRoughnessBand)
            {
                const double denominator = std::sqrt(band.modPower * bands[static_cast<size_t>(b) + 1].modPower) + 1.0e-18;
                correlationSum += cross[static_cast<size_t>(b)] / denominator;
                ++neighbours;
            }

            const double correlation = neighbours > 0 ? juce::jlimit(0.0, 1.0, correlationSum / neighbours) : 0.0;

            // Bands near or below the threshold in quiet can't contribute
            const double thresholdDb = 10.0 * std::log10(static_cast<double>(thresholdIntensity[static_cast<size_t>(b)]));
            const double audibility = juce::jlimit(0.0, 1.0, (bandSplDb[static_cast<size_t>(b)] - thresholdDb) / 40.0);

            total += audibility
                   * std::pow(depth, static_cast<double>(roughnessDepthExponent))
                   * std::pow(correlation, static_cast<double>(roughnessCorrelationExponent));
        }
    }

    return static_cast<float>(roughnessScale * total / analysisChannels);
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
    point.loudnessSones = loudnessSones.load();
    point.sharpnessAcum = sharpnessAcum.load();
    point.roughnessAsper = roughnessAsper.load();

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
    juce::String csvContent = "Timestamp_Seconds,Activation_Score,Brightness,Harshness,Dynamic_Variability,Temporal_Unpredictability,RMS_Level,Loudness_Sone,Sharpness_Acum,Roughness_Asper\n";

    for (const auto& point : dataLog)
    {
        csvContent += juce::String(point.timestamp, 3) + ",";
        csvContent += juce::String(point.activationScore, 2) + ",";
        csvContent += juce::String(point.spectralCentroid, 4) + ",";
        csvContent += juce::String(point.spectralHarshness, 4) + ",";
        csvContent += juce::String(point.dynamicVariability, 4) + ",";
        csvContent += juce::String(point.temporalUnpredictability, 4) + ",";
        csvContent += juce::String(point.rmsLevel, 6) + ",";
        csvContent += juce::String(point.loudnessSones, 3) + ",";
        csvContent += juce::String(point.sharpnessAcum, 3) + ",";
        csvContent += juce::String(point.roughnessAsper, 3) + "\n";
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