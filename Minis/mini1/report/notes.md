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
| naive | on | 45.49 | 4.17 | 43.36 | 54.26 | 793 MB |
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
| ozone, one hour of 2024 (time-only) | 3.80 ms | 3.80 ms | 3.80 ms | 34.27 ms | 1,205 | 0.013% |

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


**Re-check at the end of the project (2026-10-11).** A second 3 s `sample` of a 2024 load (2,501 samples, 4.7 s load): splitting (`memchr`, `CsvTokenizer::split`, memchr stub) 1,937 = 77.4%; number and date parsing 298 = 11.9% (grouping by function name, so slightly coarser than the first); file read 196 = 7.8%; `RowBuilder::process` 54 = 2.2%; dictionary 26 = 1.0%; store append 32 = 1.3%. Same conclusion; splitting is about 78%, other shares move by a few points. Memory re-check: four fresh full loads gave `peak_rss_bytes` of 142.2, 149.3, 88.3 and 126.6 MB against `peak_footprint_bytes` 800.8 to 800.9 MB (the original three runs gave 79.6, 87.5, 115.6 MB). The stored ten-run benchmark logs show in-process RSS 95.0 MB in run 1 and 800.9 MB in runs 2 to 10, and `/usr/bin/time` max RSS 95.5 MB then 800.9 MB; for `getline` it ranged 611.6 to 792.6 MB over its ten runs. RSS is erratic in every form we measured; footprint was not.

String-rows re-check: 8,995,978 rows, 9.42 s (first measurement 9.59 s), 624 bytes per row, extrapolated 41.0 GB, compact estimate 789 MB; in-process `peak_rss_bytes` 3.07 GB (first measurement 4.32 GB); `/usr/bin/time -l` maximum resident set size 5.62 GB (same as before) and peak memory footprint 10.08 GB.

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

## 2026-10-09: Phase 2 Step 2, library + column layout (part-b/lib, part-b/app)

What was built (all under `part-b/`; `part-a/` unchanged):
- `lib/include/aqdata/` is the only thing the app can include: value types, `Database` (pimpl facade), load options, `ColumnSlice`, metrics. `lib/src/` holds the private store, loader, tokenizer and registries. The library is built with `-fvisibility=hidden`; `nm -gU libaqdata.dylib` shows exactly 46 exported functions (the public API) and none of `AosStore`, `ColumnStore`, `DataLoader`, `CsvTokenizer`, `MonitorRegistry`.
- Two layouts selectable at run time (`mini1_b --layout aos|columns`): `AosStore` (the Phase 1 code, ported) and `ColumnStore` (one array per field: hour u32, monitor u16, value i16, qualifier u8, method u8 = 10 bytes/row, against 12). Both keep the same per-monitor block and per-file segment indexes, so the comparison isolates layout.
- Q1 `view` returns a `ColumnSlice` (strided column readers), zero-copy for both layouts.
- Tests: `test_queries` (2,980 checks, every loader and both layouts), `test_layouts_agree` (1,147 checks: 2024 data loaded into both layouts, every Q1/Q2/Q3 result mode identical, row for row), `test_full_dataset` (both layouts reproduce the 65,742,181-row totals). ASan + UBSan clean on all three.
- Build: `cmake -S part-b -B part-b/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" -DOpenMP_ROOT="$(brew --prefix libomp)"` (the `OpenMP_ROOT` is needed with Homebrew libomp); add `-DBUILD_SHARED_LIBS=ON` for the shared library.

**Failed/neutral attempts**
- Q1 `copy` on columns: first version used `push_back` per row (14.6 µs for 8,676 rows); adding `reserve` did not help (12.6 µs). The cost is reassembling each row from five arrays (~1.4 ns/row vs ~0.15 ns/row for a bulk copy of rows).
- An earlier shell comparison printed nothing because zsh does not word-split `$bin`; not a code problem.

### Benchmarks (M-A, full dataset, Release, buffered loader, reserve on)

