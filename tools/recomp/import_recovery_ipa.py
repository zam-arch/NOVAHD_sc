#!/usr/bin/env python3
"""Generate private Recovery relink inputs from a legitimate compatible iPad IPA."""
import argparse
import hmac
import re
import shutil
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from prepare_recovery import ARCHIVE_LIMIT, prepare
from relink_inputs import digest
from stage_official_apk import IMAGE_SHA

ROOT = Path(__file__).resolve().parents[2]


def run(ipa: Path, expected_sha: str, output: Path, ndk: str) -> None:
    if not re.fullmatch(r"[a-fA-F0-9]{64}", expected_sha):
        raise ValueError("Supply the expected 64-character IPA SHA-256")
    if ipa.stat().st_size > ARCHIVE_LIMIT:
        raise ValueError("IPA exceeds the 512 MiB limit")
    if not hmac.compare_digest(digest(ipa), expected_sha.lower()):
        raise ValueError("IPA SHA-256 mismatch")
    if output.exists():
        raise ValueError("Choose a new private output directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=output.parent, prefix=".nova-ipa-") as temporary:
        scratch = Path(temporary)
        result = scratch / "result"
        android_assets = result / "android/assets"
        native = scratch / "native"
        exe = scratch / "NovaHD"
        prepare(ipa, ROOT / "app/src/main/assets/campaign.tsv", exe, android_assets)
        subprocess.run([sys.executable, str(ROOT / "tools/recomp/build_apk.py"),
                        "--exe", str(exe), "--native-only", "--out", str(native), "--ndk-version", ndk,
                        "--expected-image-sha256", IMAGE_SHA, "--export-relink-inputs",
                        str(result / "relink-inputs")], cwd=ROOT, check=True)
        library = result / "android/jni/arm64-v8a"
        library.mkdir(parents=True)
        shutil.move(native / "libnovahd.so", library)
        shutil.move(native / "image.bin", android_assets)
        result.rename(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ndk-version", default="28.2.13676358")
    args = parser.parse_args()
    try:
        run(args.ipa.resolve(), args.sha256, args.out.resolve(), args.ndk_version)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Import failed: {error}\n")
