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
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r6.yaml"
log_file="$artifact_dir/r6.log"
app="$asset_common/receiver_app.psgi"
queue_file="$artifact_dir/r6.queue"

mkdir -p "$artifact_dir" "$work_dir"
rm -f "$queue_file"
export PERF_R_QUEUE_FILE="$queue_file"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed     -e "s#%(port)s#$port#g"     -e "s#%(app)s#$app#g"     -e "s#%(log_file)s#$log_file#g"     "$config_tmpl" > "$config_yaml"

"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz

python3 "$asset_common/receiver_client.py" --host 127.0.0.1 --port "$port" --payload "$asset_common/corpus/invalid/invalid_json.txt" --content-type application/json --expect-status 400
python3 "$asset_common/receiver_client.py" --host 127.0.0.1 --port "$port" --payload "$asset_common/corpus/invalid/broken_gzip.bin" --content-type application/octet-stream --expect-status 400
python3 "$asset_common/raw_abuse_client.py" --host 127.0.0.1 --port "$port" --case wrong_method > "$artifact_dir/r6_wrong_method.out"
python3 "$asset_common/raw_abuse_client.py" --host 127.0.0.1 --port "$port" --case wrong_path > "$artifact_dir/r6_wrong_path.out"
grep -q "405" "$artifact_dir/r6_wrong_method.out"
grep -q "404" "$artifact_dir/r6_wrong_path.out"
