#!/usr/bin/env python3
"""Comparison graphs: Phase 1 baseline vs part-b (row layout, column layout), static vs shared.

Usage (from Minis/mini1):
    python3 scripts/plot_compare.py

Reads results/*.csv written by the benchmark scripts and writes
results/compare_*.png. Series with a missing CSV are skipped.
Requires matplotlib.
"""

import csv
import statistics
from pathlib import Path

import matplotlib.pyplot as plt

R = Path(__file__).resolve().parent.parent / "results"
COLORS = {"Phase 1": "#7f7f7f", "part-b rows": "#1f77b4", "part-b columns": "#d62728"}


def read(name):
    p = R / name
    if not p.exists():
        return None
    with open(p, newline="") as f:
        return list(csv.DictReader(f))


def table(rows):
    """(query, mode) -> min seconds"""
    return {(r["query"], r["mode"]): float(r["query_s_min"]) for r in rows} if rows else {}


def save(fig, name):
    fig.tight_layout()
    fig.savefig(R / name, dpi=150)
    plt.close(fig)
    print("wrote", R / name)


def grouped(ax, groups, series, scale, unit, log=False):
    """series: list of (label, color, {group_key: seconds}); groups: list of (key, label)"""
    n = len(series)
    width = 0.8 / n
    for j, (lab, color, data) in enumerate(series):
        xs, ys = [], []
        for i, (key, _) in enumerate(groups):
            if key in data:
                xs.append(i + (j - (n - 1) / 2) * width)
                ys.append(data[key] * scale)
        bars = ax.bar(xs, ys, width, label=lab, color=color)
        for b, y in zip(bars, ys):
            ax.annotate(f"{y:.3g}", (b.get_x() + b.get_width() / 2, y), ha="center", va="bottom", fontsize=7)
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels([g[1] for g in groups], fontsize=8)
    ax.set_ylabel(unit)
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)


def load_graphs():
    rows = read("interleave_phase1_partb-aos_partb-cols.csv")
    if not rows:
        return
    names = [("phase1", "Phase 1"), ("partb-aos", "part-b rows"), ("partb-cols", "part-b columns")]
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 4.5))
    data = [[float(r["load_s"]) for r in rows if r["name"] == k] for k, _ in names]
    box = a1.boxplot(data, tick_labels=[v for _, v in names], patch_artist=True, showmeans=True)
    for patch, (_, v) in zip(box["boxes"], names):
        patch.set_facecolor(COLORS[v])
        patch.set_alpha(0.6)
    for i, d in enumerate(data):
        a1.annotate(f"median {statistics.median(d):.2f}", (i + 1, min(d)), ha="center", va="top", fontsize=8)
    a1.set_ylabel("load time, seconds")
    a1.set_title("Full-dataset load, interleaved runs (AC power)")
    a1.grid(axis="y", alpha=0.3)

    mem = [statistics.median(float(r["footprint_bytes"]) for r in rows if r["name"] == k) / 2**20 for k, _ in names]
    bars = a2.bar([v for _, v in names], mem, color=[COLORS[v] for _, v in names])
    for b, y in zip(bars, mem):
        a2.annotate(f"{y:.0f} MiB", (b.get_x() + b.get_width() / 2, y), ha="center", va="bottom", fontsize=9)
    a2.set_ylabel("peak footprint, MiB")
    a2.set_title("Memory after loading 65.7M rows")
    a2.grid(axis="y", alpha=0.3)
    save(fig, "compare_load.png")


