#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 ]]; then
    echo "usage: $0 <pid> <output-file> <duration-seconds>" >&2
    exit 1
fi
pid="$1"
out_file="$2"
duration_seconds="$3"
exec timeout "$duration_seconds" strace -fc -p "$pid" -o "$out_file"
