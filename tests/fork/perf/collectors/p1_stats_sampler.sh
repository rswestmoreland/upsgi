#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 4 ]]; then
    echo "usage: $0 <stats-url> <output-dir> <interval-seconds> <samples>" >&2
    exit 1
fi
stats_url="$1"
out_dir="$2"
interval_seconds="$3"
samples="$4"
mkdir -p "$out_dir"
for idx in $(seq 1 "$samples"); do
    ts=$(date -u +%Y%m%dT%H%M%SZ)
    curl -fsS "$stats_url" > "$out_dir/${ts}.json"
    sleep "$interval_seconds"
done
