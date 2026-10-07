# Mini 1: Edge Sensitivity at Package Level Code

Focus: data structures, memory, and "edges" — how data moves between
libraries inside a single process.

The full assignment is in [mini1-edges.md](mini1-edges.md) (v0.2a).

## Requirements checklist

Taken from the spec. Tick these off as the team completes them.

### Data
- [x] More than 2 million records — ~65.7M rows (see [Data set](#data-set))
- [x] Larger than 12 GB — ~15.5 GB across 12 CSVs
- [x] Not NYC 311 / violations / citations / collisions — EPA AQS data
- [ ] Data is **not** included in the submission archive

### Phase 1 — `part-a/` (serial)
- [ ] C/C++ process that consumes the data set
- [ ] Fields represented as their primitive types (ints, floats, …)
- [ ] Single project, "rushed" — everything together
- [ ] Object Oriented Design (classes), abstraction, usable as a library
      (virtual classes / templates / patterns such as facade)
- [ ] APIs for data reading and basic range searching
- [ ] **No threads**
- [ ] Tests for parsing and searches (`part-a/tests/`) — validation & verification
- [ ] Benchmark against the data set, 10+ runs per measurement, averaged
- [ ] Record successes **and** failures — this is the baseline

### Phase 2 — `part-b/` (parallel + libraries)
- [ ] Step 1 (`part-b/omp/`): apply parallelization (OpenMP / threads) to the
      Phase 1 code; compare to Phase 1 baseline
- [ ] Step 2 (`part-b/lib/` + `part-b/app/`): rewrite data classes into parallel-optimized library(ies), built
      separately and linked to the app (`main`)
- [ ] Library internals isolated from the application
- [ ] File data broken into the right data structures (grouping/layout)
- [ ] Benchmark each modification separately
- [ ] Goal: edge performance (data crossing the library ↔ app boundary)

### Tools and restrictions
- [ ] Compiler: g++ ≥ 13 or Clang ≥ 16 (**not** Apple's Xcode clang) — enforced in CMake
- [ ] Build with CMake
- [ ] No third-party libraries or services (no databases); Boost only if needed
- [ ] CSV parsing and plotting tools are allowed; Python allowed for graphs
- [ ] Do not run/test from an IDE's VM
- [ ] AI not used to write the code

### Deliverables (one `tar.gz` per team, e.g. `teamducks.tar.gz`)
- [ ] Code (both phases, in separate directories)
- [ ] Report: paragraph format; approach, results (tables **and** graphs),
      failed attempts, conclusions, citations, individual contributions
- [ ] Presentation: **exactly one slide** about **one** unique finding —
      not a summary, not a class diagram, not a tutorial
- [ ] No test data in the archive

## Data set

EPA Air Quality System (AQS) pre-generated hourly ("Raw") data:

- Download: <https://aqs.epa.gov/aqsweb/airdata/download_files.html#Raw>
- Column definitions: <https://aqs.epa.gov/aqsweb/airdata/FileFormats.html> (hourly format)

| Pollutant | Parameter code | Files | Rows | Size |
|---|---|---|---|---|
| Ozone | 44201 | `hourly_44201_2021.csv` … `2026.csv` | 47,005,021 | ~10.7 GB |
| NO₂ | 42602 | `hourly_42602_2021.csv` … `2026.csv` | 18,737,160 | ~4.8 GB |

2026 is partial (through 2026-05-31). Both pollutants share the same 24-column
layout. Units: ozone in ppm, NO₂ in ppb.

The data is **not** in git (~540 MB zipped, ~15.5 GB unzipped). Fetch it with
the download script, run from `Minis/mini1/`:

```bash
scripts/get_data.sh            # download 12 zips from EPA, verify, unzip
scripts/get_data.sh --verify   # check an existing copy, no download
```

It produces:

```
Minis/Dataset/
├── ozone/   hourly_44201_<year>.csv
└── no2/     hourly_42602_<year>.csv
```

Every zip and CSV is checked against SHA-256 checksums pinned in
`scripts/dataset-zips.sha256` and `scripts/dataset-csvs.sha256`, so all team
members benchmark byte-identical data. The pinned versions were downloaded from
EPA in June 2026 and re-checked against EPA on 2026-10-07. If EPA later revises
a file (most likely 2026), the script prints a checksum warning.

Programs take the data directory as a command-line argument instead of a
hard-coded path.

## Layout

```
mini1/
├── mini1-edges.md      assignment spec
├── cmake/
│   └── Mini1Common.cmake   shared settings: C++20, compiler check, warnings,
│                           sanitizers, test helper
├── part-a/             Phase 1: serial, single project       -> mini1_a
│   ├── include/
│   ├── src/            src/main.cpp is the entry point
│   └── tests/          one test program per .cpp (ctest)
├── part-b/             Phase 2
│   ├── omp/            step 1: Phase 1 code + OpenMP         -> mini1_b_omp
│   │   ├── include/
│   │   └── src/        omp/src/main.cpp is the entry point
│   ├── lib/include/    step 2: public headers (the library's API / edge)
│   ├── lib/src/        step 2: library internals             -> libaqdata
│   ├── app/            step 2: main, links the library       -> mini1_b
│   └── tests/          tests against the library (ctest)
├── scripts/
│   ├── run_bench.sh    run a command N times, record time + peak memory
│   ├── plot.py         summarize results/*.csv, draw graphs
│   ├── package.sh      build the submission tar.gz (no data)
│   ├── get_data.sh     download + verify the dataset from EPA
│   └── dataset-*.sha256  pinned checksums (zips and CSVs)
├── results/            benchmark output (git-ignored)
└── report/             report + one-slide presentation
```

## Setup (macOS)

Apple's clang is not allowed, so install Homebrew LLVM (includes OpenMP):

```bash
brew install cmake llvm libomp
```

Graphs need matplotlib. Homebrew's Python blocks global `pip install`, so use
a virtual environment in `Minis/mini1/` (git-ignored, excluded from the archive):

```bash
python3 -m venv .venv
.venv/bin/pip install matplotlib
.venv/bin/python scripts/plot.py
```

## Build

```bash
# Phase 1
cmake -S part-a -B part-a/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"
cmake --build part-a/build

# Phase 2
cmake -S part-b -B part-b/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"
cmake --build part-b/build
```

Builds default to `Release`. Targets are only created once their folders
contain `.cpp` files; until then CMake prints a warning.

Options (add to the `cmake -S ...` line):

| Option | Effect |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build (no optimization, symbols) |
| `-DMINI1_SANITIZE=address` | AddressSanitizer + UBSan — memory errors, overflows |
| `-DMINI1_SANITIZE=thread` | ThreadSanitizer — data races (Phase 2) |
| `-DBUILD_SHARED_LIBS=ON` | part-b only: build `aqdata` as a shared library |
| `-DMINI1_DATA_DIR=/path` | Data directory passed to tests (default `Minis/Dataset`) |

Use a separate build directory per configuration (e.g. `part-a/build-asan`)
and never benchmark a sanitizer or Debug build. Valgrind and perf do not run
on Apple Silicon; sanitizers, Instruments and `leaks` are the macOS
alternatives.

## Test

Each `tests/*.cpp` is built as its own program and registered with CTest.
A test gets the data directory as `argv[1]` and passes by returning 0.

```bash
ctest --test-dir part-a/build --output-on-failure
```

## Benchmark

Run from `Minis/mini1/`:

```bash
# 10 timed runs after one untimed warm-up run
scripts/run_bench.sh --warmup a-load 10 -- part-a/build/mini1_a ../Dataset

# Summary table + graphs (results/summary_time.png, summary_rss.png)
.venv/bin/python scripts/plot.py
```

Each run records wall/user/sys time and peak resident memory into
`results/<label>.csv`. The OS page cache affects repeated reads of the CSVs;
note whether results are cold or warm.

## Submission

```bash
scripts/package.sh <team-name>    # -> Minis/<team-name>.tar.gz
```
