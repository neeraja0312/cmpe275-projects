# Mini 1 lab notes

A running log of every measurement and every failed or surprising attempt, in
the order it happened. The report is written from this file. Append new entries;
do not delete failures. Raw CSVs are in `results/` (git-ignored); the commands
below regenerate them.

Conventions: times are wall-clock seconds unless stated; "footprint" is peak
physical memory footprint (see 2026-10-08, memory measurement); MB = 10^6 bytes,
MiB = 2^20 bytes. Every table names its machine.

## Machines

| Label | Machine | Used for |
|---|---|---|
| M-first | Apple M5, 4 performance + 6 efficiency cores, 16 GB, Release, warm cache | Phase 1 numbers in the first handoff (2024 loads, profile, first Q1/Q2 timings) |
| M-A | MacBook Air (arm64, 16 GB), Homebrew Clang 23.1.3, CMake 4.4.4, libomp 23.1.3, Release | All baselines below, and all Phase 2 benchmarks |

M-A ran about 2× faster than M-first on scans and loads (full load ~23 s vs
44.8 s; ozone count 11.4 ms vs 22 ms). The cause is unknown (cold cache or
setup differences in the first measurement are possible). Never plot the two
machines on one graph; compare ratios instead.

## 2026-10-08: setup and verification on M-A

- Homebrew, CMake and LLVM were not installed. Installed with `brew install cmake llvm libomp`, then configured with
  `cmake -S part-a -B part-a/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"`.
- Clean build, `ctest`: 5/5 pass.
- **Failed attempt: dataset download.** `scripts/get_data.sh` was interrupted twice (a foreground shell timeout, then a background process that died with its shell). It also failed once with `curl: (56) Recv failure: Connection reset by peer` from EPA's server while downloading NO₂ 2024 at 73%. Re-running the script recovered; it re-downloads missing files and re-unzips the rest. Lesson: run it detached and expect to re-run it.
- `scripts/get_data.sh --verify`: all 12 CSVs match the pinned checksums.
- `MINI1_FULL_TEST=1 ctest -R full`: passes (~23 s), reproducing the 65,742,181-row total and the 2024 counts from the first handoff.

## 2026-10-08: memory measurement fix

Problem: `getrusage` peak RSS (`peak_rss_bytes`) leaves out memory macOS has
compressed, so it under-reported a full load.

Fix (additive, nothing removed): `peakFootprintBytes()` in
`part-a/src/MemoryUsage.cpp` reads `task_info(TASK_VM_INFO)`
(`ledger_phys_footprint_peak`, the Activity Monitor figure). `mini1_a` prints
`peak_footprint_bytes`, `footprint_bytes_per_row` and `peak_footprint_bytes_end`
next to the old keys. `scripts/run_bench.sh` records a `footprint_bytes` column
and `scripts/plot.py` graphs it.

Full load, buffered, three single runs (M-A):

| Run | load_s | store_bytes | peak_rss_bytes (old) | peak_footprint_bytes (new) |
|---|---|---|---|---|
| 1 | 24.84 | 810,357,948 | 79,626,240 | 801,670,056 |
| 2 | 23.21 | 810,357,948 | 87,539,712 | 800,850,856 |
| 3 | 22.95 | 810,357,948 | 115,556,352 | 800,850,856 |

The old figure missed ~90% of the memory and varied by ~36 MB between identical
runs; the footprint was within 1 MB. The error is not constant, though: in the
ten-run benchmark below, `/usr/bin/time`'s max RSS was 95.5 MB in run 1 but
~801 MB in runs 2 to 10, and for `--no-reserve` it was 1.2 to 1.4 GB against a
true 1.6 GB. Conclusion: RSS depends on how much the OS has compressed at that
moment and is unreliable on macOS; footprint is not.

Footprint is ~1% below `store_bytes`; likely `store_bytes` counts reserved
capacity (not investigated).

## 2026-10-08: sanitizers after the change

`-DMINI1_SANITIZE=address -DCMAKE_BUILD_TYPE=Debug` (AddressSanitizer + UBSan),
separate build folder `part-a/build-asan`: all 5 tests pass; a 2024 load
(12,542,994 rows, 0 bad rows) shows no errors. The full-dataset test was not run
under the sanitizers (too slow in Debug).

## 2026-10-08: Phase 1 baseline benchmarks (M-A, full dataset, Release)

Method. Loads: `scripts/run_bench.sh --warmup <label> 10 -- part-a/build/mini1_a ../Dataset --loader <l> [--no-reserve] load`,
one untimed warm-up then 10 timed runs, processes run one after another with
the machine kept awake (`caffeinate`). Searches: one process per query, the
query repeated 10 times inside it (`--repeat 10`), minimum and mean of the
repeats read from `query_s_min` / `query_s_mean`. A search process loads the
whole dataset first, so process timing would be mostly load time. The driver
that ran everything is `results/run_baselines.sh` (git-ignored).

