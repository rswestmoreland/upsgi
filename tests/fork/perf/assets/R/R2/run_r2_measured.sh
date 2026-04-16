#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r2_measured.yaml"
log_file="$artifact_dir/r2.log"
port="${PORT:-18092}"; stats_port="${STATS_PORT:-19092}"
mkdir -p "$artifact_dir" "$work_dir"
rm -f "$artifact_dir/r2.queue"
export PERF_R_QUEUE_FILE="$artifact_dir/r2.queue"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$config_tmpl" > "$config_yaml"
printf '  stats: 127.0.0.1:%s\n  stats-http: true\n' "$stats_port" >> "$config_yaml"
"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "http://127.0.0.1:${stats_port}/" "$artifact_dir/p1_stats" 1 25 & P1=$!
python3 "$asset_common/generate_ratio_payload.py" --out "$work_dir/r2_4x_32768.gz" --meta-out "$artifact_dir/r2_4x_32768.payload.json" --compressed-target 32768 --ratio-profile 4x > "$artifact_dir/r2_4x_32768.payload.stdout"
python3 "$asset_common/receiver_bench_ab.py" --host 127.0.0.1 --port "$port" --path /push --payload "$work_dir/r2_4x_32768.gz" --content-type application/octet-stream --concurrency 20 --requests 60 --out "$artifact_dir/r2_4x_32768.json" --raw-out "$artifact_dir/r2_4x_32768.ab.txt" > "$artifact_dir/r2_4x_32768.stdout"
python3 "$asset_common/generate_ratio_payload.py" --out "$work_dir/r2_10x_32768.gz" --meta-out "$artifact_dir/r2_10x_32768.payload.json" --compressed-target 32768 --ratio-profile 10x > "$artifact_dir/r2_10x_32768.payload.stdout"
python3 "$asset_common/receiver_bench_ab.py" --host 127.0.0.1 --port "$port" --path /push --payload "$work_dir/r2_10x_32768.gz" --content-type application/octet-stream --concurrency 10 --requests 20 --out "$artifact_dir/r2_10x_32768.json" --raw-out "$artifact_dir/r2_10x_32768.ab.txt" > "$artifact_dir/r2_10x_32768.stdout"
python3 "$asset_common/generate_ratio_payload.py" --out "$work_dir/r2_10x_131072.gz" --meta-out "$artifact_dir/r2_10x_131072.payload.json" --compressed-target 131072 --ratio-profile 10x > "$artifact_dir/r2_10x_131072.payload.stdout"
python3 "$asset_common/receiver_bench_ab.py" --host 127.0.0.1 --port "$port" --path /push --payload "$work_dir/r2_10x_131072.gz" --content-type application/octet-stream --concurrency 4 --requests 8 --out "$artifact_dir/r2_10x_131072.json" --raw-out "$artifact_dir/r2_10x_131072.ab.txt" > "$artifact_dir/r2_10x_131072.stdout"
wait $P1 || true
