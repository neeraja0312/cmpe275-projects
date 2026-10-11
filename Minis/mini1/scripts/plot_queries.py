#!/usr/bin/env python3
"""Plot search (Q1/Q2/Q3) timings from a query CSV such as results/a-queries-full.csv.

Usage:
    python3 scripts/plot_queries.py [queries.csv] [output-prefix]

Defaults: results/a-queries-full.csv and prefix "queries" (files are written to
results/<prefix>_q1.png, results/<prefix>_q2.png, results/<prefix>_q3.png).

CSV columns (written by results/run_baselines.sh):
    query,args,mode,query_s_min,query_s_mean,results,pollutant_rows,selectivity_pct,footprint_bytes
Bars show the minimum over the in-process repeats; Phase 2 CSVs in the same
format can be plotted the same way (use a different output prefix).
Requires matplotlib.
"""

import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt

RESULTS = Path(__file__).resolve().parent.parent / "results"


def read(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["min"] = float(r["query_s_min"])
        r["mean"] = float(r["query_s_mean"])
    return rows


def find(rows, query, mode):
    for r in rows:
        if r["query"] == query and r["mode"] == mode:
            return r
    return None


def finish(fig, ax, title, ylabel, out, log=False):
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def label(value, unit):
    return f"{value:.3g} {unit}"


def plot_q1(rows, out):
    groups = [("q1-3h", "3 rows (3 hours)"), ("q1-year", "8,676 rows (one year)")]
    modes = ["view", "copy", "scan"]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    width = 0.25
    for j, mode in enumerate(modes):
        xs, ys = [], []
        for i, (q, _) in enumerate(groups):
            r = find(rows, q, mode)
            if r:
                xs.append(i + (j - 1) * width)
                ys.append(r["min"] * 1e9)  # nanoseconds
        bars = ax.bar(xs, ys, width, label=mode)
        for b, y in zip(bars, ys):
            text = label(y, "ns") if y < 1e3 else (label(y / 1e3, "µs") if y < 1e6 else label(y / 1e6, "ms"))
            ax.annotate(text,
                        (b.get_x() + b.get_width() / 2, y), ha="center", va="bottom", fontsize=8)
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels([g[1] for g in groups])
    ax.legend(title="mode")
    finish(fig, ax, "Q1: one monitor, time range (min of 10 repeats, log scale)", "nanoseconds", out, log=True)


def plot_q2(rows, out):
    groups = [
        ("q2-no2-gt100ppb", "NO2 > 100 ppb\n38 rows"),
        ("q2-ozone-gt0.070", "ozone > 0.070 ppm\n281k rows (0.6%)"),
        ("q2-ozone-gt0.030", "ozone > 0.030 ppm\n25.3M rows (53.8%)"),
        ("q2-ozone-onehour", "one hour, any value\n1,205 rows (time-only)"),
    ]
    modes = ["count", "copy", "callback", "virtual"]
    fig, ax = plt.subplots(figsize=(10, 5))
    width = 0.2
    for j, mode in enumerate(modes):
        xs, ys = [], []
        for i, (q, _) in enumerate(groups):
            r = find(rows, q, mode)
            if r:
                xs.append(i + (j - 1.5) * width)
                ys.append(r["min"] * 1e3)  # milliseconds
        bars = ax.bar(xs, ys, width, label=mode)
        for b, y in zip(bars, ys):
            ax.annotate(f"{y:.3g}", (b.get_x() + b.get_width() / 2, y), ha="center", va="bottom", fontsize=8)
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels([g[1] for g in groups])
    ax.legend(title="result mode")
    finish(fig, ax, "Q2: value range, by result mode (min of 10 repeats)", "milliseconds", out)


def plot_q3(rows, out):
    names = [("q3-ozone-2024-none", "none"), ("q3-ozone-2024-hour", "by hour of day"),
             ("q3-ozone-2024-monitor", "by monitor")]
    fig, ax = plt.subplots(figsize=(6, 4.2))
    xs, ys = [], []
    for q, n in names:
        r = next((r for r in rows if r["query"] == q), None)
        if r:
            xs.append(n)
            ys.append(r["min"] * 1e3)
    bars = ax.bar(xs, ys, color=["#1f77b4", "#2ca02c", "#d62728"][: len(xs)])
    for b, y in zip(bars, ys):
        ax.annotate(f"{y:.3g} ms", (b.get_x() + b.get_width() / 2, y), ha="center", va="bottom", fontsize=9)
    finish(fig, ax, "Q3: count/min/max/mean, ozone 2024 (9.0M rows)", "milliseconds", out)


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else RESULTS / "a-queries-full.csv"
    prefix = sys.argv[2] if len(sys.argv) > 2 else "queries"
    if not path.exists():
        sys.exit(f"no such file: {path}")
    rows = read(path)
    plot_q1(rows, RESULTS / f"{prefix}_q1.png")
    plot_q2(rows, RESULTS / f"{prefix}_q2.png")
    plot_q3(rows, RESULTS / f"{prefix}_q3.png")


if __name__ == "__main__":
    main()
