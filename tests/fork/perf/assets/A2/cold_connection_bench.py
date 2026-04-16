#!/usr/bin/env python3
import argparse
import concurrent.futures
import http.client
import itertools
import json
import pathlib
import threading
import time
from collections import Counter


def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = pct * (len(sorted_values) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(sorted_values) - 1)
    frac = rank - lo
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac


def do_request(host, port, timeout, item):
    method = item['method']
    path = item['path']
    body = item.get('body')
    headers = {'Connection': 'close'}
    headers.update(item.get('headers', {}))
    start = time.perf_counter()
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        data = resp.read()
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        return {'ok': resp.status in (200, 201, 202), 'status': resp.status, 'elapsed_ms': elapsed_ms, 'error': '', 'bytes': len(data)}
    except Exception as exc:
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        return {'ok': False, 'status': 0, 'elapsed_ms': elapsed_ms, 'error': exc.__class__.__name__, 'bytes': 0}
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass


def run_case(host, port, sequence, target_rate, duration, max_workers, timeout):
    total_requests = int(target_rate * duration)
    latencies = []
    status_counts = Counter()
    error_counts = Counter()
    ok = 0
    failed = 0
    bytes_in = 0
    started = time.perf_counter()
    lock = threading.Lock()
    cycle = itertools.cycle(sequence)

    def worker(item):
        return do_request(host, port, timeout, item)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as ex:
        futures = set()
        max_outstanding = max_workers * 4

        def collect(done_futures):
            nonlocal ok, failed, bytes_in
            for fut in done_futures:
                result = fut.result()
                with lock:
                    latencies.append(result['elapsed_ms'])
                    bytes_in += result['bytes']
                    if result['status']:
                        status_counts[str(result['status'])] += 1
                    if result['error']:
                        error_counts[result['error']] += 1
                    if result['ok']:
                        ok += 1
                    else:
                        failed += 1

        for idx in range(total_requests):
            target_ts = started + (idx / float(target_rate))
            now = time.perf_counter()
            if target_ts > now:
                time.sleep(target_ts - now)
            futures.add(ex.submit(worker, next(cycle)))
            if len(futures) >= max_outstanding:
                done, futures = concurrent.futures.wait(futures, return_when=concurrent.futures.FIRST_COMPLETED)
                collect(done)
        if futures:
            done, _ = concurrent.futures.wait(futures)
            collect(done)

    elapsed = time.perf_counter() - started
    latencies.sort()
    return {
        'target_rate_per_second': target_rate,
        'duration_seconds': duration,
        'scheduled_requests': total_requests,
        'elapsed_seconds': elapsed,
        'completed_requests': ok + failed,
        'successful_requests': ok,
        'failed_requests': failed,
        'throughput_requests_per_second': ((ok + failed) / elapsed) if elapsed > 0 else 0.0,
        'response_bytes_total': bytes_in,
        'status_counts': dict(status_counts),
        'error_counts': dict(error_counts),
        'latency_ms': {
            'p50': percentile(latencies, 0.50),
            'p95': percentile(latencies, 0.95),
            'p99': percentile(latencies, 0.99),
            'max': max(latencies) if latencies else 0.0,
        },
    }


def main():
    ap = argparse.ArgumentParser(description='Run A2 cold connection churn benchmark.')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--sequence', required=True)
    ap.add_argument('--rates', nargs='+', type=int, required=True)
    ap.add_argument('--durations', nargs='+', type=float, required=True)
    ap.add_argument('--max-workers', type=int, default=256)
    ap.add_argument('--timeout', type=float, default=10.0)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    if len(args.rates) != len(args.durations):
        raise SystemExit('rates and durations must have same length')
    sequence = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))['sequence']
    cases = []
    rc = 0
    for rate, duration in zip(args.rates, args.durations):
        case = run_case(args.host, args.port, sequence, rate, duration, args.max_workers, args.timeout)
        cases.append(case)
        if case['failed_requests']:
            rc = 1
    payload = {'scenario': 'A2', 'rates': args.rates, 'durations': args.durations, 'max_workers': args.max_workers, 'cases': cases}
    pathlib.Path(args.out).write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(payload, indent=2, sort_keys=True))
    raise SystemExit(rc)


if __name__ == '__main__':
    main()
