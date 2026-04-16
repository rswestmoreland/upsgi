#!/usr/bin/env python3
import argparse
import http.client
import json
import pathlib
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description='Run one mixed small/download cycle for the A4 scenario.')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--sequence', required=True)
    args = parser.parse_args()

    sequence = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))['sequence']
    conn = http.client.HTTPConnection(args.host, args.port, timeout=30)
    try:
        for idx, item in enumerate(sequence):
            conn.request(item['method'], item['path'], headers={'Connection': 'keep-alive'})
            resp = conn.getresponse()
            data = resp.read()
            if resp.status != 200:
                print(f'step {idx} failed: {item["path"]} -> {resp.status}', file=sys.stderr)
                return 1
            if 'expect_bytes' in item and len(data) != item['expect_bytes']:
                print(f'step {idx} wrong size: expected {item["expect_bytes"]}, got {len(data)}', file=sys.stderr)
                return 1
            if 'expect_bytes_min' in item and len(data) < item['expect_bytes_min']:
                print(f'step {idx} too small: got {len(data)}', file=sys.stderr)
                return 1
    finally:
        conn.close()
    print('A4 mixed download sequence ok')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
