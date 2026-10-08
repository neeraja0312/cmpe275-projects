# Mini 1 handoff

Phase 1 code is done, tested and on `main` (commit `c243196`). Still to do:
the Phase 1 benchmarks, all of Phase 2, the report and the slide.
Build, run and dataset instructions are in [README.md](README.md). The
assignment is in [mini1-edges.md](mini1-edges.md).

## Decide first: AI-written code

The Phase 1 C++ in `part-a/` was written with an AI assistant. The spec says AI
"should not be used to write your code", and the report needs an individual
contributions section. As a team, decide how to handle this (disclose it,
rewrite parts, or both) before building on it.

## What exists

| Area | State |
|---|---|
| Dataset | 12 EPA CSVs: 65,742,181 rows, 15.5 GB. `scripts/get_data.sh` downloads them and checks the pinned SHA-256 sums |
| Build | CMake with Homebrew Clang (Apple's clang is rejected on purpose); OpenMP is found for `part-b` |
| `part-a/` (Phase 1) | Serial library, the `mini1_a` driver, 5 test programs, all passing |
| `part-b/` (Phase 2) | Empty folders and CMake only: `omp/`, `lib/`, `app/`, `tests/` |
| Scripts | `run_bench.sh` (repeated runs), `plot.py` (graphs), `package.sh` (submission tar.gz) |
| `report/` | Empty |

### Phase 1 design

- **One row = 12 bytes:** a `Measurement` holding a `uint16` monitor id, a `uint32` hour since 2021-01-01, an `int16` value and two `uint8` codes.
- **Exact readings:** ozone is stored as ppm × 1000 and NO₂ as ppb × 10, both exact integers.
- **Per-instrument data stored once.** `MonitorRegistry` holds codes, location and names for each of the 1,941 instruments. `Dictionary` holds the qualifier and method codes.
- **One row array, two indexes.** `AosStore` keeps every row in one array, plus per-file "segments" (used to skip files) and per-monitor "blocks" (used for binary search).
- **`AirQualityDB` is the facade.** It is the only class `main.cpp` uses.
- **Searches:**
  - Q1: one monitor + a time range.
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
- AddressSanitizer + UBSan are clean.
- The Clang static analyzer reports no bugs.
- clang-tidy (bugprone, performance and analyzer checks) is clean on the library code.

## Results so far

All measured on an Apple M5 (4 performance + 6 efficiency cores, 16 GB RAM) with a Release build and a warm file cache.

| Measurement | Result |
|---|---|
| Load 2024 (12.5M rows), 10 runs each | naive 16.60 ± 0.40 s · getline 9.29 ± 0.22 s · buffered 8.56 ± 0.12 s |
| Load everything, buffered (single run) | 44.8 s; storage 810 MB = 12.3 bytes/row (CSV is 15.5 GB) |
| Where load time goes (`sample` profiler) | splitting lines into fields ~78%, number parsing ~12%, file reading ~6% |
| Q1, one monitor, 3 hours | ~0.4 µs |
| Q2 count, ozone > 0.070 ppm, all years | 22 ms over 47M rows |
| Q2, all ozone readings in one hour of 2024 | 7.4 ms: scans 9.0M rows to return 1,205 |
| Experiment: every field as `std::string` | 638 bytes/row, so ~41.9 GB for the dataset (won't fit in 16 GB) |
| Experiment: `float` values vs `> 0.070` | all 9,348 readings of exactly 0.070 ppm (2024) wrongly count as above; `double` and our integers get 0 wrong |

The `results/` folder is git-ignored, so re-run the benchmarks to get the CSVs
and graphs (see the README).

## Known problems

1. **Peak memory numbers are wrong on macOS.** `peak_rss_bytes` (from `getrusage`) leaves out memory that macOS has compressed. After a full load it reports ~200 MB for an 810 MB structure, with ±25 MB swings between runs. Fix this before relying on any memory graph: report peak physical footprint instead (`task_info` with `TASK_VM_INFO`, the figure Activity Monitor shows). Until then, use `store_bytes`.
2. **Time-only searches scan everything.** Rows are ordered by monitor and then time, so "all readings in this hour" without a monitor (via Q2 with a wide value range) can only skip whole files. Binary-searching each monitor's blocks (~1,300 small searches) should be much faster. This is planned as a Phase 2 improvement, with the 7.4 ms above as the baseline.
3. **Line splitting is the load bottleneck** (~78%). `CsvTokenizer::split` calls `find` twice per field, about 600M short `memchr` calls for 2024. A single-pass tokenizer should cut load time a lot (not measured yet).

## To do, in order

1. **Fix memory measurement** (problem 1), in `part-a/src/MemoryUsage.cpp`.
2. **Phase 1 baseline benchmarks** (10+ runs each):
   - Loads: full dataset, each loader, with and without `--no-reserve`.
   - Searches: these take milliseconds, so time them inside one process with `--repeat 10` and read `query_s_min`/`query_s_mean`. `run_bench.sh` times the whole process, which is mostly loading.
     - Q1 in each mode (`copy`, `view`, `scan`).
     - Q2 in each mode at several selectivities, e.g. NO₂ > 100 ppb (3 rows), ozone > 0.070 (0.7%), ozone > 0.030 (about half).
     - Q3 with each grouping.
   - Log every result and every failed attempt in `report/notes.md`.
3. **Phase 2 Step 1 (`part-b/omp/`):** copy the Phase 1 sources there and add OpenMP.
   - Start with the searches: Q2 count is a `reduction`; Q3 uses per-thread `Stats`, merged at the end; Q2 copy uses per-thread result lists, merged in order.
   - Then loading: split files into chunks at newlines; each thread gets its own registry and dictionaries, then merge and renumber the IDs.
   - Leave Q1 serial and measure why (thread start-up costs more than the 0.4 µs query).
   - Benchmark with `OMP_NUM_THREADS=1,2,4,6,8,10` and plot speedup against the Phase 1 baseline. Expect a bend after 4 threads (the efficiency cores).
   - Check for data races with `-DMINI1_SANITIZE=thread`.
4. **Phase 2 Step 2 (`part-b/lib` + `part-b/app`):**
   - Move to a column layout (SoA): Q2 then reads 2 bytes per row instead of 12.
   - Add the time-only search (problem 2) and the single-pass tokenizer (problem 3).
   - Hide the internals behind `lib/include`, compare static vs shared builds (`-DBUILD_SHARED_LIBS=ON`), and re-measure the result-return modes.
5. **Report, slide and submission:**
   - Report: paragraph form, with tables and graphs, failures, citations (EPA FileFormats page) and individual contributions.
   - Slide: exactly one finding. Candidates are the float-at-0.070 result, threads vs layout from Phase 2, or the macOS memory measurement trap.
   - Submit `scripts/package.sh <team>`; the archive excludes the dataset.

## Rules to keep

- Never commit the dataset; git already ignores `Dataset/`, `*.csv` and `*.zip`.
- Benchmark only Release builds, never sanitizer or Debug builds.
- Pass the data path on the command line (`../Dataset`); don't hard-code it.
- Re-run `ctest` (and the full test with `MINI1_FULL_TEST=1`) after changing the loader or the storage.
