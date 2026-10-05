#include "LogExporter.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Ranks starting at 1; tied values share their average rank
    std::vector<double> ranksOf(const std::vector<double>& values)
    {
        std::vector<size_t> order(values.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        std::sort(order.begin(), order.end(), [&values](size_t a, size_t b) { return values[a] < values[b]; });

        std::vector<double> ranks(values.size());
        for (size_t i = 0; i < order.size();)
        {
            size_t j = i;
            while (j + 1 < order.size() && values[order[j + 1]] == values[order[i]])
                ++j;

            const double averageRank = 0.5 * static_cast<double>(i + j) + 1.0;
            for (size_t k = i; k <= j; ++k)
                ranks[order[k]] = averageRank;

            i = j + 1;
        }

        return ranks;
    }

    // Returns false if either series is constant
    bool pearson(const std::vector<double>& a, const std::vector<double>& b, double& result)
    {
        const double n = static_cast<double>(a.size());
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < a.size(); ++i) { meanA += a[i]; meanB += b[i]; }
        meanA /= n;
        meanB /= n;

        double covariance = 0.0, varianceA = 0.0, varianceB = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            covariance += (a[i] - meanA) * (b[i] - meanB);
            varianceA += (a[i] - meanA) * (a[i] - meanA);
            varianceB += (b[i] - meanB) * (b[i] - meanB);
        }

        if (varianceA <= 0.0 || varianceB <= 0.0)
            return false;

        result = covariance / std::sqrt(varianceA * varianceB);
        return true;
    }
}

namespace LogExporter
{
    RatingSummary summariseRatings(const std::vector<LogDataPoint>& points, double windowSeconds)
    {
        RatingSummary summary;
        std::vector<double> ratings, indexValues;

        for (size_t i = 0; i < points.size(); ++i)
        {
            if (points[i].ratingEvent == 0)
                continue;

            ++summary.numRatings;

            // Mean index over the window ending at this rating
            double sum = 0.0;
            int count = 0;
            for (size_t j = i + 1; j-- > 0;)
            {
                if (points[j].timestamp < points[i].timestamp - windowSeconds)
                    break;

                sum += points[j].activationScore;
                ++count;
            }

            ratings.push_back(points[i].listenerRating);
            indexValues.push_back(sum / count);
        }

        if (summary.numRatings >= 3)
            summary.hasCorrelation = pearson(ranksOf(ratings), ranksOf(indexValues), summary.spearman);

        return summary;
    }

    juce::String buildCsv(const std::vector<LogDataPoint>& points, const LogMetadata& metadata, juce::Time exportedAt)
    {
        juce::String csv;
        csv.preallocateBytes(512 + points.size() * 96);

        // Metadata
        csv << "# " << metadata.pluginName << " " << metadata.pluginVersion << "\n";
        csv << "# exported: " << exportedAt.formatted("%Y-%m-%d %H:%M:%S") << "\n";
        csv << "# sample_rate_hz: " << juce::String(metadata.sampleRate, 1) << "\n";
        csv << "# analysed_channels: " << metadata.analysedChannels << "\n";
        csv << "# frame_rate_hz: " << juce::String(metadata.frameRateHz, 2) << "\n";
        csv << "# calibration_db_spl_at_0_dbfs_rms: " << juce::String(metadata.calibrationSpl, 1) << "\n";
        csv << "# index_weights_percent: brightness=" << juce::String(metadata.weightBrightness, 1)
            << " harshness=" << juce::String(metadata.weightHarshness, 1)
            << " dynamics=" << juce::String(metadata.weightDynamics, 1)
            << " unpredictability=" << juce::String(metadata.weightUnpredictability, 1) << "\n";
        csv << "# smoothing_ms: attack=" << juce::String(metadata.smoothingAttackMs, 0)
            << " release=" << juce::String(metadata.smoothingReleaseMs, 0) << "\n";
        csv << "# points: " << static_cast<int>(points.size()) << " dropped: " << metadata.droppedPoints << "\n";
        csv << "# note: the activation index is experimental. Its weights are research estimates and have not "
               "been validated against listener data.\n";

        const auto ratings = summariseRatings(points);
        csv << "# listener_ratings: n=" << ratings.numRatings;
        if (ratings.hasCorrelation)
            csv << " spearman_rho_index_vs_rating=" << juce::String(ratings.spearman, 3);
        csv << " (rating = how calming, 1-7; index = mean over the 5 s before each rating)\n";

        csv << "# stereo_width: share of the energy that is not common to both channels "
               "(0 = mono, about 0.5 = unrelated channels, 1 = opposite polarity); not part of the index\n";
        csv << "# units: Brightness, Harshness, Dynamic_Variability and Temporal_Unpredictability are 0-1; "
               "Activation_Score is 0-100; RMS_Level is linear (full scale = 1)\n";

        // Data
        csv << "Timestamp_Seconds,Activation_Score,Brightness,Harshness,Dynamic_Variability,"
               "Temporal_Unpredictability,RMS_Level,Loudness_Sone,Sharpness_Acum,Roughness_Asper,Stereo_Width,"
               "Listener_Rating,Rating_Event\n";

        for (const auto& p : points)
        {
            csv << juce::String(p.timestamp, 3) << ","
                << juce::String(p.activationScore, 2) << ","
                << juce::String(p.brightness, 4) << ","
                << juce::String(p.harshness, 4) << ","
                << juce::String(p.dynamicVariability, 4) << ","
                << juce::String(p.temporalUnpredictability, 4) << ","
                << juce::String(p.rmsLevel, 6) << ","
                << juce::String(p.loudnessSones, 3) << ","
                << juce::String(p.sharpnessAcum, 3) << ","
                << juce::String(p.roughnessAsper, 3) << ","
                << juce::String(p.stereoWidth, 3) << ","
                << (p.listenerRating > 0 ? juce::String(p.listenerRating) : juce::String()) << ","
                << p.ratingEvent << "\n";
        }

        return csv;
    }

