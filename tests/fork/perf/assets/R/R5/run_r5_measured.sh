#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "usage: $0 <binary> <artifact_dir> <work_dir>" >&2; exit 2; fi
binary="$1"; artifact_dir="$2"; work_dir="$3"
root_dir="$(cd "$(dirname "$0")/../../../../../.." && pwd)"
asset_dir="$(cd "$(dirname "$0")" && pwd)"
asset_common="$(cd "$asset_dir/../common" && pwd)"
port="${PORT:-18095}"; stats_port="${STATS_PORT:-19095}"
mkdir -p "$artifact_dir" "$work_dir"

python3 "$asset_common/generate_payloads.py" --out "$work_dir/r5_payload.json" --size 32768 > "$artifact_dir/r5_payload.stdout"
python3 "$asset_common/generate_payloads.py" --out "$work_dir/r5_control.json" --size 8192 > "$artifact_dir/r5_control_payload.stdout"

run_case() {
  local delay_ms="$1"; local conc="$2"; local reqs="$3"
  local case_dir="$artifact_dir/case_${delay_ms}ms"
  local config_yaml="$work_dir/r5_${delay_ms}ms.yaml"
  local log_file="$case_dir/r5.log"
  rm -rf "$case_dir"
  mkdir -p "$case_dir"
  export PERF_R_QUEUE_FILE="$case_dir/r5.queue"
  export PERF_R_APPEND_DELAY_MS="$delay_ms"

  sed -e "s#%(port)s#$port#g" -e "s#%(app)s#$asset_common/receiver_app.psgi#g" -e "s#%(log_file)s#$log_file#g" "$asset_dir/config.yaml.in" > "$config_yaml"
  printf '  stats: 127.0.0.1:%s
  stats-http: true
  limit-post: 1048576
' "$stats_port" >> "$config_yaml"

  bash "$root_dir/tests/fork/helpers/start_server.sh" "$binary" "$config_yaml" "$case_dir"
  bash "$root_dir/tests/fork/helpers/wait_http.sh" 127.0.0.1 "$port" /healthz
  source "$case_dir/meta.env"
  worker_pid="$UPSIG_PID"
  stats_url="http://127.0.0.1:${stats_port}/"
  curl -s "$stats_url" > "$case_dir/stats_before.json"
  bash "$root_dir/tests/fork/perf/collectors/p1_stats_sampler.sh" "$stats_url" "$case_dir/p1_stats" 1 20 & P1=$!
  bash "$root_dir/tests/fork/perf/collectors/p4_smaps_rollup.sh" "$worker_pid" "$case_dir/p4_smaps_rollup.log" 1 20 & P4=$!
  bash "$root_dir/tests/fork/perf/collectors/p5_disk_profile.sh" "$case_dir/p5_disk_profile.log" 20 & P5=$!

  python3 "$asset_common/receiver_append_pressure.py"     --host 127.0.0.1 --port "$port" --path /push     --payload "$work_dir/r5_payload.json"     --control-payload "$work_dir/r5_control.json"     --delay-ms "$delay_ms" --concurrency "$conc" --requests "$reqs"     --control-posts 10 --control-interval-ms 250     --out "$case_dir/r5_case.json" > "$case_dir/r5_case.stdout"

  wait "$P1" || true
  wait "$P4" || true
  wait "$P5" || true
  curl -s "$stats_url" > "$case_dir/stats_after.json"
  bash "$root_dir/tests/fork/helpers/stop_server.sh" "$case_dir" >/dev/null 2>&1 || true
}

run_case 0 32 256
run_case 5 32 256
run_case 20 16 128
run_case 50 8 64

python3 - "$artifact_dir" <<'PY'
import json, pathlib, sys
art = pathlib.Path(sys.argv[1])
cases = []
for case_dir in sorted(art.glob('case_*ms')):
    data = json.loads((case_dir/'r5_case.json').read_text())
    case = data['cases'][0]
    before = json.loads((case_dir/'stats_before.json').read_text())
    after = json.loads((case_dir/'stats_after.json').read_text())
    def core0(d):
        try: return d['workers'][0]['cores'][0]
        except Exception: return {}
    b = core0(before); a = core0(after)
    case['stats_after'] = {
        'listen_queue': int(after.get('listen_queue',0) or 0),
        'listen_queue_errors': int(after.get('listen_queue_errors',0) or 0),
    }
    case['stats_delta'] = {
        'requests': int(a.get('requests',0)) - int(b.get('requests',0)),
        'read_errors': int(a.get('read_errors',0)) - int(b.get('read_errors',0)),
        'write_errors': int(a.get('write_errors',0)) - int(b.get('write_errors',0)),
    }
    qf = case_dir/'r5.queue'
    case['queue_lines'] = sum(1 for _ in qf.open('r', encoding='utf-8')) if qf.exists() else 0
    cases.append(case)
(art/'r5_measured.json').write_text(json.dumps({'scenario':'R5','cases':cases}, indent=2, sort_keys=True)+'\n', encoding='utf-8')
print(json.dumps({'scenario':'R5','cases':cases}, indent=2, sort_keys=True))
PY
