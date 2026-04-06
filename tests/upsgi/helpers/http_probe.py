#!/usr/bin/env python3
import argparse
import http.client
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="Minimal HTTP probe for upsgi smoke tests")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--method", required=True)
    parser.add_argument("--path", required=True)
    parser.add_argument("--expect-status", type=int)
    parser.add_argument("--expect-body")
    parser.add_argument("--expect-body-contains")
    parser.add_argument("--reject-body-contains")
    parser.add_argument("--expect-empty-body", action="store_true")
    parser.add_argument("--expect-header")
    parser.add_argument("--print-summary", action="store_true")
    args = parser.parse_args()

    conn = http.client.HTTPConnection(args.host, args.port, timeout=5)
    conn.request(args.method, args.path, headers={"Host": f"{args.host}:{args.port}"})
    res = conn.getresponse()
    body = res.read()
    try:
        body_text = body.decode("utf-8")
    except UnicodeDecodeError:
        body_text = body.decode("utf-8", errors="replace")
    headers = {k.lower(): v for k, v in res.getheaders()}

    if args.print_summary:
        sys.stdout.write(f"status={res.status}\n")
        for key in sorted(headers):
            sys.stdout.write(f"header.{key}={headers[key]}\n")
        sys.stdout.write(body_text)

    if args.expect_status is not None and res.status != args.expect_status:
        sys.stderr.write(f"expected status {args.expect_status}, got {res.status}\n")
        return 1
    if args.expect_body is not None and body_text != args.expect_body:
        sys.stderr.write("body mismatch\n")
        return 1
    if args.expect_body_contains is not None and args.expect_body_contains not in body_text:
        sys.stderr.write("expected body substring missing\n")
        return 1
    if args.reject_body_contains is not None and args.reject_body_contains in body_text:
        sys.stderr.write("forbidden body substring present\n")
        return 1
    if args.expect_empty_body and body:
        sys.stderr.write("expected empty body\n")
        return 1
    if args.expect_header is not None:
        if "=" not in args.expect_header:
            sys.stderr.write("--expect-header must use name=value form\n")
            return 1
        name, value = args.expect_header.split("=", 1)
        actual = headers.get(name.lower())
        if actual != value:
            sys.stderr.write(f"expected header {name}={value}, got {actual}\n")
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
