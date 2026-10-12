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
  --exclude='REPORT_MATERIAL.md' \
  --exclude='*.out' \
  --exclude='.venv' \
  --exclude='.DS_Store' \
  --exclude='.gitkeep' \
  --exclude='report.md' \
  --exclude='slide_speaker_script.md' \
  --exclude='notes.md' \
  --exclude='results' \
  --exclude='make_slide.py' \
  --exclude='make_report_docx.py' \
  --exclude='plot*.py' \
  --exclude='figures' \
  --exclude='*.png' \
  --exclude='*.jpg' \
  --exclude='*.jpeg' \
  --exclude='*.gif' \
  --exclude='*.svg' \
  --exclude='*.webp' \
  mini1

echo "wrote $out"
echo "contents:"
tar -tzf "$out" | head -50
size=$(du -h "$out" | cut -f1)
echo "size: $size"
