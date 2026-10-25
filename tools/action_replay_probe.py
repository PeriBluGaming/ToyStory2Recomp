#!/usr/bin/env python3
"""Developer-only temporary Action Replay RAM-write experiment.

The runtime's TCP write_ram command writes one byte. This helper applies each
16-bit code as two checked little-endian byte writes.

  action_replay_probe.py apply
  action_replay_probe.py read
  action_replay_probe.py repeat [--interval SECONDS]

Repeat mode is intentionally a foreground process. Stop it with Ctrl-C or
terminate the process. It never restores old values; stopping only prevents
future writes. Any connection or write failure stops the process.
"""
import argparse
import json
import socket
import sys
import time

HOST, DEFAULT_PORT = "127.0.0.1", 4370
CODE_VALUES = (
    (0x800B21C8, 0x0009),
    (0x800B221C, 0x002A),
)


def rpc(port, command, **params):
    params["cmd"] = command
    with socket.create_connection((HOST, port), timeout=5) as sock:
        sock.sendall((json.dumps(params) + "\n").encode("ascii"))
        response = b""
        while not response.endswith(b"\n"):
            chunk = sock.recv(4096)
            if not chunk:
                raise RuntimeError(f"{command}: runtime closed the connection")
            response += chunk
    result = json.loads(response)
    if not result.get("ok"):
        raise RuntimeError(f"{command} failed: {result}")
    return result


def read_half(port, address):
    result = rpc(port, "read_ram", addr=f"0x{address:08X}", len=2)
    raw = bytes.fromhex(result["hex"])
    if len(raw) != 2:
        raise RuntimeError(
            f"read_ram returned {len(raw)} bytes for 0x{address:08X}"
        )
    return int.from_bytes(raw, "little")


def write_half(port, address, value):
    raw = value.to_bytes(2, "little")
    for offset, byte in enumerate(raw):
        rpc(
            port,
            "write_ram",
            addr=f"0x{address + offset:08X}",
            val=f"0x{byte:02X}",
        )


def selected_codes(selection):
    if selection == "first":
        return CODE_VALUES[:1]
    if selection == "second":
        return CODE_VALUES[1:]
    return CODE_VALUES


def apply_codes(port, selection, announce=True):
    codes = selected_codes(selection)
    for address, value in codes:
        write_half(port, address, value)
    if announce:
        print(f"applied {selection} code{'s' if selection == 'both' else ''}:")
        for address, value in codes:
            print(f"  0x{address:08X} <- 0x{value:04X}")


def show_values(port):
    for address, expected in CODE_VALUES:
        actual = read_half(port, address)
        print(
            f"0x{address:08X}: 0x{actual:04X} "
            f"(experiment value 0x{expected:04X})"
        )


def cmd_apply(args):
    apply_codes(args.port, args.code)


def cmd_read(args):
    show_values(args.port)


def cmd_repeat(args):
    print(
        f"repeating {args.code} code{'s' if args.code == 'both' else ''} "
        f"every {args.interval:g}s; "
        "press Ctrl-C to stop (RAM is not restored)"
    )
    try:
        while True:
            apply_codes(args.port, args.code, announce=False)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("stopped; no values were restored")
    except (ConnectionError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"experiment stopped after runtime error: {exc}", file=sys.stderr)
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    subparsers = parser.add_subparsers(dest="command", required=True)

    apply = subparsers.add_parser("apply", help="apply both codes once")
    apply.add_argument("--code", choices=("both", "first", "second"), default="both")
    apply.set_defaults(function=cmd_apply)

    read = subparsers.add_parser("read", help="read both target values")
    read.set_defaults(function=cmd_read)

    repeat = subparsers.add_parser(
        "repeat", help="repeat both writes until Ctrl-C or a runtime error"
    )
    repeat.add_argument("--code", choices=("both", "first", "second"), default="both")
    repeat.add_argument("--interval", type=float, default=0.25)
    repeat.set_defaults(function=cmd_repeat)

    args = parser.parse_args()
    if args.command == "repeat" and args.interval <= 0:
        parser.error("--interval must be greater than zero")
    try:
        args.function(args)
    except (ConnectionError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"experiment failed: {exc}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
