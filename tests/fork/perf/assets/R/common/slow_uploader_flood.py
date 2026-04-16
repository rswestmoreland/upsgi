#!/usr/bin/env python3
import argparse, http.client, json, socket, threading, time
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

def slow_sender(host, port, path, payload, content_type, chunk_size, delay_s, idx, results):
    start = time.perf_counter()
    try:
        req = (
            f"POST {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Content-Type: {content_type}\r\n"
            f"Content-Length: {len(payload)}\r\n"
            f"Connection: close\r\n\r\n"
        ).encode('ascii')
        with socket.create_connection((host, port), timeout=30) as sock:
            sock.sendall(req)
            for off in range(0, len(payload), chunk_size):
                sock.sendall(payload[off:off+chunk_size])
                time.sleep(delay_s)
            sock.shutdown(socket.SHUT_WR)
            resp = sock.recv(65536)
        ok = b' 202' in resp or b' 200' in resp
        try:
            status = resp.split(b'\r\n',1)[0].decode('ascii','replace')
        except Exception:
            status = 'unknown'
        results[idx] = {'ok': ok, 'status_line': status, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': ''}
    except Exception as exc:
        results[idx] = {'ok': False, 'status_line': '', 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': exc.__class__.__name__}

def control_post(host, port, path, payload, content_type):
    start = time.perf_counter()
    try:
        conn = http.client.HTTPConnection(host, port, timeout=30)
        conn.request('POST', path, body=payload, headers={'Content-Type': content_type, 'Content-Length': str(len(payload))})
        resp = conn.getresponse()
        resp.read()
        conn.close()
        return {'ok': resp.status == 202, 'status': resp.status, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': ''}
    except Exception as exc:
        return {'ok': False, 'status': 0, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': exc.__class__.__name__}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--path', default='/push')
    ap.add_argument('--payload', required=True)
    ap.add_argument('--content-type', default='application/json')
    ap.add_argument('--chunk-size', type=int, default=4096)
    ap.add_argument('--rates', nargs='+', type=int, required=True)
    ap.add_argument('--uploaders', nargs='+', type=int, required=True)
    ap.add_argument('--control-posts', type=int, default=5)
    ap.add_argument('--control-interval-ms', type=int, default=250)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    if len(args.rates) != len(args.uploaders):
        raise SystemExit('rates and uploaders length mismatch')
    payload = Path(args.payload).read_bytes()
    cases = []
    for rate, uploaders in zip(args.rates, args.uploaders):
        delay_s = args.chunk_size / float(rate)
        results = [None] * uploaders
        threads = []
        for i in range(uploaders):
            t = threading.Thread(target=slow_sender, args=(args.host, args.port, args.path, payload, args.content_type, args.chunk_size, delay_s, i, results), daemon=True)
            t.start()
            threads.append(t)
        time.sleep(min(2.0, max(delay_s*2.0, 0.2)))
        controls = []
        for _ in range(args.control_posts):
            controls.append(control_post(args.host, args.port, args.path, payload, args.content_type))
            time.sleep(args.control_interval_ms/1000.0)
        for t in threads:
            t.join()
        slow_ok = sum(1 for r in results if r and r.get('ok'))
        slow_fail = sum(1 for r in results if not r or not r.get('ok'))
        control_ok = sum(1 for r in controls if r.get('ok'))
        control_fail = len(controls) - control_ok
        clats = sorted(r['elapsed_ms'] for r in controls)
        slats = sorted(r['elapsed_ms'] for r in results if r)
        cases.append({
            'rate_bytes_per_sec': rate,
            'uploaders': uploaders,
            'payload_bytes': len(payload),
            'chunk_size': args.chunk_size,
            'slow_sender_delay_ms': delay_s*1000.0,
            'slow_successful': slow_ok,
            'slow_failed': slow_fail,
            'control_successful': control_ok,
            'control_failed': control_fail,
            'control_latency_ms': {
                'p50': percentile(clats, 0.50),
                'p95': percentile(clats, 0.95),
                'max': max(clats) if clats else 0.0,
            },
            'slow_elapsed_ms': {
                'p50': percentile(slats, 0.50),
                'p95': percentile(slats, 0.95),
                'max': max(slats) if slats else 0.0,
            },
            'slow_errors': sorted(set(r.get('error','') for r in results if r and r.get('error'))),
            'control_errors': sorted(set(r.get('error','') for r in controls if r.get('error'))),
            'status_lines': sorted(set(r.get('status_line','') for r in results if r and r.get('status_line'))),
        })
    out = {'scenario': 'R4', 'cases': cases}
    Path(args.out).write_text(json.dumps(out, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(out, indent=2, sort_keys=True))

if __name__ == '__main__':
    main()
