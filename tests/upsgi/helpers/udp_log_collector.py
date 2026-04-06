#!/usr/bin/env python3
import argparse
import select
import socket
import time

parser = argparse.ArgumentParser(description="Collect UDP log packets for upsgi smoke tests.")
parser.add_argument("--host", default="127.0.0.1")
parser.add_argument("--port", type=int, required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--timeout", type=float, default=5.0)
parser.add_argument("--max-packets", type=int, default=64)
args = parser.parse_args()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((args.host, args.port))
sock.setblocking(False)

deadline = time.time() + args.timeout
packets = []

while time.time() < deadline and len(packets) < args.max_packets:
    remaining = max(0.0, deadline - time.time())
    readable, _, _ = select.select([sock], [], [], remaining)
    if not readable:
        break
    data, addr = sock.recvfrom(65535)
    try:
        decoded = data.decode("utf-8", errors="replace")
    except Exception:
        decoded = repr(data)
    packets.append(f"from={addr[0]}:{addr[1]}\n{decoded}\n")

with open(args.output, "w", encoding="utf-8") as fh:
    for packet in packets:
        fh.write(packet)
        if not packet.endswith("\n"):
            fh.write("\n")
        fh.write("---\n")
