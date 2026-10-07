#!/usr/bin/env bash
# Build the Canvas submission archive: <team>.tar.gz containing mini1's code,
# report and slide. Excludes the dataset, build trees and OS junk
# (spec: "Please do not include test data").
#
# Usage: scripts/package.sh <team-name>
# Output: Minis/<team-name>.tar.gz

set -euo pipefail

[[ $# -eq 1 ]] || { echo "usage: $0 <team-name>" >&2; exit 1; }
team="$1"

mini1_dir="$(cd "$(dirname "$0")/.." && pwd)"
parent="$(dirname "$mini1_dir")"
out="$parent/$team.tar.gz"

tar -czf "$out" \
  -C "$parent" \
  --exclude='Dataset' \
  --exclude='build' \
  --exclude='build-*' \
  --exclude='cmake-build-*' \
  --exclude='*.logs' \
  --exclude='.venv' \
  --exclude='.DS_Store' \
  --exclude='.gitkeep' \
  mini1

echo "wrote $out"
echo "contents:"
tar -tzf "$out" | head -50
size=$(du -h "$out" | cut -f1)
echo "size: $size"
