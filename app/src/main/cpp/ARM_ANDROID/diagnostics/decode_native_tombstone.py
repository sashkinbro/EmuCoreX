"""Read archived Android native tombstones without third-party Python packages.

Schema: https://android.googlesource.com/platform/system/core/+/refs/heads/main/debuggerd/proto/tombstone.proto
Unknown protobuf fields are skipped; the original .pb retains all system data.
"""
import argparse
from pathlib import Path


def varint(data, offset):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError("Truncated protobuf varint")
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("Oversized protobuf varint")


def fields(data):
    result, offset = {}, 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        number, wire = tag >> 3, tag & 7
        if number == 0:
            raise ValueError("Invalid protobuf field zero")
        if wire == 0:
            value, offset = varint(data, offset)
        else:
            if wire == 2:
                size, offset = varint(data, offset)
            elif wire in (1, 5):
                size = 8 if wire == 1 else 4
            else:
                raise ValueError(f"Unsupported protobuf wire type {wire}")
            if size > len(data) - offset:
                raise ValueError("Truncated protobuf field")
            value = data[offset:offset + size]
            offset += size
        result.setdefault(number, []).append(value)
    return result


def first(message, key, default=0):
    return message.get(key, [default])[0]


def text(message, key):
    value = first(message, key, b"")
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else ""


def decode(data, all_threads=False, mappings=False):
    root = fields(data)
    signal = fields(first(root, 10, b""))
    lines = [f"timestamp: {text(root, 4)}", f"build: {text(root, 2)}",
             f"pid: {first(root, 5)} tid: {first(root, 6)}",
             f"signal: {text(signal, 2)} ({first(signal, 1)}) code: {text(signal, 4)} ({first(signal, 3)})"]
    if first(signal, 8):
        lines.append(f"fault addr: 0x{first(signal, 9):016x}")
    if text(root, 14):
        lines.append(f"Abort message: {text(root, 14)}")
    for cause in root.get(15, []):
        lines.append(f"Cause: {text(fields(cause), 1)}")
    for entry_data in root.get(16, []):
        entry = fields(entry_data)
        thread = fields(first(entry, 2, b""))
        tid = first(thread, 1, first(entry, 1))
        if not all_threads and tid != first(root, 6):
            continue
        lines.append(f"\nThread {tid}: {text(thread, 2)}")
        registers = [fields(item) for item in thread.get(3, [])]
        for offset in range(0, len(registers), 4):
            lines.append("  " + "  ".join(f"{text(reg, 1)} {first(reg, 2):016x}" for reg in registers[offset:offset + 4]))
        lines.append("backtrace:")
        for index, frame_data in enumerate(thread.get(4, [])):
            frame = fields(frame_data)
            function = text(frame, 4)
            suffix = f" ({function}+{first(frame, 5)})" if function else ""
            build = f" (BuildId: {text(frame, 8)})" if text(frame, 8) else ""
            lines.append(f"  #{index:02d} pc {first(frame, 1):016x} {text(frame, 6)}{suffix}{build}")
        lines.extend(f"  note: {item.decode('utf-8', errors='replace')}" for item in thread.get(7, []))
    if mappings:
        lines.append("\nmemory maps:")
        for item in root.get(17, []):
            mapping = fields(item)
            flags = "".join(char if first(mapping, key) else "-" for key, char in ((4, "r"), (5, "w"), (6, "x")))
            lines.append(f"{first(mapping, 1):016x}-{first(mapping, 2):016x} {flags} {first(mapping, 3):08x} {text(mapping, 7)} {text(mapping, 8)}")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tombstone", type=Path)
    parser.add_argument("--all-threads", action="store_true")
    parser.add_argument("--maps", action="store_true")
    args = parser.parse_args()
    if args.tombstone.stat().st_size > 4 * 1024 * 1024:
        parser.error("Tombstone exceeds the archive's 4 MiB limit")
    try:
        print(decode(args.tombstone.read_bytes(), args.all_threads, args.maps), end="")
    except (ValueError, TypeError) as error:
        parser.error(str(error))
