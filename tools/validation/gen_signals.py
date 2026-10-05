#!/usr/bin/env python3
"""Write calibrated test signals for the psychoacoustic validation.

Usage: python gen_signals.py OUT_DIR

For every signal this writes OUT_DIR/<name>.npy (pascals, for the reference implementation) and
OUT_DIR/<name>.bin (float32, scaled for the plugin: 2 Pa RMS = 100 dB SPL = plugin level 1.0, the
plugin's default calibration), plus manifest.json / manifest.txt. 48 kHz, 6 seconds each.
"""
import json
import os
import sys

import numpy as np

FS = 48000
DUR = 6.0
N = int(FS * DUR)
t = np.arange(N) / FS
P0 = 20e-6  # 0 dB SPL

rng = np.random.default_rng(1234)


def scale(x, spl):
    return x * (P0 * 10 ** (spl / 20) / np.sqrt(np.mean(x ** 2)))


def white():
    return rng.standard_normal(N)


def pink():
    spectrum = np.fft.rfft(rng.standard_normal(N))
    f = np.fft.rfftfreq(N, 1 / FS)
    f[0] = f[1]
    return np.fft.irfft(spectrum / np.sqrt(f), n=N)


def band(fc, bw):
    spectrum = np.fft.rfft(rng.standard_normal(N))
    f = np.fft.rfftfreq(N, 1 / FS)
    spectrum[(f < fc - bw / 2) | (f > fc + bw / 2)] = 0
    return np.fft.irfft(spectrum, n=N)


def tone(f):
    return np.sin(2 * np.pi * f * t)


def am(fc, fm, depth):
    return (1 + depth * np.sin(2 * np.pi * fm * t)) * np.sin(2 * np.pi * fc * t)


signals = {}
for spl in (20, 30, 40, 50, 60, 70, 80, 90):
    signals[f"tone_1000_{spl}"] = scale(tone(1000), spl)
for f in (100, 250, 500, 2000, 4000, 8000):
    for spl in (40, 60):
        signals[f"tone_{f}_{spl}"] = scale(tone(f), spl)
for spl in (40, 60, 80):
    signals[f"white_{spl}"] = scale(white(), spl)
    signals[f"pink_{spl}"] = scale(pink(), spl)
signals["band1000_160_60"] = scale(band(1000, 160), 60)
signals["band250_100_60"] = scale(band(250, 100), 60)
signals["band4000_700_60"] = scale(band(4000, 700), 60)
for fm in (10, 20, 30, 50, 70, 100, 150, 200, 300):
    signals[f"am_1000_fm{fm}_d100_60"] = scale(am(1000, fm, 1.0), 60)
for depth in (25, 50, 75):
    signals[f"am_1000_fm70_d{depth}_60"] = scale(am(1000, 70, depth / 100), 60)
for fc in (250, 350, 450, 500, 2000, 4000, 8000):
    signals[f"am_{fc}_fm70_d100_60"] = scale(am(fc, 70, 1.0), 60)
for spl in (30, 40, 80):
    signals[f"am_1000_fm70_d100_{spl}"] = scale(am(1000, 70, 1.0), spl)
signals["white_20"] = scale(white(), 20)
signals["pink_20"] = scale(pink(), 20)
signals["whiteAM70_60"] = scale(white() * (1 + np.sin(2 * np.pi * 70 * t)), 60)
signals["pinkAM70_60"] = scale(pink() * (1 + np.sin(2 * np.pi * 70 * t)), 60)
signals["two_tones_1000_1070_60"] = scale(tone(1000) + tone(1070), 60)
signals["two_tones_1000_1500_60"] = scale(tone(1000) + tone(1500), 60)

out = sys.argv[1] if len(sys.argv) > 1 else "signals"
os.makedirs(out, exist_ok=True)
for name, x in signals.items():
    np.save(os.path.join(out, f"{name}.npy"), x)
    (x / 2.0).astype(np.float32).tofile(os.path.join(out, f"{name}.bin"))
json.dump({"fs": FS, "names": list(signals)}, open(os.path.join(out, "manifest.json"), "w"))
open(os.path.join(out, "manifest.txt"), "w").write("\n".join(signals) + "\n")
print(f"{len(signals)} signals written to {out}")
