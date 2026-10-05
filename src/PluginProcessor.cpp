#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "AcousticAnalyzerState", createParameterLayout()),
    fft(fftOrder),
    // normalise = false: the level calculations assume a plain Hann window (coherent gain 0.5).
    // JUCE's default normalisation doubles the amplitude, which made every SPL-based
    // reading 6 dB too high.
    window(fftSize, juce::dsp::WindowingFunction<float>::hann, false)
{
    for (auto& channel : fftInput)
        channel.fill(0.0f);
    fftScratch.fill(0.0f);
    powerSpectrum.fill(0.0f);
    rmsHistoryDb.fill(rmsFloorDb);

    for (auto& band : sharedSpecificLoudness)
        band.store(0.0f);

    weightBrightnessValue = apvts.getRawParameterValue(params::weightBrightness);
    weightHarshnessValue = apvts.getRawParameterValue(params::weightHarshness);
    weightDynamicsValue = apvts.getRawParameterValue(params::weightDynamics);
    weightUnpredictabilityValue = apvts.getRawParameterValue(params::weightUnpredictability);
    calibrationSplValue = apvts.getRawParameterValue(params::calibrationSpl);
    smoothingAttackValue = apvts.getRawParameterValue(params::smoothingAttack);
    smoothingReleaseValue = apvts.getRawParameterValue(params::smoothingRelease);
    autoRecordValue = apvts.getRawParameterValue(params::autoRecord);

    startTimerHz(10);
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    // These are settings, not performance controls, so they are not offered to
    // host automation. They are still saved with the project.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout](const char* id, const juce::String& name, juce::NormalisableRange<float> range,
                         float defaultValue, const juce::String& unit)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withLabel(unit).withAutomatable(false)));
    };

    // Relative weights of the four metrics in the activation index (normalised when used)
    add(params::weightBrightness,       "Weight: Brightness",       { 0.0f, 100.0f, 1.0f }, 25.0f, "");
    add(params::weightHarshness,        "Weight: Harshness",        { 0.0f, 100.0f, 1.0f }, 35.0f, "");
    add(params::weightDynamics,         "Weight: Dynamics",         { 0.0f, 100.0f, 1.0f }, 20.0f, "");
    add(params::weightUnpredictability, "Weight: Unpredictability", { 0.0f, 100.0f, 1.0f }, 20.0f, "");

    // Gauge zones
    add(params::calmAbove,    "Calming above",  { 0.0f, 100.0f, 1.0f }, 70.0f, "");
    add(params::neutralAbove, "Neutral above",  { 0.0f, 100.0f, 1.0f }, 40.0f, "");

    // Analysis
    add(params::calibrationSpl,   "Calibration (dB SPL at 0 dBFS RMS)", { 60.0f, 130.0f, 0.5f }, 100.0f, "dB");
    add(params::smoothingAttack,  "Smoothing attack",  { 10.0f, 1000.0f, 1.0f, 0.4f }, 100.0f, "ms");
    add(params::smoothingRelease, "Smoothing release", { 50.0f, 3000.0f, 1.0f, 0.4f }, 400.0f, "ms");

    // Workflow: start recording by itself when audio first arrives
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::autoRecord, 1 }, "Auto-record", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false)));

    return layout;
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    stopTimer();
}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    signalSeen.store(false);
    autoRecordFired.store(false); // Playback restarted: auto-record may start a new recording

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

    // Smoothing coefficients follow the parameters and are updated every frame
    widthSmoother.reset();
    sumMid = sumSide = 0.0;
    latestWidth = 0.0f;
    silentNow.store(true);
    stereoWidth.store(0.0f);

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
        float channelSamples[maxAnalysisChannels] = {};

        for (int ch = 0; ch < analysisChannels; ++ch)
        {
            const float sample = buffer.getReadPointer(ch)[i];
            channelSamples[ch] = sample;
            fftInput[static_cast<size_t>(ch)][static_cast<size_t>(fftPos)] = sample;
            sumOfSquares += sample * sample;
            processRoughnessSample(ch, sample);
        }

        if (analysisChannels == 2) // Mid and side energy, for the stereo width
        {
            const double mid = 0.5 * (static_cast<double>(channelSamples[0]) + channelSamples[1]);
            const double side = 0.5 * (static_cast<double>(channelSamples[0]) - channelSamples[1]);
            sumMid += mid * mid;
            sumSide += side * side;
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

            // Stereo width for this window; held through silence
            const double energy = sumMid + sumSide;
            if (analysisChannels == 2 && energy > 1.0e-12 * rmsWindowSamples)
                latestWidth = static_cast<float>(sumSide / energy);
            sumMid = sumSide = 0.0;
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
        silentNow.store(true, std::memory_order_relaxed);

        for (auto& band : sharedSpecificLoudness) // The band display should show the silence
            band.store(0.0f, std::memory_order_relaxed);

        if (isLogging.load())
            logDataPoint();
        return;
    }

    signalSeen.store(true, std::memory_order_relaxed);
    silentNow.store(false, std::memory_order_relaxed);

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

    // Smoothing: convert the attack and release times (ms) to per-frame coefficients
    const double hopSeconds = hopSize / currentSampleRate;
    smoothingAttackCoeff = static_cast<float>(1.0 - std::exp(-hopSeconds / (0.001 * smoothingAttackValue->load())));
    smoothingReleaseCoeff = static_cast<float>(1.0 - std::exp(-hopSeconds / (0.001 * smoothingReleaseValue->load())));

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
    stereoWidth.store(widthSmoother.process(latestWidth, smoothingAttackCoeff, smoothingReleaseCoeff));
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
        const double splDb = calibrationSplValue->load() + 10.0 * std::log10(meanSquare);

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
                                                                float dynamics, float unpredictability) const
{
    // Composite score: lower values for stress-inducing features. The weights are
    // parameters (refine them with your research) and are normalised here, so
    // only their ratios matter.
    float weightB = weightBrightnessValue->load();
    float weightH = weightHarshnessValue->load();
    float weightD = weightDynamicsValue->load();
    float weightU = weightUnpredictabilityValue->load();
    float totalWeight = weightB + weightH + weightD + weightU;

    if (totalWeight <= 0.0f) // All zero: fall back to equal weights
    {
        weightB = weightH = weightD = weightU = 1.0f;
        totalWeight = 4.0f;
    }

    const float brightnessScore = (1.0f - centroid) * 100.0f;        // Lower brightness = calmer
    const float harshnessScore = (1.0f - harshness) * 100.0f;        // Lower harshness = better
    const float dynamicScore = (1.0f - dynamics) * 100.0f;           // Lower variability = calmer
    const float unpredictScore = (1.0f - unpredictability) * 100.0f; // More predictable = calmer

    const float score = (brightnessScore * weightB + harshnessScore * weightH
                         + dynamicScore * weightD + unpredictScore * weightU) / totalWeight;

    return juce::jlimit(0.0f, 100.0f, score);
}

