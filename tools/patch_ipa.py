#!/usr/bin/env python3
"""Replace donor tweaks with the VAFT framework in a decrypted Twitch IPA."""

from __future__ import annotations

import argparse
import os
import plistlib
import shutil
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

try:
    from .macho import MachOError, encryption_ids, inject_load_dylib, remove_load_dylibs
except ImportError:  # Direct execution from tools/patch_ipa.py.
    from macho import MachOError, encryption_ids, inject_load_dylib, remove_load_dylibs

LOAD_PATH = "@rpath/Tweach.framework/Tweach"
FRAMEWORK_NAME = "Tweach.framework"
FRAMEWORK_BINARY_NAME = "Tweach"
DONOR_DYLIB_NAMES = {"Tweach.dylib", "TwitchAdBlock.dylib"}
DONOR_LOAD_PATHS = {
    "@rpath/Tweach.dylib",
    "@executable_path/Frameworks/Tweach.dylib",
    "@rpath/TwitchAdBlock.dylib",
    "@executable_path/Frameworks/TwitchAdBlock.dylib",
    LOAD_PATH,
}


def _app_info_entry(archive: zipfile.ZipFile) -> str:
    matches = []
    for name in archive.namelist():
        path = PurePosixPath(name)
        if len(path.parts) == 3 and path.parts[0] == "Payload" and path.parts[1].endswith(".app") and path.name == "Info.plist":
            matches.append(name)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Payload/*.app/Info.plist, found {len(matches)}")
    return matches[0]


def _copy_entry(source: zipfile.ZipFile, destination: zipfile.ZipFile, info: zipfile.ZipInfo) -> None:
    with source.open(info, "r") as incoming, destination.open(info, "w") as outgoing:
        shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)


def patch_ipa(input_path: Path, framework_path: Path, output_path: Path,
              force: bool = False) -> tuple[bool, list[str]]:
    if not input_path.is_file():
        raise FileNotFoundError(f"input IPA not found: {input_path}")
    framework_binary_path = framework_path / FRAMEWORK_BINARY_NAME
    framework_info_path = framework_path / "Info.plist"
    if not framework_binary_path.is_file():
        raise FileNotFoundError(f"framework binary not found: {framework_binary_path}")
    if not framework_info_path.is_file():
        raise FileNotFoundError(f"framework Info.plist not found: {framework_info_path}")
    framework_info = plistlib.loads(framework_info_path.read_bytes())
    if framework_info.get("CFBundleExecutable") != FRAMEWORK_BINARY_NAME:
        raise ValueError("framework Info.plist has an unexpected CFBundleExecutable")
    if output_path.exists() and not force:
        raise FileExistsError(f"output already exists: {output_path} (use --force to replace it)")
    if input_path.resolve() == output_path.resolve():
        raise ValueError("input and output paths must differ")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_handle = tempfile.NamedTemporaryFile(
        prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent, delete=False
    )
    temp_path = Path(temp_handle.name)
    temp_handle.close()

    injected = False
    try:
        with zipfile.ZipFile(input_path, "r") as source:
            bad_entry = source.testzip()
            if bad_entry:
                raise zipfile.BadZipFile(f"CRC failure in {bad_entry}")

            info_entry = _app_info_entry(source)
            app_root = str(PurePosixPath(info_entry).parent)
            info_plist = plistlib.loads(source.read(info_entry))
            executable_name = info_plist.get("CFBundleExecutable")
            if not isinstance(executable_name, str) or not executable_name:
                raise ValueError("Info.plist has no CFBundleExecutable")
            executable_entry = f"{app_root}/{executable_name}"
            framework_root = f"{app_root}/Frameworks/{FRAMEWORK_NAME}"
            framework_binary_entry = f"{framework_root}/{FRAMEWORK_BINARY_NAME}"
            framework_info_entry = f"{framework_root}/Info.plist"
            asset_entry = f"{app_root}/Assets.car"

            names = set(source.namelist())
            if executable_entry not in names:
                raise ValueError(f"app executable is missing: {executable_entry}")
            if asset_entry not in names:
                raise ValueError("Assets.car is missing; refusing to create a known-broken package")
            asset_crc = source.getinfo(asset_entry).CRC

            executable = source.read(executable_entry)
            if any(value != 0 for value in encryption_ids(executable)):
                raise ValueError("the app executable is still encrypted; provide a decrypted IPA")
            executable, removed_load_paths = remove_load_dylibs(executable, DONOR_LOAD_PATHS)
            executable, injected = inject_load_dylib(executable, LOAD_PATH)
            framework_binary = framework_binary_path.read_bytes()
            framework_info_bytes = framework_info_path.read_bytes()

            donor_entries = {
                f"{app_root}/Frameworks/{name}" for name in DONOR_DYLIB_NAMES
            }

            with zipfile.ZipFile(temp_path, "w", allowZip64=True) as destination:
                for item in source.infolist():
                    if item.filename == executable_entry:
                        destination.writestr(item, executable)
                    elif item.filename in donor_entries or item.filename.startswith(framework_root + "/"):
                        continue
                    else:
                        _copy_entry(source, destination, item)

                binary_item = zipfile.ZipInfo(framework_binary_entry)
                binary_item.compress_type = zipfile.ZIP_DEFLATED
                binary_item.external_attr = 0o100755 << 16
                destination.writestr(binary_item, framework_binary)
                info_item = zipfile.ZipInfo(framework_info_entry)
                info_item.compress_type = zipfile.ZIP_DEFLATED
                info_item.external_attr = 0o100644 << 16
                destination.writestr(info_item, framework_info_bytes)

        with zipfile.ZipFile(temp_path, "r") as result:
            bad_entry = result.testzip()
            if bad_entry:
                raise zipfile.BadZipFile(f"output CRC failure in {bad_entry}")
            if result.getinfo(asset_entry).CRC != asset_crc:
                raise ValueError("Assets.car changed while repackaging")

        if output_path.exists():
            output_path.unlink()
        os.replace(temp_path, output_path)
        return injected, removed_load_paths
    except Exception:
        temp_path.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="decrypted Twitch IPA supplied by the user")
    default_framework = Path("Tweach.framework")
    if not default_framework.is_dir() and Path("build/Tweach.framework").is_dir():
        default_framework = Path("build/Tweach.framework")
    parser.add_argument("--framework", type=Path, default=default_framework)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    output = args.output or args.input.with_name(f"{args.input.stem}_VAFT-patched.ipa")
    try:
        injected, removed = patch_ipa(args.input, args.framework, output, args.force)
    except (OSError, ValueError, MachOError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    if removed:
        print(f"removed donor load command(s): {', '.join(removed)}")
    action = "added load command and injected" if injected else "updated"
    print(f"{action} {FRAMEWORK_NAME}: {output}")
    print("The output is not signed. Sign it with your sideloading tool before installation.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
