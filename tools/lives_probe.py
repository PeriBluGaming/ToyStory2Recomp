#!/usr/bin/env python3
"""Read-only RAM probe for locating the Toy Story 2 lives counter.

Talks to the runtime's TCP debug server (start the game with --debug-port).
Never writes guest memory.

  lives_probe.py snap  NAME            save a 2 MB RAM snapshot to ./lives_snaps/NAME.bin
  lives_probe.py diff  --snaps a b c --expect 3 2 1 [--max-value 99]
                                       list 8/16/32-bit values matching expect[i] in snaps[i]
  lives_probe.py changes OLD NEW       list changed aligned 8/16/32-bit values
  lives_probe.py trace ADDR [--width 1|2|4]
                                       arm a write trace for stores starting at ADDR
  lives_probe.py hits  ADDR [--width 1|2|4]
                                       print matching writes (pc / ra / func / old / new)
  lives_probe.py trace_status          verify configured ranges and ring capacity
  lives_probe.py read  ADDR [LEN]      read live bytes
"""
import argparse
import json
import os
import socket
import struct
import sys

HOST, PORT = "127.0.0.1", 4370
RAM_BASE, RAM_SIZE = 0x80000000, 0x200000
SNAP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lives_snaps")


def rpc(cmd, **kw):
    kw["cmd"] = cmd
    with socket.create_connection((HOST, PORT), timeout=30) as s:
        s.sendall((json.dumps(kw) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(1 << 20)
            if not chunk:
                break
            buf += chunk
    resp = json.loads(buf)
    if not resp.get("ok"):
        sys.exit(f"{cmd} failed: {resp}")
    return resp


def snap_path(name):
    return os.path.join(SNAP_DIR, name + ".bin")


def cmd_snap(a):
    r = rpc("read_ram", addr=hex(RAM_BASE), len=RAM_SIZE)
    data = bytes.fromhex(r["hex"])
    os.makedirs(SNAP_DIR, exist_ok=True)
    with open(snap_path(a.name), "wb") as f:
        f.write(data)
    frame = rpc("frame").get("frame")
    print(f"saved {a.name}: {len(data)} bytes at frame {frame}")


def values(data, width):
    fmt = {1: "B", 2: "<H", 4: "<I"}[width]
    if width == 1:
        return lambda off: data[off]
    return lambda off: struct.unpack_from(fmt, data, off)[0]


def cmd_diff(a):
    if len(a.snaps) != len(a.expect):
        sys.exit("--snaps and --expect must have the same length")
    datas = [open(snap_path(n), "rb").read() for n in a.snaps]
    for width in (1, 2, 4):
        getters = [values(d, width) for d in datas]
        hits = []
        for off in range(0, RAM_SIZE - width + 1, width):
            if all(g(off) == e for g, e in zip(getters, a.expect)):
                hits.append(off)
        print(f"-- {width * 8}-bit matches: {len(hits)}")
        for off in hits[: a.limit]:
            print(f"   0x{RAM_BASE + off:08X}  {'/'.join(str(g(off)) for g in getters)}")
        if len(hits) > a.limit:
            print(f"   ... {len(hits) - a.limit} more (raise --limit)")


def cmd_changes(a):
    old_data = open(snap_path(a.old), "rb").read()
    new_data = open(snap_path(a.new), "rb").read()
    if len(old_data) != RAM_SIZE or len(new_data) != RAM_SIZE:
        sys.exit(f"snapshots must each contain exactly {RAM_SIZE} bytes")

    for width, fmt in ((1, "B"), (2, "<H"), (4, "<I")):
        changes = []
        for off in range(0, RAM_SIZE - width + 1, width):
            old = struct.unpack_from(fmt, old_data, off)[0]
            new = struct.unpack_from(fmt, new_data, off)[0]
            difference = new - old
            if old == new:
                continue
            if a.decreases_only and difference >= 0:
                continue
            if max(old, new) > a.max_value:
                continue
            if abs(difference) < a.min_delta:
                continue
            changes.append((off, old, new, difference))

        print(f"-- {width * 8}-bit changes: {len(changes)}")
        for off, old, new, difference in changes[:a.limit]:
            print(
                f"   0x{RAM_BASE + off:08X}  "
                f"{old} -> {new}  diff={difference:+d}"
            )
        if len(changes) > a.limit:
            print(f"   ... {len(changes) - a.limit} more (raise --limit)")


def phys(addr):
    return int(addr, 0) & 0x1FFFFFFF


def cmd_trace(a):
    lo = phys(a.addr)
    rpc("wtrace_range", lo=hex(lo), hi=hex(lo + a.width))
    rpc("wtrace_clear")
    print(f"write trace armed on phys 0x{lo:08X}..0x{lo + a.width:08X} "
          f"(stores of width {a.width} starting at the address); "
          f"now lose one life, then run: hits {a.addr} --width {a.width}")


def cmd_hits(a):
    lo = phys(a.addr)
    r = rpc("wtrace_dump", addr_lo=hex(lo), addr_hi=hex(lo + a.width),
            count=256, newest=1)
    for e in r["entries"]:
        if e.get("w") == a.width:
            print({k: e.get(k) for k in
                   ("seq", "addr", "w", "old", "new", "pc", "ra", "func", "frame")})
    print(f"{r.get('emitted', 0)} address-matching entries returned; "
          f"width filter={a.width}")


def cmd_trace_status(a):
    ranges = rpc("wtrace_ranges")
    stats = rpc("wtrace_stats")
    print(json.dumps({"ranges": ranges, "stats": stats}, indent=2))


def cmd_read(a):
    print(rpc("read_ram", addr=a.addr, len=a.len)["hex"])


def main():
    global PORT
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=PORT)
    sub = p.add_subparsers(dest="c", required=True)
    s = sub.add_parser("snap"); s.add_argument("name"); s.set_defaults(f=cmd_snap)
    s = sub.add_parser("diff")
    s.add_argument("--snaps", nargs="+", required=True)
    s.add_argument("--expect", nargs="+", type=int, required=True)
    s.add_argument("--limit", type=int, default=60)
    s.set_defaults(f=cmd_diff)
    s = sub.add_parser("changes")
    s.add_argument("old")
    s.add_argument("new")
    s.add_argument("--max-value", type=int, default=255)
    s.add_argument("--min-delta", type=int, default=1)
    s.add_argument("--decreases-only", action="store_true")
    s.add_argument("--limit", type=int, default=100)
    s.set_defaults(f=cmd_changes)
    s = sub.add_parser("trace")
    s.add_argument("addr")
    s.add_argument("--width", type=int, choices=(1, 2, 4), default=2)
    s.set_defaults(f=cmd_trace)
    s = sub.add_parser("hits")
    s.add_argument("addr")
    s.add_argument("--width", type=int, choices=(1, 2, 4), default=2)
    s.set_defaults(f=cmd_hits)
    s = sub.add_parser("trace_status")
    s.set_defaults(f=cmd_trace_status)
    s = sub.add_parser("read"); s.add_argument("addr"); s.add_argument("len", nargs="?", type=int, default=16); s.set_defaults(f=cmd_read)
    a = p.parse_args()
    PORT = a.port
    a.f(a)


if __name__ == "__main__":
    main()
