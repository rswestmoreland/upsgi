#!/usr/bin/env python3
import argparse
import http.client
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    ap.add_argument("--path", default="/push")
    ap.add_argument("--payload", required=True)
    ap.add_argument("--content-type", default="application/json")
    ap.add_argument("--expect-status", type=int, default=202)
    args = ap.parse_args()

    payload = Path(args.payload).read_bytes()
    headers = {"Content-Type": args.content_type, "Content-Length": str(len(payload))}
    conn = http.client.HTTPConnection(args.host, args.port, timeout=30)
    conn.request("POST", args.path, body=payload, headers=headers)
    resp = conn.getresponse()
    body = resp.read().decode("utf-8", "replace")
    print(f"status={resp.status}")
    print(body)
    conn.close()
    raise SystemExit(0 if resp.status == args.expect_status else 1)


if __name__ == "__main__":
    main()
