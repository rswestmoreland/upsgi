#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 4 ]]; then
    echo "usage: $0 <pid> <output-file> <interval-seconds> <samples>" >&2
    exit 1
fi
pid="$1"
out_file="$2"
interval_seconds="$3"
samples="$4"
: > "$out_file"
for idx in $(seq 1 "$samples"); do
    ts=$(date -u +%Y%m%dT%H%M%SZ)
    echo "### $ts" >> "$out_file"
    if [[ -r "/proc/${pid}/smaps_rollup" ]]; then
        cat "/proc/${pid}/smaps_rollup" >> "$out_file"
    else
        echo "smaps_rollup unavailable for pid ${pid}" >> "$out_file"
        echo >> "$out_file"
        exit 0
    fi
    echo >> "$out_file"
    sleep "$interval_seconds"
done
