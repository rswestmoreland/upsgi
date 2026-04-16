#!/usr/bin/env python3
import argparse
import http.client
import json
import pathlib
import sys


def make_body(size: int) -> bytes:
    chunk = b'upsgi-A5-upload-payload\n'
    out = bytearray()
    while len(out) < size:
        out.extend(chunk)
    del out[size:]
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description='Run one mixed small/upload cycle for the A5 scenario.')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--sequence', required=True)
    args = parser.parse_args()

    sequence = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))['sequence']
    conn = http.client.HTTPConnection(args.host, args.port, timeout=30)
    try:
        for idx, item in enumerate(sequence):
            method = item['method']
            body = None
            headers = {'Connection': 'keep-alive'}
            if method == 'POST':
                body = make_body(item['body_bytes'])
                headers['Content-Type'] = 'application/octet-stream'
                headers['Content-Length'] = str(len(body))
            conn.request(method, item['path'], body=body, headers=headers)
            resp = conn.getresponse()
            data = resp.read()
            if resp.status != 200:
                print(f'step {idx} failed: {item["path"]} -> {resp.status}', file=sys.stderr)
                return 1
            if method == 'GET' and len(data) < item.get('expect_bytes_min', 1):
                print(f'step {idx} GET too small: {len(data)}', file=sys.stderr)
                return 1
    finally:
        conn.close()
    print('A5 mixed upload sequence ok')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
