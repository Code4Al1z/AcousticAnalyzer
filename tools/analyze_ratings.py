#!/usr/bin/env python3
"""Compare the activation index with listener ratings from exported CSV files.

Usage:
    python analyze_ratings.py session1.csv [session2.csv ...] [--window 5] [--save pairs.csv]

Each rating (a row with Rating_Event = 1) is paired with the mean of every metric over the
`--window` seconds before it, because ratings are retrospective. The script then reports:

  * how each metric on its own correlates with the ratings (Spearman rank correlation),
  * how well the index as recorded tracks the ratings,
  * a suggested set of index weights (non-negative least squares) with bootstrap intervals,
  * a leave-one-out check of whether those weights beat the ones in force.

Ratings are "how calming does this sound", 1 (not at all) to 7 (very), and the index is high for
calm sound, so a positive correlation means agreement.

Needs numpy, pandas and scipy:  pip install numpy pandas scipy

Read the warnings it prints. A handful of ratings from one listener in one place can't validate
or tune an index; this is a way to look at the data, not proof of anything.
"""
import argparse
import re
import sys

try:
    import numpy as np
    import pandas as pd
    from scipy import stats
    from scipy.optimize import nnls
except ImportError as error:  # pragma: no cover
    sys.exit(f"Missing package: {error.name}. Install with: pip install numpy pandas scipy")

# The four metrics that make up the index, as columns, and the plugin's default weights
COMPONENTS = ["Brightness", "Harshness", "Dynamic_Variability", "Temporal_Unpredictability"]
MIN_RATINGS_TO_FIT = 10
EXTRA = ["Activation_Score", "Loudness_Sone", "Sharpness_Acum", "Roughness_Asper", "Stereo_Width"]


def read_export(path):
    """Returns (data frame, weights in force as a dict or None)."""
    weights = None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.startswith("#"):
                break
            match = re.match(r"#\s*index_weights_percent:\s*(.*)", line)
            if match:
                weights = {k: float(v) for k, v in re.findall(r"(\w+)=([\d.]+)", match.group(1))}
    frame = pd.read_csv(path, comment="#")
    missing = [c for c in ["Timestamp_Seconds", "Rating_Event"] + COMPONENTS if c not in frame.columns]
    if missing:
        sys.exit(f"{path}: missing columns {missing}. Is this an export from this plugin?")
    return frame, weights


def pair_ratings(frame, window, session):
    """One row per rating: the rating and the mean of each metric over the window before it."""
    columns = [c for c in COMPONENTS + EXTRA if c in frame.columns]
    rows = []
    for index in np.flatnonzero(frame["Rating_Event"].to_numpy() == 1):
        now = frame["Timestamp_Seconds"].iloc[index]
        span = frame[(frame["Timestamp_Seconds"] <= now) & (frame["Timestamp_Seconds"] >= now - window)]
        row = {"session": session, "time": now, "rating": frame["Listener_Rating"].iloc[index]}
        row.update({c: span[c].mean() for c in columns})
        rows.append(row)
    return pd.DataFrame(rows)


def calm_components(pairs):
    """The index uses (1 - metric) for each component, so build those columns."""
    return np.column_stack([1.0 - pairs[c].to_numpy() for c in COMPONENTS])


def fit_weights(components, ratings):
    """Non-negative weights (summing to 100) so the weighted components track the ratings."""
    standardise = lambda x: (x - x.mean(axis=0)) / x.std(axis=0)
    if (components.std(axis=0) == 0).any() or ratings.std() == 0:
        return None
    coefficients, _ = nnls(standardise(components), standardise(ratings))
    if coefficients.sum() <= 0:
        return None
    return 100.0 * coefficients / coefficients.sum()


