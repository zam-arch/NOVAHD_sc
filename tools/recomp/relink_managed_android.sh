#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 PRIVATE_REFERENCE_DIR NEW_PRIVATE_OUTPUT_DIR" >&2
    exit 2
fi
reference=$(realpath "$1")
output=$(realpath -m "$2")
mkdir -m 700 "$output"
trap 'rm -rf -- "$output"' ERR
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
export PYTHONUNBUFFERED=1
python3 tools/recomp/build_apk.py --relink-inputs "$reference/relink-inputs" \
    --out "$output/native" --ndk-version 28.2.13676358 --native-only --frame-log \
    --expected-image-sha256 8800ee114204bca4313c80b3da3fb9e2c6a39575eda2eedc2324dd27b3c65709
mkdir -p "$output/android/jni/arm64-v8a" "$output/android/assets"
mv "$output/native/libnovahd.so" "$output/android/jni/arm64-v8a/"
mv "$output/native/image.bin" "$output/android/assets/"
cp -a --reflink=auto "$reference/android/assets/NovaHD.app" "$output/android/assets/"
