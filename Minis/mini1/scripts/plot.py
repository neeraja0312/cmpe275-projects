#!/usr/bin/env python3
"""Summarize and plot benchmark CSVs written by run_bench.sh.

Usage:
    python3 scripts/plot.py                      # every results/*.csv
    python3 scripts/plot.py a-serial b-omp-8     # only these labels

Prints mean / stddev / min / max per label and writes
results/summary_time.png and results/summary_rss.png.
Requires matplotlib (pip install matplotlib).
"""

import csv
import statistics
import sys
from pathlib import Path

import matplotlib.pyplot as plt

RESULTS = Path(__file__).resolve().parent.parent / "results"


def load(label):
    with open(RESULTS / f"{label}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    wall = [float(r["wall_s"]) for r in rows]
    # Prefer the program-reported footprint (correct on macOS); older CSVs
    # without that column fall back to /usr/bin/time max RSS.
    col = "footprint_bytes" if rows and rows[0].get("footprint_bytes") else "max_rss_bytes"
    rss_mb = [float(r[col]) / (1024 * 1024) for r in rows]
    return wall, rss_mb


def stats(xs):
    sd = statistics.stdev(xs) if len(xs) > 1 else 0.0
    return statistics.mean(xs), sd, min(xs), max(xs)


def bar(labels, values, title, ylabel, out):
    means = [stats(v)[0] for v in values]
    sds = [stats(v)[1] for v in values]
    fig, ax = plt.subplots(figsize=(max(6, 1.2 * len(labels)), 4))
    ax.bar(labels, means, yerr=sds, capsize=4)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    ax.tick_params(axis="x", rotation=30)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def main():
    labels = sys.argv[1:] or sorted(p.stem for p in RESULTS.glob("*.csv"))
    if not labels:
        sys.exit(f"no CSVs in {RESULTS}")

    walls, rsses = [], []
    print(f"{'label':<24}{'n':>4}{'mean s':>10}{'sd':>9}{'min':>9}{'max':>9}{'rss MB':>10}")
    for label in labels:
        wall, rss = load(label)
        walls.append(wall)
        rsses.append(rss)
        m, sd, lo, hi = stats(wall)
        print(f"{label:<24}{len(wall):>4}{m:>10.3f}{sd:>9.3f}{lo:>9.3f}{hi:>9.3f}{stats(rss)[0]:>10.1f}")

    bar(labels, walls, "Wall time (mean ± sd)", "seconds", RESULTS / "summary_time.png")
    bar(labels, rsses, "Peak memory (mean ± sd)", "MB", RESULTS / "summary_rss.png")


if __name__ == "__main__":
    main()
