#!/usr/bin/env python3
import argparse
import http.client
import time
import urllib.parse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    ap.add_argument("--chunks", type=int, default=64)
    ap.add_argument("--chunk-size", type=int, default=65536)
    ap.add_argument("--label", default="slow")
    ap.add_argument("--read-size", type=int, default=4096)
    ap.add_argument("--delay-ms", type=int, default=50)
    args = ap.parse_args()

    query = urllib.parse.urlencode({
        "chunks": args.chunks,
        "chunk_size": args.chunk_size,
        "label": args.label,
    })
    target = f"/stream?{query}"
    conn = http.client.HTTPConnection(args.host, args.port, timeout=30)
    conn.request("GET", target)
    resp = conn.getresponse()

    total = 0
    while True:
        block = resp.read(args.read_size)
        if not block:
            break
        total += len(block)
        time.sleep(args.delay_ms / 1000.0)

    print(f"status={resp.status}")
    print(f"total={total}")
    conn.close()

if __name__ == "__main__":
    main()
