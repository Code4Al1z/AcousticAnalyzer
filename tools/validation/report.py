#!/usr/bin/env python3
"""Compare the plugin's readings with the MoSQITo reference.

Usage: python report.py SIGNAL_DIR PLUGIN_RESULTS.csv
"""
import csv
import json
import os
import sys

import numpy as np

directory, plugin_csv = sys.argv[1], sys.argv[2]
reference = json.load(open(os.path.join(directory, "mosqito_results.json")))
plugin = {r["name"]: r for r in csv.DictReader(open(plugin_csv))}
names = [n for n in reference if n in plugin]


def ratio(a, b, floor):
    return b / a if a > floor else np.nan


rows = []
print(f"{'signal':28s} | {'loudness (sone)':^24s} | {'sharpness (acum)':^24s} | {'roughness (asper)':^24s}")
print(f"{'':28s} | {'ISO':>7s} {'plugin':>7s} {'ratio':>6s} | {'DIN':>7s} {'plugin':>7s} {'ratio':>6s} | {'D&W':>7s} {'plugin':>7s} {'ratio':>6s}")
for name in names:
    ref_n, ref_s, ref_r = reference[name]
    p = plugin[name]
    pn, ps, pr = float(p["sone"]), float(p["acum"]), float(p["asper"])
    rn, rs, rr = ratio(ref_n, pn, 0.05), ratio(ref_s, ps, 0.05), ratio(ref_r, pr, 0.1)
    rows.append((rn, rs, rr))
    show = lambda v: f"{v:6.2f}" if not np.isnan(v) else "     -"
    print(f"{name:28s} | {ref_n:7.3f} {pn:7.3f} {show(rn)} | {ref_s:7.3f} {ps:7.3f} {show(rs)} | {ref_r:7.3f} {pr:7.3f} {show(rr)}")

def ranks(values):
    return np.argsort(np.argsort(values))


print("\nSummary, plugin / reference (signals where the reference is not tiny):")
for label, index, plugin_key in (("loudness", 0, "sone"), ("sharpness", 1, "acum"), ("roughness", 2, "asper")):
    r = np.array([row[index] for row in rows])
    keep = ~np.isnan(r)
    if keep.any():
        ref_values = np.array([reference[n][index] for n in names])[keep]
        plugin_values = np.array([float(plugin[n][plugin_key]) for n in names])[keep]
        rank = np.corrcoef(ranks(ref_values), ranks(plugin_values))[0, 1]
        r = r[keep]
        print(f"  {label:10s} n={len(r):2d}  median {np.median(r):.2f}  range {r.min():.2f}-{r.max():.2f}  "
              f"within 20%: {np.mean(np.abs(r - 1) < 0.2) * 100:3.0f}%  within 35%: {np.mean(np.abs(r - 1) < 0.35) * 100:3.0f}%  "
              f"rank correlation {rank:.2f}")
