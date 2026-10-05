# Acoustic Analyzer v0.9.0-beta

**A real-time psychoacoustic analysis plugin (VST3 / AU / Standalone) for research and sound design.**

> **Experimental research tool.** Not a medical or clinical device, and it does not measure the brain.
> The Acoustic Activation Index has not been validated against listener outcomes yet (see [Known limits](#known-limits)).

Acoustic Analyzer listens to the audio passing through it and estimates how calming or stimulating the sound is,
using loudness, sharpness, roughness and level-variation measures built on 24 Bark critical bands.
The audio itself is never changed. You can record sessions, rate how calming the sound feels (keys 1 to 7),
and export the data for analysis.

## What's included

| File | Contents |
|---|---|
| `AcousticAnalyzer-0.9.0-beta-Windows.zip` | Windows 64-bit: VST3 and Standalone |
| `AcousticAnalyzer-0.9.0-beta-macOS.zip` | macOS universal (Apple Silicon + Intel): VST3, AU and Standalone, signed and notarised |
| `SHA256SUMS.txt` | Checksums to verify your download |

## Highlights

- **Acoustic Activation Index (0 to 100, high = calming)** with Calm / Neutral / Stimulating zones, each shown with colour, icon and label.
- **Psychoacoustic model:** Zwicker-style loudness (sone), sharpness (acum), simplified Daniel & Weber roughness, and a presence-region harshness measure, validated against MoSQITo.
- **History graph and table:** six series, 30 s / 1 min / 5 min windows, recording spans and rating events.
- **Bark spectrum:** specific loudness per critical band with peak hold.
- **Recording and ratings:** lock-free logging (no audio-thread locks), 1 to 7 listener ratings, live rank correlation with the index.
- **Export:** CSV and JSON (settings and calibration in the header) and an Audacity label track of rating events.
- **Settings:** presets, index weights, zone thresholds, SPL calibration, smoothing, auto-record, resizable UI (100/125/150%).
- **Analysis script:** `tools/analyze_ratings.py` for per-metric correlations and suggested weights (in the source repository).

## Installing

### Windows
1. Unzip.
2. Copy the `AcousticAnalyzer.vst3` **folder** to `C:\Program Files\Common Files\VST3\`.
3. Rescan plugins in your DAW. In Ableton Live: Preferences, Plug-ins, enable VST3 system folders, Rescan.
4. The Standalone (`AcousticAnalyzer.exe`) runs without installing.

> **Windows SmartScreen:** the Windows build is **not code-signed** yet. If Windows shows "Windows protected your PC",
> click **More info**, then **Run anyway**. You can confirm the file is the one published here by checking the SHA-256
> (see below).

### macOS
1. Unzip.
2. Copy `AcousticAnalyzer.vst3` to `/Library/Audio/Plug-Ins/VST3/` (or `~/Library/Audio/Plug-Ins/VST3/`).
3. Copy `AcousticAnalyzer.component` to `~/Library/Audio/Plug-Ins/Components/` (or `/Library/Audio/Plug-Ins/Components/`).
4. Rescan plugins in your DAW. `AcousticAnalyzer.app` is the Standalone.

The macOS build is signed with an Apple Developer ID and notarised, so Gatekeeper should open it without warnings.

### Verifying your download
Windows (PowerShell):
```powershell
Get-FileHash .\AcousticAnalyzer-0.9.0-beta-Windows.zip -Algorithm SHA256
```
macOS / Linux:
```bash
shasum -a 256 -c SHA256SUMS.txt
```
The value must match the line in `SHA256SUMS.txt`.

## Tested on

- Windows 11, Ableton Live 12 (VST3). <!-- edit to match what you actually tested -->
- pluginval strictness 5 (Linux VST3).
- macOS: <!-- fill in: macOS version, DAW and format you tested, or "not yet tested in a DAW" -->

## Known limits

- **The index is a research heuristic.** The four weights (35 / 25 / 20 / 20 by default) are adjustable starting values, not coefficients fitted to listener data.
- **Accuracy against reference software** (MoSQITo 1.2.1, 57 synthetic test signals at 48 kHz):

  | Measure | Rank correlation | Within 20% of reference |
  |---|---|---|
  | Sharpness | 0.97 | 100% |
  | Loudness | 0.99 | 64% (86% within 35%) |
  | Roughness | 0.96 | 58% (67% within 35%) |

- The plugin **orders sounds well, but absolute values are approximate.** Loudness reads about 1.5 to 2 times high on broadband noise,
  and roughness about 3 times high on broadband noise modulated at 70 Hz.
- Validation used **synthetic signals only**. Real-world music and ambience have not been measured against the reference.
- The model assumes **100 dB SPL at 0 dBFS RMS**. Change *Calibration* in Settings to match your playback level.
- Recorded data is held in memory and is **not saved with the DAW project**. Export before closing.
- Mono and stereo only.

## Feedback

This is a beta. Bug reports, odd readings and DAW compatibility notes are very welcome:
open an issue on GitHub, or get in touch via the portfolio page. Please include your OS, DAW, plugin format,
and (if you can) the exported CSV.

## Licence

Released under the AGPL-3.0 (JUCE is used under its AGPLv3 option). The source is in this repository.
