#!/usr/bin/env python3
import argparse
import http.client
import json
import socket
import time
from pathlib import Path


def send_http_bytes(host: str, port: int, request: bytes, timeout: float = 10.0):
    started = time.time()
    response_line = None
    response_len = 0
    status_code = None
    outcome = 'response'
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.sendall(request)
            try:
                data = sock.recv(65536)
                response_len = len(data)
                if data:
                    first = data.split(b'\r\n', 1)[0].decode('utf-8', 'replace')
                    response_line = first
                    parts = first.split()
                    if len(parts) >= 2 and parts[1].isdigit():
                        status_code = int(parts[1])
                else:
                    outcome = 'empty'
            except OSError:
                outcome = 'recv_error'
    except OSError as exc:
        outcome = f'connect_error:{exc.__class__.__name__}'
    return {'elapsed_ms': round((time.time() - started) * 1000.0, 3), 'outcome': outcome, 'response_line': response_line, 'response_len': response_len, 'status_code': status_code}


def post_http_client(host: str, port: int, path: str, payload: bytes, content_type: str):
    started = time.time()
    status = None
    body = ''
    outcome = 'response'
    try:
        conn = http.client.HTTPConnection(host, port, timeout=30)
        conn.request('POST', path, body=payload, headers={'Content-Type': content_type, 'Content-Length': str(len(payload)), 'Connection': 'close'})
        resp = conn.getresponse()
        status = resp.status
        body = resp.read().decode('utf-8', 'replace')
        conn.close()
    except OSError as exc:
        outcome = f'client_error:{exc.__class__.__name__}'
    return {'elapsed_ms': round((time.time() - started) * 1000.0, 3), 'outcome': outcome, 'status_code': status, 'body_snippet': body[:200]}


def control_request(host: str, port: int):
    payload = json.dumps({'source': 'control', 'seq': 1, 'payload': 'ok'}).encode('utf-8')
    return post_http_client(host, port, '/push', payload, 'application/json')


def build_case_payload(case: str, fixture_dir: Path, too_large_size: int, port: int):
    invalid_json = (fixture_dir / 'invalid' / 'invalid_json.txt').read_bytes()
    broken_gzip = (fixture_dir / 'invalid' / 'broken_gzip.bin').read_bytes()
    if case == 'wrong_method':
        return ('raw', b'GET /push HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
    if case == 'wrong_path':
        return ('raw', b'POST /wrong HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}')
    if case == 'invalid_json':
        return ('client', ('/push', invalid_json, 'application/json'))
    if case == 'broken_inflate':
        return ('client', ('/push', broken_gzip, 'application/octet-stream'))
    if case == 'mixed_chunked_bodies':
        body = b'4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n'
        req = b'POST /push HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Length: 9\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n' + body
        return ('raw', req)
    if case == 'too_large_bodies':
        payload = json.dumps({'source': 'oversize', 'seq': 1, 'payload': 'Z' * too_large_size}, separators=(',', ':')).encode('utf-8')
        return ('client', ('/push', payload, 'application/json'))
    if case == 'header_abuse':
        huge = b'A' * 8192
        req = b'POST /push HTTP/1.1\r\nHost: localhost\r\nX-Abuse: ' + huge + b'\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}'
        return ('raw', req)
    if case == 'random_disconnects':
        started = time.time()
        outcome = 'disconnect_sent'
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
                sock.sendall(b'POST /push HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000\r\nConnection: close\r\n\r\npartial')
        except OSError as exc:
            outcome = f'disconnect_error:{exc.__class__.__name__}'
        return ('prebuilt', {'elapsed_ms': round((time.time() - started) * 1000.0, 3), 'outcome': outcome, 'status_code': None})
    raise ValueError(case)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--fixture-dir', required=True)
    ap.add_argument('--cases', nargs='+', required=True)
    ap.add_argument('--repeats', type=int, default=5)
    ap.add_argument('--too-large-bytes', type=int, default=400000)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    fixture_dir = Path(args.fixture_dir)
    results = []
    for case in args.cases:
        for idx in range(args.repeats):
            mode, payload = build_case_payload(case, fixture_dir, args.too_large_bytes, args.port)
            if mode == 'raw':
                result = send_http_bytes(args.host, args.port, payload)
            elif mode == 'client':
                path, body, content_type = payload
                result = post_http_client(args.host, args.port, path, body, content_type)
            else:
                result = payload
            control = control_request(args.host, args.port)
            results.append({'case': case, 'iteration': idx + 1, 'result': result, 'control_after': control})
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    summary = {'host': args.host, 'port': args.port, 'repeats': args.repeats, 'too_large_bytes': args.too_large_bytes, 'results': results}
    out.write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
