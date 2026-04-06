#!/usr/bin/env python3
import argparse
import http.client
import json
import math
import statistics
import sys
import time


def parse_headers(values):
    headers = {}
    for item in values:
        if ':' not in item:
            raise SystemExit(f"invalid header value: {item!r}")
        key, value = item.split(':', 1)
        headers[key.strip()] = value.strip()
    return headers



def percentile(sorted_values, fraction):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return float(sorted_values[0])
    index = (len(sorted_values) - 1) * fraction
    low = math.floor(index)
    high = math.ceil(index)
    if low == high:
        return float(sorted_values[low])
    weight = index - low
    return float(sorted_values[low] * (1.0 - weight) + sorted_values[high] * weight)



def run_request(args, body, headers):
    conn = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
    started = time.perf_counter_ns()
    conn.request(args.method, args.path, body=body, headers=headers)
    resp = conn.getresponse()
    response_body = resp.read()
    ended = time.perf_counter_ns()
    conn.close()
    return resp.status, response_body, ended - started



def validate_response(args, status, response_body):
    if status != args.expect_status:
        raise RuntimeError(f"unexpected status: got {status}, expected {args.expect_status}")
    if args.expect_contains:
        needle = args.expect_contains.encode('utf-8')
        if needle not in response_body:
            raise RuntimeError(f"response body missing expected substring: {args.expect_contains!r}")
    if args.expect_body_bytes is not None and len(response_body) != args.expect_body_bytes:
        raise RuntimeError(
            f"unexpected response body length: got {len(response_body)}, expected {args.expect_body_bytes}"
        )



def main():
    parser = argparse.ArgumentParser(description='Simple sequential HTTP benchmark helper for upsgi fork checkpoints.')
    parser.add_argument('--label', required=True)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--method', default='GET')
    parser.add_argument('--path', default='/')
    parser.add_argument('--requests', type=int, required=True)
    parser.add_argument('--warmup', type=int, default=0)
    parser.add_argument('--timeout', type=float, default=5.0)
    parser.add_argument('--body-bytes', type=int, default=0)
    parser.add_argument('--header', action='append', default=[])
    parser.add_argument('--expect-status', type=int, default=200)
    parser.add_argument('--expect-contains')
    parser.add_argument('--expect-body-bytes', type=int)
    parser.add_argument('--output-json', required=True)
    args = parser.parse_args()

    headers = parse_headers(args.header)
    body = b''
    if args.body_bytes > 0:
        body = b'a' * args.body_bytes
        headers.setdefault('Content-Type', 'application/octet-stream')

    timings_ms = []
    response_bytes = 0
    sent_body_bytes = len(body) * args.requests

    for _ in range(args.warmup):
        status, response_body, _elapsed_ns = run_request(args, body, headers)
        validate_response(args, status, response_body)

    wall_started = time.perf_counter_ns()
    for _ in range(args.requests):
        status, response_body, elapsed_ns = run_request(args, body, headers)
        validate_response(args, status, response_body)
        timings_ms.append(elapsed_ns / 1_000_000.0)
        response_bytes += len(response_body)
    wall_ended = time.perf_counter_ns()

    total_wall_seconds = (wall_ended - wall_started) / 1_000_000_000.0
    sorted_timings = sorted(timings_ms)
    result = {
        'label': args.label,
        'host': args.host,
        'port': args.port,
        'method': args.method,
        'path': args.path,
        'requests': args.requests,
        'warmup': args.warmup,
        'timeout_seconds': args.timeout,
        'body_bytes': len(body),
        'sent_body_bytes_total': sent_body_bytes,
        'response_bytes_total': response_bytes,
        'wall_seconds': total_wall_seconds,
        'requests_per_second': (args.requests / total_wall_seconds) if total_wall_seconds > 0 else 0.0,
        'response_bytes_per_second': (response_bytes / total_wall_seconds) if total_wall_seconds > 0 else 0.0,
        'latency_ms': {
            'min': float(sorted_timings[0]),
            'mean': float(statistics.fmean(sorted_timings)),
            'p50': percentile(sorted_timings, 0.50),
            'p95': percentile(sorted_timings, 0.95),
            'max': float(sorted_timings[-1]),
        },
    }

    with open(args.output_json, 'w', encoding='utf-8') as fh:
        json.dump(result, fh, indent=2, sort_keys=True)
        fh.write('\n')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'benchmark failed: {exc}', file=sys.stderr)
        sys.exit(1)
