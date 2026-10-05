#pragma once

#include "../Parameters.h"
#include "../Presets.h"
#include "Theme.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{
    // Overlay with the saved settings: presets, index weights, gauge zones, analysis,
    // workflow and display. Every slider is attached to a parameter, so changes apply
    // immediately and are stored with the project.
    class SettingsPanel : public juce::Component,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
    {
    public:
        // Called when the user picks a different interface size (1.0, 1.25 or 1.5)
        std::function<void(float)> onScaleChanged;

        // Called when the user picks how much history the graph shows (30, 60 or 300 seconds)
        std::function<void(int)> onHistoryWindowChanged;

        SettingsPanel(juce::AudioProcessorValueTreeState& stateToUse, PresetStore& storeToUse)
            : state(stateToUse), store(storeToUse)
        {
            setTitle("Settings");

            // Presets
            presetBox.setTitle("Preset");
            presetBox.setTooltip("A saved set of weights, zones, calibration and smoothing. "
                                 "'Custom' means the current settings match no preset.");
            presetBox.setHasFocusOutline(true);
            presetBox.onChange = [this]()
            {
                const int id = presetBox.getSelectedId();

                if (id > 0 && id <= static_cast<int>(presets.size()))
                    PresetStore::apply(state, presets[static_cast<size_t>(id) - 1]);
            };
            addAndMakeVisible(presetBox);

            savePresetButton.setTooltip("Save the current settings as a preset under a name you choose.");
            savePresetButton.setHasFocusOutline(true);
            savePresetButton.onClick = [this]() { askForPresetName(); };
            addAndMakeVisible(savePresetButton);

            deletePresetButton.setTooltip("Delete the selected preset (built-in presets can't be deleted).");
            deletePresetButton.setHasFocusOutline(true);
            deletePresetButton.onClick = [this]() { deleteSelectedPreset(); };
            addAndMakeVisible(deletePresetButton);

            // Sliders
            const juce::String weightTip = " Weights are normalised, so only their ratios matter.";

            addRow(weights, params::weightBrightness, "Brightness",
                   "How much brightness (sharpness) counts in the activation index." + weightTip);
            addRow(weights, params::weightHarshness, "Harshness",
                   "How much harshness (roughness and presence loudness) counts in the activation index." + weightTip);
            addRow(weights, params::weightDynamics, "Dynamic variability",
                   "How much the spread of the level over time counts in the activation index." + weightTip);
            addRow(weights, params::weightUnpredictability, "Temporal unpredictability",
                   "How much level changes between short windows count in the activation index." + weightTip);

            addRow(zones, params::calmAbove, "Calming above",
                   "Index values above this read as low activation (calming).");
            addRow(zones, params::neutralAbove, "Neutral above",
                   "Index values above this read as medium activation. Below it: high activation.");

            addRow(analysis, params::calibrationSpl, "Calibration",
                   "SPL, in dB, of a signal at 0 dBFS RMS. Sets the absolute scale for loudness and roughness. "
                   "Match it to your monitoring chain; 100 dB is a typical starting point.");
            addRow(analysis, params::smoothingAttack, "Smoothing attack",
                   "How quickly the meters follow a rise. Longer is calmer.");
            addRow(analysis, params::smoothingRelease, "Smoothing release",
                   "How quickly the meters follow a fall. Longer is calmer.");

            // Workflow
            autoRecordToggle.setTooltip("Start recording by itself when audio first arrives after the plugin is loaded "
                                        "or playback restarts. Stopping stays manual.");
            autoRecordToggle.setHasFocusOutline(true);
            autoRecordAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                state, params::autoRecord, autoRecordToggle);
            addAndMakeVisible(autoRecordToggle);

            // Display
            scaleLabel.setText("Interface size", juce::dontSendNotification);
            scaleLabel.setFont(font(14.0f));
            scaleLabel.setColour(juce::Label::textColourId, palette::textPrimary);
            addAndMakeVisible(scaleLabel);

            scaleBox.setTitle("Interface size");
            scaleBox.setTooltip("Scales the whole interface. Use a larger size if the text is hard to read.");
            scaleBox.setHasFocusOutline(true);
            scaleBox.addItem("100%", 1);
            scaleBox.addItem("125%", 2);
            scaleBox.addItem("150%", 3);
            scaleBox.setSelectedId(1, juce::dontSendNotification);
            scaleBox.onChange = [this]()
            {
                if (onScaleChanged)
                    onScaleChanged(scaleForId(scaleBox.getSelectedId()));
            };
            addAndMakeVisible(scaleBox);

            historyLabel.setText("History window", juce::dontSendNotification);
            historyLabel.setFont(font(14.0f));
            historyLabel.setColour(juce::Label::textColourId, palette::textPrimary);
            addAndMakeVisible(historyLabel);

            historyBox.setTitle("History window");
            historyBox.setTooltip("How much of the last 5 minutes the graph shows. The table always lists all of it.");
            historyBox.setHasFocusOutline(true);
            historyBox.addItem("30 seconds", 1);
            historyBox.addItem("1 minute", 2);
            historyBox.addItem("5 minutes", 3);
            historyBox.setSelectedId(2, juce::dontSendNotification);
            historyBox.onChange = [this]()
            {
                if (onHistoryWindowChanged)
                    onHistoryWindowChanged(secondsForId(historyBox.getSelectedId()));
            };
            addAndMakeVisible(historyBox);

            sharesLabel.setFont(font(12.0f));
            sharesLabel.setColour(juce::Label::textColourId, palette::textMuted);
            addAndMakeVisible(sharesLabel);

            noteLabel.setFont(font(12.0f));
            noteLabel.setColour(juce::Label::textColourId, palette::textMuted);
            noteLabel.setJustificationType(juce::Justification::topLeft);
            noteLabel.setText("Settings apply immediately, are saved with your project, and are written into the "
                              "header of exported CSV files.", juce::dontSendNotification);
            addAndMakeVisible(noteLabel);

            resetButton.setTooltip("Put every setting back to its default.");
            resetButton.setHasFocusOutline(true);
            resetButton.onClick = [this]() { resetToDefaults(); };
            addAndMakeVisible(resetButton);

            for (const char* id : PresetStore::presetParameterIds())
                state.addParameterListener(id, this);

            refreshPresets();
            handleAsyncUpdate();
        }

        ~SettingsPanel() override
        {
            cancelPendingUpdate();

            for (const char* id : PresetStore::presetParameterIds())
                state.removeParameterListener(id, this);
        }

        // Shows the stored interface size without calling onScaleChanged
        void setScale(float scale)
        {
            scaleBox.setSelectedId(idForScale(scale), juce::dontSendNotification);
        }

        // Shows the stored history window without calling onHistoryWindowChanged
        void setHistoryWindow(int seconds)
        {
            historyBox.setSelectedId(seconds <= 30 ? 1 : seconds <= 60 ? 2 : 3, juce::dontSendNotification);
        }

        void paint(juce::Graphics& g) override
        {
            drawPanel(g, getLocalBounds());

            g.setColour(palette::textMuted);
            g.setFont(font(11.0f, true));
            g.drawText("SETTINGS", 16, 8, getWidth() - 32, 16, juce::Justification::centredLeft);

            for (const auto& heading : headings)
                g.drawText(heading.text, heading.bounds, juce::Justification::centredLeft);
        }

        void resized() override
        {
            headings.clear();

            auto area = getLocalBounds().reduced(20, 8).withTrimmedTop(24);
            const int gap = 28;
            auto left = area.removeFromLeft((area.getWidth() - gap) / 2);
            area.removeFromLeft(gap);
            auto right = area;

            const int rowHeight = 28;
            const int headingHeight = 20;

            auto addHeading = [this](juce::Rectangle<int>& column, const char* title, int height)
            {
                headings.push_back({ title, column.removeFromTop(height) });
            };

            auto layoutRows = [&](juce::Rectangle<int>& column, const std::vector<Row*>& rows)
            {
                for (auto* row : rows)
                {
                    auto r = column.removeFromTop(rowHeight);
                    row->label.setBounds(r.removeFromLeft(juce::jmin(190, r.getWidth() / 2)));
                    row->slider.setBounds(r);
                }
            };

            // Left: preset, weights, zones
            addHeading(left, "PRESET", headingHeight);
            {
                auto r = left.removeFromTop(rowHeight + 2);
                deletePresetButton.setBounds(r.removeFromRight(64).reduced(0, 1));
                r.removeFromRight(6);
                savePresetButton.setBounds(r.removeFromRight(84).reduced(0, 1));
                r.removeFromRight(6);
                presetBox.setBounds(r.reduced(0, 1));
            }
            left.removeFromTop(6);

            addHeading(left, "INDEX WEIGHTS", headingHeight);
            layoutRows(left, weights);
            sharesLabel.setBounds(left.removeFromTop(22));
            left.removeFromTop(6);

            addHeading(left, "GAUGE ZONES", headingHeight);
            layoutRows(left, zones);

            // Right: analysis, workflow, display
            addHeading(right, "ANALYSIS", headingHeight);
            layoutRows(right, analysis);
            right.removeFromTop(6);

            addHeading(right, "WORKFLOW", headingHeight);
            autoRecordToggle.setBounds(right.removeFromTop(rowHeight));
            right.removeFromTop(6);

            addHeading(right, "DISPLAY", headingHeight);
            {
                auto r = right.removeFromTop(rowHeight);
                scaleLabel.setBounds(r.removeFromLeft(juce::jmin(190, r.getWidth() / 2)));
                scaleBox.setBounds(r.removeFromLeft(120).reduced(0, 1));
            }
            {
                auto r = right.removeFromTop(rowHeight);
                historyLabel.setBounds(r.removeFromLeft(juce::jmin(190, r.getWidth() / 2)));
                historyBox.setBounds(r.removeFromLeft(120).reduced(0, 1));
            }
            right.removeFromTop(6);

            noteLabel.setBounds(right.removeFromTop(34));
            right.removeFromTop(4);
            resetButton.setBounds(right.removeFromTop(30).removeFromLeft(160));
        }

    private:
        struct Row
        {
            juce::Label label;
            juce::Slider slider;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        };

        struct Heading
        {
            juce::String text;
            juce::Rectangle<int> bounds;
        };

        static int secondsForId(int id) { return id == 1 ? 30 : id == 3 ? 300 : 60; }
        static float scaleForId(int id) { return id == 3 ? 1.5f : id == 2 ? 1.25f : 1.0f; }
        static int idForScale(float scale) { return scale > 1.4f ? 3 : scale > 1.1f ? 2 : 1; }

        void addRow(std::vector<Row*>& section, const char* parameterId, const juce::String& name, const juce::String& tooltip)
        {
            auto row = std::make_unique<Row>();

            row->label.setText(name, juce::dontSendNotification);
            row->label.setFont(font(14.0f));
            row->label.setColour(juce::Label::textColourId, palette::textPrimary);
            row->label.setTooltip(tooltip);

            row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 24);
            row->slider.setTooltip(tooltip);
            row->slider.setTitle(name);
            row->slider.setHasFocusOutline(true);

            // Set explicitly: this panel is built before the editor installs the look and feel
            row->slider.setColour(juce::Slider::textBoxTextColourId, palette::textPrimary);
            row->slider.setColour(juce::Slider::textBoxBackgroundColourId, palette::surfaceRaised);
            row->slider.setColour(juce::Slider::textBoxOutlineColourId, palette::outline);
            row->slider.setColour(juce::Slider::textBoxHighlightColourId, palette::accent.withAlpha(0.4f));
            row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, row->slider);

            addAndMakeVisible(row->label);
            addAndMakeVisible(row->slider);

            section.push_back(row.get());
            rows.push_back(std::move(row));
        }

        // Parameters can change from anywhere (sliders, presets, reset, host state)
        void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }

        void handleAsyncUpdate() override
        {
            updateShares();
            updatePresetSelection();
        }

        void updateShares()
        {
            float total = 0.0f;
            for (auto* row : weights)
                total += static_cast<float>(row->slider.getValue());

            juce::StringArray shares;
            for (auto* row : weights)
                shares.add(juce::String(total > 0.0f ? juce::roundToInt(100.0f * static_cast<float>(row->slider.getValue()) / total) : 25) + "%");

            sharesLabel.setText("Effective shares of the index:  " + shares.joinIntoString("  /  "), juce::dontSendNotification);
        }

        void refreshPresets()
        {
            presets = store.list();
            presetBox.clear(juce::dontSendNotification);

            bool separatorAdded = false;
            for (size_t i = 0; i < presets.size(); ++i)
            {
                if (!presets[i].builtIn && !separatorAdded)
                {
                    presetBox.addSeparator();
                    separatorAdded = true;
                }

                presetBox.addItem(presets[i].name, static_cast<int>(i) + 1);
            }

            updatePresetSelection();
        }

        void updatePresetSelection()
        {
            int matched = -1;
            for (size_t i = 0; i < presets.size(); ++i)
            {
                if (PresetStore::matches(state, presets[i]))
                {
                    matched = static_cast<int>(i);
                    break;
                }
            }

            if (matched >= 0)
            {
                presetBox.setSelectedId(matched + 1, juce::dontSendNotification);
            }
            else
            {
                presetBox.setSelectedId(0, juce::dontSendNotification);
                presetBox.setText("Custom", juce::dontSendNotification);
            }

            deletePresetButton.setEnabled(matched >= 0 && !presets[static_cast<size_t>(matched)].builtIn);
        }

        void askForPresetName()
        {
            auto* window = new juce::AlertWindow("Save preset", "Name for the current settings:",
                                                 juce::MessageBoxIconType::NoIcon, this);
            window->addTextEditor("name", {});
            window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            juce::Component::SafePointer<SettingsPanel> safeThis(this);
            juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);

            window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, safeWindow](int result)
            {
                if (result != 1 || safeThis == nullptr || safeWindow == nullptr)
                    return;

                const auto name = safeWindow->getTextEditorContents("name").trim();

                if (!safeThis->store.save(name, PresetStore::captureValues(safeThis->state)))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset not saved",
                        "The name must not be empty or the name of a built-in preset, and the preset folder must be writable.");
                    return;
                }

                safeThis->refreshPresets();
            }), true);
        }

        void deleteSelectedPreset()
        {
            const int id = presetBox.getSelectedId();

            if (id > 0 && id <= static_cast<int>(presets.size()) && !presets[static_cast<size_t>(id) - 1].builtIn)
            {
                store.remove(presets[static_cast<size_t>(id) - 1].name);
                refreshPresets();
            }
        }

        void resetToDefaults()
        {
            for (const char* id : PresetStore::presetParameterIds())
            {
                if (auto* parameter = state.getParameter(id))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->getDefaultValue());
                    parameter->endChangeGesture();
                }
            }
        }

        juce::AudioProcessorValueTreeState& state;
        PresetStore& store;

        std::vector<std::unique_ptr<Row>> rows;
        std::vector<Row*> weights, zones, analysis;
        std::vector<Heading> headings;
        std::vector<Preset> presets;

        juce::ComboBox presetBox;
        juce::TextButton savePresetButton{ "Save as..." }, deletePresetButton{ "Delete" };
        juce::ToggleButton autoRecordToggle{ "Start recording automatically when audio first arrives" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoRecordAttachment;
        juce::Label scaleLabel;
        juce::ComboBox scaleBox, historyBox;
        juce::Label historyLabel;
        juce::Label sharesLabel, noteLabel;
        juce::TextButton resetButton{ "Reset to defaults" };
    };
}
