#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r4_measured.yaml"
log_file="$artifact_dir/r4.log"
port="${PORT:-18094}"; stats_port="${STATS_PORT:-19094}"
mkdir -p "$artifact_dir" "$work_dir"
rm -f "$artifact_dir/r4.queue"
export PERF_R_QUEUE_FILE="$artifact_dir/r4.queue"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$config_tmpl" > "$config_yaml"
printf '  stats: 127.0.0.1:%s
  stats-http: true
  limit-post: 1048576
' "$stats_port" >> "$config_yaml"
bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap 'bash "$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "http://127.0.0.1:${stats_port}/" "$artifact_dir/p1_stats" 1 45 & P1=$!
python3 "$asset_common/generate_payloads.py" --out "$work_dir/r4_payload.json" --size 65536 > "$artifact_dir/r4_payload.stdout"
python3 "$asset_common/slow_uploader_flood.py"   --host 127.0.0.1 --port "$port" --path /push   --payload "$work_dir/r4_payload.json" --content-type application/json   --chunk-size 4096   --rates 8192 65536 262144   --uploaders 8 8 8   --control-posts 5 --control-interval-ms 200   --out "$artifact_dir/r4_measured.json" > "$artifact_dir/r4_measured.stdout"
wait $P1 || true
