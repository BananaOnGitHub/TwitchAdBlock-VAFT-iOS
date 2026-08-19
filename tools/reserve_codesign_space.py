#!/usr/bin/env python3
"""Reserve an in-place code-signature region in a thin 64-bit Mach-O."""

import argparse
import hashlib
import os
import struct

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
MH_MAGIC_64 = 0xFEEDFACF
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CSSLOT_CODEDIRECTORY = 0
CSSLOT_ALTERNATE_CODEDIRECTORIES = 0x1000
CSSLOT_SIGNATURESLOT = 0x10000
CS_ADHOC = 0x2


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def refresh_adhoc_code_directories(data: bytearray, signature_offset: int) -> None:
    superblob_magic, superblob_length, count = struct.unpack_from(
        ">III", data, signature_offset
    )
    if superblob_magic != CSMAGIC_EMBEDDED_SIGNATURE:
        raise ValueError("existing embedded signature is malformed")
    if superblob_length > len(data) - signature_offset:
        raise ValueError("embedded signature exceeds the file")

    code_directories = []
    for index in range(count):
        slot_type, slot_offset = struct.unpack_from(
            ">II", data, signature_offset + 12 + index * 8
        )
        if slot_type == CSSLOT_SIGNATURESLOT:
            raise ValueError("refusing to modify a certificate-signed CodeDirectory")
        if slot_type == CSSLOT_CODEDIRECTORY or (
            CSSLOT_ALTERNATE_CODEDIRECTORIES
            <= slot_type
            < CSSLOT_ALTERNATE_CODEDIRECTORIES + 5
        ):
            code_directories.append(signature_offset + slot_offset)

    if not code_directories:
        raise ValueError("embedded signature has no CodeDirectory")

    algorithms = {
        1: (hashlib.sha1, 20),
        2: (hashlib.sha256, 32),
        3: (hashlib.sha256, 20),
        4: (hashlib.sha384, 48),
    }
    for directory in code_directories:
        magic, length, _version, flags, hash_offset = struct.unpack_from(
            ">IIIII", data, directory
        )
        special_slots, code_slots, code_limit = struct.unpack_from(
            ">III", data, directory + 24
        )
        hash_size, hash_type, _platform, page_shift = struct.unpack_from(
            ">BBBB", data, directory + 36
        )
        if magic != CSMAGIC_CODEDIRECTORY or not (flags & CS_ADHOC):
            raise ValueError("expected an ad-hoc CodeDirectory")
        if hash_type not in algorithms or algorithms[hash_type][1] != hash_size:
            raise ValueError("unsupported CodeDirectory hash algorithm")
        if hash_offset + code_slots * hash_size > length:
            raise ValueError("CodeDirectory hash table is out of bounds")
        if code_limit > signature_offset:
            raise ValueError("CodeDirectory covers bytes inside its own signature")

        constructor, digest_size = algorithms[hash_type]
        page_size = 0 if page_shift == 0 else 1 << page_shift
        for slot in range(code_slots):
            start = 0 if page_size == 0 else slot * page_size
            end = code_limit if page_size == 0 else min(start + page_size, code_limit)
            digest = constructor(data[start:end]).digest()[:digest_size]
            destination = directory + hash_offset + slot * hash_size
            data[destination:destination + hash_size] = digest


def reserve(path: str, requested_size: int) -> None:
    with open(path, "rb") as stream:
        data = bytearray(stream.read())

    if len(data) < 32 or struct.unpack_from("<I", data, 0)[0] != MH_MAGIC_64:
        raise ValueError("expected a thin little-endian 64-bit Mach-O")

    ncmds = struct.unpack_from("<I", data, 16)[0]
    command_offset = 32
    code_signature_command = None
    linkedit_command = None

    for _ in range(ncmds):
        command, command_size = struct.unpack_from("<II", data, command_offset)
        if command_size < 8 or command_offset + command_size > len(data):
            raise ValueError("invalid Mach-O load command table")
        if command == LC_CODE_SIGNATURE:
            code_signature_command = command_offset
        elif command == LC_SEGMENT_64:
            segment_name = bytes(data[command_offset + 8:command_offset + 24]).split(b"\0", 1)[0]
            if segment_name == b"__LINKEDIT":
                linkedit_command = command_offset
        command_offset += command_size

    if code_signature_command is None or linkedit_command is None:
        raise ValueError("Mach-O must already contain LC_CODE_SIGNATURE and __LINKEDIT")

    signature_offset, signature_size = struct.unpack_from(
        "<II", data, code_signature_command + 8
    )
    if signature_offset + signature_size != len(data):
        raise ValueError("code signature must be the final object in the file")
    if bytes(data[signature_offset:signature_offset + 4]) != bytes.fromhex("fade0cc0"):
        raise ValueError("existing embedded signature is missing or malformed")

    reserved_size = align(max(requested_size, signature_size), 16)
    new_length = signature_offset + reserved_size
    if new_length > len(data):
        data.extend(b"\0" * (new_length - len(data)))

    struct.pack_into("<I", data, code_signature_command + 12, reserved_size)

    linkedit_file_offset = struct.unpack_from("<Q", data, linkedit_command + 40)[0]
    linkedit_file_size = new_length - linkedit_file_offset
    linkedit_vm_size = align(linkedit_file_size, 0x4000)
    struct.pack_into("<Q", data, linkedit_command + 32, linkedit_vm_size)
    struct.pack_into("<Q", data, linkedit_command + 48, linkedit_file_size)

    refresh_adhoc_code_directories(data, signature_offset)

    temporary = path + ".codesign-space"
    with open(temporary, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.chmod(temporary, os.stat(path).st_mode)
    os.replace(temporary, path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--size", type=int, default=65536)
    arguments = parser.parse_args()
    reserve(arguments.path, arguments.size)


if __name__ == "__main__":
    main()
