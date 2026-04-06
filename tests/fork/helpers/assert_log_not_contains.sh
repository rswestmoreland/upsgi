#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <log_file> <needle>" >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
log_file="$1"
needle="$2"

if grep -F -- "$needle" "$log_file" >/dev/null 2>&1; then
    echo "expected log to NOT contain: $needle" >&2
    echo "--- log tail ---" >&2
    tail -n 80 "$log_file" >&2 || true
    exit 1
fi
exit 0
