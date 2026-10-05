#!/usr/bin/env python3
"""Compute the reference values with MoSQITo (pip install mosqito matplotlib).

Usage: python reference_mosqito.py SIGNAL_DIR

Reads SIGNAL_DIR/<name>.npy and writes SIGNAL_DIR/mosqito_results.json (loudness, sharpness, roughness)
and mosqito_bands.json (specific loudness per Bark). Loudness: ISO 532-1 stationary (Zwicker),
sharpness: DIN 45692, roughness: Daniel and Weber.
"""
import json
import os
import sys
import warnings

import numpy as np

warnings.filterwarnings("ignore")
from mosqito.sq_metrics import loudness_zwst, roughness_dw, sharpness_din_st

directory = sys.argv[1] if len(sys.argv) > 1 else "signals"
manifest = json.load(open(os.path.join(directory, "manifest.json")))
fs, names = manifest["fs"], manifest["names"]
results, bands = {}, {}
for name in names:
    x = np.load(os.path.join(directory, f"{name}.npy"))
    loudness, specific, bark = loudness_zwst(x, fs, field_type="free")
    sharpness = sharpness_din_st(x, fs, weighting="din", field_type="free")
    roughness, _, _, _ = roughness_dw(x, fs, overlap=0.5)
    results[name] = (float(loudness), float(sharpness), float(np.mean(roughness)))
    specific, bark = np.asarray(specific).ravel(), np.asarray(bark).ravel()
    bands[name] = [float(np.sum(specific[(bark > b) & (bark <= b + 1)]) * 0.1) for b in range(24)]
json.dump(results, open(os.path.join(directory, "mosqito_results.json"), "w"))
json.dump(bands, open(os.path.join(directory, "mosqito_bands.json"), "w"))
print(f"reference computed for {len(results)} signals")