def query_graphs():
    p1 = table(read("a-queries-full.csv"))
    ao = table(read("b-static-aos-queries-full.csv"))
    co = table(read("b-static-columns-queries-full.csv"))
    sh_a = table(read("b-shared-aos-queries-full.csv"))
    sh_c = table(read("b-shared-columns-queries-full.csv"))
    ca = table(read("b2-static-aos-count-after.csv"))
    cc = table(read("b2-static-columns-count-after.csv"))
    if not (p1 and ao and co):
        return
    S = lambda d, mode_filter=None, q=None: d  # noqa: E731

    def by(d, mode):  # query -> seconds for one mode
        return {q: v for (q, m), v in d.items() if m == mode}

    # Q1
    groups = [("q1-3h", "3 rows"), ("q1-year", "8,676 rows")]
    fig, axes = plt.subplots(1, 3, figsize=(12, 4.2))
    for ax, mode in zip(axes, ["view", "copy", "scan"]):
        grouped(ax, groups, [("Phase 1", COLORS["Phase 1"], by(p1, mode)),
                             ("part-b rows", COLORS["part-b rows"], by(ao, mode)),
                             ("part-b columns", COLORS["part-b columns"], by(co, mode))], 1e9, "nanoseconds", log=True)
        ax.set_title(f"Q1 {mode}")
    axes[0].legend(fontsize=8)
    fig.suptitle("Q1: one monitor, time range (min of 10 repeats, log scale)")
    save(fig, "compare_q1.png")

    # Q2 non-count modes
    groups = [("q2-no2-gt100ppb", "NO2>100ppb\n38 rows"), ("q2-ozone-gt0.070", "ozone>0.070\n281k"),
              ("q2-ozone-gt0.030", "ozone>0.030\n25.3M"), ("q2-ozone-onehour", "one hour\n1,205")]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.4))
    for ax, mode in zip(axes, ["copy", "callback", "virtual"]):
        grouped(ax, groups, [("Phase 1", COLORS["Phase 1"], by(p1, mode)),
                             ("part-b rows", COLORS["part-b rows"], by(ao, mode)),
                             ("part-b columns", COLORS["part-b columns"], by(co, mode))], 1e3, "milliseconds", log=True)
        ax.set_title(f"Q2 {mode}")
    axes[0].legend(fontsize=8)
    fig.suptitle("Q2: value range, result modes that return rows (min of 10 repeats, log scale)")
    save(fig, "compare_q2.png")

    # Q2 count: before/after the count-loop fix
    series = [("Phase 1", COLORS["Phase 1"], by(p1, "count")),
              ("rows, old loop", "#9ecae1", by(ao, "count")),
              ("columns, old loop", "#fcae91", by(co, "count"))]
    if ca:
        series.append(("rows, new loop", COLORS["part-b rows"], by(ca, "count")))
    if cc:
        series.append(("columns, new loop", COLORS["part-b columns"], by(cc, "count")))
    fig, ax = plt.subplots(figsize=(10, 4.8))
    grouped(ax, groups, series, 1e3, "milliseconds")
    ax.legend(fontsize=8)
    ax.set_title("Q2 count: effect of the count-loop fix and of the layout (min of 10 repeats)")
    save(fig, "compare_q2_count.png")

    # Q3
    groups = [("q3-ozone-2024-none", "none"), ("q3-ozone-2024-hour", "by hour"), ("q3-ozone-2024-monitor", "by monitor")]
    fig, ax = plt.subplots(figsize=(7, 4.2))
    grouped(ax, groups, [("Phase 1", COLORS["Phase 1"], by(p1, "-")),
                         ("part-b rows", COLORS["part-b rows"], by(ao, "-")),
                         ("part-b columns", COLORS["part-b columns"], by(co, "-"))], 1e3, "milliseconds")
    ax.legend(fontsize=8)
    ax.set_title("Q3: aggregates, ozone 2024 (9.0M rows)")
    save(fig, "compare_q3.png")

    # Static vs shared: ratio shared/static for every query row, both layouts
    if sh_a and sh_c:
        fig, ax = plt.subplots(figsize=(7, 4.2))
        for lab, st, sh, color in [("rows", ao, sh_a, COLORS["part-b rows"]), ("columns", co, sh_c, COLORS["part-b columns"])]:
            ratios = sorted(sh[k] / st[k] for k in st if k in sh)
            ax.plot(range(len(ratios)), ratios, "o-", label=f"{lab} (median {statistics.median(ratios):.3f})", color=color)
        ax.axhline(1.0, color="k", lw=0.8)
        ax.set_ylabel("shared / static time")
        ax.set_xlabel("search configurations, sorted")
        ax.set_title("Static vs shared library: ratio per search (1.0 = identical)")
        ax.legend()
        ax.grid(alpha=0.3)
        save(fig, "compare_linkage.png")


if __name__ == "__main__":
    load_graphs()
    query_graphs()
