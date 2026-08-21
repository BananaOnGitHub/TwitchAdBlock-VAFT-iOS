from __future__ import annotations

import plistlib
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.macho import (
    LC_ENCRYPTION_INFO_64,
    LC_SEGMENT_64,
    MachOError,
    inject_load_dylib,
    loaded_dylibs,
    remove_load_dylibs,
)
from tools.patch_ipa import LOAD_PATH, patch_ipa

DONOR_LOAD_PATH = "@rpath/Tweach.dylib"


def synthetic_executable(cryptid: int = 0) -> bytes:
    section_offset = 0x4000
    segment_size = 72 + 80
    encryption_size = 24
    command_size = segment_size + encryption_size
    data = bytearray(0x5000)
    struct.pack_into("<8I", data, 0, 0xFEEDFACF, 0x0100000C, 0, 2, 2, command_size, 0, 0)
    offset = 32
    struct.pack_into("<II16s4Q4I", data, offset, LC_SEGMENT_64, segment_size, b"__TEXT", 0, 0x5000, 0, 0x5000, 7, 5, 1, 0)
    section = offset + 72
    struct.pack_into("<16s16s2Q8I", data, section, b"__text", b"__TEXT", 0x4000, 16, section_offset, 2, 0, 0, 0, 0, 0, 0)
    offset += segment_size
    struct.pack_into("<6I", data, offset, LC_ENCRYPTION_INFO_64, encryption_size, 0, 0, cryptid, 0)
    return bytes(data)


class MachOTests(unittest.TestCase):
    def test_injects_once(self) -> None:
        original = synthetic_executable()
        patched, changed = inject_load_dylib(original, LOAD_PATH)
        self.assertTrue(changed)
        self.assertIn(LOAD_PATH, loaded_dylibs(patched))
        patched_again, changed_again = inject_load_dylib(patched, LOAD_PATH)
        self.assertFalse(changed_again)
        self.assertEqual(patched, patched_again)

    def test_refuses_missing_padding(self) -> None:
        original = bytearray(synthetic_executable())
        original[32 + (72 + 80 + 24)] = 1
        with self.assertRaises(MachOError):
            inject_load_dylib(bytes(original), LOAD_PATH)

    def test_removes_donor_load_command(self) -> None:
        donor, _ = inject_load_dylib(synthetic_executable(), DONOR_LOAD_PATH)
        cleaned, removed = remove_load_dylibs(donor, {DONOR_LOAD_PATH})
        self.assertEqual(removed, [DONOR_LOAD_PATH])
        self.assertNotIn(DONOR_LOAD_PATH, loaded_dylibs(cleaned))
        self.assertEqual(len(donor), len(cleaned))


class IPAPatcherTests(unittest.TestCase):
    @staticmethod
    def make_framework(root: Path) -> Path:
        framework = root / "Tweach.framework"
        framework.mkdir()
        (framework / "Tweach").write_bytes(b"synthetic framework binary")
        (framework / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleExecutable": "Tweach",
            "CFBundleIdentifier": "io.github.bananaongithub.tas.tweach",
            "CFBundlePackageType": "FMWK",
        }))
        return framework

    def test_patches_synthetic_ipa_without_touching_assets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.ipa"
            output = root / "output.ipa"
            framework = self.make_framework(root)
            info = plistlib.dumps({"CFBundleExecutable": "Twitch"})
            assets = b"synthetic asset catalog"
            with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("Payload/Twitch.app/Info.plist", info)
                donor_executable, _ = inject_load_dylib(synthetic_executable(), DONOR_LOAD_PATH)
                archive.writestr("Payload/Twitch.app/Twitch", donor_executable)
                archive.writestr("Payload/Twitch.app/Assets.car", assets)
                archive.writestr("Payload/Twitch.app/Frameworks/Tweach.dylib", b"donor tweak")

            injected, removed = patch_ipa(source, framework, output)
            self.assertTrue(injected)
            self.assertEqual(removed, [DONOR_LOAD_PATH])
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.read("Payload/Twitch.app/Assets.car"), assets)
                self.assertNotIn("Payload/Twitch.app/Frameworks/Tweach.dylib", archive.namelist())
                self.assertEqual(
                    archive.read("Payload/Twitch.app/Frameworks/Tweach.framework/Tweach"),
                    b"synthetic framework binary",
                )
                framework_info = plistlib.loads(
                    archive.read("Payload/Twitch.app/Frameworks/Tweach.framework/Info.plist")
                )
                self.assertEqual(framework_info["CFBundleExecutable"], "Tweach")
                executable = archive.read("Payload/Twitch.app/Twitch")
                self.assertIn(LOAD_PATH, loaded_dylibs(executable))
                self.assertNotIn(DONOR_LOAD_PATH, loaded_dylibs(executable))

    def test_refuses_encrypted_ipa(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "encrypted.ipa"
            framework = self.make_framework(root)
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("Payload/Twitch.app/Info.plist", plistlib.dumps({"CFBundleExecutable": "Twitch"}))
                archive.writestr("Payload/Twitch.app/Twitch", synthetic_executable(cryptid=1))
                archive.writestr("Payload/Twitch.app/Assets.car", b"assets")
            with self.assertRaises(ValueError):
                patch_ipa(source, framework, root / "output.ipa")


if __name__ == "__main__":
    unittest.main()
