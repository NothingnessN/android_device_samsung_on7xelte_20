#!/usr/bin/env python3
"""Apply the verified on7xelte camera ABI fixups, without external ELF tools.

Only the pinned originals or their exact patched forms are accepted. Imported
symbols preceding GNU hash's symoffset can be renamed in-place: they are not
hashed exports. Symbol indices, relocations, ELF layout and file size stay intact.
"""

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile


GRAPHICBUFFER_IMPORT = "_ZN7android13GraphicBufferC1EjjijjP13native_handleb"
COMMON_IMPORTS = (
    (GRAPHICBUFFER_IMPORT, "exynos_graphicbuffer_ctor"),
    ("_ZN7android5Fence4waitEi", "exynos_fence_wait"),
    ("_ZN7android5FenceD1Ev", "exynos_fence_dtor"),
)


@dataclass(frozen=True)
class CodePatch:
    vma: int
    original: bytes
    patched: bytes
    description: str


@dataclass(frozen=True)
class BlobFixup:
    original_sha256: str
    patched_sha256: str
    imports: tuple
    code: tuple


FIXUPS = {
    "libexynoscamera.so": BlobFixup(
        "a7ba45687e5a5ee11c2d646112c1870f8d6bcdd79b5853ab60b263c003f39956",
        "36744663706cb3f96cb090bf8484fad3d12dc2a25968b8764edeea10b4f0c1de",
        COMMON_IMPORTS,
        (CodePatch(0x4c744, b"\x88\x20", b"\xa0\x20",
                   "GraphicBuffer allocation: movs r0, #0x88 -> #0xa0"),),
    ),
    "libexynoscamera3.so": BlobFixup(
        "56a8977bb9d121b92d1451d10d39f9ff86db693e5eca116f046b1b386efe928a",
        "09ed013b203f65bae4d7065731f11b8f6872136fe3b35ae0e0b0155ed5dfae52",
        COMMON_IMPORTS + (("_ZN7android5FenceC1Ei", "exynos_fence_ctor"),),
        (
            CodePatch(0x44dcc, b"\x88\x20", b"\xa0\x20",
                      "GraphicBuffer allocation: movs r0, #0x88 -> #0xa0"),
            # ExynosCameraFence::isValid(), software-Fence branch only.
            # After cmp fd, #-1, GE accepts -1 and real fds, but rejects < -1.
            # The hardware-fence branch and all wait/error paths are untouched.
            CodePatch(0x745a4, b"\x18\xbf", b"\xa8\xbf", "it ne -> it ge"),
        ),
    ),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Elf32:
    """Small, deliberately limited parser for these ELF32 little-endian blobs."""

    def __init__(self, data):
        self.data = data
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        if header[0][:7] != b"\x7fELF\x01\x01\x01" or header[1:3] != (3, 40):
            raise ValueError("Expected a little-endian ARM ELF32 shared library")
        shoff, shentsize, shnum, shstrndx = header[6], header[11], header[12], header[13]
        if shentsize != 40 or not 0 < shstrndx < shnum:
            raise ValueError("Unsupported ELF section table")
        sections = [struct.unpack_from("<10I", data, shoff + i * shentsize)
                    for i in range(shnum)]
        names = sections[shstrndx]
        self.sections = {}
        for section in sections:
            name = self.string(names[4] + section[0], names[4] + names[5])
            self.sections[name] = section

        if ".hash" in self.sections:
            raise ValueError("SysV-hashed symbols cannot use these in-place fixups")
        dynsym, dynstr = self.sections[".dynsym"], self.sections[".dynstr"]
        if dynsym[1] != 11 or dynsym[9] != 16 or sections[dynsym[6]] != dynstr:
            raise ValueError("Unsupported dynamic symbol table")
        self.symbols = []
        for index in range(dynsym[5] // dynsym[9]):
            nameoff, _, _, info, _, shndx = struct.unpack_from(
                "<IIIBBH", data, dynsym[4] + index * dynsym[9])
            start = dynstr[4] + nameoff
            name = self.string(start, dynstr[4] + dynstr[5])
            self.symbols.append((index, name, start, info, shndx))

    def string(self, offset, limit):
        if not 0 <= offset < limit <= len(self.data):
            raise ValueError("ELF string is outside its table")
        return bytes(self.data[offset:self.data.index(0, offset, limit)]).decode("ascii")

    def symbol(self, name):
        matches = [symbol for symbol in self.symbols if symbol[1] == name]
        if len(matches) != 1:
            raise ValueError("Expected exactly one dynamic symbol: " + name)
        return matches[0]

    def rename_import(self, old, new):
        index, _, start, info, shndx = self.symbol(old)
        if info != 0x12 or shndx != 0:  # STB_GLOBAL, STT_FUNC, SHN_UNDEF
            raise ValueError("Refusing to rename a non-function import: " + old)
        gnu_hash = self.sections[".gnu.hash"]
        symoffset = struct.unpack_from("<I", self.data, gnu_hash[4] + 4)[0]
        if index >= symoffset:
            raise ValueError("Refusing to rename a GNU-hashed symbol: " + old)
        versions = self.sections[".gnu.version"]
        version = struct.unpack_from("<H", self.data, versions[4] + index * 2)[0]
        if version not in (0, 1):
            raise ValueError("Refusing to rename a versioned import: " + old)

        end = start + len(old) + 1
        for other_index, name, other_start, _, _ in self.symbols:
            if other_index != index and other_start < end and start < other_start + len(name) + 1:
                raise ValueError("Import shares string storage with another symbol: " + old)
        dynamic = self.sections[".dynamic"]
        dynstr = self.sections[".dynstr"]
        for offset in range(dynamic[4], dynamic[4] + dynamic[5], 8):
            tag, value = struct.unpack_from("<iI", self.data, offset)
            if tag in (1, 14, 15, 29):  # NEEDED, SONAME, RPATH, RUNPATH
                other_start = dynstr[4] + value
                name = self.string(other_start, dynstr[4] + dynstr[5])
                if other_start < end and start < other_start + len(name) + 1:
                    raise ValueError("Import shares a dynamic string: " + old)
        replacement = new.encode("ascii") + b"\0"
        if len(replacement) > end - start:
            raise ValueError("Replacement import is too long: " + new)
        self.data[start:end] = replacement.ljust(end - start, b"\0")

    def text_offset(self, vma, size):
        text = self.sections[".text"]
        if not text[3] <= vma or vma + size > text[3] + text[5]:
            raise ValueError("Code fixup is outside .text: " + hex(vma))
        return text[4] + vma - text[3]


def fixup_blob(name, data, check=False):
    if name not in FIXUPS:
        raise ValueError("Unsupported blob: " + name)
    fixup = FIXUPS[name]
    digest = sha256(data)
    if digest == fixup.patched_sha256:
        return data
    if digest != fixup.original_sha256:
        raise ValueError("{}: unsupported SHA-256 {}; no changes made".format(name, digest))
    if check:
        raise ValueError(name + ": original blob still needs the camera ABI fixups")

    elf = Elf32(bytearray(data))
    for old, new in fixup.imports:
        elf.rename_import(old, new)
    for patch in fixup.code:
        if len(patch.original) != len(patch.patched):
            raise ValueError("Code fixups must not change the ELF size")
        offset = elf.text_offset(patch.vma, len(patch.original))
        if elf.data[offset:offset + len(patch.original)] != patch.original:
            raise ValueError("Unexpected instructions at " + hex(patch.vma))
        elf.data[offset:offset + len(patch.original)] = patch.patched
    result = bytes(elf.data)
    if len(result) != len(data) or sha256(result) != fixup.patched_sha256:
        raise ValueError("Internal fixup verification failed for " + name)
    return result


def write_atomic(path, data):
    mode = stat.S_IMODE(path.stat().st_mode)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".",
                                         suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            os.fchmod(output.fileno(), mode)
            output.write(data)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify patched blobs; never write")
    parser.add_argument("blobs", nargs="+", type=Path, help="camera library file paths")
    args = parser.parse_args(argv)
    try:
        # Preflight every input before replacing any file, including --check.
        plans = []
        for path in args.blobs:
            if path.is_symlink():
                raise ValueError("Refusing to replace a symbolic link: " + str(path))
            original = path.read_bytes()
            patched = fixup_blob(path.name, original, args.check)
            plans.append((path, original, patched))
        for path, original, patched in plans:
            if patched != original:
                write_atomic(path, patched)
                print(str(path) + ": patched and SHA-256 verified")
            else:
                print(str(path) + ": already patched; SHA-256 verified")
    except (OSError, ValueError, KeyError, struct.error) as error:
        print("camera fixup: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
