# Mini 1: Edge Sensitivity at Package Level Code

CMPE 275, Mini 1. Team: Vandan Shah, Neeraja Buch, Dhruv Jain.

A C++ library that loads the EPA hourly air-quality data (ozone and NO₂,
2021–2026: 65,742,181 readings, 15.5 GB of CSV) and answers searches over it.
The assignment is in [mini1-edges.md](mini1-edges.md).

## What this project is about

The assignment studies **edges**: how data moves between a library and the
program that uses it, inside one process. We built the library twice, measured
every change, and used it to answer three questions:

1. What does the boundary between a library and its caller cost? (We built the
   library as static and shared, with its internals hidden behind one class.)
2. What does it cost to move data across that boundary? (Each search can return
   a count, a copy, a view, batched callbacks, or one virtual call per row.)
3. Does the best way to store the data depend on the query? (Row layout against
   column layout.)

Headline results (MacBook Air, Release builds, details and caveats in the report):

| Result | Value |
|---|---|
| Static vs shared library | no measurable difference (about 1.00×) |
| Serial full load, Phase 1 → single-pass tokenizer | 23.6 s → 10.3 s |
| Full load, OpenMP at 8 threads (old tokenizer) | 6.9 s, at about 2.6× the memory |
| Value count over 47M readings, Phase 1 → column layout | 11.4 ms → 2.7 ms (about 4× faster) |
| Copy one monitor-year of readings, column vs row layout | about 11× slower |
| Search for all readings in one hour, Phase 1 → indexed | 3.8 ms → 0.13 ms (column layout) |

Known limits: OpenMP was not combined with the library, ThreadSanitizer is not
clean on the OpenMP code, and some timings vary by up to 25% between sessions.
The report explains each one.

## Repository layout

```
Minis/mini1/
├── part-a/      Phase 1: serial, object-oriented library + driver (mini1_a)
│   ├── include/ src/ tests/
├── part-b/      Phase 2
│   ├── omp/     Step 1: Phase 1 design with OpenMP (mini1_b_omp), own tests
│   ├── lib/     Step 2: the library "aqdata"
│   │   ├── include/aqdata/   public headers, all an application can see
│   │   └── src/              private internals (stores, loader, tokenizer, registries)
│   ├── app/     the program that links the library (mini1_b)
│   └── tests/   tests of the library
├── cmake/       build rules shared by both parts
├── scripts/     dataset download and checksums, benchmark runner, packaging
├── report/      the report (PDF) and the one-slide presentation
└── mini1-edges.md   the assignment
```

Three programs share the same commands and answer the same questions, so
their timings are comparable: `mini1_a` (Phase 1 baseline), `mini1_b_omp`
(threaded copy of the Phase 1 design) and `mini1_b` (the Phase 2 library).

## How the code is organized

- **Data model.** One reading is a 12-byte `Measurement` (hour, monitor id,
  value, two codes). Values are stored as exact integers (ozone ppm × 1000,
  NO₂ ppb × 10). Per-monitor details are stored once in a `MonitorRegistry`.
- **Loading.** A reader (naive, getline or buffered) supplies lines, a
  tokenizer splits them at commas, a row builder converts text to numbers, and
  rows are appended to the store in batches.
- **Storage.** One array of rows (`AosStore`) or one array per field
  (`ColumnStore`), each with two small indexes: one entry per file (to skip
  files) and one per monitor (to binary-search by time).
- **Searches.** Q1 (one monitor and a time range), Q2 (a value range), Q3
  (count, min, max, mean, optionally grouped) and Q4 (monitors by state or box),
  with several ways to return each result.
- **Library boundary.** The application includes only `aqdata/*.hpp`. The
  public `Database` class holds a pointer to a hidden implementation, and the
  shared library exports only the functions marked `AQ_API`.

## Dataset

EPA Air Quality System (AQS) hourly data:

