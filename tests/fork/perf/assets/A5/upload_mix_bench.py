#!/usr/bin/env python3
import argparse, concurrent.futures, http.client, json, pathlib, statistics, threading, time
from typing import Dict, List

def percentile(sorted_values: List[float], pct: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = pct * (len(sorted_values) - 1)
    lo = int(rank); hi = min(lo + 1, len(sorted_values) - 1); frac = rank - lo
    return sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac

def make_body(size: int) -> bytes:
    chunk = b'upsgi-A5-upload-payload\n'
    out = bytearray()
    while len(out) < size:
        out.extend(chunk)
    del out[size:]
    return bytes(out)

def do_get(host: str, port: int, timeout: float, path: str) -> Dict:
    start = time.perf_counter(); conn = None
    try:
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request('GET', path, headers={'Connection':'close'})
        resp = conn.getresponse(); data = resp.read()
        return {'ok': resp.status == 200 and len(data) > 0, 'status': resp.status, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': ''}
    except Exception as exc:
        return {'ok': False, 'status': 0, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': exc.__class__.__name__}
    finally:
        if conn:
            try: conn.close()
            except Exception: pass

def do_upload(host: str, port: int, timeout: float, body: bytes) -> Dict:
    start = time.perf_counter(); conn = None
    try:
        conn = http.client.HTTPConnection(host, port, timeout=timeout)
        conn.request('POST', '/upload', body=body, headers={'Connection':'close','Content-Type':'application/octet-stream','Content-Length':str(len(body))})
        resp = conn.getresponse(); data = resp.read()
        return {'ok': resp.status == 200 and len(data) > 0, 'status': resp.status, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': ''}
    except Exception as exc:
        return {'ok': False, 'status': 0, 'elapsed_ms': (time.perf_counter()-start)*1000.0, 'error': exc.__class__.__name__}
    finally:
        if conn:
            try: conn.close()
            except Exception: pass

def summarize(results: List[Dict]) -> Dict:
    lats = sorted(r['elapsed_ms'] for r in results)
    return {'count': len(results), 'ok': sum(1 for r in results if r['ok']), 'failed': sum(1 for r in results if not r['ok']), 'latency_ms': {'p50': percentile(lats,0.50), 'p95': percentile(lats,0.95), 'p99': percentile(lats,0.99), 'max': max(lats) if lats else 0.0, 'mean': statistics.mean(lats) if lats else 0.0}}

def run_case(host, port, timeout, upload_size, uploaders, uploads_per_uploader, control_gets, control_interval_ms):
    body = make_body(upload_size)
    upload_results=[]; control_results=[]; lock=threading.Lock()
    def uploader_worker(_):
        local=[]
        for _ in range(uploads_per_uploader):
            local.append(do_upload(host, port, timeout, body))
        with lock: upload_results.extend(local)
    def control_worker():
        for _ in range(control_gets):
            with lock: control_results.append(do_get(host, port, timeout, '/small'))
            time.sleep(control_interval_ms/1000.0)
    started=time.perf_counter(); ctl=threading.Thread(target=control_worker,daemon=True); ctl.start()
    with concurrent.futures.ThreadPoolExecutor(max_workers=uploaders) as ex:
        list(ex.map(uploader_worker, range(uploaders)))
    ctl.join(); elapsed=time.perf_counter()-started
    return {'upload_size_bytes': upload_size, 'uploaders': uploaders, 'uploads_per_uploader': uploads_per_uploader, 'control_gets': control_gets, 'elapsed_seconds': elapsed, 'upload_summary': summarize(upload_results), 'control_summary': summarize(control_results)}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--host', default='127.0.0.1'); ap.add_argument('--port', type=int, required=True); ap.add_argument('--sizes', nargs='+', type=int, required=True); ap.add_argument('--uploaders', nargs='+', type=int, required=True); ap.add_argument('--uploads-per-uploader', nargs='+', type=int, required=True); ap.add_argument('--control-gets', type=int, default=10); ap.add_argument('--control-interval-ms', type=int, default=200); ap.add_argument('--timeout', type=float, default=120.0); ap.add_argument('--out', required=True); args=ap.parse_args()
    if not (len(args.sizes)==len(args.uploaders)==len(args.uploads_per_uploader)): raise SystemExit('sizes/uploaders/uploads-per-uploader length mismatch')
    cases=[]
    for size,ups,upu in zip(args.sizes,args.uploaders,args.uploads_per_uploader):
        cases.append(run_case(args.host,args.port,args.timeout,size,ups,upu,args.control_gets,args.control_interval_ms))
    payload={'scenario':'A5','cases':cases}
    pathlib.Path(args.out).write_text(json.dumps(payload, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(payload, indent=2, sort_keys=True))

if __name__=='__main__':
    main()
