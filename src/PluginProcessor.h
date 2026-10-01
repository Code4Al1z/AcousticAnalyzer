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
    // FFT setup
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    static constexpr int numBins = fftSize / 2;

    // Input is collected in its own buffer; the transform runs on a separate
    // scratch buffer so incoming samples are never mixed with spectrum data.
    std::array<float, fftSize> fftInput{};
    std::array<float, fftSize * 2> fftScratch{}; // JUCE needs 2 * fftSize floats
    std::array<float, numBins> magnitudes{};      // Result of the last analysis frame
    int fftPos = 0;

    // Analysis parameters (atomic for thread safety)
    std::atomic<float> spectralCentroid{ 0.0f };
    std::atomic<float> spectralHarshness{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> dynamicVariability{ 0.0f };
    std::atomic<float> temporalUnpredictability{ 0.0f };
    std::atomic<float> acousticActivationScore{ 50.0f }; // 0-100 scale

    // RMS history for dynamic analysis. One entry is added per fixed time
    // window (not per audio block), so results don't depend on the host's
    // buffer size. 100 entries x 20 ms = 2 seconds of history.
    static constexpr double rmsWindowSeconds = 0.02;
    static constexpr int rmsHistorySize = 100;
    std::array<float, rmsHistorySize> rmsHistory{};
    int rmsHistoryPos = 0;
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
    static constexpr int logFifoCapacity = 4096; // ~3 minutes between drains at 44.1 kHz
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

    // Analysis functions
    void performFFTAnalysis();
    void calculateSpectralCentroid();
    void calculateSpectralHarshness();
    void calculateDynamicVariability();
    void calculateTemporalUnpredictability();
    void calculateAcousticActivationScore();
    void logDataPoint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};