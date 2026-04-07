#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
config_dir="$root_dir/tests/fork/configs"

usage() {
    echo "usage: $0 render <profile> <output_yaml> <app> <static_root> <log_file> <port>" >&2
    exit 2
}

template_for_profile() {
    case "$1" in
        baseline) echo "$config_dir/baseline.yaml.in" ;;
        baseline_no_affinity) echo "$config_dir/baseline_no_affinity.yaml.in" ;;
        debug_exceptions) echo "$config_dir/debug_exceptions.yaml.in" ;;
        legacy) echo "$config_dir/legacy_compatible.yaml.in" ;;
        *)
            echo "unknown profile: $1" >&2
            exit 2
            ;;
    esac
}

render_profile() {
    local profile="$1"
    local output_yaml="$2"
    local app="$3"
    local static_root="$4"
    local log_file="$5"
    local port="$6"
    local template
    template=$(template_for_profile "$profile")
    mkdir -p "$(dirname -- "$output_yaml")"
    python3 - "$template" "$output_yaml" "$app" "$static_root" "$log_file" "$port" <<'PY'
import sys
from pathlib import Path

template_path = Path(sys.argv[1])
output_path = Path(sys.argv[2])
app = sys.argv[3]
static_root = sys.argv[4]
log_file = sys.argv[5]
port = sys.argv[6]

text = template_path.read_text()
rendered = text % {
    'app': app,
    'static_root': static_root,
    'log_file': log_file,
    'port': port,
}
output_path.write_text(rendered)
PY
}

subcmd="${1:-}"
case "$subcmd" in
    render)
        [[ $# -eq 7 ]] || usage
        render_profile "$2" "$3" "$4" "$5" "$6" "$7"
        ;;
    *)
        usage
        ;;
esac
