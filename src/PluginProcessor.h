#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

//==============================================================================
class AudioPluginAudioProcessor : public juce::AudioProcessor
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

private:
    // FFT setup
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize * 2> fftData;
    int fftPos = 0;

    // Analysis parameters (atomic for thread safety)
    std::atomic<float> spectralCentroid{ 0.0f };
    std::atomic<float> spectralHarshness{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> dynamicVariability{ 0.0f };
    std::atomic<float> temporalUnpredictability{ 0.0f };
    std::atomic<float> acousticActivationScore{ 50.0f }; // 0-100 scale

    // RMS history for dynamic analysis
    static constexpr int rmsHistorySize = 100;
    std::array<float, rmsHistorySize> rmsHistory{};
    int rmsHistoryPos = 0;

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

    std::vector<DataPoint> dataLog;
    std::atomic<bool> isLogging{ false };
    juce::int64 loggingStartTime = 0;
    juce::CriticalSection dataLogLock;

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