- Download page: <https://aqs.epa.gov/aqsweb/airdata/download_files.html#Raw>
- Column definitions: <https://aqs.epa.gov/aqsweb/airdata/FileFormats.html>

| Pollutant | Code | Files | Rows | Size | Units |
|---|---|---|---|---|---|
| Ozone | 44201 | `hourly_44201_2021.csv` … `2026.csv` | 47,005,021 | ~10.7 GB | ppm |
| NO₂ | 42602 | `hourly_42602_2021.csv` … `2026.csv` | 18,737,160 | ~4.8 GB | ppb |

2026 covers January 1 to May 31 only. The data is not part of the repository
or the submission. From `Minis/mini1`, download it (~540 MB of zip files) and
verify it against the pinned checksums:

```bash
scripts/get_data.sh            # download, verify, unzip into Minis/Dataset
scripts/get_data.sh --verify   # check an existing copy
```

```
Minis/Dataset/
├── ozone/   hourly_44201_<year>.csv
└── no2/     hourly_42602_<year>.csv
```

EPA's server sometimes resets the connection during a download. If the script
stops, run it again: it fetches only what is missing.

A **monitor** is one instrument: State + County + Site Num + Parameter Code +
POC, written as `SS-CCC-SSSS-PPPPP-POC` (e.g. `06-037-1103-44201-1`).

## Requirements and setup

