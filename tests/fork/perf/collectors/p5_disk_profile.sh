#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
    echo "usage: $0 <output-file> <duration-seconds>" >&2
    exit 1
fi
out_file="$1"
duration_seconds="$2"
if command -v iostat >/dev/null 2>&1; then
    if timeout "$duration_seconds" iostat -x 1 > "$out_file" 2>&1; then
        exit 0
    fi
fi
if command -v vmstat >/dev/null 2>&1; then
    if timeout "$duration_seconds" vmstat 1 > "$out_file" 2>&1; then
        exit 0
    fi
fi
echo "neither iostat nor a working vmstat is available on this host" > "$out_file"
