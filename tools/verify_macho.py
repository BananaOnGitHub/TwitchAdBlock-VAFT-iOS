#!/usr/bin/env python3
"""Validate the release dylib's architecture, identity, dependencies, and signature space."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from .macho import MH_DYLIB, MachOError, code_signature, dylib_id, header, loaded_dylibs
except ImportError:  # Direct execution from tools/verify_macho.py.
    from macho import MH_DYLIB, MachOError, code_signature, dylib_id, header, loaded_dylibs


def verify(path: Path) -> None:
    data = path.read_bytes()
    info = header(data)
    if info.file_type != MH_DYLIB:
        raise MachOError("file is not a Mach-O dylib")
    if dylib_id(data) != "@rpath/TwitchAdBlock.dylib":
        raise MachOError("unexpected LC_ID_DYLIB")
    dependencies = set(loaded_dylibs(data))
    required = {"/usr/lib/libobjc.A.dylib", "/usr/lib/libSystem.B.dylib"}
    if not required.issubset(dependencies):
        raise MachOError("required system dependencies are missing")
    signature = code_signature(data)
    if not signature:
        raise MachOError("LC_CODE_SIGNATURE is missing")
    offset, reserved_size = signature
    if reserved_size < 65536:
        raise MachOError(f"signature reservation is too small: {reserved_size} bytes")
    if offset + reserved_size != len(data):
        raise MachOError("signature reservation does not end at EOF")
    if offset + 12 > len(data):
        raise MachOError("signature blob is truncated")
    magic, blob_size, _count = struct.unpack_from(">III", data, offset)
    if magic != 0xFADE0CC0 or blob_size > reserved_size:
        raise MachOError("invalid embedded signature superblob")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dylib", type=Path)
    args = parser.parse_args()
    try:
        verify(args.dylib)
    except (OSError, MachOError) as error:
        parser.error(str(error))
    print(f"valid arm64 dylib with 64 KiB signing reservation: {args.dylib}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
