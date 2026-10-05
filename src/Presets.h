#pragma once

#include "Parameters.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

// A named set of settings: the index weights, gauge zones, calibration and smoothing
struct Preset
{
    juce::String name;
    std::map<juce::String, float> values; // Parameter ID -> plain value
    bool builtIn = false;
};

// Built-in presets plus the user's own, saved as small XML files in a folder
class PresetStore
{
public:
    explicit PresetStore(juce::File directoryToUse = defaultDirectory());

    static juce::File defaultDirectory();

    // Neutral starting points, not validated recommendations. "Spectral only" and
    // "Temporal only" are for isolating which half of the index drives a result.
    static std::vector<Preset> builtInPresets();

    // Built-ins first, then saved presets sorted by name
    std::vector<Preset> list() const;

    // Fails for an empty name, a built-in name, or if the file can't be written
    bool save(const juce::String& name, const std::map<juce::String, float>& values) const;

    // Saved presets only
    bool remove(const juce::String& name) const;

    static const std::vector<const char*>& presetParameterIds();
    static std::map<juce::String, float> captureValues(juce::AudioProcessorValueTreeState& state);
    static void apply(juce::AudioProcessorValueTreeState& state, const Preset& preset);
    static bool matches(juce::AudioProcessorValueTreeState& state, const Preset& preset);

private:
    juce::File fileFor(const juce::String& name) const;

    juce::File directory;
};
