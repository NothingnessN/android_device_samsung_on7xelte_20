#!/usr/bin/env python3
"""Tests accept stock or patched blobs and never modify the vendor tree."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
CAMERA_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CAMERA_DIR))
import fixup_camera_blobs as fixups


def blob_directory():
    if "ON7XELTE_BLOB_DIR" in os.environ:
        return Path(os.environ["ON7XELTE_BLOB_DIR"])
    for parent in CAMERA_DIR.parents:
        for suffix in ("vendor/proprietary/lib", "vendor/samsung/on7xelte/proprietary/lib"):
            candidate = parent / suffix
            if all((candidate / name).is_file() for name in fixups.FIXUPS):
                return candidate
    raise RuntimeError("Set ON7XELTE_BLOB_DIR to the directory containing both camera blobs")


def original_fixture(name, data):
    """Recover the pinned stock bytes in memory if the fixture is already patched."""
    spec = fixups.FIXUPS[name]
    if fixups.sha256(data) == spec.patched_sha256:
        elf = fixups.Elf32(bytearray(data))
        for old, new in spec.imports:
            start = elf.symbol(new)[2]
            elf.data[start:start + len(old) + 1] = old.encode("ascii") + b"\0"
        for patch in spec.code:
            offset = elf.text_offset(patch.vma, len(patch.patched))
            elf.data[offset:offset + len(patch.patched)] = patch.original
        data = bytes(elf.data)
    if fixups.sha256(data) != spec.original_sha256:
        raise ValueError("Unsupported test fixture: " + name)
    return data


class BlobFixupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = blob_directory()
        cls.originals = {name: original_fixture(name, (directory / name).read_bytes())
                         for name in fixups.FIXUPS}
        cls.patched = {name: fixups.fixup_blob(name, data)
                       for name, data in cls.originals.items()}
        cls.temp_root = Path(os.environ.get("TMPDIR", "/tmp/opencode"))
        cls.temp_root.mkdir(parents=True, exist_ok=True)

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(CAMERA_DIR / "fixup_camera_blobs.py"),
                               *map(str, args)], capture_output=True, text=True, timeout=20)

    def test_hash_size_and_idempotence(self):
        for name, spec in fixups.FIXUPS.items():
            with self.subTest(blob=name):
                data = self.patched[name]
                self.assertEqual(fixups.sha256(data), spec.patched_sha256)
                self.assertEqual(len(data), len(self.originals[name]))
                self.assertEqual(fixups.fixup_blob(name, data), data)
                self.assertEqual(fixups.fixup_blob(name, data, check=True), data)

    def test_only_selected_import_names_change(self):
        for name, spec in fixups.FIXUPS.items():
            before = fixups.Elf32(self.originals[name])
            after = fixups.Elf32(self.patched[name])
            aliases = dict(spec.imports)
            for old, new in zip(before.symbols, after.symbols):
                self.assertEqual(new[0], old[0])  # symbol/relocation index
                self.assertEqual(new[2:], old[2:])  # string offset, binding/type, shndx
                self.assertEqual(new[1], aliases.get(old[1], old[1]))
                if old[4] != 0:
                    self.assertEqual(new[1], old[1])  # never rename an export

    def test_all_other_elf_tables_are_unchanged(self):
        for name in fixups.FIXUPS:
            before = fixups.Elf32(self.originals[name])
            after = fixups.Elf32(self.patched[name])
            self.assertEqual(before.sections, after.sections)
            for section_name, section in before.sections.items():
                if section_name in (".dynstr", ".text") or section[1] == 8:  # NOBITS
                    continue
                start, size = section[4], section[5]
                self.assertEqual(before.data[start:start + size], after.data[start:start + size],
                                 (name, section_name))

    def test_changed_bytes_are_bounded(self):
        for name, spec in fixups.FIXUPS.items():
            elf = fixups.Elf32(self.originals[name])
            allowed = set()
            for old, _ in spec.imports:
                start = elf.symbol(old)[2]
                allowed.update(range(start, start + len(old) + 1))
            for patch in spec.code:
                start = elf.text_offset(patch.vma, len(patch.original))
                allowed.update(range(start, start + len(patch.original)))
                self.assertEqual(self.patched[name][start:start + len(patch.patched)], patch.patched)
            changed = {i for i, (a, b) in enumerate(zip(self.originals[name], self.patched[name]))
                       if a != b}
            self.assertTrue(changed)
            self.assertTrue(changed <= allowed)

    def test_native_handle_allocation_and_real_fence_paths_unchanged(self):
        for name, native_allocation in (("libexynoscamera.so", 0x4c6ac),
                                        ("libexynoscamera3.so", 0x44d34)):
            elf = fixups.Elf32(self.patched[name])
            offset = elf.text_offset(native_allocation, 2)
            self.assertEqual(elf.data[offset:offset + 2], b"\xa0\x20")
            self.assertEqual(elf.data[offset:offset + 2], self.originals[name][offset:offset + 2])
        elf = fixups.Elf32(self.patched["libexynoscamera3.so"])
        hardware_check = elf.text_offset(0x745d4, 2)
        self.assertEqual(elf.data[hardware_check:hardware_check + 2], b"\xc8\xbf")  # IT GT
        start = elf.text_offset(0x7461c, 0x7470c - 0x7461c)
        end = elf.text_offset(0x7470c, 1)
        self.assertEqual(elf.data[start:end], self.originals["libexynoscamera3.so"][start:end])
        # HAL1's unused software-fence branch was not changed.
        elf = fixups.Elf32(self.patched["libexynoscamera.so"])
        offset = elf.text_offset(0x76e64, 2)
        self.assertEqual(elf.data[offset:offset + 2], b"\x18\xbf")

    def test_no_fence_condition_including_signed_overflow_boundaries(self):
        elf = fixups.Elf32(self.patched["libexynoscamera3.so"])
        offset = elf.text_offset(0x745a4, 2)
        instruction = struct.unpack_from("<H", elf.data, offset)[0]
        self.assertEqual(instruction, 0xbfa8)  # one-instruction IT GE
        for fd in (-0x80000000, -700, -2, -1, 0, 1, 700, 0x7fffffff):
            x, y = fd & 0xffffffff, 0xffffffff
            result = (x - y) & 0xffffffff
            negative = bool(result & 0x80000000)
            overflow = bool((x ^ y) & (x ^ result) & 0x80000000)
            self.assertEqual(negative == overflow, fd >= -1, fd)  # GE: N == V

    def test_unknown_or_partially_patched_blobs_are_refused(self):
        for name in fixups.FIXUPS:
            for data in (self.originals[name], self.patched[name]):
                bad = bytearray(data)
                bad[-1] ^= 1
                snapshot = bytes(bad)
                with self.assertRaisesRegex(ValueError, "unsupported SHA-256"):
                    fixups.fixup_blob(name, bad)
                self.assertEqual(bytes(bad), snapshot)
            elf = fixups.Elf32(bytearray(self.originals[name]))
            elf.rename_import(*fixups.FIXUPS[name].imports[0])
            with self.assertRaisesRegex(ValueError, "unsupported SHA-256"):
                fixups.fixup_blob(name, elf.data)

    def test_check_rejects_originals_and_unsupported_names(self):
        for name, data in self.originals.items():
            with self.assertRaisesRegex(ValueError, "still needs"):
                fixups.fixup_blob(name, data, check=True)
        with self.assertRaisesRegex(ValueError, "Unsupported blob"):
            fixups.fixup_blob("libhwjpeg.so", b"not a camera blob")

    def test_cli_preflight_atomic_replacement_and_mode(self):
        with tempfile.TemporaryDirectory(dir=self.temp_root) as directory:
            files = [Path(directory) / name for name in fixups.FIXUPS]
            for path in files:
                path.write_bytes(self.originals[path.name])
            files[0].chmod(0o640)
            bad = bytearray(self.originals[files[1].name])
            bad[-1] ^= 1
            files[1].write_bytes(bad)
            result = self.run_cli(*files)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(files[0].read_bytes(), self.originals[files[0].name])
            self.assertEqual(files[1].read_bytes(), bad)
            files[1].write_bytes(self.originals[files[1].name])
            self.assertEqual(self.run_cli("--check", *files).returncode, 1)
            result = self.run_cli(*files)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(files[0].stat().st_mode & 0o777, 0o640)
            for path in files:
                self.assertEqual(path.read_bytes(), self.patched[path.name])
            mtimes = [path.stat().st_mtime_ns for path in files]
            self.assertEqual(self.run_cli(*files).returncode, 0)
            self.assertEqual(self.run_cli("--check", *files).returncode, 0)
            self.assertEqual([path.stat().st_mtime_ns for path in files], mtimes)
            self.assertFalse(list(Path(directory).glob("*.tmp")))

    def test_cli_missing_file_and_symlink_do_not_modify_other_inputs(self):
        with tempfile.TemporaryDirectory(dir=self.temp_root) as directory:
            first = Path(directory) / "libexynoscamera.so"
            second = Path(directory) / "libexynoscamera3.so"
            first.write_bytes(self.originals[first.name])
            self.assertEqual(self.run_cli(first, second).returncode, 1)
            self.assertEqual(first.read_bytes(), self.originals[first.name])
            second.symlink_to(first)
            self.assertEqual(self.run_cli(first, second).returncode, 1)
            self.assertEqual(first.read_bytes(), self.originals[first.name])
            self.assertTrue(second.is_symlink())

    def test_extraction_persists_fixups_and_stops_on_unknown_revision(self):
        with tempfile.TemporaryDirectory(dir=self.temp_root) as directory:
            root = Path(directory)
            device = root / "device/samsung/on7xelte"
            vendor = root / "vendor/samsung/on7xelte/proprietary/lib"
            helper = root / "vendor/lineage/build/tools/extract_utils.sh"
            source = root / "stock"
            (device / "camera").mkdir(parents=True)
            helper.parent.mkdir(parents=True)
            source.mkdir()
            shutil.copy2(CAMERA_DIR / "fixup_camera_blobs.py", device / "camera")
            shutil.copy2(CAMERA_DIR.parent / "extract-files.sh", device)
            (device / "proprietary-files.txt").write_text("# Camera\n")
            # Mock only the external extraction helper, not our device script.
            helper.write_text('''setup_vendor() {
    mkdir -p "$3/vendor/$2/$1/proprietary/lib"
}
extract() {
    if [ "$3" != "Audio" ]; then
        cp "$2"/libexynoscamera*.so "$LINEAGE_ROOT/vendor/$VENDOR/$DEVICE/proprietary/lib/"
    fi
}
''')
            setup = device / "setup-makefiles.sh"
            setup.write_text('#!/bin/bash\ntouch "${BASH_SOURCE%/*}/makefiles-called"\n')
            setup.chmod(0o755)
            for name, data in self.originals.items():
                (source / name).write_bytes(data)
            command = ["bash", str(device / "extract-files.sh"), str(source)]
            for _ in range(2):
                result = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                for name, data in self.patched.items():
                    self.assertEqual((vendor / name).read_bytes(), data)
            marker = device / "makefiles-called"
            self.assertTrue(marker.is_file())
            marker.unlink()
            bad = bytearray(self.originals["libexynoscamera3.so"])
            bad[-1] ^= 1
            (source / "libexynoscamera3.so").write_bytes(bad)
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists())
            self.assertEqual((vendor / "libexynoscamera.so").read_bytes(),
                             self.originals["libexynoscamera.so"])
            self.assertEqual((vendor / "libexynoscamera3.so").read_bytes(), bad)


if __name__ == "__main__":
    unittest.main()
