#!/usr/bin/env bash
set -euo pipefail
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Iruntime tests/native_host_pages.c -o "$scratch/pages"
cc -std=gnu11 -Iruntime -Itools/recomp/runtime tests/native_memory_mapping.c tests/native_relink_fixture.c \
    runtime/mem.c -o "$scratch/mapping"
"$scratch/mapping"
"$scratch/pages"
cc -std=gnu11 -ffunction-sections -fdata-sections -Iruntime -Itools/recomp/runtime \
    -Wl,--gc-sections tests/native_framebuffer.c -o "$scratch/fbo"
"$scratch/fbo"
ndk=28.2.13676358
bin="$ANDROID_HOME/ndk/$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
"$bin/aarch64-linux-android26-clang" -fPIC -Itools/recomp/runtime -c tests/native_dispatch_fixture.c -o "$scratch/shard.o"
cp tests/native_relink_fixture.c "$scratch/image_meta.c"
printf 'test' > "$scratch/image.bin"
PYTHONPATH=tools/recomp python3 - "$scratch" "$ndk" <<'PY'
import sys
from pathlib import Path
from relink_inputs import export
scratch = Path(sys.argv[1])
export(scratch / "inputs", [scratch / "shard.o"], scratch, "aarch64-linux-android26", False, sys.argv[2])
PY
python3 tools/recomp/build_apk.py --relink-inputs "$scratch/inputs" --out "$scratch/link" \
  --ndk-version "$ndk" --native-only --frame-log
"$bin/llvm-readelf" --dyn-syms "$scratch/link/libnovahd.so" > "$scratch/symbols"
grep -E 'ANativeActivity_onCreate|rt_gl_measure_render' "$scratch/symbols"
if python3 tools/recomp/build_apk.py --relink-inputs "$scratch/inputs" --out "$scratch/reject-image" \
    --ndk-version "$ndk" --native-only --expected-image-sha256 "$(printf '%064d' 0)" > "$scratch/image-error" 2>&1; then
    exit 1
fi
grep -F 'Packed image SHA-256 differs from the approved build' "$scratch/image-error"
test ! -e "$scratch/reject-image/libnovahd.so"
printf 'tampered' >> "$scratch/inputs/shard.o"
if python3 tools/recomp/build_apk.py --relink-inputs "$scratch/inputs" --out "$scratch/reject" \
    --ndk-version "$ndk" --native-only > "$scratch/error" 2>&1; then
    exit 1
fi
grep -F 'SHA-256 mismatch: shard.o' "$scratch/error"
