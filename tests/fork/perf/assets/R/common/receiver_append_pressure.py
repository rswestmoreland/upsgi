#!/usr/bin/env python3
import argparse, http.client, json, pathlib, statistics, subprocess, threading, time
from typing import List

def percentile(values: List[float], pct: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    if len(values) == 1:
        return values[0]
    rank = pct * (len(values) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(values) - 1)
    frac = rank - lo
    return values[lo] * (1.0 - frac) + values[hi] * frac

def control_post(host, port, path, payload_bytes, interval_ms, count, results):
    headers = {'Content-Type': 'application/json', 'Content-Length': str(len(payload_bytes)), 'Connection': 'close'}
    for _ in range(count):
        start = time.perf_counter()
        status = None
        error = ''
        try:
            conn = http.client.HTTPConnection(host, port, timeout=30)
            conn.request('POST', path, body=payload_bytes, headers=headers)
            resp = conn.getresponse()
            resp.read()
            status = resp.status
            conn.close()
        except Exception as exc:
            error = exc.__class__.__name__
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        results.append({'status': status, 'elapsed_ms': elapsed_ms, 'error': error})
        time.sleep(interval_ms / 1000.0)

def parse_ab(text: str):
    import re
    data = {}
    m = re.search(r"Requests per second:\s+([0-9.]+)", text)
    if m: data['requests_per_second'] = float(m.group(1))
    m = re.search(r"Time per request:\s+([0-9.]+)\s+\[ms\] \(mean\)", text)
    if m: data['time_per_request_ms_mean'] = float(m.group(1))
    m = re.search(r"Failed requests:\s+(\d+)", text)
    if m: data['failed_requests'] = int(m.group(1))
    pct = {}
    for mm in re.finditer(r"^\s*(50%|95%|99%)\s+(\d+)$", text, re.MULTILINE):
        pct[mm.group(1)] = int(mm.group(2))
    if pct: data['percentiles_ms'] = pct
    return data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--path', default='/push')
    ap.add_argument('--payload', required=True)
    ap.add_argument('--control-payload', required=True)
    ap.add_argument('--content-type', default='application/json')
    ap.add_argument('--delay-ms', nargs='+', type=int, required=True)
    ap.add_argument('--concurrency', nargs='+', type=int, required=True)
    ap.add_argument('--requests', nargs='+', type=int, required=True)
    ap.add_argument('--control-posts', type=int, default=8)
    ap.add_argument('--control-interval-ms', type=int, default=200)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    if not (len(args.delay_ms) == len(args.concurrency) == len(args.requests)):
        raise SystemExit('delay-ms, concurrency, and requests must have same length')
    payload = pathlib.Path(args.payload).read_bytes()
    control_payload = pathlib.Path(args.control_payload).read_bytes()
    cases = []
    for delay, concurrency, requests in zip(args.delay_ms, args.concurrency, args.requests):
        results = []
        t = threading.Thread(target=control_post, args=(args.host, args.port, args.path, control_payload, args.control_interval_ms, args.control_posts, results))
        t.start()
        cmd = [
            'ab','-q','-k','-n',str(requests),'-c',str(concurrency),
            '-p',str(pathlib.Path(args.payload)),
            '-T',args.content_type,'-s','60',
            f'http://{args.host}:{args.port}{args.path}'
        ]
        start = time.perf_counter()
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        elapsed = time.perf_counter() - start
        t.join()
        control_lat = [r['elapsed_ms'] for r in results]
        control_ok = sum(1 for r in results if r['status'] == 202 and not r['error'])
        parsed = parse_ab(proc.stdout)
        parsed.update({
            'append_delay_ms': delay,
            'concurrency': concurrency,
            'requests': requests,
            'elapsed_seconds': elapsed,
            'control_posts_total': args.control_posts,
            'control_posts_ok': control_ok,
            'control_latency_ms': {
                'p50': percentile(control_lat, 0.50),
                'p95': percentile(control_lat, 0.95),
                'max': max(control_lat) if control_lat else 0.0,
            },
            'control_errors': [r for r in results if r['error'] or r['status'] != 202],
            'command': cmd,
            'exit_code': proc.returncode,
        })
        cases.append(parsed)
    out = {'scenario': 'R5', 'cases': cases}
    pathlib.Path(args.out).write_text(json.dumps(out, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(out, indent=2, sort_keys=True))
    raise SystemExit(0)

if __name__ == '__main__':
    main()
