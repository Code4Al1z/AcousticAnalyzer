#include "Presets.h"
#include <algorithm>

namespace
{
    const char* const presetTag = "AcousticAnalyzerPreset";

    std::map<juce::String, float> defaultValues()
    {
        return { { params::weightBrightness, 25.0f }, { params::weightHarshness, 35.0f },
                 { params::weightDynamics, 20.0f }, { params::weightUnpredictability, 20.0f },
                 { params::calmAbove, 70.0f }, { params::neutralAbove, 40.0f },
                 { params::calibrationSpl, 100.0f },
                 { params::smoothingAttack, 100.0f }, { params::smoothingRelease, 400.0f } };
    }

    Preset makeBuiltIn(const juce::String& name, float brightness, float harshness, float dynamics, float unpredictability)
    {
        Preset preset;
        preset.name = name;
        preset.builtIn = true;
        preset.values = defaultValues();
        preset.values[params::weightBrightness] = brightness;
        preset.values[params::weightHarshness] = harshness;
        preset.values[params::weightDynamics] = dynamics;
        preset.values[params::weightUnpredictability] = unpredictability;
        return preset;
    }
}

PresetStore::PresetStore(juce::File directoryToUse)
    : directory(std::move(directoryToUse))
{
}

juce::File PresetStore::defaultDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("TrailblaiZ").getChildFile("AcousticAnalyzer").getChildFile("Presets");
}

std::vector<Preset> PresetStore::builtInPresets()
{
    return { makeBuiltIn("Default", 25.0f, 35.0f, 20.0f, 20.0f),
             makeBuiltIn("Equal weights", 25.0f, 25.0f, 25.0f, 25.0f),
             makeBuiltIn("Spectral only", 50.0f, 50.0f, 0.0f, 0.0f),
             makeBuiltIn("Temporal only", 0.0f, 0.0f, 50.0f, 50.0f) };
}

juce::File PresetStore::fileFor(const juce::String& name) const
{
    return directory.getChildFile(juce::File::createLegalFileName(name.trim()) + ".xml");
}

std::vector<Preset> PresetStore::list() const
{
    auto presets = builtInPresets();

    std::vector<Preset> saved;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.xml"))
    {
        auto xml = juce::XmlDocument::parse(file);

        if (xml == nullptr || !xml->hasTagName(presetTag))
            continue;

        Preset preset;
        preset.name = xml->getStringAttribute("name");
        if (preset.name.isEmpty())
            continue;

        for (auto* value : xml->getChildWithTagNameIterator("Value"))
            preset.values[value->getStringAttribute("id")] = static_cast<float>(value->getDoubleAttribute("value"));

        saved.push_back(std::move(preset));
    }

    std::sort(saved.begin(), saved.end(), [](const Preset& a, const Preset& b)
              { return a.name.compareIgnoreCase(b.name) < 0; });

    presets.insert(presets.end(), saved.begin(), saved.end());
    return presets;
}

bool PresetStore::save(const juce::String& name, const std::map<juce::String, float>& values) const
{
    const auto trimmed = name.trim();

    if (trimmed.isEmpty())
        return false;

    for (const auto& builtIn : builtInPresets())
        if (builtIn.name.equalsIgnoreCase(trimmed))
            return false;

    if (!directory.createDirectory().wasOk())
        return false;

    juce::XmlElement xml(presetTag);
    xml.setAttribute("name", trimmed);
    xml.setAttribute("version", 1);

    for (const auto& [id, value] : values)
    {
        auto* element = xml.createNewChildElement("Value");
        element->setAttribute("id", id);
        element->setAttribute("value", static_cast<double>(value));
    }

    return xml.writeTo(fileFor(trimmed));
}

bool PresetStore::remove(const juce::String& name) const
{
    for (const auto& builtIn : builtInPresets())
        if (builtIn.name.equalsIgnoreCase(name.trim()))
            return false;

    return fileFor(name).deleteFile();
}

const std::vector<const char*>& PresetStore::presetParameterIds()
{
    static const std::vector<const char*> ids{ params::weightBrightness, params::weightHarshness, params::weightDynamics,
                                               params::weightUnpredictability, params::calmAbove, params::neutralAbove,
                                               params::calibrationSpl, params::smoothingAttack, params::smoothingRelease };
    return ids;
}

std::map<juce::String, float> PresetStore::captureValues(juce::AudioProcessorValueTreeState& state)
{
    std::map<juce::String, float> values;

    for (const char* id : presetParameterIds())
        if (auto* value = state.getRawParameterValue(id))
            values[id] = value->load();

    return values;
}

void PresetStore::apply(juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    for (const auto& [id, value] : preset.values)
    {
        if (auto* parameter = state.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    }
}

bool PresetStore::matches(juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    // Half a step of the finest parameter (calibration moves in 0.5 dB steps)
    constexpr float tolerance = 0.26f;

    for (const auto& [id, value] : preset.values)
    {
        auto* current = state.getRawParameterValue(id);

        if (current == nullptr || std::abs(current->load() - value) > tolerance)
            return false;
    }

    return true;
}
