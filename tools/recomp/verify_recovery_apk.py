#!/usr/bin/env python3
"""Reject a Recovery APK that lacks the original verified packed image or a 16 KiB-aligned ARM64 engine."""
import argparse
import hashlib
import struct
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile


def verify(apk: Path, expected_image_sha256: str) -> None:
    with ZipFile(apk) as archive:
        library = archive.read("lib/arm64-v8a/libnovahd.so")
        if library[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", library, 18)[0] != 183:
            raise ValueError("libnovahd.so is not a little-endian ARM64 ELF")
        phoff = struct.unpack_from("<Q", library, 32)[0]
        size, count = struct.unpack_from("<HH", library, 54)
        loads = [struct.unpack_from("<IIQQQQQQ", library, phoff + i * size) for i in range(count)]
        alignments = [item[-1] for item in loads if item[0] == 1]
        if not alignments or min(alignments) < 16384:
            raise ValueError("libnovahd.so has LOAD segments aligned below 16 KiB")
        image = archive.getinfo("assets/image.bin")
        with archive.open(image) as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        if image.compress_type != ZIP_STORED or digest != expected_image_sha256:
            raise ValueError("image.bin must be stored uncompressed and match the verified original")
        for name in ("classes.dex", "assets/campaign.tsv"):
            if archive.getinfo(name).file_size == 0:
                raise ValueError(f"Missing required content: {name}")
        manifest = archive.read("assets/campaign.tsv").decode("utf-8")
        paths = set()
        for row in manifest.splitlines():
            expected_hash, size, path = row.split("\t")
            name = "assets/NovaHD.app/" + path
            if name in paths or archive.getinfo(name).file_size != int(size):
                raise ValueError(f"Duplicate or incompatible campaign file: {path}")
            with archive.open(name) as source:
                if hashlib.file_digest(source, "sha256").hexdigest() != expected_hash:
                    raise ValueError(f"Campaign SHA-256 mismatch: {path}")
            paths.add(name)
        entries = [name for name in archive.namelist() if name.startswith("assets/NovaHD.app/")]
        if len(entries) != len(paths) or set(entries) != paths or len(paths) != 44:
            raise ValueError("Incomplete or unexpected campaign content")
        print(f"Verified {apk}: AArch64, 16 KiB PT_LOAD, reference image and {len(paths)} campaign assets")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("image_sha256")
    args = parser.parse_args()
    verify(args.apk, args.image_sha256)
