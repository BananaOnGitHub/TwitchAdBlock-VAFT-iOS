#!/usr/bin/env python3
"""Small, dependency-free helpers for thin arm64 Mach-O files."""

from __future__ import annotations

import struct
from dataclasses import dataclass

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_DYLIB = 0x6

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_LOAD_DYLIB = 0xC
LC_ID_DYLIB = 0xD
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001F
LC_CODE_SIGNATURE = 0x1D
LC_ENCRYPTION_INFO = 0x21
LC_ENCRYPTION_INFO_64 = 0x2C


class MachOError(ValueError):
    pass


@dataclass(frozen=True)
class Header:
    cpu_type: int
    file_type: int
    command_count: int
    command_size: int


@dataclass(frozen=True)
class LoadCommand:
    offset: int
    command: int
    size: int


def header(data: bytes | bytearray) -> Header:
    if len(data) < 32:
        raise MachOError("file is too small to be a 64-bit Mach-O")
    magic, cpu_type, _subtype, file_type, count, size, _flags, _reserved = struct.unpack_from(
        "<8I", data, 0
    )
    if magic != MH_MAGIC_64:
        raise MachOError("only thin little-endian 64-bit Mach-O files are supported")
    if cpu_type != CPU_TYPE_ARM64:
        raise MachOError("only arm64 Mach-O files are supported")
    if 32 + size > len(data):
        raise MachOError("load-command table extends past end of file")
    return Header(cpu_type, file_type, count, size)


def load_commands(data: bytes | bytearray) -> list[LoadCommand]:
    info = header(data)
    commands: list[LoadCommand] = []
    offset = 32
    command_end = 32 + info.command_size
    for _ in range(info.command_count):
        if offset + 8 > command_end:
            raise MachOError("truncated load command")
        command, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > command_end:
            raise MachOError("invalid load-command size")
        commands.append(LoadCommand(offset, command, size))
        offset += size
    if offset != command_end:
        raise MachOError("load-command count and size disagree")
    return commands


def _command_string(data: bytes | bytearray, item: LoadCommand) -> str:
    if item.size < 12:
        raise MachOError("truncated string load command")
    relative_offset = struct.unpack_from("<I", data, item.offset + 8)[0]
    if relative_offset >= item.size:
        raise MachOError("invalid string offset in load command")
    start = item.offset + relative_offset
    end = data.find(b"\0", start, item.offset + item.size)
    if end < 0:
        raise MachOError("unterminated string in load command")
    return bytes(data[start:end]).decode("utf-8")


def loaded_dylibs(data: bytes | bytearray) -> list[str]:
    kinds = {LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB}
    return [_command_string(data, item) for item in load_commands(data) if item.command in kinds]


def dylib_id(data: bytes | bytearray) -> str | None:
    for item in load_commands(data):
        if item.command == LC_ID_DYLIB:
            return _command_string(data, item)
    return None


def encryption_ids(data: bytes | bytearray) -> list[int]:
    values: list[int] = []
    for item in load_commands(data):
        if item.command in {LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64}:
            if item.size < 20:
                raise MachOError("truncated encryption-info command")
            values.append(struct.unpack_from("<I", data, item.offset + 16)[0])
    return values


def _first_section_offset(data: bytes | bytearray) -> int:
    offsets: list[int] = []
    for item in load_commands(data):
        if item.command != LC_SEGMENT_64:
            continue
        if item.size < 72:
            raise MachOError("truncated LC_SEGMENT_64")
        section_count = struct.unpack_from("<I", data, item.offset + 64)[0]
        if 72 + section_count * 80 > item.size:
            raise MachOError("truncated section table")
        for index in range(section_count):
            section_offset = struct.unpack_from(
                "<I", data, item.offset + 72 + index * 80 + 48
            )[0]
            if section_offset:
                offsets.append(section_offset)
    if not offsets:
        raise MachOError("could not locate a file-backed Mach-O section")
    return min(offsets)


def inject_load_dylib(data: bytes, path: str) -> tuple[bytes, bool]:
    """Add LC_LOAD_DYLIB when absent, using existing Mach-O header padding."""
    if path in loaded_dylibs(data):
        return data, False

    encoded = path.encode("utf-8") + b"\0"
    command_size = (24 + len(encoded) + 7) & ~7
    info = header(data)
    command_end = 32 + info.command_size
    first_section = _first_section_offset(data)
    free_space = first_section - command_end
    if free_space < command_size:
        raise MachOError(
            f"not enough Mach-O header padding: need {command_size} bytes, have {free_space}"
        )
    if any(data[command_end : command_end + command_size]):
        raise MachOError("Mach-O header padding is not empty")

    output = bytearray(data)
    command = struct.pack("<6I", LC_LOAD_DYLIB, command_size, 24, 0, 0, 0)
    command += encoded
    command += b"\0" * (command_size - len(command))
    output[command_end : command_end + command_size] = command
    struct.pack_into("<I", output, 16, info.command_count + 1)
    struct.pack_into("<I", output, 20, info.command_size + command_size)
    return bytes(output), True


def remove_load_dylibs(data: bytes, paths: set[str]) -> tuple[bytes, list[str]]:
    """Remove matching dylib load commands without shifting Mach-O file data."""
    if not paths:
        return data, []

    info = header(data)
    kinds = {LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB}
    commands = load_commands(data)
    kept: list[bytes] = []
    removed: list[str] = []
    for item in commands:
        if item.command in kinds and _command_string(data, item) in paths:
            removed.append(_command_string(data, item))
        else:
            kept.append(bytes(data[item.offset : item.offset + item.size]))

    if not removed:
        return data, []

    old_end = 32 + info.command_size
    packed = b"".join(kept)
    output = bytearray(data)
    output[32:old_end] = packed + b"\0" * (info.command_size - len(packed))
    struct.pack_into("<I", output, 16, info.command_count - len(removed))
    struct.pack_into("<I", output, 20, len(packed))
    return bytes(output), removed


def code_signature(data: bytes | bytearray) -> tuple[int, int] | None:
    for item in load_commands(data):
        if item.command == LC_CODE_SIGNATURE:
            if item.size < 16:
                raise MachOError("truncated LC_CODE_SIGNATURE")
            return struct.unpack_from("<II", data, item.offset + 8)
    return None
