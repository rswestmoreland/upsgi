#!/usr/bin/env python3
import argparse
import json
import pathlib
import shutil
import sys
from datetime import datetime, timezone

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
MANIFEST_DIR = SCRIPT_DIR / 'manifests'
INDEX_PATH = MANIFEST_DIR / 'manifest_index.json'


def load_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def sanitize_title(title):
    cleaned = []
    for ch in title.lower():
        if ch.isalnum():
            cleaned.append(ch)
        elif ch in (' ', '-', '_'):
            cleaned.append('_')
    text = ''.join(cleaned).strip('_')
    while '__' in text:
        text = text.replace('__', '_')
    return text or 'scenario'


def write_plan(output_dir, manifest, collector_map):
    lines = []
    lines.append(f"# Scenario {manifest['id']} - {manifest['title']}")
    lines.append('')
    lines.append(f"phase: {manifest['phase']}")
    lines.append(f"lane: {manifest['lane']}")
    lines.append(f"status: {manifest['status']}")
    lines.append('')
    lines.append('## Purpose')
    lines.append(manifest['purpose'])
    lines.append('')
    lines.append('## Traffic shape')
    lines.append('```json')
    lines.append(json.dumps(manifest['traffic_shape'], indent=2, sort_keys=True))
    lines.append('```')
    if manifest.get('parameters'):
        lines.append('')
        lines.append('## Parameters')
        lines.append('```json')
        lines.append(json.dumps(manifest['parameters'], indent=2, sort_keys=True))
        lines.append('```')
    lines.append('')
    lines.append('## Borrowed ideas')
    for item in manifest['borrowed_ideas']:
        ideas = ', '.join(item.get('ideas', [])) or '-'
        lines.append(f"- {item.get('source', 'unknown')}: {ideas}")
    lines.append('')
    lines.append('## Collector contract')
    for key in manifest['collectors']:
        info = collector_map[key]
        lines.append(f"- {key}: {info['script']} - {info['purpose']}")
    lines.append('')
    lines.append('## Questions to answer')
    for question in manifest['questions']:
        lines.append(f"- {question}")
    lines.append('')
    lines.append('## Assets')
    lines.append('```json')
    lines.append(json.dumps(manifest.get('assets', {}), indent=2, sort_keys=True))
    lines.append('```')
    lines.append('')
    lines.append('## Artifacts expected')
    for artifact in manifest['artifacts']:
        lines.append(f"- {artifact}")
    lines.append('')
    lines.append('## Dry-run notes')
    lines.append('- This directory was emitted by the seed manifest runner.')
    lines.append('- No traffic was generated in dry-run mode.')
    lines.append('- Wire the collector scripts to the chosen host/tooling before full execution.')
    (output_dir / 'run_plan.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def write_commands(output_dir, manifest, collector_map):
    lines = []
    lines.append('# Fill these commands with real host-specific values before execution.')
    lines.append('')
    lines.append('# Example placeholders')
    lines.append('UPSGI_BIN=./upsgi')
    lines.append('SERVER_PID=<worker-or-master-pid>')
    lines.append('STATS_URL=<http://127.0.0.1:port/stats-or-socket-bridge>')
    lines.append('OUT_DIR=' + str(output_dir))
    lines.append('')
    for key in manifest['collectors']:
        script = collector_map[key]['script']
        if key == 'P1_upsgi_stats_sampler':
            cmd = f"{script} \"$STATS_URL\" \"$OUT_DIR/p1_stats\" 1 60"
        elif key == 'P2_strace_fc':
            cmd = f"{script} \"$SERVER_PID\" \"$OUT_DIR/p2_strace_fc.txt\" 30"
        elif key == 'P3_perf_record':
            cmd = f"{script} \"$SERVER_PID\" \"$OUT_DIR/p3_perf.data\" 30"
        elif key == 'P4_smaps_rollup_sampler':
            cmd = f"{script} \"$SERVER_PID\" \"$OUT_DIR/p4_smaps_rollup.log\" 1 60"
        elif key == 'P5_disk_profile':
            cmd = f"{script} \"$OUT_DIR/p5_disk_profile.log\" 60"
        else:
            cmd = f"# unknown collector {key}"
        lines.append(cmd)
    (output_dir / 'commands.todo.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def emit_one(manifest_path, output_root, collector_map):
    manifest = load_json(manifest_path)
    scenario_slug = sanitize_title(manifest['title'])
    output_dir = output_root / manifest['phase'] / f"{manifest['id']}_{scenario_slug}"
    output_dir.mkdir(parents=True, exist_ok=True)
    lock = dict(manifest)
    lock['generated_at_utc'] = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    (output_dir / 'manifest.lock.json').write_text(json.dumps(lock, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    contract = {key: collector_map[key] for key in manifest['collectors']}
    (output_dir / 'collector_contract.json').write_text(json.dumps(contract, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    write_plan(output_dir, manifest, collector_map)
    write_commands(output_dir, manifest, collector_map)
    return output_dir


def main():
    parser = argparse.ArgumentParser(description='Emit dry-run scenario directories for the upsgi performance plan.')
    parser.add_argument('--scenario', action='append', default=[], help='Scenario id such as A3. Can be repeated.')
    parser.add_argument('--all', action='store_true', help='Emit every scenario in the manifest index.')
    parser.add_argument('--output-root', required=True, help='Directory where dry-run artifacts should be written.')
    parser.add_argument('--dry-run', action='store_true', help='Required for the current seed runner. No real traffic is executed.')
    args = parser.parse_args()

    if not args.dry_run:
        raise SystemExit('the current seed runner only supports --dry-run')

    index = load_json(INDEX_PATH)
    if args.all:
        selected = index['execution_order']
    else:
        selected = args.scenario
    if not selected:
        raise SystemExit('choose --all or at least one --scenario')

    output_root = pathlib.Path(args.output_root).resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    emitted = []
    for scenario_id in selected:
        manifest_path = MANIFEST_DIR / f'{scenario_id}.json'
        if not manifest_path.exists():
            raise SystemExit(f'missing manifest for scenario {scenario_id}')
        emitted.append(str(emit_one(manifest_path, output_root, index['collectors'])))
    print(json.dumps({'dry_run': True, 'output_root': str(output_root), 'emitted': emitted}, indent=2))


if __name__ == '__main__':
    sys.exit(main())
