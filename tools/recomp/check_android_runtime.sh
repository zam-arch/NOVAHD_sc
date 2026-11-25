#!/usr/bin/env bash
set -euo pipefail
ndk=${1:?Provide the installed Android NDK directory}
cd "$(dirname "$0")/../.."
compiler="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
sources=()
for source in runtime/*.c; do
    case "$source" in runtime/host_main.c|runtime/audio_win.c) continue ;; esac
    sources+=("$source")
done
mkdir -p build/recovery/check-runtime
for source in "${sources[@]}"; do
    "$compiler" -c -O2 -fPIC -DNOVA_MANAGED_BUNDLE -Iruntime -Itools/recomp/runtime \
        -I"$ndk/sources/android/native_app_glue" "$source" -o "build/recovery/check-runtime/$(basename "${source%.c}").o"
done
glue="$ndk/sources/android/native_app_glue"
"$compiler" -c -O2 -fPIC "$glue/android_native_app_glue.c" -o build/recovery/check-runtime/android_native_app_glue.o
"${compiler%/*}/llvm-nm" build/recovery/check-runtime/android_native_app_glue.o | grep -F ANativeActivity_onCreate
echo "Compiled ${#sources[@]} original runtime translation units and NativeActivity glue for ARM64 (Android API 26)"
