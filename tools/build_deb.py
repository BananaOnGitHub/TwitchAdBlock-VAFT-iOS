#!/usr/bin/env python3
"""Build rootful and rootless jailbreak packages without requiring Theos."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE_ID = "dev.tas.twitchadblock"
PACKAGE_NAME = "TwitchAdBlock VAFT"
INSTALL_RELATIVE = Path("Library/MobileSubstrate/DynamicLibraries")
REPRODUCIBLE_TIMESTAMP = 946684800


def control(version: str, architecture: str) -> str:
    return "\n".join(
        [
            f"Package: {PACKAGE_ID}",
            f"Name: {PACKAGE_NAME}",
            f"Version: {version}",
            f"Architecture: {architecture}",
            "Description: Native VAFT-based video ad filtering for the Twitch iOS app.",
            "Author: TwitchAdBlock-VAFT-iOS contributors",
            "Maintainer: TwitchAdBlock-VAFT-iOS contributors",
            "Section: Tweaks",
            "Depends: mobilesubstrate",
            "Conflicts: com.level3tjg.twitchadblock",
            "",
        ]
    )


def build_variant(version: str, scheme: str, prefix: Path, architecture: str) -> Path:
    dylib = ROOT / "build" / "TwitchAdBlock.dylib"
    filter_plist = ROOT / "packaging" / "TwitchAdBlock.plist"
    if not dylib.is_file():
        raise SystemExit("build/TwitchAdBlock.dylib is missing; run make build first")

    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    output = dist / f"{PACKAGE_ID}_{version}_{architecture}.deb"

    with tempfile.TemporaryDirectory(prefix=f"tas-{scheme}-") as directory:
        package_root = Path(directory)
        os.chmod(package_root, 0o755)
        metadata = package_root / "DEBIAN"
        install = package_root / prefix / INSTALL_RELATIVE
        metadata.mkdir(parents=True)
        install.mkdir(parents=True)

        (metadata / "control").write_text(control(version, architecture), encoding="utf-8")
        shutil.copy2(dylib, install / "TwitchAdBlock.dylib")
        shutil.copy2(filter_plist, install / "TwitchAdBlock.plist")
        os.chmod(install / "TwitchAdBlock.dylib", 0o755)
        os.chmod(install / "TwitchAdBlock.plist", 0o644)
        for path in sorted(package_root.rglob("*"), reverse=True):
            os.utime(path, (REPRODUCIBLE_TIMESTAMP, REPRODUCIBLE_TIMESTAMP), follow_symlinks=False)
        os.utime(package_root, (REPRODUCIBLE_TIMESTAMP, REPRODUCIBLE_TIMESTAMP))

        environment = os.environ.copy()
        environment.setdefault("SOURCE_DATE_EPOCH", str(REPRODUCIBLE_TIMESTAMP))
        subprocess.run(
            ["dpkg-deb", "--root-owner-group", "-Zxz", "-z9", "--build", str(package_root), str(output)],
            check=True,
            env=environment,
        )

    actual_architecture = subprocess.check_output(
        ["dpkg-deb", "--field", str(output), "Architecture"], text=True
    ).strip()
    if actual_architecture != architecture:
        raise SystemExit(f"unexpected DEB architecture: {actual_architecture}")
    listing = subprocess.check_output(["dpkg-deb", "--contents", str(output)], text=True)
    expected = f"./{prefix.as_posix() + '/' if prefix.parts else ''}{INSTALL_RELATIVE.as_posix()}/TwitchAdBlock.dylib"
    if expected not in listing:
        raise SystemExit(f"DEB is missing expected install path: {expected}")
    return output


def main() -> int:
    if shutil.which("dpkg-deb") is None:
        raise SystemExit("dpkg-deb is required to build jailbreak packages")
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    outputs = [
        build_variant(version, "rootful", Path(), "iphoneos-arm"),
        build_variant(version, "rootless", Path("var/jb"), "iphoneos-arm64"),
    ]
    for output in outputs:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
