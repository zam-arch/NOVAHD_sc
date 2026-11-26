"""Record and verify the private, relocatable inputs used to link an Android runtime."""
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ABI = ROOT / "tools/recomp/runtime/recomp.h"


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def export(directory, objects, build, triple, checked, ndk):
    directory.mkdir(parents=True, exist_ok=False)
    files = {}
    for source in [*objects, build / "image_meta.c", build / "image.bin"]:
        if source.name in files:
            raise ValueError(f"Duplicate relink input: {source.name}")
        target = directory / source.name
        shutil.copyfile(source, target)
        files[source.name] = digest(target)
    manifest = dict(version=1, triple=triple, checked=checked, ndk=ndk, abi=digest(ABI),
                    files=files, objects=[p.name for p in objects])
    (directory / "relink.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def restore(directory, build, triple, checked, ndk):
    manifest = json.loads((directory / "relink.json").read_text(encoding="utf-8"))
    expected = dict(version=1, triple=triple, checked=checked, ndk=ndk, abi=digest(ABI))
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise ValueError("Relink bundle version, target, NDK, checked mode or translated ABI differs")
    objects = manifest["objects"]
    files = manifest["files"]
    if (not isinstance(objects, list) or not isinstance(files, dict) or not objects
            or any(not isinstance(name, str) for name in objects) or len(set(objects)) != len(objects)
            or set(files) != set(objects) | {"image_meta.c", "image.bin"}):
        raise ValueError("Incomplete or duplicate relink inputs")
    for name, checksum in files.items():
        if Path(name).name != name or "\\" in name:
            raise ValueError("Invalid relink input path")
        path = directory / name
        if digest(path) != checksum:
            raise ValueError(f"Relink input SHA-256 mismatch: {name}")
        if name in objects:
            with path.open("rb") as stream:
                header = stream.read(20)
            if (not name.endswith(".o") or header[:6] != b"\x7fELF\x02\x01"
                    or header[16:20] != b"\x01\x00\xb7\x00"):
                raise ValueError(f"Expected a relocatable AArch64 ELF object: {name}")
    for name in ("image_meta.c", "image.bin"):
        if (directory / name).resolve() != (build / name).resolve():
            shutil.copyfile(directory / name, build / name)
    return [directory / name for name in objects]