Method as in the Phase 1 baseline: 1 untimed warm-up + 10 timed loads; searches in one process with `--repeat 10`, min reported. Driver: `results/run_stage.sh` (git-ignored). **Caveat: the run started with the Mac on battery power (32%) and was plugged in partway through (observed on AC 20 minutes later); `b-static-aos` and probably part of `b-static-columns` ran on battery.** Compare ratios inside a configuration more than absolute times across configurations, and re-run anything that matters.

Loads:

| Configuration | mean s | sd | min | max | footprint |
|---|---|---|---|---|---|
| Phase 1 (`part-a`) | 22.95 | 0.79 | 22.58 | 25.16 | 801 MB |
| part-b static, aos | 23.88 | 0.80 | 22.80 | 25.56 | 801 MB |
| part-b shared, aos | 23.87 | 0.24 | 23.62 | 24.37 | 801 MB |
| part-b static, columns | 24.57 | 0.61 | 23.92 | 25.66 | 669 MB |
| part-b shared, columns | 23.64 | 0.06 | 23.55 | 23.73 | 669 MB |

- The part-b `aos` loads looked ~4% slower than Phase 1 with identical loader code. **Settled, it was noise/drift:** an interleaved run on AC power (`results/interleave_load.py`, 10 rounds, Phase 1 / part-b rows / part-b columns alternating) gave medians 23.59 s / 23.57 s / 23.66 s (means 23.60 / 23.90 / 23.83; the part-b rows mean includes one 26.79 s outlier). The library layer costs nothing measurable on load. Prefer these interleaved numbers over the sequential table above.
- Columns use 16% less memory (669 vs 801 MB) and load about as fast (24.57 s static on battery, 23.64 s shared).

Searches (min of 10 repeats), ratio = columns / aos in part-b (below 1.00 = columns faster):

| Query | mode | Phase 1 | aos | columns | ratio |
|---|---|---|---|---|---|
| Q2 ozone > 0.070 | count | 11.4 ms | 11.3 ms | 8.32 ms | 0.74 |
| Q2 NO₂ > 100 ppb | count | 4.49 ms | 4.51 ms | 3.34 ms | 0.74 |
| Q2 ozone, one hour (time-only) | count | 3.80 ms | 3.75 ms | 3.75 ms | 1.00 |
| Q1 3 h / 1 yr | scan | 15.2 / 15.3 ms | 15.4 / 15.4 ms | 5.98 / 5.99 ms | 0.39 |
| Q2 ozone > 0.030 (54%) | copy | 57.8 ms | 58.3 ms | 72.8 ms | 1.25 |
| Q2 ozone > 0.030 (54%) | callback | 62.6 ms | 62.9 ms | 126 ms | 2.00 |
| Q2 ozone > 0.070 | virtual | 33.9 ms | 34.0 ms | 63.3 ms | 1.86 |
| Q2 ozone, one hour | virtual | 34.3 ms | 34.7 ms | 69.5 ms | 2.00 |
| Q1 1 yr (8,676 rows) | copy | 1.33 µs | 1.33 µs | 12.5 µs | 9.41 |
| Q1 3 h / 1 yr | view | 41 ns | 41 ns | 41 ns | 1.00 |
| Q3 none / hour / monitor | | 4.18 / 5.04 / 25.1 ms | 3.80 / 5.06 / 25.7 ms | 3.78 / 5.29 / 26.6 ms | 0.99 / 1.04 / 1.03 |

Static vs shared library (ratio shared/static over all ms-scale queries): aos median 1.001 (0.984 to 1.025), columns median 0.993 (0.882 to 1.060). No measurable difference: the library is entered once per query, so symbol resolution is not on the hot path.

