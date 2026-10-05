// Feeds calibrated test signals through the real processor and writes its steady-state readings.
//
//   ValidationHarness SIGNAL_DIR OUT.csv
//
// SIGNAL_DIR holds manifest.txt (one signal name per line) and <name>.bin (float32 mono at 48 kHz,
// scaled so that 2 Pa RMS = 100 dB SPL = plugin level 1.0). See gen_signals.py.
#include "PluginProcessor.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: ValidationHarness SIGNAL_DIR OUT.csv\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI init;
    const std::string directory = argv[1];
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    std::ifstream manifest(directory + "/manifest.txt");
    std::ofstream out(argv[2]);
    out << "name,sone,acum,asper,brightness,harshness,dynamics,unpredictability,score,rms\n";

    std::string name;
    while (std::getline(manifest, name))
    {
        if (name.empty())
            continue;

        std::ifstream file(directory + "/" + name + ".bin", std::ios::binary | std::ios::ate);
        if (!file)
        {
            std::printf("missing %s\n", name.c_str());
            continue;
        }

        std::vector<float> samples(static_cast<size_t>(file.tellg()) / sizeof(float));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(float)));

        AudioPluginAudioProcessor processor;
        processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
        processor.prepareToPlay(sampleRate, blockSize);

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;

        double sone = 0, acum = 0, asper = 0, brightness = 0, harshness = 0, dynamics = 0, unpredictability = 0, score = 0, rms = 0;
        int probes = 0;
        size_t position = 0, nextProbe = static_cast<size_t>(0.1 * sampleRate);

        while (position + blockSize <= samples.size())
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = samples[position + static_cast<size_t>(i)];
                buffer.setSample(0, i, s);
                buffer.setSample(1, i, s);
            }

            processor.processBlock(buffer, midi);
            position += blockSize;

            if (position >= nextProbe)
            {
                nextProbe += static_cast<size_t>(0.1 * sampleRate);

                if (position > static_cast<size_t>(3.0 * sampleRate)) // Average the steady state after 3 s
                {
                    sone += processor.getLoudnessSones();
                    acum += processor.getSharpnessAcum();
                    asper += processor.getRoughnessAsper();
                    brightness += processor.getSpectralCentroid();
                    harshness += processor.getSpectralHarshness();
                    dynamics += processor.getDynamicVariability();
                    unpredictability += processor.getTemporalUnpredictability();
                    score += processor.getAcousticActivationScore();
                    rms += processor.getRMSLevel();
                    ++probes;
                }
            }
        }

        const double n = juce::jmax(1, probes);
        out << name << "," << sone / n << "," << acum / n << "," << asper / n << "," << brightness / n << ","
            << harshness / n << "," << dynamics / n << "," << unpredictability / n << "," << score / n << "," << rms / n << "\n";
        std::printf("%s\n", name.c_str());
    }

    return 0;
}
