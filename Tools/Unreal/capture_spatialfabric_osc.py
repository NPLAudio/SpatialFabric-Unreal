"""Capture local showcase OSC endpoints; run with ordinary Python, not Unreal Python.
Usage: python Plugins/SpatialFabric/Tools/Unreal/capture_spatialfabric_osc.py --output Saved/SpatialFabricDemo --duration 90
Does not reuse occupied UDP ports or send network messages. Stop early by creating stop_capture.flag.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re
import select
import socket
import struct
import time


def decode(data):
    if data.startswith(b"#bundle\0"):
        offset = 16
        while offset < len(data):
            size = struct.unpack_from(">i", data, offset)[0]
            offset += 4
            if size < 0 or offset + size > len(data): raise ValueError("Invalid bundle size")
            yield from decode(data[offset:offset + size])
            offset += size
        return
    def string(offset):
        end = data.index(0, offset)
        return data[offset:end].decode("utf-8"), (end + 4) & ~3
    address, offset = string(0)
    tags, offset = string(offset)
    if not address.startswith("/") or not tags.startswith(","): raise ValueError("Invalid OSC header")
    values = []
    for tag in tags[1:]:
        if tag in "ifd":
            fmt = {"i": ">i", "f": ">f", "d": ">d"}[tag]
            value = struct.unpack_from(fmt, data, offset)[0]
            offset += struct.calcsize(fmt)
        elif tag == "s": value, offset = string(offset)
        elif tag in "TFN": value = {"T": True, "F": False, "N": None}[tag]
        else: raise ValueError("Unsupported OSC type " + tag)
        if isinstance(value, float) and not math.isfinite(value): raise ValueError("Non-finite OSC float")
        values.append(value)
    yield {"address": address, "tags": tags, "args": values}


def capture(output, duration):
    output.mkdir(parents=True, exist_ok=True)
    stop = output / "stop_capture.flag"
    stop.unlink(missing_ok=True)
    (output / "capture_ready.json").unlink(missing_ok=True)
    (output / "udp_summary.json").unlink(missing_ok=True)
    sockets = []
    counts = Counter(); addresses = defaultdict(Counter); states = defaultdict(list)
    samples = defaultdict(set); failures = []; latest_xyz = {}; spherical_errors = []
    try:
        for port in (50018, 9002, 9001):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("127.0.0.1", port))
            sockets.append(sock)
        (output / "capture_ready.json").write_text(json.dumps({"ports": [s.getsockname()[1] for s in sockets]}))
        start = time.monotonic()
        with (output / "udp_capture.jsonl").open("w", encoding="utf-8") as log:
            while time.monotonic() - start < duration and not stop.exists():
                ready, _, _ = select.select(sockets, [], [], 0.1)
                for sock in ready:
                    packet, peer = sock.recvfrom(65535)
                    port = sock.getsockname()[1]
                    try:
                        for message in decode(packet):
                            address, args = message["address"], message["args"]
                            counts[port] += 1; addresses[port][address] += 1
                            message.update(port=port, seconds=round(time.monotonic() - start, 5))
                            log.write(json.dumps(message) + "\n")
                            if address.endswith("/state"):
                                if not states[address] or states[address][-1] != args: states[address].append(args)
                            if address.endswith("/xyz"):
                                samples[address].add(tuple(round(float(v), 4) for v in args))
                            match = re.fullmatch(r"/adm/obj/(\d+)/xyz", address)
                            if match: latest_xyz[match[1]] = args
                            match = re.fullmatch(r"/adm/obj/(\d+)/aed", address)
                            if match and match[1] in latest_xyz:
                                x, y, z = latest_xyz[match[1]]
                                distance = math.sqrt(x * x + y * y + z * z)
                                if abs(distance - args[2]) > 0.002 or distance > 1.002:
                                    spherical_errors.append({"xyz": [x, y, z], "aed": args})
                    except Exception as error:
                        failures.append(str(error))
        result = {"seconds": round(time.monotonic() - start, 2), "counts": dict(counts),
            "addresses": {str(p): dict(a) for p, a in addresses.items()},
            "distinct_xyz": {a: len(v) for a, v in samples.items()}, "states": dict(states),
            "decode_failures": failures[:10], "spherical_errors": spherical_errors[:10]}
        (output / "udp_summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items() if k != "addresses"}, indent=2))
    finally:
        for sock in sockets: sock.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=90)
    args = parser.parse_args()
    capture(args.output, args.duration)
