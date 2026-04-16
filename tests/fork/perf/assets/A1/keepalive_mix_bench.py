#!/usr/bin/env python3
import argparse
import concurrent.futures
import http.client
import json
import pathlib
import time
from typing import Dict, List


def percentile(sorted_values: List[float], pct: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = pct * (len(sorted_values) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(sorted_values) - 1)
    frac = rank - lo
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac


def run_client(host: str, port: int, sequence: List[Dict], iterations: int, timeout: float) -> Dict:
    conn = None
    reconnects = 0
    sequences_ok = 0
    requests_ok = 0
    request_errors = 0
    sequence_latencies_ms: List[float] = []
    request_latencies_ms: List[float] = []
    try:
        for _ in range(iterations):
            seq_start = time.perf_counter()
            for item in sequence:
                method = item['method']
                path = item['path']
                body = item.get('body')
                headers = {'Connection': 'keep-alive'}
                headers.update(item.get('headers', {}))
                attempt = 0
                while True:
                    attempt += 1
                    if conn is None:
                        conn = http.client.HTTPConnection(host, port, timeout=timeout)
                    req_start = time.perf_counter()
                    try:
                        conn.request(method, path, body=body, headers=headers)
                        resp = conn.getresponse()
                        data = resp.read()
                    except (http.client.RemoteDisconnected, BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
                        reconnects += 1
                        try:
                            conn.close()
                        except Exception:
                            pass
                        conn = None
                        if attempt >= 2:
                            request_errors += 1
                            break
                        continue
                    request_latencies_ms.append((time.perf_counter() - req_start) * 1000.0)
                    if resp.status != 200 or not data:
                        request_errors += 1
                    else:
                        requests_ok += 1
                    break
            sequence_latencies_ms.append((time.perf_counter() - seq_start) * 1000.0)
            sequences_ok += 1
    finally:
        if conn is not None:
            conn.close()
    return {
        'sequences_ok': sequences_ok,
        'requests_ok': requests_ok,
        'request_errors': request_errors,
        'reconnects': reconnects,
        'sequence_latencies_ms': sequence_latencies_ms,
        'request_latencies_ms': request_latencies_ms,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description='Run the A1 keepalive benchmark mix.')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--sequence', required=True)
    ap.add_argument('--clients', type=int, required=True)
    ap.add_argument('--iterations', type=int, required=True)
    ap.add_argument('--timeout', type=float, default=10.0)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    sequence = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))['sequence']
    started = time.perf_counter()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.clients) as ex:
        futs = [ex.submit(run_client, args.host, args.port, sequence, args.iterations, args.timeout) for _ in range(args.clients)]
        for fut in concurrent.futures.as_completed(futs):
            results.append(fut.result())
    elapsed = time.perf_counter() - started
    seq_lat = sorted([x for r in results for x in r['sequence_latencies_ms']])
    req_lat = sorted([x for r in results for x in r['request_latencies_ms']])
    total_sequences = sum(r['sequences_ok'] for r in results)
    total_requests_ok = sum(r['requests_ok'] for r in results)
    total_request_errors = sum(r['request_errors'] for r in results)
    total_reconnects = sum(r['reconnects'] for r in results)
    payload = {
        'scenario': 'A1',
        'clients': args.clients,
        'iterations_per_client': args.iterations,
        'sequence_length': len(sequence),
        'elapsed_seconds': elapsed,
        'total_sequences': total_sequences,
        'total_requests_ok': total_requests_ok,
        'total_request_errors': total_request_errors,
        'total_reconnects': total_reconnects,
        'throughput_requests_per_second': (total_requests_ok / elapsed) if elapsed > 0 else 0.0,
        'throughput_sequences_per_second': (total_sequences / elapsed) if elapsed > 0 else 0.0,
        'sequence_latency_ms': {
            'p50': percentile(seq_lat, 0.50),
            'p95': percentile(seq_lat, 0.95),
            'p99': percentile(seq_lat, 0.99),
            'max': max(seq_lat) if seq_lat else 0.0,
        },
        'request_latency_ms': {
            'p50': percentile(req_lat, 0.50),
            'p95': percentile(req_lat, 0.95),
            'p99': percentile(req_lat, 0.99),
            'max': max(req_lat) if req_lat else 0.0,
        },
    }
    pathlib.Path(args.out).write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if total_request_errors == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