### Loads

| Loader | reserve | mean s | sd | min | max | footprint |
|---|---|---|---|---|---|---|
| buffered | on | 22.95 | 0.79 | 22.58 | 25.16 | 801 MB |
| buffered | off | 22.91 | 0.14 | 22.69 | 23.12 | 1,606 MB |
| getline | on | 23.90 | 0.15 | 23.73 | 24.15 | 792 MB |
| getline | off | 24.12 | 0.11 | 24.00 | 24.35 | 1,598 MB |
| naive | on | 45.49 | 4.17 | 43.36 | 54.26 | 792 MB |
| naive | off | 43.48 | 0.22 | 43.27 | 44.02 | 1,598 MB |

Observations:

- The 25.16 s in buffered/on is run 1 (the first timed run after the warm-up); runs 2 to 10 are 22.58 to 23.00 s. Cause not investigated (cache or thermal).
- **Surprising result: naive/on has two outliers**, run 4 = 52.43 s and run 7 = 54.26 s, while the other eight runs are 43.36 to 43.75 s. This machine is a fanless MacBook Air, so thermal throttling or background OS activity is plausible, but unproven. The outliers are kept in the statistics. Median is ~43.5 s.
- `--no-reserve` changes time by under 1% but exactly doubles the peak footprint (1,606 vs 801 MB) because the row vector grows by doubling and the old and new buffers coexist. Pre-sizing from the total file size (`kReserveBytesPerRow = 230`) is free and halves peak memory.
- Loader ranking at full scale: buffered 22.9 s < getline 23.9 s (+4%) < naive ~43.5 s (about 1.9×). The first handoff measured 16.60 vs 8.56 s (1.94×) on 2024 only, so the ratio is stable across machines and data sizes. The buffered-vs-getline gap is 4% here but was 9% on 2024 (9.29 vs 8.56 s).
- getline's footprint (792 MB) is 9 MB below buffered's (801 MB); presumably the buffered reader's 8 MB chunk buffer. Not verified.

### Searches (min of 10 in-process repeats)

Q1, monitor `06-037-1103-44201-1` (8,676 rows in 2024):

| Range | rows | copy | view | scan (no index) |
|---|---|---|---|---|
| 2024-07-01 to 2024-07-01T03 | 3 | 83 ns | 41 ns | 15.23 ms |
| all of 2024 | 8,676 | 1.33 µs | 42 ns | 15.26 ms |

Q2:

| Query | count | copy | callback | virtual | rows | selectivity |
|---|---|---|---|---|---|---|
| NO₂ > 100 ppb | 4.49 ms | 4.67 ms | 4.69 ms | 13.07 ms | 38 | 0.0002% |
| ozone > 0.070 ppm | 11.36 ms | 13.03 ms | 13.09 ms | 33.87 ms | 281,349 | 0.60% |
| ozone > 0.030 ppm | 11.34 ms | 57.83 ms | 62.62 ms | 62.79 ms | 25,272,790 | 53.8% |
| ozone, one hour of 2024 (time-only) | 3.80 ms | 3.81 ms | 3.80 ms | 34.27 ms | 1,205 | 0.013% |

Q3 (ozone, 2024, 8,995,978 rows): none 4.18 ms, by hour of day 5.04 ms, by monitor 25.14 ms.

Observations:

