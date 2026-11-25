#!/usr/bin/env python3
"""Stage or verify the official 0.4 engine and campaign for private use."""
import argparse
import hashlib
import shutil
from pathlib import Path
from zipfile import ZipFile

OFFICIAL_SHA = "75622eb6eb70edaeee21b520b64abe746d5e7df439fbbbf4b0fe7906aa471e00"
LIB_SHA = "ab6a0489e69bce594bd9eb32abb42eaf30b73ea577adbc7406b1c7ee9dcfc24c"
IMAGE_SHA = "8800ee114204bca4313c80b3da3fb9e2c6a39575eda2eedc2324dd27b3c65709"
LIB = "lib/arm64-v8a/libnovahd.so"
IMAGE = "assets/image.bin"
BUNDLE = "assets/NovaHD.app/"


def campaign_entries(manifest: str) -> dict[str, tuple[str, int]]:
    result = {}
    for row in manifest.splitlines():
        digest, size, path = row.split("\t")
        name = BUNDLE + path
        if name in result or "\\" in path or any(p in {"", ".", ".."} for p in path.split("/")):
            raise ValueError(f"Invalid campaign path: {path}")
        result[name] = (digest, int(size))
    if len(result) != 44:
        raise ValueError("Expected 44 campaign entries")
    return result


def verify(path: Path, manifest: str) -> None:
    from verify_recovery_apk import verify as verify_recovery
    verify_recovery(path, IMAGE_SHA)
    with ZipFile(path) as apk, apk.open(LIB) as library:
        if hashlib.file_digest(library, "sha256").hexdigest() != LIB_SHA:
            raise ValueError("The packaged native library is not byte-identical to 0.4")
        if apk.read("assets/campaign.tsv").decode("utf-8") != manifest:
            raise ValueError("Unexpected packaged campaign manifest")
        if "assets/bundle.lst" in apk.namelist():
            raise ValueError("The original extractor must leave installation to the manager")


def stage(path: Path, manifest: str, destination: Path) -> None:
    with path.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != OFFICIAL_SHA:
            raise ValueError("Input must match the official N.O.V.A. HD Android 0.4 APK SHA-256")
    with ZipFile(path) as apk:
        entries = campaign_entries(manifest)
        entries[LIB] = (LIB_SHA, 95992992)
        entries[IMAGE] = (IMAGE_SHA, 4517888)
        destination.mkdir(parents=True, exist_ok=False)
        try:
            for name, (digest, size) in entries.items():
                if apk.getinfo(name).file_size != size:
                    raise ValueError(f"Unexpected size for {name}")
                target = destination / (name if name != LIB else "jni/" + name.removeprefix("lib/"))
                target.parent.mkdir(parents=True, exist_ok=True)
                with apk.open(name) as source:
                    if hashlib.file_digest(source, "sha256").hexdigest() != digest:
                        raise ValueError(f"Unexpected SHA-256 for {name}")
                    source.seek(0)
                    with target.open("wb") as out:
                        shutil.copyfileobj(source, out)
        except Exception:
            shutil.rmtree(destination)
            raise
    print("Verified and staged the original ARM64 engine, image and 44 campaign files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--stage", type=Path)
    args = parser.parse_args()
    manifest = args.manifest.read_text(encoding="utf-8")
    if args.stage is None:
        verify(args.apk, manifest)
    else:
        stage(args.apk, manifest, args.stage)
