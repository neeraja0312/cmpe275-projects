# Mini 1 handoff

Done: Phase 1; Phase 2 Step 1 (`part-b/omp`); Phase 2 Step 2 (library, column
layout, static vs shared); the single-pass tokenizer; indexed constrained-time
queries; correctness tests; and Windows/WSL benchmarks. Still to do: integrate
OpenMP into `part-b/lib`, run the final macOS benchmarks, then finish the report,
slide and submission.
Build, run and dataset instructions are in [README.md](README.md). The assignment
is in [mini1-edges.md](mini1-edges.md).

## Decide first: AI-written code

The Phase 1 C++ in `part-a/` was written with an AI assistant. The spec says AI
"should not be used to write your code", and the report needs an individual
contributions section. As a team, decide how to handle this (disclose it,
rewrite parts, or both) before building on it. **Decision for now: keep building
on it. Revisit before submission**, since the report's individual contributions
section still has to say who wrote what.

## Team split for what is left

Two people share the remaining work (the Phase 1 author is done).

| Teammate | Owns |
|---|---|
| macOS teammate (Apple-silicon Mac) | Done: memory fix, Phase 1 baselines, Phase 2 Step 2 (`part-b/lib`, `part-b/app`, tests, benchmarks, graphs). To do: integrate the Windows teammate's pieces into `part-b/lib`, OpenMP inside the library, all final benchmarks and graphs, re-run the Step 2 benchmarks cleanly on AC power, `scripts/package.sh` |
| Windows teammate (WSL, gcc 13+) | Done: `part-b/omp/` (OpenMP searches and file-parallel loading), single-pass tokenizer, indexed constrained-time search, tests, sanitizers and WSL benchmarks |
| Both | Report, slide, individual contributions, the AI-written-code decision |

Report split: the macOS teammate writes the Phase 1 baseline, memory
measurement, library/layout/static-vs-shared and final benchmark sections (all
the numbers). The Windows teammate writes the OpenMP, tokenizer and time-only
search sections (design, correctness, failures) and their own contributions
paragraph. Each person writes their own entry in the individual contributions
section. The slide finding is picked together.

What the Windows teammate hands over (so integration is mechanical):
- `part-b/omp/`: its own Phase 1 source copy building `mini1_b_omp`, with deterministic file-parallel loading and Q2/Q3 searches. ASan/UBSan passes; GCC ThreadSanitizer reports documented false positives across uninstrumented libgomp barriers while all checks complete successfully.
- Single-pass tokenizer: a class with the same interface as `CsvTokenizer` in `part-b/lib/src/CsvTokenizer.hpp` plus a test that it returns identical fields on the 2024 files. Drop-in replacement, no other file changes.
- Time-only search: a method on the store that gives the same results as `countByValue`/`selectByValue` for time-only queries, using a binary search in each monitor's blocks, plus a test comparing it to the scan on the 2024 data (`part-b/tests/test_layouts_agree.cpp` shows how).
- Windows/WSL results are labeled W-1 and kept separate from M-A. The macOS teammate still produces the final comparable report graphs. Nothing macOS-only lives outside `MemoryUsage.cpp`.

## What exists

