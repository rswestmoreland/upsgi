#!/usr/bin/env python3
import argparse
import concurrent.futures
import http.client
import json
import pathlib
import time
from typing import Dict, List, Tuple


def percentile(v: List[float], p: float) -> float:
    if not v:
        return 0.0
    if len(v) == 1:
        return v[0]
    r = p * (len(v) - 1)
    lo = int(r)
    hi = min(lo + 1, len(v) - 1)
    f = r - lo
    return v[lo] * (1.0 - f) + v[hi] * f


def request_once(host: str, port: int, path: str, timeout: float) -> Tuple[bool, int, float, str]:
    conn = None
    start = time.perf_counter()
    try:
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request('GET', path, headers={'Connection': 'close'})
        resp = conn.getresponse()
        data = resp.read()
        elapsed = (time.perf_counter() - start) * 1000.0
        return (resp.status == 200 and len(data) > 0), resp.status, elapsed, ''
    except Exception as exc:
        return False, 0, (time.perf_counter() - start) * 1000.0, exc.__class__.__name__
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass


def run_sequence(host: str, port: int, spec: Dict, timeout: float) -> Dict:
    doc, api, assets = [], [], []
    status_counts: Dict[str, int] = {}
    error_counts: Dict[str, int] = {}
    okc = 0
    errc = 0

    def count(m: Dict[str, int], k: str) -> None:
        m[k] = m.get(k, 0) + 1

    seq_start = time.perf_counter()
    for kind, paths in [('document', [spec['document']]), ('api_probe', [spec['api_probe']]), ('asset', spec['assets'])]:
        for path in paths:
            ok, status, elapsed, err = request_once(host, port, path, timeout)
            if kind == 'document':
                doc.append(elapsed)
            elif kind == 'api_probe':
                api.append(elapsed)
            else:
                assets.append(elapsed)
            if status:
                count(status_counts, str(status))
            if err:
                count(error_counts, err)
            if ok:
                okc += 1
            else:
                errc += 1

    return {
        'sequence_elapsed_ms': (time.perf_counter() - seq_start) * 1000.0,
        'document_latencies_ms': doc,
        'api_latencies_ms': api,
        'asset_latencies_ms': assets,
        'request_ok': okc,
        'request_errors': errc,
        'status_counts': status_counts,
        'error_counts': error_counts,
    }


def merge(dst: Dict[str, int], src: Dict[str, int]) -> None:
    for k, v in src.items():
        dst[k] = dst.get(k, 0) + v


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--sequence', required=True)
    ap.add_argument('--clients', type=int, required=True)
    ap.add_argument('--iterations', type=int, required=True)
    ap.add_argument('--timeout', type=float, default=10.0)
    ap.add_argument('--label', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    spec = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))
    started = time.perf_counter()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.clients) as ex:
        futs = [ex.submit(run_sequence, args.host, args.port, spec, args.timeout) for _ in range(args.clients * args.iterations)]
        for fut in concurrent.futures.as_completed(futs):
            results.append(fut.result())
    elapsed = time.perf_counter() - started

    seq = sorted(r['sequence_elapsed_ms'] for r in results)
    doc = sorted(x for r in results for x in r['document_latencies_ms'])
    api = sorted(x for r in results for x in r['api_latencies_ms'])
    assets = sorted(x for r in results for x in r['asset_latencies_ms'])
    ok = sum(r['request_ok'] for r in results)
    errs = sum(r['request_errors'] for r in results)
    status_counts: Dict[str, int] = {}
    error_counts: Dict[str, int] = {}
    for r in results:
        merge(status_counts, r['status_counts'])
        merge(error_counts, r['error_counts'])

    payload = {
        'scenario': 'A3',
        'label': args.label,
        'clients': args.clients,
        'iterations_per_client': args.iterations,
        'sequence_length': 2 + len(spec['assets']),
        'total_sequences': len(results),
        'total_requests_ok': ok,
        'total_request_errors': errs,
        'elapsed_seconds': elapsed,
        'throughput_requests_per_second': (ok / elapsed if elapsed > 0 else 0.0),
        'throughput_sequences_per_second': (len(results) / elapsed if elapsed > 0 else 0.0),
        'sequence_latency_ms': {'p50': percentile(seq, 0.50), 'p95': percentile(seq, 0.95), 'p99': percentile(seq, 0.99), 'max': (max(seq) if seq else 0.0)},
        'document_latency_ms': {'p50': percentile(doc, 0.50), 'p95': percentile(doc, 0.95), 'p99': percentile(doc, 0.99), 'max': (max(doc) if doc else 0.0)},
        'api_latency_ms': {'p50': percentile(api, 0.50), 'p95': percentile(api, 0.95), 'p99': percentile(api, 0.99), 'max': (max(api) if api else 0.0)},
        'asset_latency_ms': {'p50': percentile(assets, 0.50), 'p95': percentile(assets, 0.95), 'p99': percentile(assets, 0.99), 'max': (max(assets) if assets else 0.0)},
        'status_counts': status_counts,
        'error_counts': error_counts,
    }
    pathlib.Path(args.out).write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if errs == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
