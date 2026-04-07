#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <binary> <config_yaml> <artifact_dir>" >&2
    exit 2
}

[[ $# -eq 3 ]] || usage
binary="$1"
config_yaml="$2"
artifact_dir="$3"
server_log="$artifact_dir/server.log"
pid_file="$artifact_dir/server.pid"
launch_file="$artifact_dir/launch.cmd"
meta_file="$artifact_dir/meta.env"

mkdir -p "$artifact_dir"
: > "$server_log"
rm -f "$pid_file"
printf '%q --config %q --pidfile %q\n' "$binary" "$config_yaml" "$pid_file" > "$launch_file"
{
    echo "UPSIG_BINARY=$binary"
    echo "UPSIG_CONFIG=$config_yaml"
    echo "UPSIG_ARTIFACT_DIR=$artifact_dir"
    echo "UPSIG_SERVER_LOG=$server_log"
    echo "UPSIG_PIDFILE=$pid_file"
} > "$meta_file"

"$binary" --config "$config_yaml" --pidfile "$pid_file" >>"$server_log" 2>&1 &
launch_pid=$!
echo "UPSIG_LAUNCH_PID=$launch_pid" >> "$meta_file"

for _ in $(seq 1 80); do
    if [[ -s "$pid_file" ]]; then
        break
    fi
    if ! kill -0 "$launch_pid" 2>/dev/null; then
        break
    fi
    sleep 0.10
done

if [[ ! -s "$pid_file" ]]; then
    echo "pid file not created: $pid_file" >&2
    exit 1
fi

master_pid=$(cat "$pid_file")
echo "UPSIG_PID=$master_pid" >> "$meta_file"
