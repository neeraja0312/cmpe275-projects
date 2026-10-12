#!/usr/bin/env python3
"""One figure for the report: Phase 1 full-dataset load time and peak memory per reader.

Usage (from Minis/mini1): python3 scripts/plot_phase1.py
Reads results/a-load-full-*.csv (run_bench.sh format) and writes results/phase1_loads.png.
"""
import csv
import statistics as st
from pathlib import Path

import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 12, "axes.titlesize": 13})

R = Path(__file__).resolve().parent.parent / "results"
SERIES = [("a-load-full-buffered", "buffered"), ("a-load-full-buffered-noreserve", "buffered,\nno pre-size"),
          ("a-load-full-getline", "getline"), ("a-load-full-getline-noreserve", "getline,\nno pre-size"),
          ("a-load-full-naive", "naive"), ("a-load-full-naive-noreserve", "naive,\nno pre-size")]

times, tsd, mems, labels = [], [], [], []
for stem, lab in SERIES:
    with open(R / f"{stem}.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    t = [float(r["wall_s"]) for r in rows]
    m = [float(r["footprint_bytes"]) / 2**20 for r in rows]
    times.append(st.mean(t)); tsd.append(st.stdev(t)); mems.append(st.mean(m)); labels.append(lab)

fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.6))
colors = ["#1f77b4", "#aec7e8", "#2ca02c", "#98df8a", "#d62728", "#ff9896"]
bars = a1.bar(labels, times, yerr=tsd, capsize=4, color=colors)
for b, v in zip(bars, times):
    a1.annotate(f"{v:.1f}", (b.get_x() + b.get_width() / 2, v), ha="center", va="bottom", fontsize=10, xytext=(0, 4), textcoords="offset points")
a1.set_ylabel("seconds (mean, error bar = 1 sd)")
a1.set_title("Full-dataset load time")
bars = a2.bar(labels, mems, color=colors)
for b, v in zip(bars, mems):
    a2.annotate(f"{v:.0f}", (b.get_x() + b.get_width() / 2, v), ha="center", va="bottom", fontsize=10, xytext=(0, 4), textcoords="offset points")
a2.set_ylabel("peak footprint, MiB")
a2.set_title("Peak memory")
for a in (a1, a2):
    a.grid(axis="y", alpha=0.3)
    a.tick_params(axis="x", labelsize=10)
fig.tight_layout()
fig.savefig(R / "phase1_loads.png", dpi=150)
print("wrote", R / "phase1_loads.png")
