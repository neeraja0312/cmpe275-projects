# Mini 1: Edge Sensitivity at Package Level Code

C++ library that loads EPA hourly air-quality data (ozone and NO₂, 2021–2026)
and searches it. The assignment is in [mini1-edges.md](mini1-edges.md).

- `part-a/`: Phase 1, serial
- `part-b/`: Phase 2, libraries and layouts (`lib/`, `app/`, `tests/`: done); OpenMP (`omp/`: not started)

## Dataset

EPA Air Quality System (AQS) hourly data:

- Download page: <https://aqs.epa.gov/aqsweb/airdata/download_files.html#Raw>
- Column definitions: <https://aqs.epa.gov/aqsweb/airdata/FileFormats.html>

| Pollutant | Code | Files | Rows | Size | Units |
|---|---|---|---|---|---|
| Ozone | 44201 | `hourly_44201_2021.csv` … `2026.csv` | 47,005,021 | ~10.7 GB | ppm |
| NO₂ | 42602 | `hourly_42602_2021.csv` … `2026.csv` | 18,737,160 | ~4.8 GB | ppb |

2026 covers January 1 to May 31 only. The data is not in git. From
`Minis/mini1`, download it (~540 MB) and verify it against the pinned
checksums:

```bash
scripts/get_data.sh            # download, verify, unzip into Minis/Dataset
scripts/get_data.sh --verify   # check an existing copy
```

```
Minis/Dataset/
├── ozone/   hourly_44201_<year>.csv
└── no2/     hourly_42602_<year>.csv
```

A **monitor** is one instrument: State + County + Site Num + Parameter Code +
POC, written as `SS-CCC-SSSS-PPPPP-POC` (e.g. `06-037-1103-44201-1`).

## Setup (macOS)

Apple's clang is not allowed, so use Homebrew LLVM:

```bash
brew install cmake llvm libomp
python3 -m venv .venv && .venv/bin/pip install matplotlib   # for graphs
```

## Build

From `Minis/mini1`:

```bash
cmake -S part-a -B part-a/build -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"
cmake --build part-a/build
```

Add `-DMINI1_SANITIZE=address` (in a separate build folder) for a
memory-checked build. Benchmark only the default Release build.

## Run

```bash
part-a/build/mini1_a <data-path> [options] <command> [args...]
part-a/build/mini1_a --help
```

| Command | Does |
|---|---|
| `load` | Load and print statistics |
| `monitors [ozone\|no2]` | List monitors |
| `q1 <monitor> <from> <to>` | One monitor's readings in a time range |
| `q2 <ozone\|no2> <lo> <hi> [<from> <to>]` | Readings with `lo <= value <= hi` |
| `q3 <ozone\|no2> <from> <to> [none\|monitor\|hour]` | Count / min / max / mean |
| `q4 state <code>` or `q4 box <latMin> <latMax> <lonMin> <lonMax>` | Monitors by place |

- Times are `YYYY-MM-DD` or `YYYY-MM-DDTHH`, and ranges are `[from, to)`.
- Common options:
  - `--years 2024` loads one year (about 9 s instead of about 45 s for everything).
  - `--pollutant ozone|no2` loads one pollutant.
  - `--mode` picks how results are returned.
  - `--repeat N` repeats the query.

Examples:

```bash
A=part-a/build/mini1_a
$A ../Dataset load
$A ../Dataset --years 2024 q1 06-037-1103-44201-1 2024-07-01 2024-07-01T03
$A ../Dataset --years 2024 q2 ozone 0.071 1
$A ../Dataset --years 2024 q2 ozone -1 1 2024-07-04T12 2024-07-04T13
$A ../Dataset --years 2024 q3 ozone 2024-01-01 2025-01-01 hour
$A ../Dataset --years 2024 q4 state 6 ozone
```

Q2 limits are inclusive: "above 0.070 ppm" is `0.071`. To get every reading in
a time window regardless of value, use a wide range (`-1 1` for ozone,
`-100 3000` for NO₂).

## Test

```bash
ctest --test-dir part-a/build --output-on-failure
MINI1_FULL_TEST=1 ctest --test-dir part-a/build -R full --output-on-failure   # all 12 files, ~1 min
```

## Benchmark

```bash
scripts/run_bench.sh --warmup a-load 10 -- part-a/build/mini1_a ../Dataset --years 2024 load
.venv/bin/python scripts/plot.py
```

`run_bench.sh` runs a command 10 times after an untimed warm-up and saves
time and peak memory to `results/<label>.csv`. `plot.py` prints averages and
draws graphs.

## Submission

```bash
scripts/package.sh <team-name>    # -> Minis/<team-name>.tar.gz, without the dataset
```
