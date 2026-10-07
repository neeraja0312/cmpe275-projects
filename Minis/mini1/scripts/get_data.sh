#!/usr/bin/env bash
# Download the Mini 1 dataset (EPA AQS hourly ozone + NO2, 2021-2026) and
# verify it against the checksums pinned in this repo, so every teammate
# benchmarks identical data.
#
# Usage (from Minis/mini1):
#   scripts/get_data.sh                 download, verify, unzip into ../Dataset
#   scripts/get_data.sh --keep-zips     same, but keep the .zip files
#   scripts/get_data.sh --verify        only check existing CSVs (no download)
#   scripts/get_data.sh --dest DIR      use DIR instead of ../Dataset
#
# Result:
#   <dest>/ozone/hourly_44201_<year>.csv
#   <dest>/no2/hourly_42602_<year>.csv
#
# Pinned checksums:
#   scripts/dataset-zips.sha256   the 12 zip files as downloaded from EPA
#   scripts/dataset-csvs.sha256   the 12 extracted CSVs (what the code reads)
#
# EPA revises files over time (2026 in particular). A checksum mismatch means
# the file on EPA's site is no longer the version this project benchmarked.

set -euo pipefail

BASE_URL="https://aqs.epa.gov/aqsweb/airdata"
YEARS=(2021 2022 2023 2024 2025 2026)

script_dir="$(cd "$(dirname "$0")" && pwd)"
zip_sums="$script_dir/dataset-zips.sha256"
csv_sums="$script_dir/dataset-csvs.sha256"

dest="$script_dir/../../Dataset"
keep_zips=0
verify_only=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dest)      dest="$2"; shift 2 ;;
    --keep-zips) keep_zips=1; shift ;;
    --verify)    verify_only=1; shift ;;
    -h|--help)   sed -n '2,21p' "$0"; exit 0 ;;
    *)           echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

if command -v shasum > /dev/null; then
  sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
  sha256() { sha256sum "$1" | cut -d' ' -f1; }
fi

# expected_sum <sums-file> <file-name>
expected_sum() {
  awk -v f="$2" '$2 == f {print $1}' "$1"
}

folder_for() {
  case "$1" in
    44201) echo ozone ;;
    42602) echo no2 ;;
  esac
}

verify_csvs() {
  local bad=0
  for code in 44201 42602; do
    for year in "${YEARS[@]}"; do
      local name="hourly_${code}_${year}.csv"
      local path="$dest/$(folder_for "$code")/$name"
      if [[ ! -f "$path" ]]; then
        echo "MISSING  $path"
        bad=1
        continue
      fi
      if [[ "$(sha256 "$path")" == "$(expected_sum "$csv_sums" "$name")" ]]; then
        echo "OK       $name"
      else
        echo "MISMATCH $name"
        bad=1
      fi
    done
  done
  return $bad
}

mkdir -p "$dest"
dest="$(cd "$dest" && pwd)"

if (( verify_only )); then
  echo "Verifying CSVs in $dest ..."
  if verify_csvs; then
    echo "All 12 CSVs match the pinned checksums."
  else
    echo "Some CSVs are missing or differ from the pinned versions." >&2
    exit 1
  fi
  exit 0
fi

zip_dir="$dest/_zips"
mkdir -p "$zip_dir" "$dest/ozone" "$dest/no2"

for code in 44201 42602; do
  for year in "${YEARS[@]}"; do
    zip_name="hourly_${code}_${year}.zip"
    zip_path="$zip_dir/$zip_name"
    want="$(expected_sum "$zip_sums" "$zip_name")"

    if [[ -f "$zip_path" && "$(sha256 "$zip_path")" == "$want" ]]; then
      echo "have     $zip_name"
    else
      echo "download $zip_name"
      curl -fSL --retry 3 --progress-bar -o "$zip_path" "$BASE_URL/$zip_name"
      got="$(sha256 "$zip_path")"
      if [[ "$got" != "$want" ]]; then
        echo "WARNING  $zip_name does not match the pinned checksum." >&2
        echo "         EPA has likely revised this file since it was pinned." >&2
        echo "         expected $want" >&2
        echo "         got      $got" >&2
      fi
    fi

    echo "unzip    $zip_name -> $(folder_for "$code")/"
    unzip -oq "$zip_path" -d "$dest/$(folder_for "$code")"
  done
done

if (( ! keep_zips )); then
  rm -rf "$zip_dir"
fi

echo
echo "Verifying extracted CSVs ..."
if verify_csvs; then
  echo "Done. Dataset is in $dest and matches the pinned checksums."
else
  echo "Done, but some CSVs differ from the pinned versions (see above)." >&2
  exit 1
fi
