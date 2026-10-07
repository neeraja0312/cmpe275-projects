#!/usr/bin/env bash
# Run a command N times and record wall time + peak memory per run.
#
# Usage:
#   scripts/run_bench.sh <label> <runs> -- <command> [args...]
#
# Example:
#   scripts/run_bench.sh a-serial-load 10 -- part-a/build/mini1_a ../Dataset
#
# Output: results/<label>.csv  with columns  run,wall_s,user_s,sys_s,max_rss_bytes
# Note: the first run reads the CSVs from disk; later runs may hit the OS page
# cache. Use --warmup to do one untimed run first so all timed runs are "warm".

set -euo pipefail

usage() {
  echo "usage: $0 [--warmup] <label> <runs> -- <command> [args...]" >&2
  exit 1
}

warmup=0
if [[ "${1:-}" == "--warmup" ]]; then
  warmup=1
  shift
fi

[[ $# -ge 4 ]] || usage
label="$1"
runs="$2"
[[ "$3" == "--" ]] || usage
shift 3

script_dir="$(cd "$(dirname "$0")" && pwd)"
results_dir="$script_dir/../results"
mkdir -p "$results_dir"
out="$results_dir/$label.csv"
log_dir="$results_dir/$label.logs"
mkdir -p "$log_dir"

if [[ "$(uname)" == "Darwin" ]]; then
  time_flag="-l"
else
  time_flag="-v"
fi

if (( warmup )); then
  echo "warmup run (not recorded)..."
  "$@" > /dev/null
fi

echo "run,wall_s,user_s,sys_s,max_rss_bytes" > "$out"

for (( i = 1; i <= runs; i++ )); do
  tfile="$(mktemp)"
  /usr/bin/time "$time_flag" "$@" > "$log_dir/run_$i.out" 2> "$tfile"

  if [[ "$(uname)" == "Darwin" ]]; then
    # "  1.23 real   0.50 user   0.10 sys"  /  "  123456  maximum resident set size"
    read -r wall user sys < <(awk '/ real / {print $1, $3, $5}' "$tfile")
    rss=$(awk '/maximum resident set size/ {print $1}' "$tfile")
  else
    # GNU time -v; elapsed is h:mm:ss or m:ss.ss, max RSS is in KB
    wall=$(awk -F': ' '/Elapsed \(wall clock\)/ {
             n = split($2, p, ":"); s = 0
             for (k = 1; k <= n; k++) s = s * 60 + p[k]
             print s }' "$tfile")
    user=$(awk -F': ' '/User time/ {print $2}' "$tfile")
    sys=$(awk -F': ' '/System time/ {print $2}' "$tfile")
    rss=$(awk -F': ' '/Maximum resident set size/ {print $2 * 1024}' "$tfile")
  fi

  # Program's stderr, followed by the time report
  cp "$tfile" "$log_dir/run_$i.err"
  rm -f "$tfile"

  echo "$i,$wall,$user,$sys,$rss" >> "$out"
  printf "run %2d/%d  wall=%ss  rss=%s bytes\n" "$i" "$runs" "$wall" "$rss"
done

echo "wrote $out"
