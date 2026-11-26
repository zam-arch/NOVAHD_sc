#!/usr/bin/env bash
set -euo pipefail
out=$(mktemp -d)
trap 'rm -rf -- "$out"' EXIT
cc -std=gnu11 -ffunction-sections -fdata-sections -Iruntime -Itools/recomp/runtime \
    -Wl,--gc-sections tests/native_graphics_v010.c -lm -o "$out/graphics"
"$out/graphics"
javac -d "$out" tests/MusicCatalogWireTest.java app/src/main/java/com/novahd/recomp/MusicCatalog.java
java -cp "$out" com.novahd.recomp.MusicCatalogWireTest "$out/catalog"
cc -std=c11 -Wall -Wextra -Werror tests/native_catalog_wire.c -o "$out/catalog-test"
"$out/catalog-test" "$out/catalog"