def spearman(a, b):
    """Spearman rho, or nan when either series is (numerically) constant."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    if len(a) < 3 or np.ptp(a) < 1e-9 or np.ptp(b) < 1e-9:
        return np.nan
    return stats.spearmanr(a, b)[0]


def show(value):
    return "  n/a" if np.isnan(value) else f"{value:+.2f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", help="CSV exports from the plugin")
    parser.add_argument("--window", type=float, default=5.0, help="seconds before each rating to average over (default 5)")
    parser.add_argument("--save", help="write the paired table to this CSV")
    parser.add_argument("--bootstrap", type=int, default=1000, help="bootstrap resamples (default 1000)")
    args = parser.parse_args()

    tables, forced = [], []
    for number, path in enumerate(args.files):
        frame, weights = read_export(path)
        pairs = pair_ratings(frame, args.window, number)
        print(f"{path}: {len(pairs)} ratings")
        tables.append(pairs)
        forced.append(weights)
    pairs = pd.concat(tables, ignore_index=True).dropna(subset=COMPONENTS + ["rating"])
    n = len(pairs)
    if args.save:
        pairs.to_csv(args.save, index=False)
    print(f"\n{n} ratings, {pairs['session'].nunique()} session(s); each paired with the mean of the {args.window:g} s before it\n")
    if n < 3:
        sys.exit("Need at least 3 ratings to compare anything.")

    ratings = pairs["rating"].to_numpy(float)
    print("Spearman correlation with the rating (rating = how calming; + means more calming):")
    for column in [c for c in COMPONENTS + EXTRA if c in pairs.columns]:
        print(f"  {column:28s} {show(spearman(pairs[column].to_numpy(float), ratings))}")
    if "Activation_Score" in pairs.columns:
        print(f"\nThe index as recorded: rho = {show(spearman(pairs['Activation_Score'].to_numpy(float), ratings))}")
    if len({tuple(w.items()) if w else None for w in forced}) > 1:
        print("  Note: the sessions were recorded with different index weights.")

    components = calm_components(pairs)
    weights = fit_weights(components, ratings) if n >= MIN_RATINGS_TO_FIT else None
    if n < MIN_RATINGS_TO_FIT:
        print(f"\nNo weights fitted: {n} ratings is too few (need at least {MIN_RATINGS_TO_FIT}, and many more to trust them).")
    elif weights is None:
        print("\nNo weights fitted: a component or the ratings did not vary.")
    else:
        rng = np.random.default_rng(0)
        draws = []
        for _ in range(args.bootstrap):
            pick = rng.integers(0, n, n)
            fitted = fit_weights(components[pick], ratings[pick])
            if fitted is not None:
                draws.append(fitted)
        draws = np.array(draws)
        print("\nSuggested weights for the index (percent), with 90% bootstrap intervals:")
        for k, name in enumerate(COMPONENTS):
            low, high = np.percentile(draws[:, k], [5, 95]) if len(draws) else (np.nan, np.nan)
            print(f"  {name:28s} {weights[k]:5.1f}   [{low:5.1f} - {high:5.1f}]")

        # Leave-one-out: does a fit made without a rating predict it better than chance / the defaults?
        defaults = np.array([25.0, 35.0, 20.0, 20.0])
        predicted = np.full(n, np.nan)
        for i in range(n):
            keep = np.arange(n) != i
            fitted = fit_weights(components[keep], ratings[keep])
            if fitted is not None:
                predicted[i] = components[i] @ fitted / 100.0
        usable = ~np.isnan(predicted)
        print(f"\nLeave-one-out rho with fitted weights : {show(spearman(predicted[usable], ratings[usable]))}")
        print(f"rho with the default weights          : {show(spearman(components @ defaults, ratings))}")

    print("\nCaution:")
    if n < 30:
        print(f"  - Only {n} ratings. Under about 30 these numbers move a lot with one more rating.")
    if pairs["session"].nunique() < 3:
        print("  - Few sessions. Different listeners, places and sounds are needed before weights mean anything.")
    print("  - Ratings close in time share most of their data, so the effective sample is smaller than n.")
    print("  - Weights fitted to one person's ratings describe that person, not 'people'.")
    print("  - The interval is a bootstrap over ratings, not over listeners.")


if __name__ == "__main__":
    main()
