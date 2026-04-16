#!/usr/bin/env python3
import argparse
import socket


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    ap.add_argument("--case", choices=["wrong_method", "wrong_path", "header_abuse", "disconnect"], required=True)
    args = ap.parse_args()

    if args.case == "wrong_method":
        request = b"GET /push HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    elif args.case == "wrong_path":
        request = b"POST /wrong HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
    elif args.case == "header_abuse":
        big = b"A" * 8192
        request = b"POST /push HTTP/1.1\r\nHost: localhost\r\nX-Abuse: " + big + b"\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
    else:
        request = b"POST /push HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000\r\nConnection: close\r\n\r\npartial"

    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.sendall(request)
        if args.case != "disconnect":
            try:
                resp = sock.recv(65536)
                print(resp.decode("utf-8", "replace"))
            except OSError:
                pass


if __name__ == "__main__":
    main()