void AudioPluginAudioProcessor::startLogging()
{
    // Message thread. Throw away anything left over from a previous recording
    isLogging.store(false);
    drainLogFifo();
    dataLog.clear();
    droppedLogPoints.store(0);
    currentRating.store(0);
    ratingEventPending.store(false);
    ratingCount.store(0);

    loggingStartSample.store(samplesProcessedPublished.load());
    isLogging.store(true);
}

void AudioPluginAudioProcessor::stopLogging()
{
    isLogging.store(false);
    drainLogFifo();
}

void AudioPluginAudioProcessor::clearLog()
{
    if (isLogging.load())
        return;

    drainLogFifo();
    dataLog.clear();
    droppedLogPoints.store(0);
    currentRating.store(0);
    ratingCount.store(0);
}

void AudioPluginAudioProcessor::submitRating(int rating)
{
    if (!isLogging.load() || rating < 1 || rating > 7)
        return;

    currentRating.store(rating);
    ratingEventPending.store(true);
    ratingCount.fetch_add(1);
}

void AudioPluginAudioProcessor::timerCallback()
{
    drainLogFifo();

    // Auto-record: start once when audio first arrives (re-armed by prepareToPlay or by
    // switching the option off and on); stopping is always manual
    if (autoRecordValue->load() > 0.5f)
    {
        if (!isLogging.load() && signalSeen.load() && !autoRecordFired.load())
        {
            autoRecordFired.store(true);
            startLogging();
        }
    }
    else
    {
        autoRecordFired.store(false);
    }
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

    LogDataPoint& point = logFifoStorage[static_cast<size_t>(start1)];
    point.timestamp = static_cast<double>(samplesProcessed - loggingStartSample.load()) / currentSampleRate;
    point.activationScore = acousticActivationScore.load();
    point.brightness = spectralCentroid.load();
    point.harshness = spectralHarshness.load();
    point.dynamicVariability = dynamicVariability.load();
    point.temporalUnpredictability = temporalUnpredictability.load();
    point.rmsLevel = rmsLevel.load();
    point.loudnessSones = loudnessSones.load();
    point.sharpnessAcum = sharpnessAcum.load();
    point.roughnessAsper = roughnessAsper.load();
    point.stereoWidth = stereoWidth.load();
    point.listenerRating = currentRating.load();
    point.ratingEvent = ratingEventPending.exchange(false) ? 1 : 0;

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

std::vector<LogDataPoint> AudioPluginAudioProcessor::getLogSnapshot()
{
    drainLogFifo(); // Pick up anything the audio thread logged since the last drain
    return dataLog;
}

LogMetadata AudioPluginAudioProcessor::getLogMetadata() const
{
    LogMetadata metadata;
    metadata.pluginName = getName();
#if defined(JucePlugin_VersionString)
    metadata.pluginVersion = JucePlugin_VersionString;
#else
    metadata.pluginVersion = "dev";
#endif
    metadata.sampleRate = currentSampleRate;
    metadata.analysedChannels = analysisChannels;
    metadata.frameRateHz = currentSampleRate / hopSize;
    metadata.calibrationSpl = calibrationSplValue->load();

    // Shares of the index in percent; all-zero weights fall back to equal shares
    const float weights[] = { weightBrightnessValue->load(), weightHarshnessValue->load(),
                              weightDynamicsValue->load(), weightUnpredictabilityValue->load() };
    const float total = weights[0] + weights[1] + weights[2] + weights[3];
    auto share = [total](float weight) { return total > 0.0f ? 100.0f * weight / total : 25.0f; };

    metadata.weightBrightness = share(weights[0]);
    metadata.weightHarshness = share(weights[1]);
    metadata.weightDynamics = share(weights[2]);
    metadata.weightUnpredictability = share(weights[3]);

    metadata.smoothingAttackMs = smoothingAttackValue->load();
    metadata.smoothingReleaseMs = smoothingReleaseValue->load();
    metadata.droppedPoints = droppedLogPoints.load();
    return metadata;
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", 1, nullptr); // For migrating older projects later

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}