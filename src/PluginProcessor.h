#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LogExporter.h"
#include "Parameters.h"
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

    // Settings (index weights, zone thresholds, calibration, smoothing) and UI
    // state, saved with the project
    juce::AudioProcessorValueTreeState apvts;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Acoustic Analyzer"; }
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

    // Share of the signal energy that is not common to both channels: 0 = identical
    // channels (mono), about 0.5 = unrelated channels, 1 = opposite polarity. Always 0
    // for a mono input.
    float getStereoWidth() const { return stereoWidth.load(); }

    // False until audio has arrived, and while the input is digital silence (the other
    // readings then hold their last values)
    bool isSignalPresent() const { return signalSeen.load() && !silentNow.load(); }

    // Critical band (Bark) layout used by the analysis, shared with the UI
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

    // Last frame's specific loudness per Bark band (sone/Bark), for display
    void getSpecificLoudness(std::array<float, numBarkBands>& destination) const
    {
        for (size_t i = 0; i < destination.size(); ++i)
            destination[i] = sharedSpecificLoudness[i].load(std::memory_order_relaxed);
    }

    // Data logging functions
    void startLogging();
    void stopLogging();
    bool isCurrentlyLogging() const { return isLogging.load(); }
    // Message thread only. Recorded frames so far (collects any pending ones first)
    std::vector<LogDataPoint> getLogSnapshot();
    // Message thread only. Forget the recorded frames (ignored while recording)
    void clearLog();

    // Listener rating of how calming the sound is, 1 (not at all) to 7 (very).
    // Only accepted while recording; it is logged with the frames from then on.
    void submitRating(int rating);
    int getCurrentRating() const { return currentRating.load(); }
    int getRatingCount() const { return ratingCount.load(); }
    // The settings in force now, for the CSV header
    LogMetadata getLogMetadata() const;
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
    // 0 dBFS RMS (a full-scale sine is 3 dB below that). This is the
    // "calibrationSpl" parameter (default 100 dB). Set it to match your monitoring
    // chain; the metrics are only as absolute as this number.

    // Set in prepareToPlay for the current sample rate
    std::array<int, numBarkBands + 1> barkBinEdges{};              // First FFT bin of each band
    std::array<float, numBarkBands> thresholdIntensity{};          // Threshold in quiet, 10^(dB SPL / 10)
    std::array<float, numBarkBands> bandSplDb{};                   // Last frame's level per band
    std::array<float, numBarkBands> specificLoudness{};            // Last frame's sone per Bark

    // Roughness filterbank (audio rate, double precision). Only bands from
    // firstRoughnessBand up are used (about 350 Hz and above): below that the carrier is
    // too close to the modulation range to separate the envelope.
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

    static constexpr int firstRoughnessBand = 3;
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
    // not inside the calculations. Readings with reference signals at 48 kHz,
    // 100 dB SPL = 0 dBFS RMS (60 dB SPL unless stated):
    //
    //   signal                      bri   har   dyn   unp    sone   acum   asper
    //   1 kHz tone, 40 dB           0.24  0.00  0.00  0.00   0.82   0.96   0.00
    //   1 kHz tone 100% AM at 70 Hz 0.24  0.24  0.04  0.18   4.48   0.97   0.98   (1 asper reference)
    //   sine 250 Hz                 0.09  0.00  0.00  0.00   2.39   0.34   0.01
    //   sine 1 kHz                  0.25  0.00  0.00  0.00   3.86   1.00   0.00
    //   sine 4 kHz                  0.62  0.11  0.00  0.00   5.18   2.48   0.00
    //   white noise                 0.60  0.18  0.01  0.03  14.91   2.41   0.08
    //   pink noise, 40 dB           0.48  0.05  0.16  0.21   3.47   1.90   0.04
    //   pink noise, 60 dB           0.48  0.15  0.13  0.25  14.64   1.94   0.08
    //   pink noise, 80 dB           0.49  0.45  0.16  0.25  48.86   1.97   0.09
    //   pink noise, 4 Hz AM 50%     0.48  0.16  0.29  0.57  15.34   1.94   0.10
    //   pink noise, 4 Hz AM 100%    0.48  0.17  0.66  1.00  15.15   1.93   0.15
    //   speech-like bursts, 65 dB   0.48  0.21  0.99  0.87  17.50   1.93   0.20
    //
    // Checked against MoSQITo (ISO 532-1 loudness, DIN 45692 sharpness, Daniel & Weber
    // roughness) on 57 calibrated test signals; tools/validation repeats the comparison.
    // Plugin / reference:
    //   sharpness  median 0.95, every signal within 20%, rank correlation 0.97
    //   loudness   median 1.00, 64% within 20% and 86% within 35%, rank correlation 0.99.
    //              Tones from 30 to 80 dB are mostly within 25%; it reads high for broadband
    //              noise (about 1.5 to 2 times, more at low levels) and for 2 kHz tones, and low
    //              around 250 Hz. A 1 kHz tone at 40 dB reads 0.82 sone (ISO: 1.0): the scale is
    //              fitted for the smallest overall error rather than anchored on that one tone.
    //   roughness  median 1.09 and rank correlation 0.96 on the signals the reference rates
    //              above 0.1 asper. The reference tone, the modulation-frequency curve
    //              (10-300 Hz), depth and carrier (250 Hz-8 kHz) are mostly within 25%.
    //              Steady noise reads 0.04-0.09 asper (reference 0.02). Broadband noise
    //              modulated at 70 Hz reads about 3 times too high.
    static constexpr float loudnessScale = 1.2f;            // Fitted against ISO 532-1 (see the notes above); a 1 kHz tone at 40 dB SPL reads 0.8 sone
    static constexpr float roughnessScale = 0.60f;          // Raw sum -> asper (1 asper = 1 kHz tone, 70 Hz AM, 100%, 60 dB)
    static constexpr float roughnessDepthExponent = 1.3f;   // Roughness grows a little faster than linearly with depth
    static constexpr float roughnessCorrelationExponent = 5.0f; // Suppresses weakly correlated (noise-like) bands
    static constexpr float sharpnessFullScaleAcum = 4.0f;   // Sharpness that maps to brightness 1
    static constexpr float roughnessFullScaleAsper = 2.0f;  // Roughness that maps to 1
    static constexpr float presenceFullScaleSone = 17.0f;   // Loudness in the 2-5 kHz region that maps to 1
    // Harshness = this much roughness + the rest presence loudness
    static constexpr float harshnessRoughnessWeight = 0.5f;

    // Dynamics are measured in dB, so they don't depend on playback level
    static constexpr float rmsFloorDb = -80.0f;               // floor for silent windows
    static constexpr float dynamicFullScaleDb = 12.0f;        // std dev of level that maps to 1
    static constexpr float temporalFullScaleDb = 6.0f;        // mean |level change| per 50 ms that maps to 1

    // Frames quieter than this (mean square, -80 dBFS RMS) are treated as digital
    // silence: the metrics hold their last value instead of jumping to "calm"
    static constexpr float silenceMeanSquare = 1.0e-8f;

    // Meter smoothing: one-pole filter applied once per FFT frame, with attack and
    // release times taken from the parameters (defaults 100 ms and 400 ms)

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
             dynamicSmoother, temporalSmoother, widthSmoother;
    float smoothingAttackCoeff = 1.0f;
    float smoothingReleaseCoeff = 1.0f;

    // Parameter values, read on the audio thread
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* weightBrightnessValue = nullptr;
    std::atomic<float>* weightHarshnessValue = nullptr;
    std::atomic<float>* weightDynamicsValue = nullptr;
    std::atomic<float>* weightUnpredictabilityValue = nullptr;
    std::atomic<float>* calibrationSplValue = nullptr;
    std::atomic<float>* smoothingAttackValue = nullptr;
    std::atomic<float>* smoothingReleaseValue = nullptr;

    // Analysis parameters (atomic for thread safety)
    std::atomic<float> spectralCentroid{ 0.0f };
    std::atomic<float> spectralHarshness{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> dynamicVariability{ 0.0f };
    std::atomic<float> temporalUnpredictability{ 0.0f };
    std::atomic<float> acousticActivationScore{ 50.0f }; // 0-100 scale
    std::array<std::atomic<float>, numBarkBands> sharedSpecificLoudness{}; // Written by the audio thread, read by the UI
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
    // Recording is split across two threads so the audio thread never locks or allocates:
    //  - audio thread: pushes LogDataPoints into a fixed-size lock-free FIFO
    //  - message thread: drains the FIFO into dataLog (timer, stop and export)
    // dataLog is only ever touched on the message thread.
    static constexpr int logFifoCapacity = 8192; // ~3 minutes between drains at 44.1 kHz
    juce::AbstractFifo logFifo{ logFifoCapacity };
    std::array<LogDataPoint, logFifoCapacity> logFifoStorage{};
    std::vector<LogDataPoint> dataLog;
    std::atomic<int> droppedLogPoints{ 0 };

    std::atomic<bool> isLogging{ false };
    std::atomic<juce::int64> loggingStartSample{ 0 };

    // Timestamps come from the sample count, not the wall clock
    juce::int64 samplesProcessed = 0; // audio thread only
    std::atomic<juce::int64> samplesProcessedPublished{ 0 }; // updated once per block

    void timerCallback() override; // Message thread, 10 Hz: collects frames, runs auto-record
    void drainLogFifo();

    std::atomic<int> currentRating{ 0 };       // 0 = none
    std::atomic<bool> ratingEventPending{ false };
    std::atomic<int> ratingCount{ 0 };
    std::atomic<bool> signalSeen{ false };      // A non-silent frame has been analysed
    std::atomic<bool> silentNow{ true };        // The latest frame was digital silence
    std::atomic<float> stereoWidth{ 0.0f };
    double sumMid = 0.0, sumSide = 0.0;         // Audio thread: energy of (L+R)/2 and (L-R)/2 in the current window
    float latestWidth = 0.0f;                   // Last completed window's width, before smoothing
    std::atomic<bool> autoRecordFired{ false }; // Auto-record has already started a recording
    std::atomic<float>* autoRecordValue = nullptr;

    // Analysis functions. Each compute function returns a raw 0-1 value.
    void performFFTAnalysis();
    void computeBarkAnalysis(float& loudness, float& sharpness, float& presenceLoudness);
    float computeRoughness() const;
    void processRoughnessSample(int channel, float sample);
    float computeDynamicVariability() const;
    float computeTemporalUnpredictability() const;
    float computeAcousticActivationScore(float centroid, float harshness,
                                         float dynamics, float unpredictability) const;
    void logDataPoint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};