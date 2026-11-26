#!/usr/bin/env bash
set -euo pipefail
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT

# Demonstrate the difference between a normal dynamic import and lookup on a specific handle.
printf '%s\n' 'int submit(void) { return 1; }' > "$scratch/original.c"
printf '%s\n' 'int submit(void) { return 2; }' > "$scratch/wrapper.c"
cat > "$scratch/client.c" <<'C'
#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
extern int submit(void);
int main(void) {
    assert(submit() == (getenv("LD_PRELOAD") ? 2 : 1));
    void *module = dlopen("liboriginal.so", RTLD_NOW | RTLD_NOLOAD);
    assert(module);
    int (*explicit_handle)(void) = (int (*)(void))dlsym(module, "submit");
    assert(explicit_handle && explicit_handle() == 1);
    dlclose(module);
}
C
cc -fPIC -shared "$scratch/original.c" -o "$scratch/liboriginal.so"
cc -fPIC -shared "$scratch/wrapper.c" -o "$scratch/libwrapper.so"
cc "$scratch/client.c" -L"$scratch" -loriginal -Wl,-rpath,"$scratch" -ldl -o "$scratch/client"
env -u LD_PRELOAD "$scratch/client"
LD_PRELOAD="$scratch/libwrapper.so" "$scratch/client"
echo "Synthetic Linux interposition: normal import intercepted; explicit dlsym handle retained original"

PYTHONPATH=tools/recomp python3 - "$scratch" <<'PY'
import hashlib
import io
import struct
import sys
from pathlib import Path
from zipfile import ZipFile

from import_recovery_ipa import run
from prepare_recovery import verify_macho
from relink_inputs import export, restore

scratch = Path(sys.argv[1])

def reject(function, expected):
    try:
        function()
    except ValueError as error:
        assert expected in str(error), error
    else:
        raise AssertionError(f"Missing rejection: {expected}")

def macho(cryptid):
    return (struct.pack("<7I", 0xfeedface, 12, 6, 2, 1, 20, 0)
            + struct.pack("<5I", 0x21, 20, 0, 0, cryptid))

raw = macho(0)
verify_macho(io.BytesIO(raw), len(raw))
encrypted = macho(1)
reject(lambda: verify_macho(io.BytesIO(encrypted), len(encrypted)), "cryptid")
reject(lambda: verify_macho(io.BytesIO(raw[:-1]), len(raw)), "Incomplete")
reject(lambda: verify_macho(io.BytesIO(b"not Mach-O"), 10), "thin armv6")
ipa = scratch / "fixture.ipa"
with ZipFile(ipa, "w") as archive:
    archive.writestr("Payload/NovaHD.app/NovaHD", encrypted)
output = scratch / "unpublished"
reject(lambda: run(ipa, "0" * 64, output, "28.2.13676358"), "IPA SHA-256 mismatch")
reject(lambda: run(ipa, hashlib.sha256(ipa.read_bytes()).hexdigest(), output, "28.2.13676358"), "cryptid")
assert not output.exists()
# A SHA-256 manifest does not make a shared object a relocatable input.
for name in ("image_meta.c", "image.bin"):
    (scratch / name).write_bytes(b"test")
fake_object = scratch / "linked.o"
fake_object.write_bytes(b"\x7fELF\x02\x01" + bytes(10) + b"\x03\x00\xb7\x00")
bundle = scratch / "bundle"
export(bundle, [fake_object], scratch, "aarch64-linux-android26", False, "28.2.13676358")
reject(lambda: restore(bundle, scratch, "aarch64-linux-android26", False, "28.2.13676358"),
       "Expected a relocatable AArch64 ELF object")
print("IPA/Mach-O preflight and rejection of linked ELF passed")
PY
