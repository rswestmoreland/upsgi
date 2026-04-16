#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <binary> <port> <artifact_dir> <work_dir>" >&2
    exit 2
fi

binary="$1"
port="$2"
artifact_dir="$3"
work_dir="$4"
root_dir="$(cd "$(dirname "$0")/../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/a6.yaml"
log_file="$artifact_dir/a6.log"
app="$asset_dir/app_async_getline.psgi"

mkdir -p "$artifact_dir" "$work_dir"
sed \
    -e "s#%(port)s#$port#g" \
    -e "s#%(app)s#$app#g" \
    -e "s#%(log_file)s#$log_file#g" \
    "$config_tmpl" > "$config_yaml"

bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap 'bash "$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /small

python3 "$asset_dir/slow_reader_client.py" \
    --host 127.0.0.1 --port "$port" --chunks 32 --chunk-size 65536 --read-size 4096 --delay-ms 50 \
    > "$artifact_dir/a6_slow_reader.txt" &
slow_pid=$!

python3 "$root_dir/tests/fork/bench/http_bench.py" \
    --label a6-small --host 127.0.0.1 --port "$port" --path /small --requests 25 --warmup 3 \
    --expect-status 200 --expect-contains 'ok' \
    --output-json "$artifact_dir/a6_small.json"

wait "$slow_pid"