    namespace
    {
        juce::String jsonString(const juce::String& text) { return juce::JSON::toString(juce::var(text)); }
    }

    juce::String buildJson(const std::vector<LogDataPoint>& points, const LogMetadata& metadata, juce::Time exportedAt)
    {
        const auto ratings = summariseRatings(points);

        juce::String json;
        json.preallocateBytes(1024 + points.size() * 110);

        json << "{\n";
        json << "  \"generator\": " << jsonString(metadata.pluginName + " " + metadata.pluginVersion) << ",\n";
        json << "  \"exported\": " << jsonString(exportedAt.toISO8601(true)) << ",\n";
        json << "  \"note\": \"The activation index is experimental. Its weights are research estimates and have not "
                "been validated against listener data.\",\n";
        json << "  \"settings\": {\n";
        json << "    \"sampleRateHz\": " << juce::String(metadata.sampleRate, 1) << ",\n";
        json << "    \"analysedChannels\": " << metadata.analysedChannels << ",\n";
        json << "    \"frameRateHz\": " << juce::String(metadata.frameRateHz, 2) << ",\n";
        json << "    \"calibrationDbSplAt0DbfsRms\": " << juce::String(metadata.calibrationSpl, 1) << ",\n";
        json << "    \"indexWeightsPercent\": { \"brightness\": " << juce::String(metadata.weightBrightness, 1)
             << ", \"harshness\": " << juce::String(metadata.weightHarshness, 1)
             << ", \"dynamics\": " << juce::String(metadata.weightDynamics, 1)
             << ", \"unpredictability\": " << juce::String(metadata.weightUnpredictability, 1) << " },\n";
        json << "    \"smoothingMs\": { \"attack\": " << juce::String(metadata.smoothingAttackMs, 0)
             << ", \"release\": " << juce::String(metadata.smoothingReleaseMs, 0) << " },\n";
        json << "    \"droppedPoints\": " << metadata.droppedPoints << "\n";
        json << "  },\n";
        json << "  \"ratings\": { \"scale\": \"how calming, 1 (not at all) to 7 (very calming)\", \"count\": "
             << ratings.numRatings << ", \"spearmanRhoIndexVsRating\": "
             << (ratings.hasCorrelation ? juce::String(ratings.spearman, 3) : juce::String("null"))
             << ", \"indexWindowSeconds\": 5 },\n";
        json << "  \"columns\": [\"timestampSeconds\", \"activationScore\", \"brightness\", \"harshness\", "
                "\"dynamicVariability\", \"temporalUnpredictability\", \"rmsLevel\", \"loudnessSone\", "
                "\"sharpnessAcum\", \"roughnessAsper\", \"stereoWidth\", \"listenerRating\", \"ratingEvent\"],\n";
        json << "  \"rows\": [";

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& p = points[i];
            json << (i == 0 ? "\n    [" : ",\n    [")
                 << juce::String(p.timestamp, 3) << ", " << juce::String(p.activationScore, 2) << ", "
                 << juce::String(p.brightness, 4) << ", " << juce::String(p.harshness, 4) << ", "
                 << juce::String(p.dynamicVariability, 4) << ", " << juce::String(p.temporalUnpredictability, 4) << ", "
                 << juce::String(p.rmsLevel, 6) << ", " << juce::String(p.loudnessSones, 3) << ", "
                 << juce::String(p.sharpnessAcum, 3) << ", " << juce::String(p.roughnessAsper, 3) << ", "
                 << juce::String(p.stereoWidth, 3) << ", "
                 << (p.listenerRating > 0 ? juce::String(p.listenerRating) : juce::String("null")) << ", "
                 << p.ratingEvent << "]";
        }

        json << (points.empty() ? "]\n}\n" : "\n  ]\n}\n");
        return json;
    }

    juce::String buildLabelTrack(const std::vector<LogDataPoint>& points)
    {
        juce::String labels;

        for (const auto& p : points)
        {
            if (p.ratingEvent == 0)
                continue;

            const auto time = juce::String(p.timestamp, 6);
            labels << time << "\t" << time << "\trating " << p.listenerRating << " / 7 (index "
                   << juce::String(p.activationScore, 1) << ")\n";
        }

        return labels;
    }
}
