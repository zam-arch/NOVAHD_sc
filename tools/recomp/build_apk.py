#!/usr/bin/env python3
"""Build the 64-bit Android APK: the translated executable and runtime/ for arm64-v8a, in a NativeActivity app.

  py -3 tools/recomp/build_apk.py --exe NovaHD [--checked] [--out build/apk] [--install] [--push NovaHD.app]

The APK (package com.novahd.recomp, label "N.O.V.A. HD") holds one library, lib/arm64-v8a/libnovahd.so (the
translated code compiled for aarch64 with the NDK's clang, the runtime with runtime/android_main.c and
runtime/audio_android.c in place of host_main.c and audio_win.c, and the NDK's native_app_glue), the packed image as
assets/image.bin, and a manifest with no Java code (android.app.NativeActivity, landscape). It is signed with a
debug key made once in <out>/debug.keystore.

Needs the Android SDK (build-tools, a platform, the NDK) under ANDROID_HOME, ANDROID_SDK_ROOT, C:\\Android\\Sdk or
%LOCALAPPDATA%\\Android\\Sdk, and a JDK (JAVA_HOME, or Microsoft's JDK under Program Files) for keytool/apksigner.
--checked builds the translated code and the runtime with RECOMP_CHECKED (bounds checks and the game call chain in
crash reports; slower). The translated objects are cached in build/recomp-android[-checked], so a rebuild only
recompiles what changed; the first build compiles every shard and takes a while.

The APK does not contain the game. Copy the bundle (the NovaHD.app folder of the IPA) to the device once:
  adb push <...>/Payload/NovaHD.app /sdcard/Android/data/com.novahd.recomp/files/
(or pass --push <NovaHD.app folder>; the app must have been installed first so the folder exists). Logs:
  adb logcat -s novahd
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

from relink_inputs import export, restore

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = "com.novahd.recomp"
LIBNAME = "novahd"
API = 26  # AAudio, ASensorManager_getInstanceForPackage


def runtime_sources() -> list[str]:
    """build_runner.RUNTIME_SRC (the Windows runner's list) with the Android front end and audio back end."""
    known = ["core", "mem", "printf", "pthread", "libc", "cxx", "libstdcxx", "objc", "foundation", "collections",
             "system", "uikit", "graphics", "offline", "gles1", "openal", "audio_win", "host_main"]
    try:
        from build_runner import RUNTIME_SRC
    except Exception:  # noqa: BLE001 - fall back to the known module list
        RUNTIME_SRC = known
    swap = {"host_main": "android_main", "audio_win": "audio_android"}
    srcs = [swap.get(n, n) for n in RUNTIME_SRC]
    # a module of the framework that build_runner's list does not name yet (it lags behind while modules are
    # added) is still linked when its source exists: android_main calls every *_define of rt.h
    for n in [swap.get(n, n) for n in known]:
        if n not in srcs and (ROOT / "runtime" / f"{n}.c").exists():
            srcs.append(n)
    return srcs


MANIFEST = f"""<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{PACKAGE}"
    android:versionCode="4" android:versionName="0.4">
  <uses-sdk android:minSdkVersion="{API}" android:targetSdkVersion="34" />
  <uses-feature android:glEsVersion="0x00010001" android:required="true" />
  <uses-feature android:name="android.hardware.sensor.accelerometer" android:required="false" />
  <uses-feature android:name="android.hardware.touchscreen.multitouch.distinct" android:required="false" />
  <application android:label="N.O.V.A. HD" android:hasCode="false" android:extractNativeLibs="true"
      android:theme="@style/Game">
    <activity android:name="android.app.NativeActivity" android:exported="true" android:launchMode="singleTask"
        android:screenOrientation="sensorLandscape"
        android:configChanges="orientation|keyboardHidden|keyboard|screenSize|smallestScreenSize|screenLayout|navigation|uiMode|density">
      <meta-data android:name="android.app.lib_name" android:value="{LIBNAME}" />
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
  </application>
</manifest>
"""


def sdk_root() -> Path:
    for v in (os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"), r"C:\Android\Sdk",
              os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"),
              str(Path.home() / "AppData" / "Local" / "Android" / "Sdk")):
        if v and Path(v, "build-tools").exists():
            return Path(v)
    sys.exit("Android SDK not found (set ANDROID_HOME)")


def newest(folder: Path) -> Path:
    items = [p for p in folder.iterdir() if p.is_dir()]
    if not items:
        sys.exit(f"nothing in {folder}")
    return sorted(items, key=lambda p: [int(x) if x.isdigit() else 0 for x in p.name.replace("-", ".").split(".")])[-1]


def java_env() -> dict:
    env = dict(os.environ)
    if not env.get("JAVA_HOME"):
        jdks = sorted(Path(r"C:\Program Files\Microsoft").glob("jdk-*")) if os.name == "nt" else []
        if not jdks:
            sys.exit("no JDK found (set JAVA_HOME)")
        env["JAVA_HOME"] = str(jdks[-1])
    if os.name == "nt":  # the JDK's AF_UNIX loopback socket fails under long temp paths on Windows
        Path("C:/jtmp").mkdir(exist_ok=True)
        env["JAVA_TOOL_OPTIONS"] = "-Djdk.net.unixdomain.tmpdir=C:\\jtmp"
    return env


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        raise RuntimeError(f"{Path(str(cmd[0])).name} failed:\n{(r.stdout + r.stderr)[-4000:]}")
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    inputs = ap.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--exe", type=Path, help="the original NovaHD executable (Mach-O)")
    inputs.add_argument("--relink-inputs", type=Path, help="private bundle exported from a matching Android build")
    ap.add_argument("--export-relink-inputs", type=Path, help="save exact translated objects and image metadata")
    ap.add_argument("--frame-log", action="store_true", help="log native swap timing and resolutions")
    ap.add_argument("--out", type=Path, default=Path("build/apk"))
    ap.add_argument("--checked", action="store_true", help="RECOMP_CHECKED translated code and runtime")
    ap.add_argument("--bundle", type=Path, help="pack this NovaHD.app folder into the APK (extracted on first run)")
    ap.add_argument("--icon", type=Path, help="launcher icon, a PNG or JPEG (default: the iTunesArtwork beside the "
                                              "Payload folder of --bundle)")
    ap.add_argument("--native-only", action="store_true",
                    help="build the ARM64 library and image for the Gradle resource manager")
    ap.add_argument("--ndk-version", help="select an exact installed NDK version")
    ap.add_argument("--runtime-only", action="store_true", help="reuse the translated objects of the last build")
    ap.add_argument("--install", action="store_true", help="adb install -r the APK afterwards")
    ap.add_argument("--push", type=Path, help="adb push this NovaHD.app folder to the app's files folder")
    args = ap.parse_args()
    if args.runtime_only and (args.relink_inputs or args.export_relink_inputs):
        ap.error("--runtime-only is the legacy cache mode and cannot be combined with relink bundles")
    t0 = time.time()
    sdk = sdk_root()
    ndk = sdk / "ndk" / args.ndk_version if args.ndk_version else newest(sdk / "ndk")
    if not ndk.is_dir():
        ap.error(f"NDK not installed: {ndk}")
    if args.exe and not args.exe.is_file():
        ap.error("the original iPad NovaHD Mach-O is required")
    bt = newest(sdk / "build-tools")
    android_jar = newest(sdk / "platforms") / "android.jar"
    host = "windows-x86_64" if os.name == "nt" else ("darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64")
    tc = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    clang = tc / ("clang.exe" if os.name == "nt" else "clang")
    triple = f"aarch64-linux-android{API}"
    print(f"NDK {ndk.name}, build-tools {bt.name}, {android_jar.parent.name}")

    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    if args.exe:
        run([sys.executable, ROOT / "tools/recomp/pack_image.py", "--exe", args.exe, "--out", out])

    opt = ["-O1", "-fPIC", "-g0", "-DRECOMP_CHECKED"] if args.checked else ["-O2", "-fPIC", "-g0"]
    work = Path("build/recomp-android" + ("-checked" if args.checked else ""))
    if args.relink_inputs:
        try:
            objs = restore(args.relink_inputs, out, triple, args.checked, ndk.name)
        except (OSError, ValueError, KeyError, TypeError) as error:
            ap.error(f"Cannot relink: {error}")
    elif args.runtime_only:
        newest_obj = {}  # one object per source: names carry a content hash, older ones stay behind
        for o in work.glob("*.o"):
            k = o.name.split(".")[0]
            if k not in newest_obj or o.stat().st_mtime > newest_obj[k].stat().st_mtime:
                newest_obj[k] = o
        objs = sorted(newest_obj.values())
    else:
        # the translated code, as for the Windows runner (variant "runtime": rc_import/rc_hostcall come from runtime/)
        from difftest import compile_objects
        from lift import Lifter, Unsupported, entries
        from macho import Image

        image = Image(args.exe)
        ents = entries(image)
        lifter = Lifter(image, ents)
        sources = {}
        for a, f in ents.items():
            try:
                sources[a] = lifter.lift(f)
            except Unsupported:
                pass
        objs = compile_objects(image, ents, sources, work, target=(triple, opt, "unused", [clang]), variant="runtime")
    if args.export_relink_inputs:
        export(args.export_relink_inputs, objs, out, triple, args.checked, ndk.name)
    print(f"translated code: {len(objs)} objects, {time.time() - t0:.0f}s")

    # the runtime, the glue, image_meta.c
    glue = ndk / "sources/android/native_app_glue"
    flags = ["-target", triple, "-O2", "-fPIC", "-g", "-Wall", "-Wno-unused-parameter", f"-I{ROOT / 'runtime'}",
             f"-I{ROOT / 'tools/recomp/runtime'}", f"-I{glue}"] + (["-DRECOMP_CHECKED"] if args.checked else [])
    if args.frame_log:
        flags.append("-DNOVA_FRAME_LOG")
    if args.native_only:
        flags.append("-DNOVA_MANAGED_BUNDLE")
    rt_dir = out / ("obj-checked" if args.checked else "obj")
    rt_dir.mkdir(exist_ok=True)
    names = runtime_sources()
    print("runtime:", " ".join(names))
    srcs = [ROOT / "runtime" / f"{n}.c" for n in names] + [out / "image_meta.c", glue / "android_native_app_glue.c"]

    def cc(src):
        obj = rt_dir / (src.stem + ".o")
        r = run([clang, "-c", *flags, src, "-o", obj])
        if r.stderr.strip():
            print(r.stderr.strip()[-2000:])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        rt_objs = list(pool.map(cc, srcs))
    lib = out / f"lib{LIBNAME}.so"
    run([clang, "-target", triple, "-shared", "-o", lib, *objs, *rt_objs, "-llog", "-landroid", "-lEGL",
         "-lGLESv1_CM", "-laaudio", "-ldl", "-lm", "-Wl,-u,ANativeActivity_onCreate", "-Wl,--no-undefined",
         "-Wl,-z,max-page-size=16384", "-s"])
    print(f"{lib}: {lib.stat().st_size} bytes")

    if args.native_only:
        return

    # package: manifest + assets/image.bin + lib/arm64-v8a/libnovahd.so, aligned and signed
    exe = ".exe" if os.name == "nt" else ""
    bat = ".bat" if os.name == "nt" else ""
    icon = args.icon
    if not icon and args.bundle and (args.bundle.resolve().parent.parent / "iTunesArtwork").is_file():
        icon = args.bundle.resolve().parent.parent / "iTunesArtwork"
    manifest = MANIFEST
    res = out / "res"
    shutil.rmtree(res, ignore_errors=True)
    # the theme: full screen, and from Android 9 the window also covers the strip beside a camera cutout
    for folder, item in (("values", ""), ("values-v28",
                         '<item name="android:windowLayoutInDisplayCutoutMode">shortEdges</item>')):
        (res / folder).mkdir(parents=True)
        (res / folder / "styles.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
            f'  <style name="Game" parent="@android:style/Theme.NoTitleBar.Fullscreen">{item}</style>\n'
            '</resources>\n', encoding="utf-8")
    if icon:
        # the art as it is for old launchers; for adaptive ones it fills the visible 72 of 108 dp on black
        ext = ".png" if icon.read_bytes()[:4] == b"\x89PNG" else ".jpg"
        (res / "mipmap-xxxhdpi").mkdir(parents=True)
        (res / "mipmap-anydpi-v26").mkdir()
        shutil.copyfile(icon, res / "mipmap-xxxhdpi" / f"ic_launcher{ext}")
        shutil.copyfile(icon, res / "mipmap-xxxhdpi" / f"ic_art{ext}")
        (res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '  <background android:drawable="@android:color/black" />\n'
            '  <foreground><inset android:drawable="@mipmap/ic_art" android:inset="16.7%" /></foreground>\n'
            '</adaptive-icon>\n', encoding="utf-8")
        manifest = manifest.replace("<application ", '<application android:icon="@mipmap/ic_launcher" ', 1)
        print(f"icon: {icon.name}")
    res_zip = [out / "res.zip"]
    run([bt / f"aapt2{exe}", "compile", "--dir", res, "-o", res_zip[0]])
    (out / "AndroidManifest.xml").write_text(manifest, encoding="utf-8")
    assets = out / "assets"
    assets.mkdir(exist_ok=True)
    shutil.copyfile(out / "image.bin", assets / "image.bin")
    unsigned = out / "unsigned.apk"
    # -0 bin: image.bin stays uncompressed, so AAsset_getBuffer maps it instead of inflating it
    run([bt / f"aapt2{exe}", "link", "-o", unsigned, "-I", android_jar, "--manifest", out / "AndroidManifest.xml",
         "-A", assets, "-0", ".bin", *res_zip])
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(lib, f"lib/arm64-v8a/lib{LIBNAME}.so")
    if args.bundle:  # stored, not deflated: the data packs are already compressed
        skip = {"NovaHD", "embedded.mobileprovision", "PkgInfo"}
        files = [f for f in sorted(args.bundle.rglob("*")) if f.is_file() and f.name not in skip
                 and "_CodeSignature" not in f.parts and "SC_Info" not in f.parts]
        with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_STORED) as z:
            z.writestr("assets/bundle.lst", "".join(f"{f.stat().st_size} {f.relative_to(args.bundle).as_posix()}\n"
                                                    for f in files))
            for f in files:
                z.write(f, "assets/NovaHD.app/" + f.relative_to(args.bundle).as_posix())
        print(f"bundle: {len(files)} files, {sum(f.stat().st_size for f in files) >> 20} MB packed as assets")
    aligned = out / "aligned.apk"
    run([bt / f"zipalign{exe}", "-f", "-P", "16", "4", unsigned, aligned])
    env = java_env()
    keystore = out / "debug.keystore"
    if not keystore.exists():
        keytool = Path(env["JAVA_HOME"]) / "bin" / f"keytool{exe}"
        run([keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android", "-alias",
             "novahd", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=novahd-recomp debug"],
            env=env)
    apk = out / "novahd-recomp.apk"
    run([bt / f"apksigner{bat}", "sign", "--ks", keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android",
         "--ks-key-alias", "novahd", "--out", apk, aligned], env=env)
    run([bt / f"apksigner{bat}", "verify", apk], env=env)
    print(f"{apk}: {apk.stat().st_size} bytes, signed and verified, {time.time() - t0:.0f}s")

    adb = sdk / "platform-tools" / f"adb{exe}"
    if args.install:
        run([adb, "install", "-r", apk])
        print("installed")
    if args.push:
        dest = f"/sdcard/Android/data/{PACKAGE}/files/"
        run([adb, "shell", "mkdir", "-p", dest])
        run([adb, "push", args.push, dest])
        print(f"pushed {args.push} to {dest}")


if __name__ == "__main__":
    main()
