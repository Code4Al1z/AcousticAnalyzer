# Acoustic Analyzer

A real-time analyser for psychoacoustic research, built with JUCE (VST3 / Standalone, plus AU on macOS).
It listens to the audio passing through it, estimates how **calming or stimulating** the sound is, and
lets you record, rate and export the results for analysis.

The plugin **never changes the audio**. It is a meter, not an effect. Put it on any track or on the master.

> **Status: experimental research tool.** The numbers are calibrated against reference implementations
> (see [Accuracy](#accuracy)) but they are not a certified measurement and not a clinical or
> therapeutic instrument.

<!-- IMAGE 1 (hero)
     Full plugin window at its default size, playing something with a clear character
     (e.g. calm ambient pad). Gauge in the "calm" zone, graph has ~1 minute of history,
     Bark spectrum visible. Capture at 100% scale on a dark DAW background.
     Save as docs/images/hero.png -->
![Plugin overview](docs/images/hero.png)

---

## Contents

1. [What it measures](#what-it-measures)
2. [Installing and building](#installing-and-building)
3. [The interface](#the-interface)
4. [Settings](#settings)
5. [Recording, rating and exporting](#recording-rating-and-exporting)
6. [Analysing a study](#analysing-a-study)
7. [Keyboard shortcuts](#keyboard-shortcuts)
8. [Accuracy](#accuracy)
9. [Project layout](#project-layout)
10. [Credits and licence](#credits-and-licence)

---

## What it measures

Every second the plugin analyses the last 2048 samples (50% overlap) and computes:

| Metric | What it means | Based on |
|---|---|---|
| **Loudness** (sone) | How loud it *feels*, not just the level | Zwicker loudness on 24 Bark bands, Terhardt threshold in quiet |
| **Brightness** | Where the spectral weight sits (dull to sharp) | Sharpness (von Bismarck / Zwicker, acum) |
| **Harshness** | Rough, rasping or piercing content | Roughness (simplified Daniel & Weber) combined with loudness in the 2 to 5.3 kHz presence region |
| **Dynamic variability** | How much the level moves over the last 5 s | Spread of 50 ms RMS levels in dB |
| **Temporal unpredictability** | How surprising the level changes are | Jump size between successive 50 ms windows |
| **Stereo width** | Mono (0), unrelated channels (0.5), opposite polarity (1) | Side / (mid + side) energy |
| **Level** (dBFS) | Plain RMS level | RMS |

### The Acoustic Activation Index (0 to 100)

The gauge shows one number: a weighted average of `(1 - metric) x 100` over four metrics
(brightness, harshness, dynamic variability, temporal unpredictability).

- **High = calming**, low = stimulating.
- The default weights are 25 / 35 / 20 / 20 and can be changed in [Settings](#settings).
- Loudness, level and stereo width are shown but are **not** part of the index.
- At digital silence the gauge shows *No signal* and holds its values instead of drifting.

Zones, by default: **70 and above = Calm**, **40 to 69 = Neutral**, **below 40 = Stimulating**.
Each zone has its own colour, icon *and* label, so it never relies on colour alone.

---

## Installing and building

### Requirements

- CMake 3.22 or newer
- A C++17 compiler (MSVC 2022, Xcode, GCC or Clang)
- On Linux: the usual JUCE dev packages (ALSA, X11, freetype, fontconfig)
- JUCE 8.0.8. If `modules/JUCE` is missing, CMake downloads it automatically.

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows with Visual Studio:

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The VST3 bundle is built to `build/AcousticAnalyzer_artefacts/Release/VST3/AcousticAnalyzer.vst3`.
Copy it to your VST3 folder yourself (`C:\Program Files\Common Files\VST3` on Windows). The standalone
app is in `build/AcousticAnalyzer_artefacts/Release/Standalone/`.

| CMake option | Default | Effect |
|---|---|---|
| `ACOUSTIC_JUCE_TAG` | `8.0.8` | JUCE version used by the automatic download |
| `ACOUSTIC_BUILD_VALIDATION` | `OFF` | Also builds the validation harness (see [Accuracy](#accuracy)) |
| `AAX_SDK_PATH` | unset | If set, an AAX build is added (needs Avid's SDK) |

### Using it in Ableton Live

1. Rescan plugins (Preferences, Plug-ins, *Rescan*) and enable VST3 folders.
2. Drop **AcousticAnalyzer** (TrailblaiZ) on an audio track or the master.
3. The analysis works on whatever is playing on that track *at that point of the chain*.

The plugin has no automatable parameters on purpose: it is a measuring instrument and its settings
should not change during a recording.

---

## The interface

<!-- IMAGE 2 (annotated interface)
     The same window as the hero shot, but with numbered callouts (1 to 6) added in an
     image editor: 1 gauge, 2 metric bars, 3 history graph, 4 Bark spectrum, 5 footer
     buttons, 6 rating strip. Callout style: small circles in the brand cyan. -->
![Annotated interface](docs/images/interface-annotated.png)

| Area | What you see |
|---|---|
| **Gauge** (top left) | The index as an arc and a number, the zone label and icon. An *EXPERIMENTAL* chip reminds you this is a research tool. |
| **Metric panel** (top right) | Four bars (brightness, harshness, dynamics, unpredictability) plus readouts for loudness, level and stereo width. |
| **History** (middle) | A graph or a table of the recent past. Switch with the **Graph / Table** buttons. |
| **Bark spectrum** (bottom) | 24 critical-band bars with peak hold. The 2 to 5.3 kHz presence region is marked. |
| **Footer** | Record, Export, Settings, Clear and a two-line status. |

### History graph

<!-- IMAGE 3 (history graph)
     Close-up of the history panel with all six lines visible, a recording span shaded,
     two or three rating dots, and the hover tooltip open on one point.
     Capture at 125% UI scale so the lines are readable. -->
![History graph](docs/images/history-graph.png)

- Six series: **index** (cyan), **brightness** (orange), **harshness** (violet), **dynamics**,
  **unpredictability** and **listener rating** (yellow dots).
- Click a legend chip to hide or show a series. The chips are keyboard-focusable.
- Hover for a tooltip with the exact values at that time.
- Shaded spans mark when you were recording; dots mark rating events.
- The window shows 30 s, 1 min or 5 min (set in Settings).

### History table

<!-- IMAGE 4 (history table)
     The history panel switched to Table view with a few rows, including at least one
     row that has a rating. -->
![History table](docs/images/history-table.png)

The same data as rows. Each row is announced in full by screen readers
(for example "12 seconds, index 74, calm").

### Bark spectrum

<!-- IMAGE 5 (Bark spectrum)
     Close-up of the Bark panel while playing broadband noise or music, so most bars are
     active, with the presence region label visible. -->
![Bark spectrum](docs/images/bark-spectrum.png)

The bars show *specific loudness* per critical band, not raw FFT magnitude. That is why low and
very high frequencies look smaller than on an ordinary analyser: the ear is less sensitive there.

### Resizing

Drag the window corner to resize (820 x 700 up to 1800 x 1300). The size is saved with the project.

---

## Settings

Click **Settings** in the footer. Nothing here changes the audio. Settings are saved in the DAW
project, are written into the header of every CSV/JSON export, and are **not** host-automatable.

<!-- IMAGE 6 (settings overlay)
     The Settings overlay fully open, with the default values, showing every section
     from Preset down to Reset to defaults. -->
![Settings panel](docs/images/settings.png)

| Setting | Default | What it does | What it affects |
|---|---|---|---|
| **Preset** | Default | Loads a saved set of weights. Built-ins: *Default*, *Equal weights*, *Spectral only*, *Temporal only*. You can save your own. | All four weights at once |
| **Weights** (brightness, harshness, dynamics, unpredictability) | 25 / 35 / 20 / 20 | How much each metric counts toward the index. They are normalised, so only the ratios matter. All zero means equal weights. | The gauge number, the index line, exports and the rating comparison. **Not** the four metric bars. |
| **Calm above** | 70 | Index at which the gauge turns *Calm* | Gauge label, icon and colour only |
| **Neutral above** | 40 | Index at which the gauge turns *Neutral* (below it is *Stimulating*) | Gauge label, icon and colour only |
| **Calibration** (dB SPL at 0 dBFS RMS) | 100 | Tells the model how loud your playback system is. Lower it if you listen quietly. | Loudness, roughness audibility and the presence part of harshness, therefore the index. Not level, stereo width or the dynamics metrics. |
| **Smoothing attack / release** | 100 / 400 ms | How fast the meters rise and fall | Steadiness of the displays and the logged values |
| **Auto-record** | Off | Starts a recording when sound begins and stops it after silence | The recording log |
| **Interface size** | 100% | 100, 125 or 150% | Window scale only |
| **History window** | 1 min | 30 s, 1 min or 5 min of graph | Graph only, not the log |
| **Reset to defaults** | | Resets the nine settings above | Does not touch your recorded log or window size |

### Preset files

Your own presets are small XML files in:

- Windows: `%APPDATA%\TrailblaiZ\AcousticAnalyzer\Presets`
- macOS: `~/Library/Application Support/TrailblaiZ/AcousticAnalyzer/Presets`
- Linux: `~/.config/TrailblaiZ/AcousticAnalyzer/Presets`

You can share them with collaborators by copying the files.

### Choosing a calibration value

The model needs to know how loud the audio is *in the room*. If your system plays a full-scale
sine at about 100 dB SPL, keep the default. If you use studio monitors at a lower level, measure with
an SPL meter and enter the measured value for 0 dBFS RMS. For headphone-based studies, treat the
loudness values as relative, not absolute.

---

## Recording, rating and exporting

1. Press **Record** (or enable *Auto-record*). The status line shows time and number of points.
2. While recording, rate how calming the sound feels **right now** with the rating strip or keys
   **1 to 7** (1 = not at all, 7 = very calming).
3. Press **Record** again to stop, then **Export**.

<!-- IMAGE 7 (recording in progress)
     Window while recording: the Record button in its active state, status line showing
     elapsed time and a rating count with "rho" visible, rating strip with one value
     highlighted. -->
![Recording with ratings](docs/images/recording.png)

The log is stored without locks on the audio thread, so recording cannot cause dropouts.
It is held in memory (not saved with the project), so **export before closing**.
**Clear** empties it (you are asked to confirm if it holds data).

### Export formats

Choose the format by the file extension in the save dialog.

| Extension | Use |
|---|---|
| `.csv` | Spreadsheets, R, Python. Starts with `#` metadata lines (settings, calibration, plugin version, ratings summary). |
| `.json` | Scripts and web tools. Valid JSON, same columns. |
| `.txt` (label track) | Rating events only, as Audacity labels (File, Import, Labels): each rating appears on the waveform with the index at that moment. |

CSV columns end with `... Roughness_Asper, Stereo_Width, Listener_Rating, Rating_Event`.

### Ratings summary

When you have at least three ratings, the status line shows **rho**: the Spearman rank correlation
between your ratings and the plugin's mean index over the 5 s before each rating. A high positive
value means the index tracks how calming you found the sound. It is shown in the export header too.

---

## Analysing a study

`tools/analyze_ratings.py` takes exported CSVs and reports:

- Spearman correlation of each metric with the ratings
- the correlation of the index itself
- suggested weights (non-negative least squares) with bootstrap 90% intervals
- a leave-one-out comparison against the current weights

```bash
python tools/analyze_ratings.py session1.csv session2.csv
```

It refuses to fit with fewer than 10 ratings and prints cautions. Treat the suggested weights as a
starting point for your next study, not as a result.

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| **1 to 7** (also number pad) | Submit a rating, while recording |
| **Tab / Shift+Tab** | Move focus; every control has a visible focus outline |
| **Space / Enter** | Activate the focused button or legend chip |

Some DAWs capture keys before plugins see them. If 1 to 7 do nothing in your host, click inside the
plugin first or use the rating buttons.

---

## Accuracy

The metrics were checked against [MoSQITo](https://github.com/Eomys/MoSQITo) 1.2.1
(ISO 532-1 loudness, DIN 45692 sharpness, Daniel & Weber roughness) using 57 calibrated test signals
at 48 kHz. Plugin value divided by reference value:

| Metric | Median ratio | Within 20% | Rank correlation | Known limits |
|---|---|---|---|---|
| Sharpness | 0.95 | 100% | 0.97 | none significant |
| Loudness | 1.00 | 64% (86% within 35%) | 0.99 | reads about 1.5 to 2 x high on broadband noise and 2 kHz tones, low near 250 Hz |
| Roughness (reference above 0.1 asper) | 1.09 | 58% (67% within 35%) | 0.96 | broadband noise modulated at 70 Hz reads about 3 x high |

The plugin is therefore good at *ranking* sounds and at relative comparison, and approximate in
absolute values. Only synthetic signals were used for the validation.

To re-run it yourself, see `tools/validation/README.md`.

---

## Project layout

```
CMakeLists.txt          build, JUCE download, formats
src/
  PluginProcessor.*     DSP: FFT, Bark model, roughness, metrics, lock-free log
  PluginEditor.*        window, layout, keyboard, export
  LogExporter.*         CSV / JSON / label track, rating statistics
  Presets.*             built-in and user presets
  Parameters.h          parameter IDs (never rename: they are stored in projects)
  ui/                   theme and components (gauge, graph, table, Bark view, settings)
tools/
  analyze_ratings.py    study analysis
  validation/           MoSQITo comparison pipeline and C++ harness
docs/images/            README screenshots
```

---

## Credits and licence

Built with [JUCE](https://juce.com) 8. Reference implementations used for validation:
[MoSQITo](https://github.com/Eomys/MoSQITo). Colour palette by TrailblaiZ.

Add your licence here before publishing. Note that JUCE itself is dual-licensed (AGPLv3 or a
commercial licence), which applies to anything you distribute built with it.
