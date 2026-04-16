#!/usr/bin/env python3
import argparse
import hashlib
import http.client
import time
import urllib.parse


def expected_stream(chunks: int, chunk_size: int, label: str):
    for idx in range(chunks):
        prefix = f"chunk={idx:06d};label={label};".encode()
        body = bytearray(prefix)
        token = f"[{idx:06d}:{label}]".encode()
        while len(body) < chunk_size:
            body.extend(token)
        if len(body) > chunk_size:
            del body[chunk_size:]
        yield bytes(body)


def sha256_iter(parts):
    h = hashlib.sha256()
    total = 0
    for part in parts:
        h.update(part)
        total += len(part)
    return h.hexdigest(), total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    ap.add_argument("--path", default="/stream")
    ap.add_argument("--chunks", type=int, default=32)
    ap.add_argument("--chunk-size", type=int, default=65536)
    ap.add_argument("--label", default="alpha")
    ap.add_argument("--read-size", type=int, default=16384)
    ap.add_argument("--delay-ms", type=int, default=0)
    args = ap.parse_args()

    query = urllib.parse.urlencode({
        "chunks": args.chunks,
        "chunk_size": args.chunk_size,
        "label": args.label,
    })
    target = f"{args.path}?{query}"

    expected_hash, expected_total = sha256_iter(
        expected_stream(args.chunks, args.chunk_size, args.label)
    )

    conn = http.client.HTTPConnection(args.host, args.port, timeout=30)
    conn.request("GET", target)
    resp = conn.getresponse()

    actual = hashlib.sha256()
    total = 0
    while True:
        block = resp.read(args.read_size)
        if not block:
            break
        actual.update(block)
        total += len(block)
        if args.delay_ms > 0:
            time.sleep(args.delay_ms / 1000.0)

    conn.close()

    actual_hash = actual.hexdigest()
    print(f"status={resp.status}")
    print(f"expected_total={expected_total}")
    print(f"actual_total={total}")
    print(f"expected_sha256={expected_hash}")
    print(f"actual_sha256={actual_hash}")

    ok = resp.status == 200 and total == expected_total and actual_hash == expected_hash
    print(f"match={'yes' if ok else 'no'}")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
