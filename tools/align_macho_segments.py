#!/usr/bin/env python3
"""Extend Mach-O segment ranges across their existing alignment padding."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02


def align_segments(path: Path) -> None:
    data = bytearray(path.read_bytes())
    ncmds = struct.unpack_from("<I", data, 16)[0]
    command_offset = 32
    segments = []
    signature = None
    for _ in range(ncmds):
        command, command_size = struct.unpack_from("<II", data, command_offset)
        if command == LC_SEGMENT_64:
            name = bytes(data[command_offset + 8:command_offset + 24]).split(b"\0", 1)[0].decode()
            values = struct.unpack_from("<QQQQ", data, command_offset + 24)
            segments.append((command_offset, name, *values))
        elif command == LC_CODE_SIGNATURE:
            signature = struct.unpack_from("<II", data, command_offset + 8)
        command_offset += command_size
    if signature is None or len(segments) < 2:
        raise ValueError("Mach-O is missing segments or LC_CODE_SIGNATURE")

    text_limit = None
    for current, following in zip(segments, segments[1:]):
        offset, name, vmaddr, _vmsize, fileoff, filesize = current
        _next_offset, _next_name, next_vmaddr, _next_vmsize, next_fileoff, _next_filesize = following
        if name == "__LINKEDIT":
            continue
        new_vmsize = next_vmaddr - vmaddr
        new_filesize = next_fileoff - fileoff
        if new_vmsize <= 0 or new_filesize <= 0:
            raise ValueError(f"invalid segment ordering around {name}")
        if any(data[fileoff + filesize:fileoff + new_filesize]):
            raise ValueError(f"non-zero bytes in {name} alignment padding")
        struct.pack_into("<Q", data, offset + 32, new_vmsize)
        struct.pack_into("<Q", data, offset + 48, new_filesize)
        if name == "__TEXT":
            text_limit = new_vmsize
    if text_limit is None:
        raise ValueError("Mach-O has no __TEXT segment")

    signature_offset, signature_size = signature
    magic, declared_length, count = struct.unpack_from(">III", data, signature_offset)
    if magic != CSMAGIC_EMBEDDED_SIGNATURE or declared_length > signature_size:
        raise ValueError("malformed embedded signature")
    for index in range(count):
        slot_type, relative_offset = struct.unpack_from(">II", data, signature_offset + 12 + index * 8)
        blob_offset = signature_offset + relative_offset
        blob_magic, blob_length = struct.unpack_from(">II", data, blob_offset)
        if slot_type == 0 and blob_magic == CSMAGIC_CODEDIRECTORY:
            version = struct.unpack_from(">I", data, blob_offset + 8)[0]
            if version >= 0x20400 and blob_length >= 88:
                struct.pack_into(">Q", data, blob_offset + 64, 0)
                struct.pack_into(">Q", data, blob_offset + 72, text_limit)
    path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    align_segments(args.path)


if __name__ == "__main__":
    main()
