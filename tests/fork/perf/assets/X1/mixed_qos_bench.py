#!/usr/bin/env python3
import argparse
import json
import pathlib
import subprocess
import threading
import time
from typing import Dict, List, Optional


def run_cmd(name: str, cmd: List[str], stdout_path: pathlib.Path, result: Dict) -> None:
    start = time.perf_counter()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    elapsed = time.perf_counter() - start
    stdout_path.write_text(proc.stdout, encoding='utf-8')
    result.update({'name': name, 'command': cmd, 'exit_code': proc.returncode, 'elapsed_seconds': elapsed})


def load_json(path: Optional[pathlib.Path]):
    if not path:
        return None
    return json.loads(path.read_text(encoding='utf-8'))


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description='Run mixed-host QoS lanes concurrently for X1.')
    ap.add_argument('--out', required=True)
    ap.add_argument('--work-dir', required=True)
    ap.add_argument('--mode', choices=['s1', 's2', 's3'], default='s3')

    ap.add_argument('--control-script', required=True)
    ap.add_argument('--control-host', default='127.0.0.1')
    ap.add_argument('--control-port', type=int, required=True)
    ap.add_argument('--control-sequence', required=True)
    ap.add_argument('--control-clients', type=int, default=50)
    ap.add_argument('--control-iterations', type=int, default=4)
    ap.add_argument('--control-timeout', type=float, default=10.0)

    ap.add_argument('--upload-script', required=True)
    ap.add_argument('--upload-host', default='127.0.0.1')
    ap.add_argument('--upload-port', type=int, required=True)
    ap.add_argument('--upload-sizes', nargs='*', type=int, default=[])
    ap.add_argument('--uploaders', nargs='*', type=int, default=[])
    ap.add_argument('--uploads-per-uploader', nargs='*', type=int, default=[])
    ap.add_argument('--control-gets', type=int, default=8)
    ap.add_argument('--control-interval-ms', type=int, default=200)
    ap.add_argument('--upload-timeout', type=float, default=120.0)

    ap.add_argument('--receiver-script', required=True)
    ap.add_argument('--receiver-host', default='127.0.0.1')
    ap.add_argument('--receiver-port', type=int, required=True)
    ap.add_argument('--receiver-path', default='/push')
    ap.add_argument('--receiver-payload', default='')
    ap.add_argument('--receiver-content-type', default='application/octet-stream')
    ap.add_argument('--receiver-concurrency', type=int, default=0)
    ap.add_argument('--receiver-requests', type=int, default=0)
    ap.add_argument('--receiver-timeout', type=float, default=30.0)
    args = ap.parse_args()
    if args.mode in ('s1', 's3'):
        if not (len(args.upload_sizes) == len(args.uploaders) == len(args.uploads_per_uploader)):
            raise SystemExit('upload-sizes/uploaders/uploads-per-uploader length mismatch')
    return args


def main() -> int:
    args = parse_args()
    out_path = pathlib.Path(args.out)
    work_dir = pathlib.Path(args.work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)

    results: Dict[str, Dict] = {}
    threads: List[threading.Thread] = []
    payload_paths: Dict[str, pathlib.Path] = {}

    control_json = work_dir / 'x1_control_lane.json'
    control_stdout = work_dir / 'x1_control_lane.stdout'
    control_cmd = [
        'python3', args.control_script,
        '--host', args.control_host,
        '--port', str(args.control_port),
        '--sequence', args.control_sequence,
        '--clients', str(args.control_clients),
        '--iterations', str(args.control_iterations),
        '--timeout', str(args.control_timeout),
        '--out', str(control_json),
    ]
    payload_paths['control'] = control_json
    results['control'] = {}
    threads.append(threading.Thread(target=run_cmd, args=('control', control_cmd, control_stdout, results['control']), daemon=True))

    if args.mode in ('s1', 's3'):
        upload_json = work_dir / 'x1_upload_lane.json'
        upload_stdout = work_dir / 'x1_upload_lane.stdout'
        upload_cmd = [
            'python3', args.upload_script,
            '--host', args.upload_host,
            '--port', str(args.upload_port),
            '--sizes', *[str(x) for x in args.upload_sizes],
            '--uploaders', *[str(x) for x in args.uploaders],
            '--uploads-per-uploader', *[str(x) for x in args.uploads_per_uploader],
            '--control-gets', str(args.control_gets),
            '--control-interval-ms', str(args.control_interval_ms),
            '--timeout', str(args.upload_timeout),
            '--out', str(upload_json),
        ]
        payload_paths['upload'] = upload_json
        results['upload'] = {}
        threads.append(threading.Thread(target=run_cmd, args=('upload', upload_cmd, upload_stdout, results['upload']), daemon=True))

    if args.mode in ('s2', 's3'):
        if not args.receiver_payload:
            raise SystemExit('receiver payload is required for modes s2 and s3')
        receiver_json = work_dir / 'x1_receiver_lane.json'
        receiver_stdout = work_dir / 'x1_receiver_lane.stdout'
        receiver_ab = work_dir / 'x1_receiver_lane.ab.txt'
        receiver_cmd = [
            'python3', args.receiver_script,
            '--host', args.receiver_host,
            '--port', str(args.receiver_port),
            '--path', args.receiver_path,
            '--payload', args.receiver_payload,
            '--content-type', args.receiver_content_type,
            '--concurrency', str(args.receiver_concurrency),
            '--requests', str(args.receiver_requests),
            '--out', str(receiver_json),
            '--raw-out', str(receiver_ab),
        ]
        payload_paths['receiver'] = receiver_json
        results['receiver'] = {}
        threads.append(threading.Thread(target=run_cmd, args=('receiver', receiver_cmd, receiver_stdout, results['receiver']), daemon=True))

    started = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - started

    summary = {
        'scenario': 'X1',
        'mode': args.mode,
        'elapsed_seconds': elapsed,
        'lanes': {
            lane: {
                **meta,
                'result': load_json(payload_paths.get(lane)),
            }
            for lane, meta in results.items()
        },
    }
    out_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if all(meta.get('exit_code', 1) == 0 for meta in results.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