Required by the assignment: gcc/g++ 13 or newer, or Clang 16 or newer (not
Apple's Xcode clang), and CMake. The build refuses Apple's clang on purpose.
No third-party libraries are used.

macOS (Homebrew):

```bash
brew install cmake llvm libomp
```

Linux / WSL: GCC 13 or newer, CMake and OpenMP (part of GCC).

## Build

All commands run from `Minis/mini1`. The default build type is Release, and the
only one to benchmark.

**Phase 1:**

```bash
cmake -S part-a -B part-a/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"
cmake --build part-a/build
```

**Phase 2** (builds `aqdata`, `mini1_b`, `mini1_b_omp` and the tests):

```bash
cmake -S part-b -B part-b/build \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DOpenMP_ROOT="$(brew --prefix libomp)"
cmake --build part-b/build
```

- Shared library instead of static: configure a separate folder with
  `-DBUILD_SHARED_LIBS=ON` (for example `part-b/build-shared`).
- Memory-checked build: a separate folder with `-DMINI1_SANITIZE=address`, or
  `-DMINI1_SANITIZE=thread` for ThreadSanitizer (use Debug, never benchmark
  these; ThreadSanitizer reports false alarms inside the OpenMP runtime).
- Data location for the tests: `-DMINI1_DATA_DIR=/path/to/Dataset`
  (default `Minis/Dataset`).

Linux / WSL (GCC 13+), keeping generated files on the Linux filesystem when the
source is under `/mnt/c`:

```bash
cmake -S part-b -B ~/cmpe275-build/part-b -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Release -DMINI1_DATA_DIR="$HOME/cmpe275-data"
cmake --build ~/cmpe275-build/part-b --target mini1_b_omp -j 8
OMP_NUM_THREADS=8 ~/cmpe275-build/part-b/mini1_b_omp "$HOME/cmpe275-data" load
ctest --test-dir ~/cmpe275-build/part-b --output-on-failure -R '^omp_'
```

## Run

```bash
part-a/build/mini1_a   <data-path> [options] <command> [args...]    # Phase 1
part-b/build/mini1_b   <data-path> [options] <command> [args...]    # Phase 2 library
OMP_NUM_THREADS=8 part-b/build/mini1_b_omp <data-path> [options] <command> [args...]
<program> --help
```

| Command | Does |
|---|---|
| `load` | Load and print statistics |
| `monitors [ozone\|no2]` | List monitors |
| `q1 <monitor> <from> <to>` | One monitor's readings in a time range |
| `q2 <ozone\|no2> <lo> <hi> [<from> <to>]` | Readings with `lo <= value <= hi` |
| `q3 <ozone\|no2> <from> <to> [none\|monitor\|hour]` | Count / min / max / mean |
| `q4 state <code>` or `q4 box <latMin> <latMax> <lonMin> <lonMax>` | Monitors by place |

Options:

- `--years 2024` (or `2023-2025`) loads fewer years (about 2 s for one year in
  the Phase 2 library instead of about 10 s for everything).
- `--pollutant ozone|no2` loads one pollutant.
- `--mode` picks how results are returned: Q1 `copy|view|scan`, Q2
  `count|copy|callback|virtual`.
- `--repeat N` repeats the query N times in one process and reports the minimum
  and mean time (use this to time searches, because every process loads the data first).
- `--loader naive|getline|buffered` picks the file reader (default buffered).
- `--layout aos|columns` picks the storage layout (`mini1_b` only).
- `--no-reserve` turns off pre-sizing of the row array (it doubles peak memory).
- `--print N` prints up to N result rows.

Times are `YYYY-MM-DD` or `YYYY-MM-DDTHH`, and ranges are `[from, to)`.

Examples (Phase 2 library):

```bash
B=part-b/build/mini1_b
$B ../Dataset load
$B ../Dataset --years 2024 q1 06-037-1103-44201-1 2024-07-01 2024-07-01T03
$B ../Dataset --layout columns --mode count q2 ozone 0.071 1
$B ../Dataset --years 2024 q2 ozone -1 1 2024-07-04T12 2024-07-04T13
$B ../Dataset --years 2024 q3 ozone 2024-01-01 2025-01-01 hour
$B ../Dataset --years 2024 q4 state 6 ozone
```

Q2 limits are inclusive: "above 0.070 ppm" is `0.071`. To get every reading in
a time window regardless of value, use a wide range (`-1 1` for ozone,
`-100 3000` for NO₂).

## Test

```bash
ctest --test-dir part-a/build --output-on-failure      # 5 tests
ctest --test-dir part-b/build --output-on-failure      # 9 tests (library, layouts, OpenMP)
MINI1_FULL_TEST=1 ctest --test-dir part-b/build -R full --output-on-failure   # all 12 files, about 1 minute
```

The full-dataset tests reproduce the known totals (65,742,181 rows, 0 bad rows,
47,005,021 ozone and 18,737,160 NO₂ readings). `test_layouts_agree` checks that
the row and column layouts return identical results for every search and mode.

## Benchmark

```bash
scripts/run_bench.sh --warmup a-load 10 -- part-a/build/mini1_a ../Dataset load
```

`run_bench.sh` runs a command 10 times after an untimed warm-up and saves time
and peak memory to `results/<label>.csv` (the folder is created). Method used
for the report:

- Release builds only, on one machine, with the data already downloaded.
- Loads are timed as whole processes (one warm-up, then 10 runs). When two
  builds are compared, alternate them run by run so drift affects both.
- Searches are timed inside one process with `--repeat`, using the minimum.
- Memory is the physical footprint, printed as `peak_footprint_bytes` and
  recorded in the `footprint_bytes` column. On macOS the usual resident-size
  figures are unreliable for this workload.

## Troubleshooting

- *"Apple clang is not allowed"*: configure with
  `-DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"`.
- *OpenMP not found on macOS*: add `-DOpenMP_ROOT="$(brew --prefix libomp)"`.
- *Tests skip or fail to find data*: pass `-DMINI1_DATA_DIR=/path/to/Dataset`
  when configuring, and run `scripts/get_data.sh --verify`.
- *Timings differ from the report*: use a Release build, a warm cache and mains
  power, and compare ratios inside one session. Absolute times differ by machine.

## Submission

```bash
scripts/package.sh <team-name>    # -> Minis/<team-name>.tar.gz
```

The archive holds the code, the report (`report/Report_mini1.pdf`) and the
one-slide presentation (`report/slide.pptx`). It leaves out the dataset, build
folders, benchmark data and working notes. Individual contributions and the
disclosure of AI assistance are in the report.
