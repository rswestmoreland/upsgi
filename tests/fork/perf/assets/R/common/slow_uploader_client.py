#!/usr/bin/env python3
import argparse
import socket
import time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    ap.add_argument("--path", default="/push")
    ap.add_argument("--payload", required=True)
    ap.add_argument("--content-type", default="application/json")
    ap.add_argument("--chunk-size", type=int, default=4096)
    ap.add_argument("--delay-ms", type=int, default=50)
    args = ap.parse_args()

    payload = Path(args.payload).read_bytes()
    req = (
        f"POST {args.path} HTTP/1.1\r\n"
        f"Host: {args.host}:{args.port}\r\n"
        f"Content-Type: {args.content_type}\r\n"
        f"Content-Length: {len(payload)}\r\n"
        f"Connection: close\r\n\r\n"
    ).encode("ascii")
    with socket.create_connection((args.host, args.port), timeout=30) as sock:
        sock.sendall(req)
        for off in range(0, len(payload), args.chunk_size):
            sock.sendall(payload[off:off + args.chunk_size])
            time.sleep(args.delay_ms / 1000.0)
        sock.shutdown(socket.SHUT_WR)
        resp = sock.recv(65536)
    print(resp.decode("utf-8", "replace"))


if __name__ == "__main__":
    main()
