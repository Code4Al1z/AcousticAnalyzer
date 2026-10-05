#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Metric tooltips: what each number means
    brightnessBar.setTooltip("Sharpness (Zwicker / von Bismarck): how much of the loudness sits at high frequencies. "
                             "0 to 4 acum maps to 0 to 100%.");
    harshnessBar.setTooltip("Half roughness (fast amplitude modulation around 70 Hz, in asper), half the loudness "
                            "in the 2 to 5 kHz region where hearing is most sensitive.");
    dynamicsBar.setTooltip("Spread of the level (in dB) over the last 5 seconds. 12 dB or more reads 100%.");
    unpredictabilityBar.setTooltip("Average level change between 50 ms windows over the last 5 seconds. "
                                   "6 dB or more reads 100%.");
    loudnessStat.setTooltip("Zwicker loudness in sone. Assumes 100 dB SPL at 0 dBFS RMS, so treat it as relative "
                            "unless your monitoring is calibrated to that.");
    levelStat.setTooltip("RMS level of the input, averaged over the analysed channels.");
    widthStat.setTooltip("How much of the energy is not common to both channels: 0% = identical channels (mono), "
                         "about 50% = unrelated channels, 100% = opposite polarity. Not part of the index. "
                         "Shown as - for a mono input.");

    spectrum.setBandLayout(AudioPluginAudioProcessor::barkEdgesHz, AudioPluginAudioProcessor::barkCentresHz);
    spectrum.setPresenceBands(AudioPluginAudioProcessor::presenceFirstBand, AudioPluginAudioProcessor::presenceLastBand);

    recordButton.setTooltip("Start or stop logging the analysis (about 40 points per second) for CSV export.");
    recordButton.onClick = [this]()
        {
            if (processor.isCurrentlyLogging())
                processor.stopLogging();
            else
                processor.startLogging();

            updateRecordingControls();
        };

    exportButton.setEnabled(false);
    exportButton.setTooltip("Save the recorded data as CSV or JSON (the settings in force are written with it), or the listener ratings as an Audacity label track (.txt).");
    exportButton.onClick = [this]() { startExport(); };

    clearButton.setEnabled(false);
    clearButton.setTooltip("Forget the recorded data and clear the graph. Not available while recording.");
    clearButton.onClick = [this]() { confirmClear(); };

    graphButton.setClickingTogglesState(true);
    graphButton.setRadioGroupId(101);
    graphButton.setToggleState(true, juce::dontSendNotification);
    graphButton.setTooltip("Show the history as a graph.");
    graphButton.onClick = [this]() { setHistoryView(false); };

    tableButton.setClickingTogglesState(true);
    tableButton.setRadioGroupId(101);
    tableButton.setTooltip("Show the same history as a table of numbers (newest first).");
    tableButton.onClick = [this]() { setHistoryView(true); };

    ratingStrip.onRating = [this](int rating) { processor.submitRating(rating); };

    for (juce::Button* b : { static_cast<juce::Button*>(&recordButton), static_cast<juce::Button*>(&exportButton),
                             static_cast<juce::Button*>(&settingsButton), static_cast<juce::Button*>(&clearButton),
                             static_cast<juce::Button*>(&graphButton), static_cast<juce::Button*>(&tableButton) })
        b->setHasFocusOutline(true);

    statusLabel.setMinimumHorizontalScale(1.0f);
    statusLabel.setTooltip("Ratings: how many you entered. rho: Spearman rank correlation between your ratings and the "
                           "activation index (mean over the 5 seconds before each rating). Positive means they agree.");

    settingsButton.setClickingTogglesState(true);
    settingsButton.setTooltip("Index weights, gauge zones, calibration and smoothing.");
    settingsButton.onClick = [this]() { showSettings(settingsButton.getToggleState()); };
    settingsPanel.setVisible(false);

    calmAboveValue = processor.apvts.getRawParameterValue(params::calmAbove);
    neutralAboveValue = processor.apvts.getRawParameterValue(params::neutralAbove);

    statusLabel.setFont(ui::font(13.0f));
    statusLabel.setColour(juce::Label::textColourId, ui::palette::textSecondary);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    for (juce::Component* c : std::initializer_list<juce::Component*>{ &gauge, &brightnessBar, &harshnessBar, &dynamicsBar,
             &unpredictabilityBar, &loudnessStat, &levelStat, &widthStat, &history, &spectrum, &recordButton, &exportButton,
             &settingsButton, &clearButton, &statusLabel, &historyTable, &ratingStrip, &graphButton, &tableButton })
        addAndMakeVisible(c);

    historyTable.setVisible(false);
    setWantsKeyboardFocus(true);

    addChildComponent(settingsPanel); // On top of the graph and band view, hidden until Settings is pressed

    // Restore what was saved with the project: window size and hidden graph series
    setResizable(true, true);
    setResizeLimits(820, 700, 1800, 1300);

    const auto savedUi = getUiState();
    setSize(juce::jlimit(820, 1800, static_cast<int>(savedUi.getProperty("width", 940))),
            juce::jlimit(700, 1300, static_cast<int>(savedUi.getProperty("height", 760))));

    const int allSeries = (1 << ui::HistoryGraph::numSeries) - 1;
    const int seriesMask = static_cast<int>(savedUi.getProperty("seriesMask", allSeries));
    for (int s = 0; s < ui::HistoryGraph::numSeries; ++s)
        history.setSeriesVisible(static_cast<ui::HistoryGraph::SeriesId>(s), (seriesMask >> s) & 1);

    history.onSeriesVisibilityChanged = [this]()
    {
        int mask = 0;
        for (int s = 0; s < ui::HistoryGraph::numSeries; ++s)
            if (history.isSeriesVisible(static_cast<ui::HistoryGraph::SeriesId>(s)))
                mask |= 1 << s;

        getUiState().setProperty("seriesMask", mask, nullptr);
    };

    // History window, interface size and history view
    const int savedWindow = static_cast<int>(savedUi.getProperty("historyWindow", 60));
    history.setWindowSeconds(savedWindow);
    settingsPanel.setHistoryWindow(history.getWindowSeconds());
    settingsPanel.onHistoryWindowChanged = [this](int seconds)
    {
        history.setWindowSeconds(seconds);
        getUiState().setProperty("historyWindow", history.getWindowSeconds(), nullptr);
        repaint();
    };

    const float savedScale = static_cast<float>(savedUi.getProperty("uiScale", 1.0f));
    settingsPanel.setScale(savedScale);
    settingsPanel.onScaleChanged = [this](float scale) { applyScale(scale); };
    if (savedScale > 1.01f)
        setScaleFactor(savedScale);

    if (savedUi.getProperty("historyView", "graph").toString() == "table")
    {
        tableMode = true;
        graphButton.setToggleState(false, juce::dontSendNotification);
        tableButton.setToggleState(true, juce::dontSendNotification);
        updateVisibility();
    }

    uiStateRestored = true;

    updateRecordingControls();
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::palette::window);

    // Header: a bar in the brand gradient (orange to pink), then the title
    const auto accentBar = juce::Rectangle<float>(static_cast<float>(headerArea.getX()), static_cast<float>(headerArea.getCentreY()) - 13.0f,
                                                  5.0f, 26.0f);
    g.setGradientFill(juce::ColourGradient(ui::palette::brandOrange, accentBar.getX(), accentBar.getY(),
                                           ui::palette::brandPink, accentBar.getX(), accentBar.getBottom(), false));
    g.fillRoundedRectangle(accentBar, 2.5f);

    g.setColour(ui::palette::textPrimary);
    g.setFont(ui::font(20.0f, true));
    g.drawText("Acoustic Analyzer", headerArea.withTrimmedLeft(16), juce::Justification::centredLeft);

    const auto betaFont = ui::font(11.0f, true);
    const auto betaWidth = static_cast<float>(ui::textWidth(betaFont, "BETA v0.1")) + 18.0f;
    const auto beta = juce::Rectangle<float>(betaWidth, 22.0f)
                          .withRightX(static_cast<float>(headerArea.getRight()))
                          .withCentre({ static_cast<float>(headerArea.getRight()) - betaWidth * 0.5f, static_cast<float>(headerArea.getCentreY()) });
    g.setColour(ui::palette::outline);
    g.drawRoundedRectangle(beta.reduced(0.5f), 11.0f, 1.0f);
    g.setColour(ui::palette::textSecondary);
    g.setFont(betaFont);
    g.drawText("BETA v0.1", beta, juce::Justification::centred);

    // Panels and their titles
    const int window = history.getWindowSeconds();
    const juce::String historyTitle = "HISTORY - LAST " + (window >= 120 ? juce::String(window / 60) + " MINUTES"
                                                                         : juce::String(window) + " SECONDS");

    struct Titled { juce::Rectangle<int> area; juce::String title; };
    for (const auto& panel : { Titled{ gaugePanel, {} },
                               Titled{ metricsPanel, "PERCEPTUAL METRICS" },
                               Titled{ historyPanel, historyTitle },
                               Titled{ spectrumPanel, "LOUDNESS PER CRITICAL BAND (BARK SCALE)" } })
    {
        ui::drawPanel(g, panel.area);

        if (panel.title.isNotEmpty())
        {
            g.setColour(ui::palette::textMuted);
            g.setFont(ui::font(11.0f, true));
            g.drawText(panel.title, panel.area.getX() + 16, panel.area.getY() + 10, panel.area.getWidth() - 32, 16,
                       juce::Justification::centredLeft);
        }
    }

    g.setColour(ui::palette::grid);
    g.fillRect(juce::Rectangle<float>(static_cast<float>(loudnessStat.getX()) - 10.0f, static_cast<float>(metricsPanel.getY() + 40),
                                      1.0f, static_cast<float>(metricsPanel.getHeight() - 52)));

    // The index is a work in progress: say so on the gauge and in the footer
    const auto chip = juce::Rectangle<float>(static_cast<float>(gaugePanel.getX()) + 12.0f,
                                             static_cast<float>(gaugePanel.getY()) + 8.0f, 98.0f, 18.0f);
    g.setColour(ui::palette::outline);
    g.drawRoundedRectangle(chip.reduced(0.5f), 9.0f, 1.0f);
    g.setColour(ui::palette::textSecondary);
    g.setFont(ui::font(10.0f, true));
    g.drawText("EXPERIMENTAL", chip, juce::Justification::centred);

    g.setColour(ui::palette::textMuted);
    g.setFont(ui::font(11.0f));
    g.drawText("Experimental index: the weights are research estimates, not yet validated against listener ratings.",
               disclaimerArea, juce::Justification::centred);
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    const int gap = 12;

    headerArea = area.removeFromTop(36);
    area.removeFromTop(gap);

    disclaimerArea = area.removeFromBottom(16);
    area.removeFromBottom(6);

    auto footer = area.removeFromBottom(36);
    area.removeFromBottom(gap);

    // Top row: gauge and metrics
    auto topRow = area.removeFromTop(juce::jlimit(176, 230, juce::roundToInt(static_cast<float>(area.getHeight()) * 0.27f)));
    area.removeFromTop(gap);

    gaugePanel = topRow.removeFromLeft(juce::jlimit(250, 340, juce::roundToInt(static_cast<float>(topRow.getWidth()) * 0.32f)));
    topRow.removeFromLeft(gap);
    metricsPanel = topRow;

    // Below: history graph and band display
    historyPanel = area.removeFromTop(juce::roundToInt(static_cast<float>(area.getHeight() - gap) * 0.55f));
    area.removeFromTop(gap);
    spectrumPanel = area;

    // Children
    gauge.setBounds(gaugePanel.withTrimmedTop(26).reduced(10, 6)); // Leaves a strip for the "experimental" chip

    // Metrics: four bars on the left, the raw readouts stacked on the right
    auto metrics = metricsPanel.reduced(16, 0).withTrimmedTop(34).withTrimmedBottom(10);
    auto stats = metrics.removeFromRight(150);
    metrics.removeFromRight(20);

    const int barHeight = metrics.getHeight() / 4;
    for (auto* bar : { &brightnessBar, &harshnessBar, &dynamicsBar, &unpredictabilityBar })
        bar->setBounds(metrics.removeFromTop(barHeight).reduced(0, 3));

    const int statHeight = stats.getHeight() / 3;
    loudnessStat.setBounds(stats.removeFromTop(statHeight));
    levelStat.setBounds(stats.removeFromTop(statHeight));
    widthStat.setBounds(stats);

    history.setBounds(historyPanel.withTrimmedTop(30).reduced(10, 6));
    spectrum.setBounds(spectrumPanel.withTrimmedTop(30).reduced(10, 6));

    recordButton.setBounds(footer.removeFromLeft(130));
    footer.removeFromLeft(gap);
    exportButton.setBounds(footer.removeFromLeft(130));
    footer.removeFromLeft(gap);
    settingsButton.setBounds(footer.removeFromLeft(110));
    footer.removeFromLeft(gap);
    clearButton.setBounds(footer.removeFromLeft(80));
    footer.removeFromLeft(16);
    statusLabel.setBounds(footer);

    // History panel title row: rating strip on the left of the Graph / Table switch
    auto titleRow = juce::Rectangle<int>(historyPanel.getX() + 10, historyPanel.getY() + 4, historyPanel.getWidth() - 20, 24);
    tableButton.setBounds(titleRow.removeFromRight(64));
    titleRow.removeFromRight(4);
    graphButton.setBounds(titleRow.removeFromRight(64));
    titleRow.removeFromRight(16);
    ratingStrip.setBounds(titleRow.removeFromRight(ui::RatingStrip::preferredWidth));

    historyTable.setBounds(historyPanel.withTrimmedTop(32).reduced(10, 6));
    settingsPanel.setBounds(historyPanel.getUnion(spectrumPanel));

    if (uiStateRestored)
    {
        auto savedUi = getUiState();
        savedUi.setProperty("width", getWidth(), nullptr);
        savedUi.setProperty("height", getHeight(), nullptr);
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::timerCallback()
{
    const float score = processor.getAcousticActivationScore();
    const float brightness = processor.getSpectralCentroid();
    const float harshness = processor.getSpectralHarshness();
    const float dynamics = processor.getDynamicVariability();
    const float unpredictability = processor.getTemporalUnpredictability();

    gauge.setThresholds(calmAboveValue->load(), neutralAboveValue->load());
    gauge.setScore(score);
    gauge.setNoSignal(!processor.isSignalPresent());
    brightnessBar.setValue(brightness, juce::String(processor.getSharpnessAcum(), 2) + " acum");
    harshnessBar.setValue(harshness, juce::String(processor.getRoughnessAsper(), 2) + " asper");
    dynamicsBar.setValue(dynamics, {});
    unpredictabilityBar.setValue(unpredictability, {});

    loudnessStat.setValue(juce::String(processor.getLoudnessSones(), 1));

    const float rmsDb = juce::Decibels::gainToDecibels(processor.getRMSLevel(), -100.0f);
    levelStat.setValue(rmsDb <= -99.9f ? juce::String("-inf") : juce::String(rmsDb, 1));
    widthStat.setValue(processor.getTotalNumInputChannels() < 2 ? juce::String("-")
                                                                : juce::String(juce::roundToInt(processor.getStereoWidth() * 100.0f)));

    std::array<float, ui::BarkSpectrumView::numBands> bands{};
    processor.getSpecificLoudness(bands);
    spectrum.update(bands);

    // The graph keeps 10 samples per second. The rating is mapped from 1-7 onto 0-100
    // (negative: none yet).
    const int rating = processor.getCurrentRating();
    ratingStrip.setState(processor.isCurrentlyLogging(), rating);

    if (++timerTicks % 3 == 0)
    {
        history.push({ score, brightness * 100.0f, harshness * 100.0f, dynamics * 100.0f, unpredictability * 100.0f,
                       rating > 0 ? static_cast<float>(rating - 1) / 6.0f * 100.0f : -1.0f },
                     processor.isCurrentlyLogging());

        if (tableMode && historyTable.isVisible())
            historyTable.refresh();
    }

    // Compare ratings with the index about once a second, when a new rating has arrived
    if (timerTicks % 30 == 0 && processor.getRatingCount() != lastRatingCount)
        updateRatingSummary();

    updateRecordingControls();
}

void AudioPluginAudioProcessorEditor::updateRecordingControls()
{
    const bool logging = processor.isCurrentlyLogging();
    const int points = processor.getDataPointCount();

    // When a recording ends, take a final look at the ratings
    if (wasLogging && !logging)
        updateRatingSummary();
    wasLogging = logging;

    recordButton.setToggleState(logging, juce::dontSendNotification);
    exportButton.setEnabled(!logging && points > 0);
    clearButton.setEnabled(!logging && points > 0);

    juce::String status;
    if (logging)
        status = "Recording  " + formatTime(processor.getRecordingTime()) + "   -   " + juce::String(points) + " points";
    else if (points > 0)
        status = "Ready to export  -  " + juce::String(points) + " points";
    else
        status = "Not recording";

    if (processor.getDroppedPointCount() > 0)
        status += "   -   " + juce::String(processor.getDroppedPointCount()) + " dropped";

    // Second line: the listener ratings and how they compare with the index
    if (processor.getRatingCount() > 0 || ratingSummary.numRatings > 0)
    {
        const int numRatings = juce::jmax(processor.getRatingCount(), ratingSummary.numRatings);
        status += "\n" + juce::String(numRatings) + (numRatings == 1 ? " rating" : " ratings");

        if (ratingSummary.hasCorrelation)
            status += "   -   rho " + juce::String(ratingSummary.spearman, 2) + " vs index";
        else if (numRatings < 3)
            status += "   -   3 needed to compare";
    }

    statusLabel.setText(status, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, logging ? ui::palette::textPrimary : ui::palette::textSecondary);
}

void AudioPluginAudioProcessorEditor::showSettings(bool show)
{
    settingsPanel.setVisible(show);
    settingsButton.setToggleState(show, juce::dontSendNotification);
    updateVisibility();
}

void AudioPluginAudioProcessorEditor::setHistoryView(bool showTable)
{
    tableMode = showTable;
    graphButton.setToggleState(!showTable, juce::dontSendNotification);
    tableButton.setToggleState(showTable, juce::dontSendNotification);
    getUiState().setProperty("historyView", showTable ? "table" : "graph", nullptr);

    if (showTable)
        historyTable.refresh();

    updateVisibility();
}

// What is on screen below the top row: the settings overlay, or the history (graph
// or table) and the band display
void AudioPluginAudioProcessorEditor::updateVisibility()
{
    const bool settings = settingsPanel.isVisible();

    history.setVisible(!settings && !tableMode);
    historyTable.setVisible(!settings && tableMode);
    ratingStrip.setVisible(!settings);
    graphButton.setVisible(!settings);
    tableButton.setVisible(!settings);
    spectrum.setVisible(!settings);
}

void AudioPluginAudioProcessorEditor::applyScale(float scale)
{
    setScaleFactor(scale);
    getUiState().setProperty("uiScale", scale, nullptr);
}

void AudioPluginAudioProcessorEditor::updateRatingSummary()
{
    lastRatingCount = processor.getRatingCount();
    ratingSummary = LogExporter::summariseRatings(processor.getLogSnapshot());
}

void AudioPluginAudioProcessorEditor::confirmClear()
{
    if (processor.getDataPointCount() == 0)
        return;

    juce::Component::SafePointer<AudioPluginAudioProcessorEditor> safeThis(this);

    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Clear recorded data?")
                                     .withMessage("This forgets the " + juce::String(processor.getDataPointCount())
                                                  + " recorded points. Export them first if you need them.")
                                     .withButton("Clear")
                                     .withButton("Cancel")
                                     .withAssociatedComponent(this),
                                 [safeThis](int result)
                                 {
                                     if (result != 1 || safeThis == nullptr)
                                         return;

                                     safeThis->processor.clearLog();
                                     safeThis->history.clear();
                                     safeThis->ratingSummary = {};
                                     safeThis->lastRatingCount = 0;
                                     safeThis->updateRecordingControls();
                                 });
}

bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    // Digits 1-7 rate the sound. Some hosts deliver no text character, so also accept the
    // key code, and the number pad.
    int digit = 0;
    const auto character = key.getTextCharacter();
    const int keyCode = key.getKeyCode();

    if (character >= '1' && character <= '7')
        digit = character - '0';
    else if (keyCode >= '1' && keyCode <= '7')
        digit = keyCode - '0';
    else if (keyCode >= juce::KeyPress::numberPad1 && keyCode <= juce::KeyPress::numberPad7)
        digit = keyCode - juce::KeyPress::numberPad0;

    if (digit == 0)
        return false;

    processor.submitRating(digit);
    return true;
}

juce::ValueTree AudioPluginAudioProcessorEditor::getUiState() const
{
    return processor.apvts.state.getOrCreateChildWithName("ui", nullptr);
}

void AudioPluginAudioProcessorEditor::startExport()
{
    auto points = processor.getLogSnapshot();

    if (points.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "No data",
                                               "There is nothing to export yet. Record some audio first.");
        return;
    }

    const auto metadata = processor.getLogMetadata();
    const auto exportedAt = juce::Time::getCurrentTime();
    const int numDropped = processor.getDroppedPointCount();

    // Start in the folder used last time, with a time-stamped name
    juce::File folder(getUiState().getProperty("lastExportDir", {}).toString());
    if (!folder.isDirectory())
        folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    const auto fileName = "acoustic_data_" + exportedAt.formatted("%Y-%m-%d_%H%M%S") + ".csv";

    // The file type follows the extension: .csv (default), .json, or .txt for an
    // Audacity label track of the listener ratings
    fileChooser = std::make_unique<juce::FileChooser>("Save recorded data (.csv, .json, or .txt for rating labels)",
                                                      folder.getChildFile(fileName), "*.csv;*.json;*.txt");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<AudioPluginAudioProcessorEditor> safeThis(this);

    fileChooser->launchAsync(flags, [safeThis, points = std::move(points), metadata, exportedAt, numDropped](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();

        if (file == juce::File{})
            return; // Cancelled

        const auto extension = file.getFileExtension().toLowerCase();
        juce::String content;

        if (extension == ".json")
            content = LogExporter::buildJson(points, metadata, exportedAt);
        else if (extension == ".txt")
            content = LogExporter::buildLabelTrack(points);
        else
        {
            if (extension.isEmpty())
                file = file.withFileExtension(".csv");

            content = LogExporter::buildCsv(points, metadata, exportedAt);
        }

        if (extension == ".txt" && content.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "No ratings to label",
                                                   "The label track holds one label per listener rating, and this recording has none. "
                                                   "Save as .csv or .json to export the data.");
            return;
        }

        if (file.replaceWithText(content))
        {
            if (safeThis != nullptr)
                safeThis->getUiState().setProperty("lastExportDir", file.getParentDirectory().getFullPathName(), nullptr);

            juce::String message = "Saved to:\n" + file.getFullPathName() + "\n\nPoints: " + juce::String(static_cast<int>(points.size()));
            if (numDropped > 0)
                message += "\nDropped (buffer overflow): " + juce::String(numDropped);

            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export successful", message);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                   "Could not write the file. Check that the folder is writable.");
        }
    });
}

juce::String AudioPluginAudioProcessorEditor::formatTime(double seconds)
{
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int tenths = static_cast<int>((seconds - static_cast<int>(seconds)) * 10);

    return juce::String::formatted("%02d:%02d.%01d", mins, secs, tenths);
}
