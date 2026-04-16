#!/usr/bin/env python3
import argparse, http.client, json, threading, time, urllib.parse
from pathlib import Path

def percentile(vals, pct):
    if not vals:
        return 0.0
    vals = sorted(vals)
    if len(vals) == 1:
        return vals[0]
    rank = pct * (len(vals)-1)
    lo = int(rank)
    hi = min(lo+1, len(vals)-1)
    frac = rank - lo
    return vals[lo]*(1-frac) + vals[hi]*frac

def slow_reader(host, port, chunks, chunk_size, label, read_size, delay_s, idx, results):
    query = urllib.parse.urlencode({
        'chunks': chunks,
        'chunk_size': chunk_size,
        'label': label,
    })
    target = f"/stream?{query}"
    start = time.perf_counter()
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port, timeout=60)
        conn.request('GET', target, headers={'Connection': 'close'})
        resp = conn.getresponse()
        total = 0
        while True:
            block = resp.read(read_size)
            if not block:
                break
            total += len(block)
            time.sleep(delay_s)
        results[idx] = {
            'ok': resp.status == 200,
            'status': resp.status,
            'total_bytes': total,
            'elapsed_ms': (time.perf_counter()-start)*1000.0,
            'error': '',
        }
    except Exception as exc:
        results[idx] = {
            'ok': False,
            'status': 0,
            'total_bytes': 0,
            'elapsed_ms': (time.perf_counter()-start)*1000.0,
            'error': exc.__class__.__name__,
        }
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass

def control_get(host, port, path):
    start = time.perf_counter()
    conn = None
    try:
        conn = http.client.HTTPConnection(host, port, timeout=30)
        conn.request('GET', path, headers={'Connection': 'close'})
        resp = conn.getresponse()
        body = resp.read()
        return {
            'ok': resp.status == 200 and b'ok' in body,
            'status': resp.status,
            'elapsed_ms': (time.perf_counter()-start)*1000.0,
            'error': '',
        }
    except Exception as exc:
        return {
            'ok': False,
            'status': 0,
            'elapsed_ms': (time.perf_counter()-start)*1000.0,
            'error': exc.__class__.__name__,
        }
    finally:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--read-rates', nargs='+', type=int, required=True)
    ap.add_argument('--readers', nargs='+', type=int, required=True)
    ap.add_argument('--chunks', type=int, default=32)
    ap.add_argument('--chunk-size', type=int, default=65536)
    ap.add_argument('--read-size', type=int, default=4096)
    ap.add_argument('--control-path', default='/small')
    ap.add_argument('--control-requests', type=int, default=5)
    ap.add_argument('--control-interval-ms', type=int, default=250)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    if len(args.read_rates) != len(args.readers):
        raise SystemExit('read-rates and readers length mismatch')
    cases = []
    for rate, readers in zip(args.read_rates, args.readers):
        delay_s = args.read_size / float(rate)
        results = [None] * readers
        threads = []
        for i in range(readers):
            t = threading.Thread(
                target=slow_reader,
                args=(args.host, args.port, args.chunks, args.chunk_size, f'slow-{rate}-{i}', args.read_size, delay_s, i, results),
                daemon=True,
            )
            t.start()
            threads.append(t)
        time.sleep(min(2.0, max(delay_s*2.0, 0.2)))
        controls = []
        for _ in range(args.control_requests):
            controls.append(control_get(args.host, args.port, args.control_path))
            time.sleep(args.control_interval_ms / 1000.0)
        for t in threads:
            t.join()
        rlats = sorted(r['elapsed_ms'] for r in results if r)
        clats = sorted(r['elapsed_ms'] for r in controls)
        cases.append({
            'rate_bytes_per_sec': rate,
            'readers': readers,
            'chunks': args.chunks,
            'chunk_size': args.chunk_size,
            'read_size': args.read_size,
            'slow_successful': sum(1 for r in results if r and r.get('ok')),
            'slow_failed': sum(1 for r in results if not r or not r.get('ok')),
            'control_successful': sum(1 for r in controls if r.get('ok')),
            'control_failed': sum(1 for r in controls if not r.get('ok')),
            'control_latency_ms': {
                'p50': percentile(clats, 0.50),
                'p95': percentile(clats, 0.95),
                'max': max(clats) if clats else 0.0,
            },
            'slow_elapsed_ms': {
                'p50': percentile(rlats, 0.50),
                'p95': percentile(rlats, 0.95),
                'max': max(rlats) if rlats else 0.0,
            },
            'slow_errors': sorted(set(r.get('error','') for r in results if r and r.get('error'))),
            'control_errors': sorted(set(r.get('error','') for r in controls if r.get('error'))),
        })
    out = {'scenario': 'A6', 'cases': cases}
    Path(args.out).write_text(json.dumps(out, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(out, indent=2, sort_keys=True))

if __name__ == '__main__':
    main()
