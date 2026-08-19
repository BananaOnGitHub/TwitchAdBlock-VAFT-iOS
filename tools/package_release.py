#!/usr/bin/env python3
"""Create the release bundle."""

from __future__ import annotations

import hashlib
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    dylib = ROOT / "build" / "TwitchAdBlock.dylib"
    if not dylib.is_file():
        raise SystemExit("build/TwitchAdBlock.dylib is missing; run make build first")

    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    release_dylib = dist / "TwitchAdBlock.dylib"
    shutil.copy2(dylib, release_dylib)
    debs = sorted(dist.glob(f"dev.tas.twitchadblock_{version}_*.deb"))
    if len(debs) != 2:
        raise SystemExit("expected rootful and rootless DEBs; run make deb first")
    bundle = dist / f"TwitchAdBlock-VAFT-iOS-{version}.zip"
    files = [
        (dylib, "TwitchAdBlock.dylib"),
        (ROOT / "tools" / "patch_ipa.py", "tools/patch_ipa.py"),
        (ROOT / "tools" / "macho.py", "tools/macho.py"),
        (ROOT / "README.md", "README.md"),
        (ROOT / "CHANGELOG.md", "CHANGELOG.md"),
        (ROOT / "LICENSE", "LICENSE"),
        (ROOT / "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        (ROOT / "UPSTREAM.md", "UPSTREAM.md"),
    ]
    files.extend((deb, deb.name) for deb in debs)
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as archive:
        for source, destination in files:
            archive.write(source, f"TwitchAdBlock-VAFT-iOS-{version}/{destination}")

    checksums = []
    for path in (release_dylib, *debs, bundle):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksums.append(f"{digest}  {path.name}")
    (dist / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    print(bundle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