Findings so far:
1. The library boundary costs nothing measurable: part-b `aos` equals Phase 1 on every search of 1 ms or more to within 2.2%, except the ungrouped Q3 aggregate, which was 9% faster (3.80 vs 4.18 ms).
2. Column layout helps only queries that touch few fields (`count` 1.35×, Q1 `scan` 2.5×) and hurts everything that must return whole rows (`copy` up to 1.25×, `callback`/`virtual` up to 2×, Q1 `copy` 9×), because rows are rebuilt from five arrays. "Do queries shape your design?" Yes: the right layout depends on what the query returns.
3. The column `count` gain was first smaller than the byte ratio suggests (2 vs 12 bytes per row): 94 MB in 8.3 ms is ~11 GB/s, far below what the `aos` scan reaches (564 MB in 11.3 ms ≈ 50 GB/s), so the loop was compute-bound. Fixed, see the next section.
4. The time-only search does not benefit from either layout (3.75 ms in both); it needs the per-monitor binary search (W's task).

### Count-loop fix (2026-10-09, AC power)

Cause: the loop added `(v >= lo) & (v <= hi)` widened to a 64-bit `size_t` counter on int16 data, which stops the compiler from using 16/32-bit SIMD lanes (inference from the speedup, not from inspecting assembly). Fix: one unsigned compare, `(uint16_t)(v - lo) <= (uint16_t)(hi - lo)` (valid for lo <= hi; returns 0 otherwise), with a 32-bit counter flushed to the 64-bit total every 65,536 rows. The same loop style was also given to `AosStore::countByValue` so the layout comparison is fair. Results are identical (281,349 rows etc.) and all tests pass.

| Q2 count (min of 10) | Phase 1 | rows, old loop | rows, new loop | columns, old loop | columns, new loop |
|---|---|---|---|---|---|
| ozone > 0.070 (0.6%) | 11.4 ms | 11.3 | 8.49 | 8.32 | **2.73** |
| ozone > 0.030 (54%) | 11.3 ms | 11.3 | 7.62 | 8.36 | **2.73** |
| NO₂ > 100 ppb | 4.49 ms | 4.51 | 2.94 | 3.34 | **1.09** |
| one hour (time-only) | 3.80 ms | 3.75 | 3.89 | 3.75 | 3.75 |

Columns are now ~4.2× faster than Phase 1 and the time no longer depends on selectivity (memory-bound: 94 MB in 2.73 ms ≈ 34 GB/s). Separating the two effects: the loop alone gives rows 1.3 to 1.5×; the layout adds another 2.8 to 3.1× on top. The time-only search is unchanged because it scans all values and the time column. Not re-run after the fix: copy/callback/virtual modes (they do not use this loop). The other Q2 numbers above are from before the fix and the battery caveat still applies to them.

Graphs: `scripts/plot_compare.py` writes `results/compare_load.png`, `compare_q1.png`, `compare_q2.png`, `compare_q2_count.png`, `compare_q3.png`, `compare_linkage.png`.

## 2026-10-10: single-pass tokenizer (W-1, Windows 11 / WSL2)

Machine W-1: Intel Core i9-13900HX (16 cores, 32 logical CPUs), 15 GiB visible
to WSL2, Ubuntu, GCC 13.4.0, Release build. The verified dataset and
build directories were on WSL's Linux filesystem rather than `/mnt/c`.

Method: an external copy of the exact same `part-b` source restored only the
previous multi-`find` tokenizer; the working build used the new single forward
scan. Both used `--layout aos`. Runs were interleaved to reduce drift, with one
warm-up per executable followed by 10 timed runs each. Both versions produced
the same row counts with zero bad rows. The focused tokenizer test also compared
every field from both 2024 CSVs against the previous parser, and the full
dataset regression passed for both layouts.

| Dataset | Rows | Previous mean ± sd | Single-pass mean ± sd | Speedup | Peak RSS |
|---|---:|---:|---:|---:|---:|
| 2024 ozone + NO₂ | 12,542,994 | 7.5812 ± 0.0246 s | 4.2995 ± 0.0295 s | 1.76× | 164.7 MB both |
| All 12 files | 65,742,181 | 42.3366 ± 0.5626 s | 24.6615 ± 0.7702 s | 1.72× | 811.2 / 811.3 MB |

The tokenizer reduced load time by 43% on 2024 and 42% on the full dataset
without changing memory use or results. WSL reports RSS rather than the macOS
physical-footprint metric, so these memory values must not be combined with the
M-A footprint chart. Raw CSVs are in `~/cmpe275-results/` on W-1 and are not
committed.

## 2026-10-10: indexed constrained-time Q2 (W-1)

Partially overlapping time ranges now reuse the per-monitor time-sorted blocks:
two binary searches narrow each block before applying the value predicate.
Ranges are sorted by row offset before copy/callback output, preserving the
previous file order. Queries covering an entire file segment keep the original
vectorized scan.

Method: an external copy of the same `part-b` source retained the previous
segment scan; the working build used the block index. Both used the single-pass
tokenizer, 2024 data, Release mode, and 20 in-process repeats (minimum shown).
All counts and copy/callback checksums matched.

| Layout | Window | Count old → indexed | Copy old → indexed | Callback old → indexed |
|---|---|---:|---:|---:|
| rows | 1 hour (1,205 rows) | 12.852 → 0.336 ms (38.2×) | 11.668 → 0.298 ms (39.2×) | 12.540 → 0.291 ms (43.2×) |
| rows | 5 days (140,298 rows) | 12.977 → 0.605 ms (21.4×) | 12.080 → 1.026 ms (11.8×) | 12.801 → 0.853 ms (15.0×) |
| columns | 1 hour (1,205 rows) | 9.522 → 0.241 ms (39.6×) | 9.847 → 0.301 ms (32.7×) | 11.467 → 0.296 ms (38.8×) |
| columns | 5 days (140,298 rows) | 9.495 → 0.414 ms (23.0×) | 10.820 → 1.337 ms (8.1×) | 11.972 → 1.046 ms (11.4×) |

The first full-year control looked noisy, so it was repeated as five interleaved
processes with 100 query repetitions each. Mean minimum time was unchanged
within normal variation: rows 9.673 → 9.589 ms and columns 1.573 → 1.552 ms.
Thus narrow ranges gain 8–43× without slowing the whole-segment path.

Correctness: synthetic Q2 tests cover value-filtered and time-only indexed
ranges in both layouts; the 2024 layout-equivalence test, all normal tests, the
65,742,181-row full regression, and the ASan/UBSan synthetic query test passed.
Raw benchmark CSVs are in `~/cmpe275-results/` on W-1.

## 2026-10-11: Phase 2 Step 1, OpenMP (W-1)

`part-b/omp` is an isolated copy of the Phase 1 design. Q2 count uses an
OpenMP reduction, Q2 copy writes one result vector per input segment and merges
them in file order, and Q3 writes per-thread `Stats` arrays before a serial
merge. Q1, callbacks and the per-row virtual experiment remain serial because
their interfaces or query size make thread startup inappropriate.

Loading parses discovered files in parallel. Every file has a private monitor
registry, qualifier/method dictionaries and row vector; the main thread merges
files in discovery order and renumbers local ids. This preserves serial row,
monitor and dictionary order at every thread count. File-level parallelism was
already strong, so byte-chunking individual files was not added.

### Full-data load scaling

W-1, Release, verified 65,742,181-row dataset on the WSL filesystem. One warm-up
per thread count, then 10 timed runs interleaved forward/reverse.

| Threads | Mean ± sd | Speedup vs 1 thread | Efficiency | Mean peak RSS |
|---:|---:|---:|---:|---:|
| 1 | 43.416 ± 0.575 s | 1.00× | 100% | 915 MB |
| 2 | 23.533 ± 1.770 s | 1.84× | 92% | 924 MB |
| 4 | 12.654 ± 0.291 s | 3.43× | 86% | 943 MB |
| 6 | 9.196 ± 0.104 s | 4.72× | 79% | 949 MB |
| 8 | 7.299 ± 0.314 s | **5.95×** | 74% | 1,061 MB |
| 10 | 7.445 ± 0.411 s | 5.83× | 58% | 1,070 MB |

Against the 42.337 s W-1 Phase 1 baseline, 8 threads are 5.80× faster. Scaling
stops after 8 threads while memory rises because more per-file parse buffers
are live concurrently. Every run loaded the exact row total with zero bad rows.

### Query scaling

Full dataset, 20 in-process repeats, minimum shown. Q2 searched all ozone years
above 0.070 ppm (281,349 rows); Q3 aggregated all 47,005,021 ozone rows.

| Threads | Q2 count | Q2 copy | Q3 ungrouped |
|---:|---:|---:|---:|
| 1 | 49.276 ms | 58.304 ms | 51.992 ms |
| 2 | 29.459 ms | 35.411 ms | 31.090 ms |
| 4 | 20.510 ms | 25.795 ms | 21.569 ms |
| 6 | **13.396 ms (3.68×)** | **15.304 ms (3.81×)** | 14.262 ms (3.65×) |
| 8 | 13.503 ms | 15.422 ms | 14.555 ms |
| 10 | 14.326 ms | 17.200 ms | **13.655 ms (3.81×)** |

**Failed first attempt:** contiguous `schedule(static)` assigned all six ozone
segments to one worker at two threads and all six NO₂ segments (immediately
skipped) to the other, giving no speedup. `schedule(static, 1)` distributes
pollutant segments round-robin. Useful work is only six ozone files, explaining
the query plateau around six threads. Counts and copy checksums matched at every
thread count.

### Validation and sanitizer result

The five copied Phase 1 tests pass at 1 and 4 threads. The full-dataset test
passes at 1 and 8 threads (17/17 checks), and ASan/UBSan passes at four threads.
GCC ThreadSanitizer completed all 1,010 synthetic checks but exited 66 after
reporting races across OpenMP barriers. The reports pair worker writes to
separate preallocated file/segment results with reads after the implicit
barrier, and similarly report read-only query data; this is the known
uninstrumented-libgomp limitation warned about by `Mini1Common.cmake`, not a
claim of a clean TSan run. Raw CSVs are in `~/cmpe275-results/` on W-1.

## 2026-10-11: ThreadSanitizer on macOS (M-A) for `part-b/omp`

Built with `-DMINI1_SANITIZE=thread -DCMAKE_BUILD_TYPE=Debug` (Homebrew Clang + libomp), ran `omp_test_queries` (1,010 checks) and `omp_test_real_data` (19 checks) at 4 threads on the 2024 data. All checks pass; TSan reports 22 and 2 data-race warnings (exit 134), the same outcome as the Windows run (GCC, exit 66).

Classification of all 24 warnings by their stack frames: every one pairs an access by a worker thread inside a parallel region (`DataLoader.cpp:248/436` per-file `RowBuilder`/results, `AosStore.cpp:113/138/140/144/145/186` per-segment partial results and read-only query data) with an access by the main thread to the same address after the region (`DataLoader.cpp:479/491/495`, `AosStore.cpp:129/149/150/156/182`). None pairs two worker threads. This is the pattern expected if TSan cannot see libomp's end-of-region barrier as a synchronization point, since the library is not TSan-instrumented. **This is an inference from the stack traces, not a proof**: a clean run would need an OpenMP runtime built with TSan/Archer support, which we did not have. The design relies on disjoint writes (one result slot per file or segment, `schedule(static, 1)`) and a serial merge after the region. Report this as a limitation, not as "TSan clean".

## 2026-10-11: final measurements on M-A (AC power, after merging the Windows teammate's work)

Machine M-A (Apple-silicon MacBook Air, 4 performance + 6 efficiency cores, 16 GB), plugged in, `caffeinate`, Release, Homebrew Clang 23. Driver `results/run_final.sh` (git-ignored); raw data `results/final-loads.csv`, `final-omp-queries.csv`, `b3-static-{aos,columns}-queries-full.csv`. Graphs: `scripts/plot_compare.py` (`compare_load.png`, `compare_omp_queries.png`, `compare_q2_count.png`, `compare_q1/q2/q3.png`).

### Loads, full dataset (65,742,181 rows), 10 interleaved rounds, 1 warm-up

| Build | mean s | median s | min..max | peak footprint |
|---|---|---|---|---|
| Phase 1 (`part-a`) | 24.15 | 23.61 | 23.32..26.55 | 764 MiB |
| part-b rows (single-pass tokenizer) | 10.61 | 10.28 | 10.04..12.15 | 764 MiB |
| part-b columns | 10.72 | 10.32 | 10.20..12.49 | 638 MiB |
| OpenMP (`omp/`), 1 thread | 24.62 | 24.06 | 23.68..26.90 | 1,540 MiB |
| OpenMP, 4 threads | 9.47 | 8.70 | 7.73..13.29 | 1,874 MiB |
| OpenMP, 8 threads | 7.38 | 6.94 | 6.23..9.60 | 1,966 MiB |

- The single-pass tokenizer alone makes the serial load **2.3× faster** than Phase 1 (23.6 to 10.3 s, medians) with identical memory. The Windows machine (W-1) measured 1.72×; the ratio differs by machine and must not be mixed.
- OpenMP file-level parallelism reaches 3.5× (8 threads vs 1 thread, medians 24.06 to 6.94 s), but at 8 threads it is only 1.5× faster than the *serial* single-pass loader (10.3 s), and it needs 2.6× the memory. Per-file private row vectors that are merged afterwards roughly double the footprint even at 1 thread (1,540 vs 764 MiB). The OpenMP code has the old tokenizer; combining both was not done (see open items).
- Outliers (26 s in the Phase 1 and OpenMP rows, 12 to 13 s in part-b) are single slow rounds, kept in the CSV.

### OpenMP search scaling (ozone data, min of 10 in-process repeats)

| Threads | Q2 count | Q2 copy | Q3 aggregate |
|---|---|---|---|
| 1 | 16.3 ms | 15.7 | 24.6 |
| 2 | 8.2 | 9.9 | 15.6 |
| 4 | 7.5 | 8.5 | 11.1 |
| 6 | 5.8 (2.8×) | 6.8 | 9.0 (2.7×) |
| 8 | 6.9 | 6.3 (2.5×) | 10.1 |
| 10 | 5.7 (2.9×) | 6.8 | 10.7 |

Speedup stops near 2.5 to 2.9× at 6 threads (W-1 measured up to 3.8×). Only six ozone files exist, so at most six units of work per query, and the Mac has four performance cores. Note that the serial part-b column `count` (2.7 ms) is already faster than the OpenMP row-layout count at any thread count (5.7 ms): the data layout and the inner loop matter more than threads for this query.

### Searches, part-b with new tokenizer and indexed time-range search (min of 10)

| Query | mode | Phase 1 | rows | columns |
|---|---|---|---|---|
| Q2 ozone, one hour (time-only) | count | 3.80 ms | **0.179 ms (21×)** | **0.127 ms (30×)** |
| same | copy | 3.80 ms | 0.167 ms | 0.133 ms |
| same | callback | 3.80 ms | 0.162 ms | 0.132 ms |
| Q2 ozone > 0.070 | count | 11.4 ms | 8.28 ms | 4.26* ms |
| Q2 ozone > 0.030 | count | 11.3 ms | 8.09 ms | 3.57 ms |
| Q2 NO₂ > 100 ppb | count | 4.49 ms | 3.17 ms | 1.42 ms |

*Outlier: three reruns of the same query with both pollutants loaded gave 2.73, 3.06 and 2.93 ms (columns) and 7.5 ms (rows); the whole-range path is unchanged code. The raw CSV is left as measured.

The time-only search, the 3.80 ms baseline from Phase 1, is now 0.13 to 0.18 ms: each monitor's time-sorted block is binary-searched instead of scanning every row.

Not an improvement, within noise: Q3 (rows 5.1 ms vs Phase 1 3.8 to 5.0 ms measured back to back in the same session), Q1, and the row-returning Q2 modes (copy, callback, virtual). **Session-to-session variance on this Mac is up to ±25% for large copies and Q3**: Phase 1 alone gave 58.5 to 64.9 ms for the 25M-row copy and 3.9 to 5.0 ms for Q3 in four consecutive measurements. part-b copy measured 69 to 82 ms in the same session, which may be a small real slowdown (the covered-segment path was refactored) or the same noise; we cannot separate them with these data. Do not claim a regression or a gain for these modes.

## 2026-10-11: verification pass (corrections to earlier entries)

Raw data: `results/verify-regression.csv`, `verify-omp-loads-all.csv`, `verify-omp-loads-2-6-8-10.csv` (git-ignored). Both runs were on battery power (96% to 86%), interleaved so relative comparisons are valid.

**Corrections.**
1. The Step 2 finding "callback and virtual about 2x slower on columns" came from the first Step 2 run (began on battery; its columns callback of 126 ms was an outlier). The final mains-power run gives columns/rows: copy 1.18, callback 1.13, virtual 1.25 to 1.76, Q1 copy 11.2. The report uses the final run (Table 5).
2. "Part-b is slower than Phase 1 on row-returning searches" in the final run (rows layout 16% to 73% slower, median 28%, excluding the one-hour searches) is not reproducible. Phase 1 vs pre-merge part-b (commit 88caa24, built in a temp directory) vs current part-b, rows layout, ozone only, `--repeat 20`, 4 interleaved rounds, medians in ms: copy 0.031: 58.70 / 58.49 / 59.62; virtual 0.071: 34.15 / 34.12 / 34.20; Q3 ungrouped: 3.92 / 3.95 / 3.83 (no regression). Batched callback 0.031: 63.01 / 58.21 / 65.87, a consistent +13% against the pre-merge build and +5% against Phase 1 (cause not investigated; the covered-segment path was restructured by the indexed-search change). The high times in the final run are unexplained; that run loaded both pollutants, the check loaded ozone only.
3. The second-session OpenMP load table (all six thread counts, one interleaved session, 10 rounds, battery): medians 23.88 / 13.70 / 8.18 / 7.78 / 6.61 / 6.11 s at 1/2/4/6/8/10 threads (speedup 1.00 / 1.74 / 2.92 / 3.07 / 3.61 / 3.91, efficiency 100 / 87 / 73 / 51 / 45 / 39%); footprint 1,643 / 1,753 / 1,874 / 2,095 / 2,113 / 2,130 MiB. The earlier mains-power session gave 24.06 / 8.70 / 6.94 s at 1/4/8 threads, within 6%.
4. Confirmed in code: the OpenMP `AosStore::countByValue` in `part-b/omp` still uses the original branchy per-row loop on the row layout, so comparing it with the library's column count mixes layout and loop effects.
5. Medians and MiB figures quoted from graphs match `results/final-loads.csv` exactly.

**Investigation of the two open points (battery power, relative comparisons only).**
- Callback +5% to +13%: cause is inlining. The indexed-search change left two loops in `scanValues`, so the callback lambda (batch push, virtual `onBatch`) is inlined twice. Variant A (move the `ranges` vector into the partial branch): callback 112 ms vs 66 ms current (much worse). Variant B (one loop for both cases, whole-file segments as one range; `results/single-call-site-scanvalues.patch`): rows callback 53.7 ms vs 66.0 current and 58.4 pre-change; copy 60.0 vs 58.7 (+2%); virtual unchanged. Columns: callback 63.9 vs 85.1 ms (-25%), copy 81.4 vs 73.1 (+11%), virtual +2%, count unchanged. All 9 tests pass with B. Not adopted, so the measured binary matches the committed code (rebuilt binary is byte-identical to the measured one).
- High row-returning times in the final run: not reproduced. (a) Both pollutants loaded: current 59.4 ms vs Phase 1 57.7 ms (25M-row copy); (b) 5 min of sustained OpenMP loads then copy: 58.8 ms vs 59.4 cool; (c) pre-change build 58.5 vs current 59.6. (d) Charger attached ("AC attached, not charging", 68%): copy 58.0 to 59.0 ms, Phase 1 57.7 to 58.3 ms. Still unexplained. Untested: actively charging, background activity (e.g. Spotlight) during the final run.

## Open items

- Phase 2 Step 1 (`part-b/omp`), Step 2 (`part-b/lib`), the single-pass tokenizer and indexed time-range search are done and measured on M-A. **OpenMP is not combined with the library/column layout/new tokenizer** (`omp/` is a separate copy of the Phase 1 design). Suggested but not done: use the single-pass tokenizer in `omp/` and compare; add OpenMP to `part-b/lib`. The report must state this plainly.
- ThreadSanitizer is not clean (Windows and macOS runs); see the section above.
- Individual contributions and the AI-assisted Phase 1 code decision: undecided; needed for the report.
