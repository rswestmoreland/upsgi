#!/usr/bin/env python3
import argparse
import gzip
import json
import os
from pathlib import Path


def build_valid_payload(target_bytes: int, seq: int = 1):
    base = {"source": "fixture", "seq": seq, "payload": ""}
    encoded = json.dumps(base, separators=(",", ":")).encode("utf-8")
    overhead = len(encoded)
    body_len = max(0, target_bytes - overhead)
    base["payload"] = "X" * body_len
    return json.dumps(base, separators=(",", ":")).encode("utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, required=True)
    ap.add_argument("--seq", type=int, default=1)
    ap.add_argument("--compress", action="store_true")
    ap.add_argument("--invalid-json", action="store_true")
    ap.add_argument("--broken-gzip", action="store_true")
    args = ap.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    if args.broken_gzip:
        out.write_bytes(b"not-a-valid-gzip-stream\x00\x01\x02")
        return

    if args.invalid_json:
        payload = b'{"source":"fixture","seq":1,"payload":'
    else:
        payload = build_valid_payload(args.size, args.seq)

    if args.compress:
        out.write_bytes(gzip.compress(payload))
    else:
        out.write_bytes(payload)


if __name__ == "__main__":
    main()
