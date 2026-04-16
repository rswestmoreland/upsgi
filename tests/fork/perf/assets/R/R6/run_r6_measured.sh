#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r6_measured.yaml"
log_file="$artifact_dir/r6.log"
port="${PORT:-18096}"; stats_port="${STATS_PORT:-19096}"
mkdir -p "$artifact_dir" "$work_dir"
rm -f "$artifact_dir/r6.queue"
export PERF_R_QUEUE_FILE="$artifact_dir/r6.queue"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$config_tmpl" > "$config_yaml"
printf '  stats: 127.0.0.1:%s\n  stats-http: true\n  limit-post: 262144\n' "$stats_port" >> "$config_yaml"
"$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap '"$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
"$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "http://127.0.0.1:${stats_port}/" "$artifact_dir/p1_stats" 1 20 & P1=$!
python3 "$asset_common/hostile_case_runner.py" --host 127.0.0.1 --port "$port" --fixture-dir "$asset_common/corpus" --cases wrong_method wrong_path invalid_json broken_inflate mixed_chunked_bodies too_large_bodies header_abuse random_disconnects --repeats 5 --too-large-bytes 400000 --out "$artifact_dir/r6_cases.json" > "$artifact_dir/r6_cases.stdout"
wait $P1 || true
