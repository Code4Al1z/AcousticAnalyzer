#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// One analysis frame as recorded for export
struct LogDataPoint
{
    double timestamp = 0.0; // Seconds since recording started
    float activationScore = 0.0f;
    float brightness = 0.0f;
    float harshness = 0.0f;
    float dynamicVariability = 0.0f;
    float temporalUnpredictability = 0.0f;
    float rmsLevel = 0.0f;
    float loudnessSones = 0.0f;
    float sharpnessAcum = 0.0f;
    float roughnessAsper = 0.0f;
    float stereoWidth = 0.0f;
    int listenerRating = 0; // 0 = none yet, otherwise 1-7 (how calming), held until the next rating
    int ratingEvent = 0;    // 1 on the frame where a rating was entered
};

// How the index compares with the listener's ratings
struct RatingSummary
{
    int numRatings = 0;
    bool hasCorrelation = false; // Needs at least 3 ratings that are not all identical
    double spearman = 0.0;       // Rank correlation between rating and the index just before it
};

// Settings in force while recording, written at the top of the CSV so a file can
// be interpreted later without knowing how the plugin was configured
struct LogMetadata
{
    juce::String pluginName;
    juce::String pluginVersion;
    double sampleRate = 0.0;
    int analysedChannels = 0;
    double frameRateHz = 0.0;
    float calibrationSpl = 0.0f; // dB SPL at 0 dBFS RMS
    float weightBrightness = 0.0f; // Shares of the index, in percent
    float weightHarshness = 0.0f;
    float weightDynamics = 0.0f;
    float weightUnpredictability = 0.0f;
    float smoothingAttackMs = 0.0f;
    float smoothingReleaseMs = 0.0f;
    int droppedPoints = 0;
};

namespace LogExporter
{
    // CSV text: a block of "# key: value" lines, then the column header and one row
    // per point. Read it with pandas using read_csv(path, comment='#').
    juce::String buildCsv(const std::vector<LogDataPoint>& points, const LogMetadata& metadata, juce::Time exportedAt);

    // Pairs each rating with the mean index over the `windowSeconds` before it (ratings
    // are retrospective), then takes the Spearman rank correlation. Ratings are
    // "how calming" and the index is high for calm sound, so a positive value means
    // they agree.
    RatingSummary summariseRatings(const std::vector<LogDataPoint>& points, double windowSeconds = 5.0);

    // The same data as JSON: settings, rating summary, column names and one array per frame
    // (the rating is null until the first one)
    juce::String buildJson(const std::vector<LogDataPoint>& points, const LogMetadata& metadata, juce::Time exportedAt);

    // One line per listener rating (start, end, label, tab separated) that Audacity can
    // import as a label track. Times are seconds from the start of the recording.
    juce::String buildLabelTrack(const std::vector<LogDataPoint>& points);
}
