#!/usr/bin/env python3
import argparse
import json
import pathlib
import re
import subprocess

REQ_S_RE = re.compile(r"Requests per second:\s+([0-9.]+)")
TPR_RE = re.compile(r"Time per request:\s+([0-9.]+)\s+\[ms\] \(mean\)")
TPR_ALL_RE = re.compile(r"Time per request:\s+([0-9.]+)\s+\[ms\] \(mean, across all concurrent requests\)")
FAILED_RE = re.compile(r"Failed requests:\s+(\d+)")
PCT_RE = re.compile(r"^\s*(50%|95%|99%)\s+(\d+)$", re.MULTILINE)


def parse_ab(text: str):
    data = {}
    for rx, key, cast in [
        (REQ_S_RE, 'requests_per_second', float),
        (TPR_RE, 'time_per_request_ms_mean', float),
        (TPR_ALL_RE, 'time_per_request_ms_mean_across_all', float),
        (FAILED_RE, 'failed_requests', int),
    ]:
        m = rx.search(text)
        if m:
            data[key] = cast(m.group(1))
    pct = {}
    for m in PCT_RE.finditer(text):
        pct[m.group(1)] = int(m.group(2))
    if pct:
        data['percentiles_ms'] = pct
    return data


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--path', default='/push')
    ap.add_argument('--payload', required=True)
    ap.add_argument('--content-type', default='application/json')
    ap.add_argument('--concurrency', type=int, required=True)
    ap.add_argument('--requests', type=int, required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--raw-out', required=True)
    args = ap.parse_args()
    cmd = ['ab','-q','-k','-n',str(args.requests),'-c',str(args.concurrency),'-p',args.payload,'-T',args.content_type,'-s','30',f'http://{args.host}:{args.port}{args.path}']
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    pathlib.Path(args.raw_out).write_text(proc.stdout, encoding='utf-8')
    parsed = parse_ab(proc.stdout)
    parsed.update({'command': cmd,'exit_code': proc.returncode,'concurrency': args.concurrency,'requests': args.requests,'payload': args.payload})
    pathlib.Path(args.out).write_text(json.dumps(parsed, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(parsed, indent=2, sort_keys=True))
    return proc.returncode

if __name__ == '__main__':
    raise SystemExit(main())
