# Psychoacoustic validation

The loudness (sone), sharpness (acum) and roughness (asper) in the plugin are checked against
[MoSQITo](https://github.com/Eomys/MoSQITo), an open-source implementation of ISO 532-1 loudness,
DIN 45692 sharpness and Daniel and Weber roughness. Run this after any change to the analysis in
`src/PluginProcessor.cpp`.

```
pip install numpy scipy matplotlib mosqito

cmake -B build -DACOUSTIC_BUILD_VALIDATION=ON
cmake --build build --config Release --target ValidationHarness

python tools/validation/gen_signals.py signals          # 57 calibrated test signals
python tools/validation/reference_mosqito.py signals    # the reference values (about 2 minutes)
build/ValidationHarness_artefacts/Release/ValidationHarness signals plugin.csv
python tools/validation/report.py signals plugin.csv
```

(The harness path varies with the generator; on Windows it is
`build\ValidationHarness_artefacts\Release\ValidationHarness.exe`.)

The test signals are tones, noise, band-limited noise and amplitude-modulated tones at known
sound pressure levels. The plugin assumes 100 dB SPL at 0 dBFS RMS (its `calibrationSpl`
setting), and the signals are scaled to match.

## What the numbers looked like when this was written

Plugin / reference, over the 57 signals:

| | n | median | within 20% | within 35% | rank correlation | notes |
|---|---|---|---|---|---|---|
| sharpness | 57 | 0.95 | 100% | 100% | 0.97 | |
| loudness | 56 | 1.00 | 64% | 86% | 0.99 | reads high for broadband noise (1.5 to 2 times), for 2 kHz tones, and low around 250 Hz |
| roughness | 24 | 1.09 | 58% | 67% | 0.96 | only signals the reference rates above 0.1 asper; broadband noise modulated at 70 Hz reads about 3 times high |

The roughness curve against modulation frequency (10-300 Hz), depth, carrier (250 Hz to 8 kHz) and
level is mostly within 25% of the reference; steady noise reads 0.04 to 0.09 asper where the
reference gives about 0.02.

## Things this found

* **JUCE's `WindowingFunction` normalises by default**, which doubles a Hann window's amplitude.
  Every SPL-based reading was 6 dB too high. It is constructed with `normalise = false` now. A
  quick way to see this class of bug again: a 1 kHz tone at 40 dB SPL should read close to 1 sone.
* The loudness scale is fitted for the smallest overall error against ISO 532-1, not anchored on
  the 1 kHz / 40 dB tone, so that tone reads 0.8 sone. Anchoring on it made noise read about
  twice as loud as the standard says.

## What was tried and not kept

* Replacing the loudness formula with the ISO 532-1 constants (0.0635, exponent 0.25), or the
  threshold in quiet with ISO 226 equal-loudness offsets: no better overall.
* Fitting a sensitivity value per Bark band to the reference: better on the signals it was fitted
  on, worse on signals left out of the fit (leave-one-group-out), so it was overfitting.
* A shorter audibility ramp for roughness: worse at low levels.
