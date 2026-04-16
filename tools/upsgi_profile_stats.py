#!/usr/bin/env python3
import argparse, json, socket, sys, urllib.request
from typing import Any, Dict, List

def fetch_http(url: str) -> Dict[str, Any]:
    with urllib.request.urlopen(url, timeout=5) as resp:
        return json.loads(resp.read().decode('utf-8'))

def fetch_tcp(addr: str) -> Dict[str, Any]:
    host, port_text = addr.rsplit(':', 1)
    port = int(port_text)
    chunks: List[bytes] = []
    with socket.create_connection((host, port), timeout=5) as sock:
        while True:
            buf = sock.recv(4096)
            if not buf:
                break
            chunks.append(buf)
    return json.loads(b''.join(chunks).decode('utf-8'))

def fetch_file(path: str) -> Dict[str, Any]:
    with open(path, 'r', encoding='utf-8') as fh:
        return json.load(fh)

def value(obj: Dict[str, Any], key: str) -> int:
    raw = obj.get(key, 0)
    if raw is None:
        return 0
    if isinstance(raw, bool):
        return int(raw)
    if isinstance(raw, (int, float)):
        return int(raw)
    try:
        return int(raw)
    except Exception:
        return 0

def stable_status_counts(workers: List[Dict[str, Any]]) -> str:
    counts: Dict[str, int] = {}
    for worker in workers:
        status = str(worker.get('status', 'unknown'))
        counts[status] = counts.get(status, 0) + 1
    parts = [f'{name}={counts[name]}' for name in sorted(counts)]
    return ', '.join(parts) if parts else 'none'

def summarize(stats: Dict[str, Any]) -> str:
    workers = stats.get('workers', []) or []
    worker = workers[0] if workers else {}
    core = ((worker.get('cores') or [{}])[0]) if worker else {}
    lines: List[str] = []
    lines.append('upsgi stats profile')
    lines.append('')
    lines.append('server')
    lines.append(f"  version: {stats.get('version', 'unknown')}")
    lines.append(f"  pid: {stats.get('pid', 'unknown')}")
    lines.append(f"  listen_queue: {value(stats, 'listen_queue')}")
    lines.append(f"  listen_queue_errors: {value(stats, 'listen_queue_errors')}")
    lines.append(f"  workers: {len(workers)}")
    lines.append(f"  worker_status: {stable_status_counts(workers)}")
    lines.append('')
    lines.append('logging')
    for k in ['log_records','req_log_records','log_backpressure_events','req_log_backpressure_events','log_sink_stall_events','log_dropped_messages']:
        lines.append(f"  {k}: {value(stats, k)}")
    lines.append('')
    lines.append('worker_1')
    for k in ['requests','respawn_count','rss','avg_rt','tx']:
        lines.append(f"  {k}: {value(worker, k)}")
    lines.append('')
    lines.append('static_path')
    for k in ['static_path_cache_hits','static_path_cache_misses','static_realpath_calls','static_stat_calls','static_open_calls','static_open_failures','static_index_checks']:
        lines.append(f"  {k}: {value(worker, k)}")
    lines.append('')
    lines.append('body_scheduler')
    for k in ['body_sched_rounds','body_sched_completed_items','body_sched_promotions_to_bulk','body_sched_no_credit_skips','body_sched_bytes_total','body_sched_credit_granted_bytes','body_sched_credit_unused_bytes','body_sched_active_items','body_sched_disabled_fallbacks']:
        lines.append(f"  {k}: {value(worker, k)}")
    lines.append('')
    lines.append('core_1')
    for k in ['requests','static_requests','write_errors','read_errors','in_request']:
        lines.append(f"  {k}: {value(core, k)}")
    return '\n'.join(lines) + '\n'

def main() -> int:
    parser = argparse.ArgumentParser(description='Fetch and summarize upsgi stats.')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--http', metavar='URL')
    group.add_argument('--tcp', metavar='HOST:PORT')
    group.add_argument('--file', metavar='PATH')
    args = parser.parse_args()
    if args.http:
        stats = fetch_http(args.http)
    elif args.tcp:
        stats = fetch_tcp(args.tcp)
    else:
        stats = fetch_file(args.file)
    sys.stdout.write(summarize(stats))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
