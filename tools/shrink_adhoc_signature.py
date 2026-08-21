#!/usr/bin/env python3
"""Shrink a trailing ad-hoc signature reservation to its declared blob size."""

from __future__ import annotations

import argparse
import os
import struct
from pathlib import Path

try:
    from .reserve_codesign_space import align, refresh_adhoc_code_directories
except ImportError:  # Direct execution from tools/shrink_adhoc_signature.py.
    from reserve_codesign_space import align, refresh_adhoc_code_directories

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D


def shrink(path: Path) -> None:
    data = bytearray(path.read_bytes())
    ncmds = struct.unpack_from("<I", data, 16)[0]
    command_offset = 32
    signature_command = None
    linkedit_command = None
    for _ in range(ncmds):
        command, command_size = struct.unpack_from("<II", data, command_offset)
        if command == LC_CODE_SIGNATURE:
            signature_command = command_offset
        elif command == LC_SEGMENT_64:
            name = bytes(data[command_offset + 8:command_offset + 24]).split(b"\0", 1)[0]
            if name == b"__LINKEDIT":
                linkedit_command = command_offset
        command_offset += command_size
    if signature_command is None or linkedit_command is None:
        raise ValueError("Mach-O is missing LC_CODE_SIGNATURE or __LINKEDIT")

    signature_offset, signature_size = struct.unpack_from("<II", data, signature_command + 8)
    if signature_offset + signature_size != len(data):
        raise ValueError("code signature is not the final file object")
    declared_size = struct.unpack_from(">I", data, signature_offset + 4)[0]
    new_signature_size = align(declared_size, 16)
    if new_signature_size > signature_size:
        raise ValueError("declared signature exceeds its reservation")
    new_length = signature_offset + new_signature_size
    del data[new_length:]
    struct.pack_into("<I", data, signature_command + 12, new_signature_size)
    linkedit_file_offset = struct.unpack_from("<Q", data, linkedit_command + 40)[0]
    linkedit_file_size = new_length - linkedit_file_offset
    struct.pack_into("<Q", data, linkedit_command + 32, align(linkedit_file_size, 0x4000))
    struct.pack_into("<Q", data, linkedit_command + 48, linkedit_file_size)
    refresh_adhoc_code_directories(data, signature_offset)
    temporary = path.with_name(path.name + ".shrink")
    temporary.write_bytes(data)
    os.chmod(temporary, path.stat().st_mode)
    os.replace(temporary, path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    shrink(args.path)


if __name__ == "__main__":
    main()
