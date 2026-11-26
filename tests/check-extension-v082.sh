#!/usr/bin/env bash
set -euo pipefail
out=$(mktemp -d)
trap 'rm -rf -- "$out"' EXIT
flags=(-std=gnu11 -Iruntime -Itools/recomp/runtime -ffunction-sections -fdata-sections)
cc "${flags[@]}" -shared -fPIC tests/music_selector_extension.c -o "$out/extension.so"
cc "${flags[@]}" -Wl,--gc-sections,--export-dynamic-symbol=rt_objc_method \
    tests/music_selector_host.c tests/native_relink_fixture.c runtime/{core,objc,mem}.c \
    -ldl -pthread -o "$out/host"
"$out/host" "$out/extension.so"
jdk=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
fixture=tests/window-probe
cc -shared -fPIC -D_GNU_SOURCE -Wall -Wextra -Werror -I"$fixture" -I"$jdk/include" \
   -I"$jdk/include/linux" app/src/extension/window_probe.c -ldl -o "$out/libnova_probe.so"
printf '%s\n' 'void ANativeActivity_onCreate(void) {}' 'void rt_platform_swap(void) {}' | \
  cc -x c -shared -fPIC -Wl,-soname,libnovahd.so -o "$out/libnovahd.so" -
"$jdk/bin/javac" -d "$out" app/src/main/java/com/novahd/recomp/NativeWindowProbe.java \
   "$fixture/android/view/Surface.java" "$fixture/com/novahd/recomp/WindowProbeTest.java"
"$jdk/bin/java" -Djava.library.path="$out" -cp "$out" com.novahd.recomp.WindowProbeTest