- The NO₂ > 100 ppb count is 38 over all years. The first handoff's "3 rows" was 2024 only.
- count, copy and callback cost about the same while results are small, because all three scan the whole pollutant once. Counting costs 11.3 ms at 0.6% and at 54% selectivity (no data-dependent branching cost visible).
- Copying 25.3M rows (303 MB of 12-byte rows) adds ~46 ms to the scan (57.83 vs 11.34 ms): at high selectivity the result mode dominates. This is the data-movement cost across the library boundary that the assignment is about.
- A virtual call per row costs 2.6 to 9× the templated scan on sparse results (13.07 vs 4.49 ms; 33.87 vs 11.36 ms; 34.27 vs 3.80 ms).
- The Q1 index makes a 3-row lookup ~370,000× faster than a scan. `view` is independent of result size (41 to 42 ns) because it returns spans into storage.
- Q3 by monitor is 6× slower than ungrouped (25.1 vs 4.2 ms): random access into ~1,283 per-monitor accumulators instead of one.
- **Time-only search baseline: 3.80 ms** on M-A (the first handoff's 7.4 ms was on M-first). It scans 9.0M rows to return 1,205. This is the number a per-monitor binary search must beat.
- Q4 (monitors by state or box) was not benchmarked; it is not required by the handoff.

## 2026-10-08: Phase 1 experiments reproduced on M-A

Both run on `Dataset/ozone/hourly_44201_2024.csv` (8,995,978 rows); the experiment
commands take a single CSV, not the folder (running them on the folder fails with
"../Dataset is not named like an EPA hourly file").

**float vs exact integers** (`mini1_a <file> experiment float-values`):

| Check | Wrong |
|---|---|
| `float` truncated | 0 |
| `float` rounded | 0 |
| `double` truncated | 0 |
| `double` rounded | 0 |
| rows exactly at the 0.070 ppm threshold | 9,348 |
| `float` threshold test (`> 0.070`) | **9,348 wrong** (every one of them) |
| `double` threshold test | 0 wrong |

Reproduces the first handoff exactly. Storing values as `float` is harmless for
rounding to the displayed decimals but wrongly counts every reading of exactly
0.070 ppm as above the 0.070 standard. Likely cause (our inference, not yet
checked in the code): the nearest `float` to 0.070 is 0.07000000029802…, slightly
above the `double` 0.070 it is compared with, so `value > 0.070` is true. Our
scaled integers (ppm × 1000) avoid the problem entirely.

**Every field as `std::string`** (`mini1_a <file> experiment string-rows`):

| Measure | Result |
|---|---|
| rows | 8,995,978 |
| time | 9.59 s |
| bytes per row (counted) | 624 (first handoff: 638) |
| compact store for the same rows | 12 bytes per row, 789 MB for the dataset |
| extrapolated to 65.7M rows | 41.0 GB (first handoff: 41.9 GB), versus 810 MB compact |
| in-process peak RSS (`getrusage`) | 4.32 GB |
| `/usr/bin/time -l` max RSS | 5.62 GB |

The string layout is about 52× larger and could not be loaded on a 16 GB machine.
**Another instance of the macOS RSS problem:** the in-process `getrusage` peak
(4.32 GB) is 23% below `/usr/bin/time`'s figure (5.62 GB) for the same process,
and 5.62 GB matches the 624 bytes × 9.0M rows count, so the in-process number
under-reports even at multi-GB scale.

## 2026-10-08: where load time goes (macOS `sample`, M-A)

`part-a/build/mini1_a ../Dataset --years 2024 --loader buffered load`, sampled for
3 s (2,553 samples), flat "top of stack" counts:

| Work | Samples | Share |
|---|---|---|
| Line splitting: `_platform_memchr` 1,249 + `CsvTokenizer::split` 517 + `memchr` stub 268 | 2,034 | **79.7%** |
| Reading the file (`__read_nocancel`) | 154 | 6.0% |
| Number and date parsing (`FastNumbers::*`, `from_chars`, `hoursSinceEpoch`) | 232 | 9.1% |
| `RowBuilder::process` | 65 | 2.5% |
| Dictionary intern and compare | 45 | 1.8% |
| Store append (`appendBatch`, `push_back`) | 23 | 0.9% |

Reproduces the first handoff (~78% splitting, ~12% parsing, ~6% reading). The
splitter dominates because it calls `memchr` for every field (two `find` calls
per field), roughly 600M short calls for 2024. This is the evidence for the
single-pass tokenizer in Phase 2. Caveat: the sample covers about 3 s of a
4.4 s load, not the start-up.

## 2026-10-08: graphs

Created a virtual environment (`python3 -m venv .venv && .venv/bin/pip install matplotlib`,
matplotlib 3.9.4; `.venv/` is git-ignored) and ran
`.venv/bin/python scripts/plot.py <labels>` for the six full-load results:
`results/summary_time.png` (wall time, mean ± sd) and `results/summary_rss.png`
(peak memory, from the `footprint_bytes` column). The memory axis was labelled
"MB" but is MiB (divided by 2^20); fixed in `plot.py`. The graphs show the two
observations above clearly (naive ~1.9× slower with an error bar from its
outliers; `--no-reserve` doubles memory in all three loaders). Search graphs
come from `scripts/plot_queries.py` (reads `results/a-queries-full.csv`, which has
a different format from the `run_bench.sh` CSVs): `results/queries_q1.png`
(view/copy/scan, log scale), `queries_q2.png` (four result modes at four
selectivities) and `queries_q3.png` (three groupings). Phase 2 query CSVs in the
same format can be plotted with a different output prefix, e.g.
`.venv/bin/python scripts/plot_queries.py results/b-queries-full.csv b`.

## Open items

- Phase 2 (OpenMP in `part-b/omp`; library, column layout and static-vs-shared in `part-b/lib` and `part-b/app`): not started. Add entries here as results arrive, including failures.
- Individual contributions and the AI-assisted Phase 1 code decision: undecided; needed for the report.
