#!/usr/bin/env python3
"""Verify a legitimate original IPA against the campaign manifest and extract its Mach-O for a private build."""
import argparse
import hashlib
import shutil
import struct
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

PREFIX = "Payload/NovaHD.app/"
ARCHIVE_LIMIT = 512 * 1024 * 1024


def verify_macho(source, file_size: int) -> None:
    header = source.read(28)
    if (len(header) != 28 or header[:12] != bytes.fromhex("cefaedfe0c00000006000000")
            or header[12:16] != bytes.fromhex("02000000")):
        raise ValueError("Expected a thin armv6 Mach-O executable")
    ncmds, size = struct.unpack_from("<II", header, 16)
    if not ncmds or size > file_size - 28 or ncmds > size // 8:
        raise ValueError("Invalid Mach-O load-command table")
    commands = source.read(size)
    if len(commands) != size:
        raise ValueError("Incomplete Mach-O load-command table")
    offset = 0
    for _ in range(ncmds):
        if size - offset < 8:
            raise ValueError("Truncated Mach-O load command")
        cmd, length = struct.unpack_from("<II", commands, offset)
        if length < 8 or length % 4 or length > size - offset:
            raise ValueError("Invalid Mach-O load-command size")
        if cmd == 0x21:  # LC_ENCRYPTION_INFO in a 32-bit Mach-O
            if length < 20:
                raise ValueError("Truncated LC_ENCRYPTION_INFO")
            if struct.unpack_from("<I", commands, offset + 16)[0] != 0:
                raise ValueError("Encrypted Mach-O (cryptid != 0); an authorized unencrypted copy is required")
        offset += length
    if offset != size:
        raise ValueError("Mach-O load-command table length mismatch")


def prepare(ipa: Path, manifest: Path, destination: Path, assets: Path) -> None:
    if ipa.stat().st_size > ARCHIVE_LIMIT:
        raise ValueError("IPA exceeds the 512 MiB limit")
    with ZipFile(ipa) as archive:
        names = archive.namelist()
        name = PREFIX + "NovaHD"
        if names.count(name) != 1 or not 0 < archive.getinfo(name).file_size <= 32 * 1024 * 1024:
            raise ValueError("The IPA must contain the original iPad executable at " + name)
        with archive.open(name) as source:
            verify_macho(source, archive.getinfo(name).file_size)
            source.seek(0)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("wb") as output:
                shutil.copyfileobj(source, output)
        for line in manifest.read_text(encoding="utf-8").splitlines():
            digest, size, path = line.split("\t")
            relative = PurePosixPath(path)
            if relative.is_absolute() or ".." in relative.parts or "\\" in path:
                raise ValueError(f"Invalid campaign manifest path: {path}")
            name = PREFIX + path
            if names.count(name) != 1 or archive.getinfo(name).file_size != int(size):
                raise ValueError(f"Missing, duplicate or incompatible campaign file: {path}")
            with archive.open(name) as source:
                if hashlib.file_digest(source, "sha256").hexdigest() != digest:
                    raise ValueError(f"Campaign SHA-256 mismatch: {path}")
                source.seek(0)
                asset = assets / "NovaHD.app" / relative
                asset.parent.mkdir(parents=True, exist_ok=True)
                with asset.open("wb") as output:
                    shutil.copyfileobj(source, output)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    prepare(args.ipa, args.manifest, args.output, args.assets)
