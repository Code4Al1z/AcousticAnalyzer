#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

//==============================================================================
class AudioPluginAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Acoustic Environment Research Tool"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Analysis getters
    float getSpectralCentroid() const { return spectralCentroid.load(); }
    float getSpectralHarshness() const { return spectralHarshness.load(); }
    float getRMSLevel() const { return rmsLevel.load(); }
    float getDynamicVariability() const { return dynamicVariability.load(); }
    float getTemporalUnpredictability() const { return temporalUnpredictability.load(); }
    float getAcousticActivationScore() const { return acousticActivationScore.load(); }

    // Psychoacoustic quantities in their own units (the getters above are 0-1)
    float getLoudnessSones() const { return loudnessSones.load(); }
    float getSharpnessAcum() const { return sharpnessAcum.load(); }
    float getRoughnessAsper() const { return roughnessAsper.load(); }

    // Data logging functions
    void startLogging();
    void stopLogging();
    bool isCurrentlyLogging() const { return isLogging.load(); }
    void exportToCSV();
    double getRecordingTime() const;
    int getDataPointCount() const { return static_cast<int>(dataLog.size()); }
    int getDroppedPointCount() const { return droppedLogPoints.load(); }

private:
    // FFT setup. Frames overlap by 50%: a new spectrum every hopSize samples,
    // each one covering the latest fftSize samples.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    static constexpr int hopSize = fftSize / 2;
    static constexpr int numBins = fftSize / 2;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    // Mono and stereo are analysed. Each channel is analysed separately and the
    // power spectra are averaged, so out-of-phase content can't cancel the way it
    // would in a mono mixdown.
    static constexpr int maxAnalysisChannels = 2;
    int analysisChannels = 2; // Set in prepareToPlay

    // Input is one circular buffer per channel (fftPos is the next write index,
    // i.e. the oldest sample); the transform runs on a separate scratch buffer so
    // incoming samples are never mixed with spectrum data.
    std::array<std::array<float, fftSize>, maxAnalysisChannels> fftInput{};
    std::array<float, fftSize * 2> fftScratch{}; // JUCE needs 2 * fftSize floats
    std::array<float, numBins> powerSpectrum{};   // Channel-averaged power of the last frame
    int fftPos = 0;
    int fftSamplesCollected = 0; // Saturates at fftSize; no analysis until the buffer is full
    int samplesSinceFrame = 0;

    // ---- Psychoacoustic model ----------------------------------------------
    // The power spectrum is grouped into the 24 Bark critical bands, then:
    //  - loudness (sone): Zwicker excitation slopes + specific loudness
    //  - sharpness (acum): von Bismarck / Zwicker weighting of specific loudness
    //  - roughness (asper): envelope modulation around 70 Hz per band, weighted by
    //    how correlated neighbouring bands are (simplified Daniel & Weber model)
    //
    // Absolute units need a level calibration: SPL when the digital signal has
    // 0 dBFS RMS (a full-scale sine is 3 dB below that). Set it to match your
    // monitoring chain; the metrics are only as absolute as this number.
    static constexpr float referenceSplAtFullScaleRms = 100.0f;

    static constexpr int numBarkBands = 24;
    static constexpr std::array<float, numBarkBands + 1> barkEdgesHz{
        0.0f, 100.0f, 200.0f, 300.0f, 400.0f, 510.0f, 630.0f, 770.0f, 920.0f, 1080.0f,
        1270.0f, 1480.0f, 1720.0f, 2000.0f, 2320.0f, 2700.0f, 3150.0f, 3700.0f, 4400.0f,
        5300.0f, 6400.0f, 7700.0f, 9500.0f, 12000.0f, 15500.0f };
    static constexpr std::array<float, numBarkBands> barkCentresHz{
        50.0f, 150.0f, 250.0f, 350.0f, 450.0f, 570.0f, 700.0f, 840.0f, 1000.0f, 1170.0f,
        1370.0f, 1600.0f, 1850.0f, 2150.0f, 2500.0f, 2900.0f, 3400.0f, 4000.0f, 4800.0f,
        5800.0f, 7000.0f, 8500.0f, 10500.0f, 13500.0f };

    // "Presence" region: Bark bands 13-18 = 2.0 kHz to 5.3 kHz, where hearing is most sensitive
    static constexpr int presenceFirstBand = 13;
    static constexpr int presenceLastBand = 18;

    // Set in prepareToPlay for the current sample rate
    std::array<int, numBarkBands + 1> barkBinEdges{};              // First FFT bin of each band
    std::array<float, numBarkBands> thresholdIntensity{};          // Threshold in quiet, 10^(dB SPL / 10)
    std::array<float, numBarkBands> bandSplDb{};                   // Last frame's level per band
    std::array<float, numBarkBands> specificLoudness{};            // Last frame's sone per Bark

    // Roughness filterbank (audio rate, double precision). Only bands from
    // firstRoughnessBand up are used: below ~500 Hz the carrier is too close to
    // the modulation range to separate the envelope.
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0.0; }

        double process(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        // RBJ cookbook, constant 0 dB peak gain bandpass
        void setBandpass(double sampleRate, double frequency, double q)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
            const double alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
            a1 = -2.0 * std::cos(w0) / a0; a2 = (1.0 - alpha) / a0;
        }

        void setLowpass(double sampleRate, double frequency, double q)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
            const double alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (1.0 - std::cos(w0)) * 0.5 / a0; b1 = (1.0 - std::cos(w0)) / a0; b2 = b0;
            a1 = -2.0 * std::cos(w0) / a0; a2 = (1.0 - alpha) / a0;
        }
    };

    struct RoughnessBand
    {
        Biquad bandpass;     // Isolates one Bark band
        Biquad envLowpass;   // Smooths the rectified signal into an envelope
        Biquad modBandpass;  // Modulation weighting, peak at roughnessModulationHz
        double envMean = 0.0;   // Slow average of the envelope (the "DC" level)
        double modPower = 0.0;  // Average power of the weighted modulation
        double lastModulation = 0.0;
    };

    static constexpr int firstRoughnessBand = 5;
    static constexpr double roughnessModulationHz = 70.0;
    static constexpr double roughnessModulationQ = 0.7;
    static constexpr double roughnessEnvelopeCutoffHz = 500.0;
    static constexpr double roughnessMeanSeconds = 0.08;
    static constexpr double roughnessPowerSeconds = 0.12;

    std::array<std::array<RoughnessBand, numBarkBands>, maxAnalysisChannels> roughnessBands{};
    std::array<std::array<double, numBarkBands>, maxAnalysisChannels> modulationCross{}; // band b x band b+1
    int lastRoughnessBand = numBarkBands; // Exclusive; bands near Nyquist are skipped
    double roughnessMeanCoeff = 0.0;
    double roughnessPowerCoeff = 0.0;

    // ---- Calibration ------------------------------------------------------
    // Every metric is normalised to 0-1 using these reference points. Retune here,
    // not inside the calculations. Readings with reference signals at 44.1 kHz,
    // 100 dB SPL = 0 dBFS RMS (60 dB SPL unless stated):
    //
    //   signal                      bri   har   dyn   unp    sone   acum   asper
    //   1 kHz tone, 40 dB           0.24  0.00  0.00  0.00   1.00   0.97   0.00   (1 sone reference)
    //   1 kHz tone 100% AM at 70 Hz 0.25  0.25  0.04  0.18   4.90   1.01   1.00   (1 asper reference)
    //   sine 250 Hz                 0.09  0.00  0.00  0.00   2.86   0.36   0.00
    //   sine 1 kHz                  0.26  0.00  0.00  0.00   4.52   1.02   0.00
    //   sine 4 kHz                  0.64  0.12  0.00  0.00   5.83   2.56   0.00
    //   white noise                 0.60  0.21  0.01  0.02  16.63   2.41   0.18
    //   pink noise                  0.49  0.21  0.10  0.18  17.33   1.95   0.23
    //   pink noise, 40 / 80 dB      0.48/0.49  0.08/0.54  .10  .18  4.7/54.8  1.9/2.0  0.15/0.23
    //   pink noise, 4 Hz AM 100%    0.48  0.19  0.68  1.00   8.85   1.92   0.44
    //   speech-like bursts, 65 dB   0.49  0.29  1.00  0.85  21.32   1.94   0.39
    //
    // Known limits: roughness is a simplified Daniel & Weber model, calibrated at
    // the 1 asper reference and checked against the published shape (peak near
    // 70 Hz, rising with depth and level). It under-reads modulation above ~100 Hz,
    // ignores carriers below ~500 Hz, and values above ~3 asper are unvalidated.
    // Loudness follows the Zwicker sone scale but has none of the low-frequency
    // corrections of ISO 532-1, so it reads a little high below ~250 Hz.
    static constexpr float loudnessScale = 0.9f;            // Makes a 1 kHz tone at 40 dB SPL read 1 sone
    static constexpr float roughnessScale = 0.51f;          // Raw sum -> asper (1 asper = 1 kHz tone, 70 Hz AM, 100%, 60 dB)
    static constexpr float roughnessDepthExponent = 1.5f;   // Roughness grows a little faster than linearly with depth
    static constexpr float roughnessCorrelationExponent = 3.0f; // Suppresses weakly correlated (noise-like) bands
    static constexpr float sharpnessFullScaleAcum = 4.0f;   // Sharpness that maps to brightness 1
    static constexpr float roughnessFullScaleAsper = 2.0f;  // Roughness that maps to 1
    static constexpr float presenceFullScaleSone = 20.0f;   // Loudness in the 2-5 kHz region that maps to 1
    // Harshness = this much roughness + the rest presence loudness
    static constexpr float harshnessRoughnessWeight = 0.5f;

    // Dynamics are measured in dB, so they don't depend on playback level
    static constexpr float rmsFloorDb = -80.0f;               // floor for silent windows
    static constexpr float dynamicFullScaleDb = 12.0f;        // std dev of level that maps to 1
    static constexpr float temporalFullScaleDb = 6.0f;        // mean |level change| per 50 ms that maps to 1

    // Frames quieter than this (mean square, -80 dBFS RMS) are treated as digital
    // silence: the metrics hold their last value instead of jumping to "calm"
    static constexpr float silenceMeanSquare = 1.0e-8f;

    // Meter smoothing (one-pole filter, applied once per FFT frame)
    static constexpr float smoothingAttackSeconds = 0.1f;
    static constexpr float smoothingReleaseSeconds = 0.4f;

    struct Smoother
    {
        float value = 0.0f;
        bool primed = false;

        void reset() { value = 0.0f; primed = false; }

        float process(float target, float attackCoeff, float releaseCoeff)
        {
            if (!primed) // Start at the first real value instead of ramping up from zero
            {
                value = target;
                primed = true;
                return value;
            }

            value += (target > value ? attackCoeff : releaseCoeff) * (target - value);
            return value;
        }
    };

    Smoother sharpnessSmoother, roughnessSmoother, presenceSmoother, loudnessSmoother,
             dynamicSmoother, temporalSmoother;
    float smoothingAttackCoeff = 1.0f;
    float smoothingReleaseCoeff = 1.0f;

    // Analysis parameters (atomic for thread safety)
    std::atomic<float> spectralCentroid{ 0.0f };
    std::atomic<float> spectralHarshness{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> dynamicVariability{ 0.0f };
    std::atomic<float> temporalUnpredictability{ 0.0f };
    std::atomic<float> acousticActivationScore{ 50.0f }; // 0-100 scale
    std::atomic<float> loudnessSones{ 0.0f };
    std::atomic<float> sharpnessAcum{ 0.0f };
    std::atomic<float> roughnessAsper{ 0.0f };

    // Level history for dynamic analysis, in dB. One entry is added per fixed time
    // window (not per audio block), so results don't depend on the host's
    // buffer size. 100 entries x 50 ms = 5 seconds of history.
    static constexpr double rmsWindowSeconds = 0.05;
    static constexpr int rmsHistorySize = 100;
    std::array<float, rmsHistorySize> rmsHistoryDb{};
    int rmsHistoryPos = 0;        // Next write index
    int rmsHistoryCount = 0;      // Valid entries (saturates at rmsHistorySize)
    int rmsWindowSamples = 882;   // Set in prepareToPlay from the sample rate
    int rmsSampleCount = 0;       // Samples accumulated in the current window
    double rmsSumOfSquares = 0.0; // Running sum of squares for the current window

    double currentSampleRate = 44100.0;

    // Data logging
    struct DataPoint
    {
        double timestamp;
        float activationScore;
        float spectralCentroid;
        float spectralHarshness;
        float dynamicVariability;
        float temporalUnpredictability;
        float rmsLevel;
        float loudnessSones;
        float sharpnessAcum;
        float roughnessAsper;
    };

    // Recording is split across two threads so the audio thread never locks or allocates:
    //  - audio thread: pushes DataPoints into a fixed-size lock-free FIFO
    //  - message thread: drains the FIFO into dataLog (timer, stop and export)
    // dataLog is only ever touched on the message thread.
    static constexpr int logFifoCapacity = 8192; // ~3 minutes between drains at 44.1 kHz
    juce::AbstractFifo logFifo{ logFifoCapacity };
    std::array<DataPoint, logFifoCapacity> logFifoStorage{};
    std::vector<DataPoint> dataLog;
    std::atomic<int> droppedLogPoints{ 0 };

    std::atomic<bool> isLogging{ false };
    std::atomic<juce::int64> loggingStartSample{ 0 };

    // Timestamps come from the sample count, not the wall clock
    juce::int64 samplesProcessed = 0; // audio thread only
    std::atomic<juce::int64> samplesProcessedPublished{ 0 }; // updated once per block

    void timerCallback() override { drainLogFifo(); }
    void drainLogFifo();

    // Analysis functions. Each compute function returns a raw 0-1 value.
    void performFFTAnalysis();
    void computeBarkAnalysis(float& loudness, float& sharpness, float& presenceLoudness);
    float computeRoughness() const;
    void processRoughnessSample(int channel, float sample);
    float computeDynamicVariability() const;
    float computeTemporalUnpredictability() const;
    static float computeAcousticActivationScore(float centroid, float harshness,
                                                float dynamics, float unpredictability);
    void logDataPoint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};