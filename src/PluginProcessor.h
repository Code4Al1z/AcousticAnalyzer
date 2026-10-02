#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
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

    // ---- Calibration ------------------------------------------------------
    // Every metric is normalised to 0-1 using these reference points. Retune here,
    // not inside the calculations. Readings with reference signals (44.1 kHz,
    // stereo, any level from -60 to -20 dBFS):
    //
    //   signal                   brightness  harshness  dynamics  unpredictability
    //   sine 250 Hz                  0.17       0.00      0.00        0.00
    //   sine 1 kHz                   0.50       0.00      0.00        0.00
    //   sine 4 kHz                   0.83       1.00      0.00        0.00
    //   white noise                  1.00       0.91      0.01        0.03
    //   pink noise (steady)          0.79       0.36      0.09        0.23
    //   pink noise, 4 Hz AM 50%      0.76       0.34      0.21        0.44
    //   pink noise, 4 Hz AM 100%     0.75       0.32      0.82        1.00
    //   speech-like bursts           0.77       0.33      0.88        0.57

    // Brightness: power-weighted spectral centroid on a log-frequency axis
    static constexpr float centroidMinHz = 125.0f;   // maps to 0
    static constexpr float centroidMaxHz = 8000.0f;  // maps to 1 (6 octaves)

    // Harshness: fraction of spectral power above this frequency (already 0-1)
    static constexpr float harshnessCrossoverHz = 2000.0f;

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

    Smoother centroidSmoother, harshnessSmoother, dynamicSmoother, temporalSmoother;
    float smoothingAttackCoeff = 1.0f;
    float smoothingReleaseCoeff = 1.0f;

    // Analysis parameters (atomic for thread safety)
    std::atomic<float> spectralCentroid{ 0.0f };
    std::atomic<float> spectralHarshness{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> dynamicVariability{ 0.0f };
    std::atomic<float> temporalUnpredictability{ 0.0f };
    std::atomic<float> acousticActivationScore{ 50.0f }; // 0-100 scale

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
    float computeSpectralCentroid() const;
    float computeSpectralHarshness() const;
    float computeDynamicVariability() const;
    float computeTemporalUnpredictability() const;
    static float computeAcousticActivationScore(float centroid, float harshness,
                                                float dynamics, float unpredictability);
    void logDataPoint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};