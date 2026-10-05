#pragma once

// Parameter IDs. These are written into saved projects, so never rename one;
// add a new ID (and bump its version hint) instead.
namespace params
{
    // Index weights: relative importance of each metric in the activation index.
    // They are normalised, so only their ratios matter.
    inline constexpr const char* weightBrightness       = "weightBrightness";
    inline constexpr const char* weightHarshness        = "weightHarshness";
    inline constexpr const char* weightDynamics         = "weightDynamics";
    inline constexpr const char* weightUnpredictability = "weightUnpredictability";

    // Where the gauge changes zone
    inline constexpr const char* calmAbove    = "calmAbove";
    inline constexpr const char* neutralAbove = "neutralAbove";

    // Analysis
    inline constexpr const char* calibrationSpl  = "calibrationSpl";
    inline constexpr const char* smoothingAttack = "smoothingAttackMs";
    inline constexpr const char* smoothingRelease = "smoothingReleaseMs";

    // Workflow
    inline constexpr const char* autoRecord = "autoRecord";
}
