#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <artifact_dir>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
artifact_dir="$1"
pid_file="$artifact_dir/server.pid"

if [[ ! -f "$pid_file" ]]; then
    echo "missing pid file: $pid_file" >&2
    exit 1
fi

pid=$(cat "$pid_file")
kill -HUP "$pid"
