#!/usr/bin/env python3
"""Checks that every symbol the redirect asks for is a symbol bionic exports.

## Why this exists

A GOT hook that asks for a name nothing exports patches nothing, and the diagnostic for
that is indistinguishable from the healthy case:

    io_redirect: nothing in this process imports: __openat, open64, creat64, …

`open64` and `creat64` are real symbols that this particular process happens not to use.
`__openat` was not: bionic exports `__open_2` and `__openat_2`, and the name in the table
had been written from memory. It sat there for four phone runs, in the same line as the
symbols it was mistaken for, and the eighteenth run is what it cost — a Unity game
compiled with `_FORTIFY_SOURCE`, whose two-argument `open` becomes `__open_2`, could not
open the APK path UNIQUE had just published to it, and told its player the device was out
of storage.

So the names are checked against the NDK's own `libc.so` rather than against anybody's
memory. It is the same rule as ARCHITECTURE §18.8 — every shim verifies its binding —
applied to the one hook that had no way to.

## What it needs

`ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, or an `ndk/` under `ANDROID_HOME`, and
`llvm-readelf` from that NDK or from the host. Without them it says so and exits 0: this
suite is meant to run on a machine with no Android toolchain at all.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCE = os.path.join(ROOT, "..", "core", "native", "src", "main", "cpp", "io_redirect.cpp")

# `{"open", reinterpret_cast<void*>(h_open), …}` — the first field of a HookRequest.
REQUEST = re.compile(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*reinterpret_cast<void\*>')

# Asked for on purpose and not libc's: the loader entry points the library-load watch
# hooks, which live in the dynamic linker.
NOT_LIBC = {"android_dlopen_ext", "dlopen"}


def ndk_root() -> str | None:
    for var in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        path = os.environ.get(var)
        if path and os.path.isdir(path):
            return path
    home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not home:
        return None
    ndk = os.path.join(home, "ndk")
    if not os.path.isdir(ndk):
        return None
    versions = sorted(os.listdir(ndk))
    return os.path.join(ndk, versions[-1]) if versions else None


def libc_path(root: str) -> str | None:
    base = os.path.join(root, "toolchains", "llvm", "prebuilt", "linux-x86_64", "sysroot",
                        "usr", "lib", "aarch64-linux-android")
    if not os.path.isdir(base):
        return None
    # The highest API level the NDK ships a stub for. A symbol added in a later release
    # than the one UNIQUE targets would fail here, which is the right way round.
    levels = sorted((d for d in os.listdir(base) if d.isdigit()), key=int)
    for level in reversed(levels):
        candidate = os.path.join(base, level, "libc.so")
        if os.path.isfile(candidate):
            return candidate
    return None


def exported(readelf: str, libc: str) -> set[str]:
    out = subprocess.run([readelf, "--dyn-syms", libc], capture_output=True, text=True)
    names = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        name = parts[-1]
        if name in ("Name", ""):
            continue
        names.add(name.split("@")[0])
    return names


def main() -> int:
    with open(SOURCE, encoding="utf-8") as f:
        requested = sorted({m.group(1) for m in REQUEST.finditer(f.read())} - NOT_LIBC)
    if not requested:
        print("FAIL  no hook requests found in io_redirect.cpp — has the table moved?")
        return 1

    root = ndk_root()
    libc = libc_path(root) if root else None
    readelf = shutil.which("llvm-readelf") or shutil.which("readelf")
    if libc is None or readelf is None:
        print(f"SKIP  {len(requested)} symbol name(s) unchecked: "
              f"{'no NDK' if libc is None else 'no readelf'} on this machine")
        return 0

    names = exported(readelf, libc)
    missing = [s for s in requested if s not in names]
    for symbol in missing:
        print(f"FAIL  the redirect asks for '{symbol}', which bionic does not export — "
              f"it can never match, and reports as a symbol nothing imports")
    if missing:
        return 1
    print(f"{len(requested)} checks, 0 failures  "
          f"(every redirect symbol is exported by {os.path.basename(os.path.dirname(libc))} bionic)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
