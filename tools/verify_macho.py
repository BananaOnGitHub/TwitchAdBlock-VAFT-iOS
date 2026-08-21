#!/usr/bin/env python3
"""Validate a release Mach-O's identity, dependencies, segments, and signature."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from .macho import MH_DYLIB, MachOError, code_signature, dylib_id, header, loaded_dylibs
except ImportError:  # Direct execution from tools/verify_macho.py.
    from macho import MH_DYLIB, MachOError, code_signature, dylib_id, header, loaded_dylibs


LC_SEGMENT_64 = 0x19


def _segments(data: bytes) -> list[tuple[str, int, int, int, int]]:
    count = struct.unpack_from("<I", data, 16)[0]
    command_offset = 32
    result = []
    for _ in range(count):
        command, command_size = struct.unpack_from("<II", data, command_offset)
        if command == LC_SEGMENT_64:
            name = bytes(data[command_offset + 8:command_offset + 24]).split(b"\0", 1)[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, command_offset + 24)
            result.append((name, vmaddr, vmsize, fileoff, filesize))
        command_offset += command_size
    return result


def _verify_page_aligned_segments(data: bytes) -> None:
    segments = _segments(data)
    if not segments:
        raise MachOError("LC_SEGMENT_64 commands are missing")
    for name, vmaddr, vmsize, fileoff, _filesize in segments:
        if vmaddr % 0x4000 or fileoff % 0x4000:
            raise MachOError(f"{name} does not start on a 16 KiB boundary")
        if vmsize % 0x4000:
            raise MachOError(f"{name} virtual size is not 16 KiB aligned")
    for current, following in zip(segments, segments[1:]):
        name, vmaddr, vmsize, fileoff, filesize = current
        _next_name, next_vmaddr, _next_vmsize, next_fileoff, _next_filesize = following
        if name != "__LINKEDIT" and (vmaddr + vmsize != next_vmaddr or fileoff + filesize != next_fileoff):
            raise MachOError(f"{name} does not fill its aligned segment range")


def verify(path: Path, expected_identity: str = "@rpath/TwitchAdBlock.dylib",
           minimum_signature_size: int = 65536, page_aligned_segments: bool = False) -> None:
    data = path.read_bytes()
    info = header(data)
    if info.file_type != MH_DYLIB:
        raise MachOError("file is not a Mach-O dylib")
    if dylib_id(data) != expected_identity:
        raise MachOError("unexpected LC_ID_DYLIB")
    dependencies = set(loaded_dylibs(data))
    required = {"/usr/lib/libobjc.A.dylib", "/usr/lib/libSystem.B.dylib"}
    if not required.issubset(dependencies):
        raise MachOError("required system dependencies are missing")
    signature = code_signature(data)
    if not signature:
        raise MachOError("LC_CODE_SIGNATURE is missing")
    offset, reserved_size = signature
    if reserved_size < minimum_signature_size:
        raise MachOError(f"signature reservation is too small: {reserved_size} bytes")
    if offset + reserved_size != len(data):
        raise MachOError("signature reservation does not end at EOF")
    if offset + 12 > len(data):
        raise MachOError("signature blob is truncated")
    magic, blob_size, _count = struct.unpack_from(">III", data, offset)
    if magic != 0xFADE0CC0 or blob_size > reserved_size:
        raise MachOError("invalid embedded signature superblob")
    if page_aligned_segments:
        _verify_page_aligned_segments(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dylib", type=Path)
    parser.add_argument("--identity", default="@rpath/TwitchAdBlock.dylib")
    parser.add_argument("--minimum-signature-size", type=int, default=65536)
    parser.add_argument("--page-aligned-segments", action="store_true")
    args = parser.parse_args()
    try:
        verify(args.dylib, args.identity, args.minimum_signature_size,
               args.page_aligned_segments)
    except (OSError, MachOError) as error:
        parser.error(str(error))
    print(f"valid arm64 dylib: {args.dylib}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