| Area | State |
|---|---|
| Dataset | 12 EPA CSVs: 65,742,181 rows, 15.5 GB. `scripts/get_data.sh` downloads them and checks the pinned SHA-256 sums (EPA's server can reset the connection mid-download; re-running the script resumes) |
| Build | CMake with Homebrew Clang (Apple's clang is rejected on purpose); OpenMP is found for `part-b` |
| `part-a/` (Phase 1) | Serial library, the `mini1_a` driver, 5 test programs, all passing |
| `part-b/` (Phase 2) | **Step 1 done:** isolated `omp/` source, `mini1_b_omp`, five test programs, deterministic file-parallel loading and Q2/Q3. **Step 2 done:** `lib/` (`aqdata`, pimpl facade, row/column layouts, static/shared), `app/` (`mini1_b`) and tests. |
| Scripts | `run_bench.sh` (repeated runs; records `footprint_bytes`), `plot.py`, `plot_queries.py`, `plot_compare.py` (Phase 1 vs part-b graphs), `package.sh` (submission tar.gz) |
| `report/` | `notes.md`: committable lab log with all results, failures and the Step 2 findings. The report text itself is still to be written |

Step 2 results in one paragraph (details in `report/notes.md`): the library boundary and static vs shared cost nothing measurable (loads 23.6 s in all three variants when interleaved on AC power; searches within 1 to 2%). The column layout uses 16% less memory (669 vs 801 MB), makes narrow queries faster (Q2 `count` 11.4 to 2.73 ms after the loop fix, Q1 `scan` 2.5×) and makes row-returning modes slower (`copy` 1.25×, `callback`/`virtual` ~2×, Q1 `copy` 9×).

### Phase 1 design

- **One row = 12 bytes:** a `Measurement` holding a `uint16` monitor id, a `uint32` hour since 2021-01-01, an `int16` value and two `uint8` codes.
- **Exact readings:** ozone is stored as ppm × 1000 and NO₂ as ppb × 10, both exact integers.
- **Per-instrument data stored once.** `MonitorRegistry` holds codes, location and names for each of the 1,941 instruments. `Dictionary` holds the qualifier and method codes.
- **One row array, two indexes.** `AosStore` keeps every row in one array, plus per-file "segments" (used to skip files) and per-monitor "blocks" (used for binary search). It implements the `MeasurementStore` interface, so another layout can be added behind it.
- **`AirQualityDB` is the facade.** It is the only class `main.cpp` uses.
- **Searches:**
  - Q1: one monitor + a time range (`copy`, `view` and `scan` modes).
  - Q2: a value range, returned as count, copy, batched callback, or virtual call per row.
  - Q3: count/min/max/mean, grouped by nothing, by monitor, or by hour of day.
  - Q4: monitors in a state or a lat/lon box.
- **Three loaders** (`--loader naive|getline|buffered`) are kept so they can be compared.
- **Not stored:** MDL, Date of Last Change, Method Name and Uncertainty (the last is always empty).

### Verified correct

- `test_full_dataset` reproduces totals checked independently with Python on the raw CSVs:
  - 65,742,181 rows with 0 bad rows
  - 2024 monitors: 1,283 ozone and 473 NO₂
  - 3 NO₂ readings above 100 ppb in 2024
  - 62,074 ozone readings above 0.070 ppm in 2024
- Re-verified on the macOS machine after the memory fix: all 5 tests pass, including `MINI1_FULL_TEST=1` (about 23 s).
- AddressSanitizer + UBSan are clean (all 5 tests, plus a 2024 load of 12.5M rows with 0 bad rows). Re-run after the memory change. The full-dataset test was not repeated under the sanitizers (slow in Debug).
- The Clang static analyzer reports no bugs.
- clang-tidy (bugprone, performance and analyzer checks) is clean on the library code.

## Results so far

Phase 1 baselines, full dataset, Release build, macOS (Homebrew Clang 23.1.3, 16 GB RAM), 1 untimed warm-up then 10 timed runs. The first handoff numbers came from a different (slower) machine, so **never mix the two in one graph**; ratios between configurations agree on both machines. Raw CSVs are in `results/` (git-ignored); re-run to regenerate (see README). The driver is `results/run_baselines.sh` (also git-ignored; recreate from the commands below if needed).

### Loads (all 12 files)

| Loader | reserve | mean time | sd | peak footprint |
|---|---|---|---|---|
| buffered | on | 22.95 s | 0.79 (one 25.2 s first run) | 801 MB |
| buffered | off (`--no-reserve`) | 22.91 s | 0.14 | 1,606 MB |
| getline | on | 23.90 s | 0.15 | 792 MB |
| getline | off | 24.12 s | 0.11 | 1,598 MB |
| naive | on | 45.49 s | 4.17 (two outliers: 52.4 s and 54.3 s; the other eight 43.4–43.8 s) | 792 MB |
| naive | off | 43.48 s | 0.22 | 1,598 MB |

- Reserve costs nothing in time and halves peak memory (the row vector never has to double).
- `naive` is about 1.9× slower than `buffered`, the same ratio the first handoff measured on 2024 data (16.60 s vs 8.56 s).
- Storage is 810 MB = 12.3 bytes/row for 65.7M rows (CSV is 15.5 GB).
- Where load time goes (`sample` profiler, first handoff): splitting lines into fields ~78%, number parsing ~12%, file reading ~6%.

### Searches (full dataset, min of 10 in-process repeats)

| Q2 search | count | copy | callback | virtual | rows returned |
|---|---|---|---|---|---|
| NO₂ > 100 ppb (all years) | 4.49 ms | 4.67 ms | 4.69 ms | 13.07 ms | 38 |
| ozone > 0.070 ppm | 11.36 ms | 13.03 ms | 13.09 ms | 33.87 ms | 281,349 (0.60%) |
| ozone > 0.030 ppm | 11.34 ms | 57.83 ms | 62.62 ms | 62.79 ms | 25,272,790 (53.8%) |
| ozone, all readings in one hour of 2024 (time-only) | 3.80 ms | 3.81 ms | 3.80 ms | 34.27 ms | 1,205 |

- Q1 (one monitor, 3 hours = 3 rows): `view` 41 ns, `copy` 83 ns, `scan` 15.2 ms. One year of one monitor (8,676 rows): `view` 42 ns, `copy` 1.33 µs, `scan` 15.3 ms.
- Q3 (ozone, 2024, 9.0M rows): none 4.18 ms, by hour of day 5.04 ms, by monitor 25.14 ms.
- Counting costs the same at any selectivity (it scans every row). Copying 25.3M rows costs about 46 ms more than counting them: at high selectivity the result mode, not the scan, dominates.
- The per-row virtual call is 2.6–9× slower than the templated scans.
- **Time-only search baseline on this machine: 3.80 ms** (it scans 9.0M rows to return 1,205). The first handoff's 7.4 ms was on the other machine.

### Experiments (first handoff)

| Experiment | Result |
|---|---|
| Every field as `std::string` | 638 bytes/row, so ~41.9 GB for the dataset (won't fit in 16 GB) |
| `float` values vs `> 0.070` | all 9,348 readings of exactly 0.070 ppm (2024) wrongly count as above; `double` and our integers get 0 wrong |

### Memory measurement (fixed)

`getrusage` peak RSS (`peak_rss_bytes`) leaves out memory macOS has compressed.
It reported 80–116 MB for an ~810 MB structure, and `/usr/bin/time` RSS was
wrong or noisy too (95 MB in one of ten identical runs; 1.2–1.4 GB against a true
1.6 GB with `--no-reserve`). The program now also prints `peak_footprint_bytes`
(`task_info` with `TASK_VM_INFO`, the Activity Monitor figure), stable to within
1 MB between runs. `run_bench.sh` records it in a new `footprint_bytes` column
and `plot.py` graphs that column. `peak_rss_bytes` is kept unchanged.

## Phase 2 findings and remaining costs

1. **Indexed time ranges are done.** Binary-searching each monitor block made one-hour Q2 queries 33–43× faster and five-day queries 8–23× faster on W-1, with no whole-segment regression.
2. **Single-pass tokenization is done.** It made 2024 loads 1.76× and full loads 1.72× faster on W-1 with identical fields and memory use.
3. **Per-row virtual calls and result copies are expensive** (see tables above). The column layout helps `count` but not the row-returning modes (measured, see `report/notes.md`).

## To do, in order

1. ~~Fix memory measurement~~ done.
2. ~~Phase 1 baseline benchmarks~~ done. Not done: Q4 was not benchmarked (not required), and the graphs (`plot.py` needs a venv: `python3 -m venv .venv && .venv/bin/pip install matplotlib`).
3. ~~**Phase 2 Step 1 (`part-b/omp/`)**~~ done.
   - Q2 count uses reduction; Q2 copy uses ordered per-segment buckets; Q3 uses per-thread `Stats`; Q1 remains serial.
   - Loading parses files in parallel with worker-local registries/dictionaries, then merges and renumbers in discovery order. File-level parallelism reached 5.95× at 8 threads on W-1, so byte-chunking individual files was not added.
   - Benchmarked `OMP_NUM_THREADS=1,2,4,6,8,10`; query scheduling changed from contiguous to round-robin after the first run exposed pollutant imbalance.
   - ASan/UBSan passes. ThreadSanitizer's libgomp barrier false positives and successful checks are recorded in `report/notes.md`.
4. **Phase 2 Step 2 (`part-b/lib` + `part-b/app`), owner macOS teammate**, in stages so each change is measured on its own:
   - Design decisions already made:
     - **Pimpl facade:** `lib/include` holds only the public value types and one `Database` class with a private implementation. The store, loader, registry and tokenizer are private in `lib/src`, so the app cannot include them.
     - **Q1 `view` on a column layout** returns a column slice (a struct of spans for hours, values, qualifiers and methods), which stays zero-copy.
   - Stage 1: port Phase 1 behind the facade, still with the array-of-structs store. Compare static vs shared builds (`-DBUILD_SHARED_LIBS=ON`) and measure the cost of the library boundary.
   - Stage 2: add the column layout (SoA) as another `MeasurementStore`: Q2 then reads 2 bytes per row instead of 12. Re-measure all result-return modes.
   - Stages 1 and 2 are **done and measured**. The tokenizer and indexed-time changes are already in `part-b/lib`.
   - Stage 3: integrate the proven `part-b/omp` patterns inside the library and measure the combination.
   - Before the final numbers: re-run `results/run_all_stages.sh` on AC power (the Step 2 searches other than Q2 `count` were measured partly on battery, see `report/notes.md`).
5. **Report, slide and submission (both; split as in the team table):**
   - Revisit the AI-code question (top of this file) before writing the contributions entries.
   - Each teammate sets git `user.name` and `user.email` before committing (macOS teammate: done).
   - Report: paragraph form, with tables and graphs, failures, citations (EPA FileFormats page) and individual contributions. Keep a committable log of results and failed attempts in `report/notes.md`.
   - Slide: exactly one finding. Candidates are the memory-measurement trap (reproduced above, with the nuance that RSS is wrong or noisy, not always 10× low), the float-at-0.070 result, `--no-reserve` doubling peak memory for free, or threads vs layout from Phase 2.
   - Submission (macOS teammate): final `ctest` on both machines, then `scripts/package.sh <team>`; the archive excludes the dataset and private notes.

## Rules to keep

- Never commit the dataset; git already ignores `Dataset/`, `*.csv` and `*.zip`. `results/` is also ignored.
- Benchmark only Release builds, never sanitizer or Debug builds.
- Benchmark on one machine and name it in every table.
- Pass the data path on the command line (`../Dataset`); don't hard-code it.
- Do not change `part-a/` behavior: it is the baseline. Re-run `ctest` (and the full test with `MINI1_FULL_TEST=1`) after changing the loader or the storage.
