#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <artifact_dir>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
artifact_dir="$1"
pid_file="$artifact_dir/server.pid"
status_file="$artifact_dir/stop.status"

if [[ ! -f "$pid_file" ]]; then
    echo "missing pid file: $pid_file" >&2
    exit 1
fi

pid=$(cat "$pid_file")
if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    for _ in $(seq 1 50); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "stopped=term" > "$status_file"
            exit 0
        fi
        sleep 0.10
    done
    kill -9 "$pid" 2>/dev/null || true
    echo "stopped=kill" > "$status_file"
    exit 0
fi

echo "stopped=already_dead" > "$status_file"
exit 0
