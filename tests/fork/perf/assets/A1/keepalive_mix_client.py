#!/usr/bin/env python3
import argparse
import http.client
import json
import pathlib
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description='Run one keepalive request sequence for the A1 scenario.')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--sequence', required=True)
    args = parser.parse_args()

    sequence = json.loads(pathlib.Path(args.sequence).read_text(encoding='utf-8'))['sequence']
    conn = None
    reconnects = 0
    try:
        for idx, item in enumerate(sequence):
            method = item['method']
            path = item['path']
            body = item.get('body')
            headers = {'Connection': 'keep-alive'}
            headers.update(item.get('headers', {}))
            if conn is None:
                conn = http.client.HTTPConnection(args.host, args.port, timeout=10)
            try:
                conn.request(method, path, body=body, headers=headers)
                resp = conn.getresponse()
            except http.client.RemoteDisconnected:
                reconnects += 1
                try:
                    conn.close()
                except Exception:
                    pass
                conn = http.client.HTTPConnection(args.host, args.port, timeout=10)
                conn.request(method, path, body=body, headers=headers)
                resp = conn.getresponse()
            data = resp.read()
            if resp.status != 200:
                print(f'step {idx} failed: {method} {path} -> {resp.status}', file=sys.stderr)
                return 1
            if not data:
                print(f'step {idx} returned empty body: {method} {path}', file=sys.stderr)
                return 1
    finally:
        if conn is not None:
            conn.close()
    print(f'A1 keepalive sequence ok reconnects={reconnects}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
