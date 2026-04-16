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
config_yaml="$work_dir/a3.yaml"
log_file="$artifact_dir/a3.log"
app="$asset_dir/app_static_fanout.psgi"
static_root="$asset_dir/static"

mkdir -p "$artifact_dir" "$work_dir"
sed \
    -e "s#%(port)s#$port#g" \
    -e "s#%(app)s#$app#g" \
    -e "s#%(static_root)s#$static_root#g" \
    -e "s#%(log_file)s#$log_file#g" \
    "$config_tmpl" > "$config_yaml"

"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /

python3 "$root_dir/tests/fork/bench/http_bench.py" \
    --label a3-page --host 127.0.0.1 --port "$port" --path /page --requests 5 --warmup 1 \
    --expect-status 200 --expect-contains 'static fanout fixture' \
    --output-json "$artifact_dir/a3_page.json"

python3 "$root_dir/tests/fork/bench/http_bench.py" \
    --label a3-asset --host 127.0.0.1 --port "$port" --path /assets/css/layout.css --requests 20 --warmup 2 \
    --expect-status 200 --expect-contains 'display:grid' \
    --output-json "$artifact_dir/a3_asset.json"
