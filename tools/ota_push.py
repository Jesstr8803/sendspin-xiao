#!/usr/bin/env python3
"""Push a firmware binary to a sendspin-xiao device over WiFi."""

import argparse
import os
import socket
import sys
import time
import urllib.error
import urllib.request


def discover_via_mdns(timeout: float = 3.0) -> list[tuple[str, str, int]]:
    """Best-effort mDNS discovery of _sendspin-ota._tcp services.

    Returns a list of (instance_name, host, port). Requires `zeroconf` package
    if used; otherwise returns []. Pass --host explicitly to skip discovery.
    """
    try:
        from zeroconf import ServiceBrowser, Zeroconf
    except ImportError:
        return []

    found: list[tuple[str, str, int]] = []

    class Listener:
        def add_service(self, zc, type_, name):
            info = zc.get_service_info(type_, name, timeout=int(timeout * 1000))
            if info:
                addrs = info.parsed_addresses()
                if addrs:
                    found.append((info.name, addrs[0], info.port))

        def remove_service(self, zc, type_, name):
            pass

        def update_service(self, zc, type_, name):
            pass

    zc = Zeroconf()
    ServiceBrowser(zc, "_sendspin-ota._tcp.local.", Listener())
    time.sleep(timeout)
    zc.close()
    return found


def push(host: str, port: int, token: str, bin_path: str) -> int:
    if not os.path.isfile(bin_path):
        print(f"error: firmware file not found: {bin_path}", file=sys.stderr)
        return 2

    size = os.path.getsize(bin_path)
    url = f"http://{host}:{port}/ota"
    print(f"-> POST {url}  ({size:,} bytes)")

    with open(bin_path, "rb") as f:
        data = f.read()

    req = urllib.request.Request(url, data=data, method="POST")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Content-Length", str(size))

    socket.setdefaulttimeout(120)
    try:
        start = time.time()
        with urllib.request.urlopen(req) as resp:
            body = resp.read().decode(errors="replace")
            elapsed = time.time() - start
            kbps = (size / 1024.0) / elapsed if elapsed > 0 else 0
            print(f"<- {resp.status} {resp.reason}  ({elapsed:.1f}s, {kbps:.0f} kB/s)")
            print(body.strip())
            print("Device is rebooting into the new firmware.")
            return 0
    except urllib.error.HTTPError as e:
        print(f"<- {e.code} {e.reason}", file=sys.stderr)
        try:
            print(e.read().decode(errors="replace"), file=sys.stderr)
        except Exception:
            pass
        return 1
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


def main() -> int:
    p = argparse.ArgumentParser(description="Push firmware OTA to sendspin-xiao")
    p.add_argument("host", nargs="?", help="device IP or hostname (omit to mDNS-discover)")
    p.add_argument("--port", type=int, default=8080, help="OTA HTTP port (default 8080)")
    p.add_argument("--token", default="", help="bearer token if device has one configured")
    p.add_argument("--bin", default="build/sendspin-xiao.bin",
                   help="firmware binary (default build/sendspin-xiao.bin)")
    p.add_argument("--discover-only", action="store_true",
                   help="just print discovered devices and exit")
    args = p.parse_args()

    if args.host is None or args.discover_only:
        print("Discovering _sendspin-ota._tcp ...")
        devices = discover_via_mdns()
        if not devices:
            print("No devices found via mDNS. Provide IP explicitly or install `zeroconf`.")
            return 1
        for name, host, port in devices:
            print(f"  {name}  ->  {host}:{port}")
        if args.discover_only:
            return 0
        # If host wasn't given but discovery found exactly one device, use it.
        if args.host is None:
            if len(devices) == 1:
                args.host = devices[0][1]
                args.port = devices[0][2]
                print(f"Auto-selecting {args.host}:{args.port}")
            else:
                print("Multiple devices found; please pass the IP explicitly.")
                return 1

    return push(args.host, args.port, args.token, args.bin)


if __name__ == "__main__":
    raise SystemExit(main())
