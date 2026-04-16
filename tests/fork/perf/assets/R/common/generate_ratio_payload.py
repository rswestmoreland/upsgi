#!/usr/bin/env python3
import argparse
import gzip
import json
from pathlib import Path


def build_text(length: int, profile: str) -> str:
    if profile == '4x':
        import random
        rng = random.Random(1)
        chars = 'ABCD'
        return ''.join(rng.choice(chars) for _ in range(length))
    if profile == '10x':
        import random
        rng = random.Random(1)
        words = ['alpha', 'bravo', 'charlie', 'delta', 'echo', 'foxtrot', 'golf', 'hotel']
        parts = []
        current = 0
        while current < length:
            word = rng.choice(words)
            parts.append(word)
            current += len(word) + 1
        return ' '.join(parts)[:length]
    raise ValueError(f'unsupported profile: {profile}')


def build_json_payload(length: int, profile: str, seq: int) -> bytes:
    body = {'source': 'fixture', 'seq': seq, 'profile': profile, 'payload': build_text(length, profile)}
    return json.dumps(body, separators=(',', ':')).encode('utf-8')


def build_for_target(target_compressed: int, profile: str, seq: int):
    lo = 1024
    hi = max(8192, target_compressed * 64)
    best = None
    for _ in range(40):
        mid = (lo + hi) // 2
        payload = build_json_payload(mid, profile, seq)
        compressed = gzip.compress(payload)
        csize = len(compressed)
        candidate = (abs(csize - target_compressed), payload, compressed)
        if best is None or candidate[0] < best[0]:
            best = candidate
        if csize < target_compressed:
            lo = mid + 1
        elif csize > target_compressed:
            hi = max(lo, mid - 1)
        else:
            break
    _, payload, compressed = best
    return payload, compressed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--meta-out', required=True)
    ap.add_argument('--compressed-target', type=int, required=True)
    ap.add_argument('--ratio-profile', choices=['4x', '10x'], required=True)
    ap.add_argument('--seq', type=int, default=1)
    args = ap.parse_args()
    out = Path(args.out)
    meta_out = Path(args.meta_out)
    out.parent.mkdir(parents=True, exist_ok=True)
    meta_out.parent.mkdir(parents=True, exist_ok=True)
    payload, compressed = build_for_target(args.compressed_target, args.ratio_profile, args.seq)
    out.write_bytes(compressed)
    meta = {
        'compressed_target_bytes': args.compressed_target,
        'compressed_actual_bytes': len(compressed),
        'decoded_actual_bytes': len(payload),
        'ratio_profile': args.ratio_profile,
        'inflate_ratio_actual': round(len(payload) / len(compressed), 4),
        'seq': args.seq,
    }
    meta_out.write_text(json.dumps(meta, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(meta, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
