#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
config_tmpl="$asset_dir/config.yaml.in"
config_yaml="$work_dir/r3_measured.yaml"
log_file="$artifact_dir/r3.log"
port="${PORT:-18093}"; stats_port="${STATS_PORT:-19093}"
mkdir -p "$artifact_dir" "$work_dir"
rm -f "$artifact_dir/r3.queue"
export PERF_R_QUEUE_FILE="$artifact_dir/r3.queue"
export PERF_R_APPEND_DELAY_MS="${PERF_R_APPEND_DELAY_MS:-0}"
sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$config_tmpl" > "$config_yaml"
printf '  stats: 127.0.0.1:%s\n  stats-http: true\n  limit-post: 33554432\n' "$stats_port" >> "$config_yaml"
bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$artifact_dir"
trap 'bash "$root_dir/tests/fork/helpers/stop_server.sh" "$artifact_dir" >/dev/null 2>&1 || true' EXIT
bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "http://127.0.0.1:${stats_port}/" "$artifact_dir/p1_stats" 1 90 & P1=$!

run_case() {
  local size="$1"; local variant="$2"; local conc="$3"; local reqs="$4"; local stem="r3_${variant}_${size}"
  local payload="$work_dir/${stem}"
  if [[ "$variant" == "compressed" ]]; then
    payload="${payload}.gz"
    python3 "$asset_common/generate_payloads.py" --out "$payload" --size "$size" --compress > "$artifact_dir/${stem}.payload.stdout"
    ctype="application/octet-stream"
  else
    python3 "$asset_common/generate_payloads.py" --out "$payload" --size "$size" > "$artifact_dir/${stem}.payload.stdout"
    ctype="application/json"
  fi
  python3 "$asset_common/receiver_bench_ab.py"         --host 127.0.0.1 --port "$port" --path /push         --payload "$payload" --content-type "$ctype"         --concurrency "$conc" --requests "$reqs"         --out "$artifact_dir/${stem}.json"         --raw-out "$artifact_dir/${stem}.ab.txt"         > "$artifact_dir/${stem}.stdout" || true
}

# size-scaled conservative matrix
run_case 262144 uncompressed 4 8
run_case 262144 compressed   4 8
run_case 1048576 uncompressed 2 4
run_case 1048576 compressed   2 4
run_case 4194304 uncompressed 1 2
run_case 4194304 compressed   1 2
run_case 16777216 uncompressed 1 1
run_case 16777216 compressed   1 1

wait $P1 || true
