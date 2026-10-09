# Mini 1 handoff

Phase 1 is finished: code, tests, memory-measurement fix and the full-dataset
baseline benchmarks. Still to do: all of Phase 2, the graphs, the report and the
slide. Build, run and dataset instructions are in [README.md](README.md). The
assignment is in [mini1-edges.md](mini1-edges.md).

## Decide first: AI-written code

The Phase 1 C++ in `part-a/` was written with an AI assistant. The spec says AI
"should not be used to write your code", and the report needs an individual
contributions section. As a team, decide how to handle this (disclose it,
rewrite parts, or both) before building on it. **Still undecided.**

## Team split for what is left

Two people share the remaining work (the Phase 1 author is done).

| Person | Owns |
|---|---|
| A (macOS) | Memory fix and Phase 1 baselines (done), Phase 2 Step 2 library + app (`part-b/lib`, `part-b/app`): column layout, pimpl facade, static vs shared, result-mode re-measurement, integration, all final benchmarks |
| W (Windows/WSL, gcc 13+) | Phase 2 Step 1 (`part-b/omp/`): OpenMP searches and loading, ThreadSanitizer run; single-pass tokenizer and time-only search as self-contained classes with tests |
| Both | Report, slide, individual contributions, final `ctest`, `scripts/package.sh` |

Rules: report benchmark numbers only from the macOS machine; W's runs check
correctness. No macOS-only code outside `MemoryUsage.cpp`. Each person works in
their own directories so merges do not conflict.

## What exists

| Area | State |
|---|---|
| Dataset | 12 EPA CSVs: 65,742,181 rows, 15.5 GB. `scripts/get_data.sh` downloads them and checks the pinned SHA-256 sums (EPA's server can reset the connection mid-download; re-running the script resumes) |
| Build | CMake with Homebrew Clang (Apple's clang is rejected on purpose); OpenMP is found for `part-b` |
| `part-a/` (Phase 1) | Serial library, the `mini1_a` driver, 5 test programs, all passing |
| `part-b/` (Phase 2) | Empty folders and CMake only: `omp/`, `lib/`, `app/`, `tests/`. CMake already builds `mini1_b_omp`, the library `aqdata` (static, or shared with `-DBUILD_SHARED_LIBS=ON`) and the app `mini1_b` once sources exist |
| Scripts | `run_bench.sh` (repeated runs; now also records `footprint_bytes`), `plot.py` (graphs), `package.sh` (submission tar.gz) |
| `report/` | Empty (`notes.md` still to be written) |

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

## Known problems (still open, planned for Phase 2)

1. **Time-only searches scan everything.** Rows are ordered by monitor and then time, so "all readings in this hour" without a monitor can only skip whole files. Binary-searching each monitor's blocks (~1,300 small searches) should be much faster. Baseline: 3.80 ms. Owner: W.
2. **Line splitting is the load bottleneck** (~78%). `CsvTokenizer::split` calls `find` twice per field, about 600M short `memchr` calls for 2024. A single-pass tokenizer should cut load time a lot (not measured yet). Owner: W.
3. **Per-row virtual calls and result copies are expensive** (see tables above); a column layout should help the first one for Q2.

## To do, in order

1. ~~Fix memory measurement~~ done.
2. ~~Phase 1 baseline benchmarks~~ done. Not done: Q4 was not benchmarked (not required), and the graphs (`plot.py` needs a venv: `python3 -m venv .venv && .venv/bin/pip install matplotlib`).
3. **Phase 2 Step 1 (`part-b/omp/`), owner W:** copy the Phase 1 sources there and add OpenMP.
   - Start with the searches: Q2 count is a `reduction`; Q3 uses per-thread `Stats`, merged at the end; Q2 copy uses per-thread result lists, merged in order.
   - Then loading: split files into chunks at newlines; each thread gets its own registry and dictionaries, then merge and renumber the IDs.
   - Leave Q1 serial and measure why (thread start-up costs more than the 0.4 µs query).
   - Benchmark with `OMP_NUM_THREADS=1,2,4,6,8,10` and plot speedup against the Phase 1 baseline above. Expect a bend after 4 threads (the efficiency cores). Final timings are taken on the macOS machine.
   - Check for data races with `-DMINI1_SANITIZE=thread`.
4. **Phase 2 Step 2 (`part-b/lib` + `part-b/app`), owner A**, in stages so each change is measured on its own:
   - Design decisions already made:
     - **Pimpl facade:** `lib/include` holds only the public value types and one `Database` class with a private implementation. The store, loader, registry and tokenizer are private in `lib/src`, so the app cannot include them.
     - **Q1 `view` on a column layout** returns a column slice (a struct of spans for hours, values, qualifiers and methods), which stays zero-copy.
   - Stage 1: port Phase 1 behind the facade, still with the array-of-structs store. Compare static vs shared builds (`-DBUILD_SHARED_LIBS=ON`) and measure the cost of the library boundary.
   - Stage 2: add the column layout (SoA) as another `MeasurementStore`: Q2 then reads 2 bytes per row instead of 12. Re-measure all result-return modes.
   - Stage 3: integrate W's single-pass tokenizer and time-only search, then OpenMP inside the library, and measure the combination.
5. **Report, slide and submission:**
   - Report: paragraph form, with tables and graphs, failures, citations (EPA FileFormats page) and individual contributions. Keep a committable log of results and failed attempts in `report/notes.md`.
   - Slide: exactly one finding. Candidates are the memory-measurement trap (reproduced above, with the nuance that RSS is wrong or noisy, not always 10× low), the float-at-0.070 result, `--no-reserve` doubling peak memory for free, or threads vs layout from Phase 2.
   - Submit `scripts/package.sh <team>`; the archive excludes the dataset.

## Rules to keep

- Never commit the dataset; git already ignores `Dataset/`, `*.csv` and `*.zip`. `results/` is also ignored.
- Benchmark only Release builds, never sanitizer or Debug builds.
- Benchmark on one machine and name it in every table.
- Pass the data path on the command line (`../Dataset`); don't hard-code it.
- Do not change `part-a/` behavior: it is the baseline. Re-run `ctest` (and the full test with `MINI1_FULL_TEST=1`) after changing the loader or the storage.
