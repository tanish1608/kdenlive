# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Build a scoped Qt 6.11.1 Cocoa ownership fix; never replace Craft libraries."""

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path, PurePosixPath

VERSION = "6.11.1"
SOURCE = "59c81a3c2247b821b9b84b4eb8d939b77e07e276"
PREFIX = "src/plugins/platforms/cocoa/"
TOOLS = Path(__file__).resolve().parent / "qt-cocoa"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sdk_identity(craft: Path) -> dict:
    return {name: sha256(craft / f"lib/{name}.framework/Versions/A/{name}")
            for name in ("QtCore", "QtGui")}


def download(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read()


def validate(root: Path, craft: Path) -> Path:
    """Refuse a stale, modified or differently configured local plugin."""
    version = subprocess.check_output([craft / "bin/qmake", "-query", "QT_VERSION"], text=True).strip()
    record = json.loads((root / "manifest.json").read_text())
    library = root / "platforms/libqcocoa.dylib"
    if record != {
        "qt_version": version,
        "qt_source": SOURCE,
        "architecture": platform.machine(),
        "craft_root": str(craft.resolve()),
        "sdk_sha256": sdk_identity(craft),
        "patch_sha256": sha256(TOOLS / "table-ownership.patch"),
        "plugin_sha256": sha256(library),
    } or version != VERSION:
        raise ValueError("The local Cocoa fix does not match this Craft SDK; rebuild it")
    return library


def build(root: Path, craft: Path, jobs: int) -> None:
    if platform.system() != "Darwin":
        raise SystemExit("This development dependency fix is for macOS only")
    version = subprocess.check_output([craft / "bin/qmake", "-query", "QT_VERSION"], text=True).strip()
    if version != VERSION:
        raise SystemExit(f"This patch is scoped to Qt {VERSION}; the SDK reports {version}")
    source = root / "source"
    source.mkdir(parents=True, exist_ok=True)
    tree = json.loads(download(f"https://api.github.com/repos/qt/qtbase/git/trees/{SOURCE}?recursive=1"))
    if tree.get("sha") != SOURCE or tree.get("truncated"):
        raise ValueError("Incomplete pinned Qt source listing")
    entries = [entry for entry in tree["tree"] if entry["type"] == "blob" and entry["path"].startswith(PREFIX)]
    if not entries:
        raise ValueError("Pinned Cocoa sources are absent")

    def fetch(entry):
        relative = PurePosixPath(entry["path"][len(PREFIX):])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Invalid Qt source path")
        data = download(f"https://raw.githubusercontent.com/qt/qtbase/{SOURCE}/{entry['path']}")
        # Git blob hashes verify every download against the pinned tree.
        digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if digest != entry["sha"]:
            raise ValueError("Qt source digest mismatch")
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)

    with ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(fetch, entries))
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p5", "-i", str(TOOLS / "table-ownership.patch")], cwd=source, check=True)
    subprocess.run([
        "cmake", "-S", str(TOOLS), "-B", str(root / "build"), "-G", "Ninja",
        f"-DCMAKE_PREFIX_PATH={craft}", f"-DMONTAZ_QT_COCOA_SOURCE={source}",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}",
    ], check=True)
    subprocess.run(["cmake", "--build", str(root / "build"), "-j", str(jobs)], check=True)
    destination = root / "platforms"
    destination.mkdir(exist_ok=True)
    library = destination / "libqcocoa.dylib"
    temporary = destination / "libqcocoa.dylib.tmp"
    shutil.copy2(root / "build/platforms/libqcocoa.dylib", temporary)
    temporary.replace(library)
    record = dict(qt_version=VERSION, qt_source=SOURCE, architecture=platform.machine(),
                  craft_root=str(craft.resolve()), sdk_sha256=sdk_identity(craft),
                  patch_sha256=sha256(TOOLS / "table-ownership.patch"),
                  plugin_sha256=sha256(library))
    manifest = root / "manifest.tmp"
    manifest.write_text(json.dumps(record, indent=2) + "\n")
    manifest.replace(root / "manifest.json")
    validate(root, craft)
    print(f"Built local Cocoa fix: {root}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--craft-root", type=Path, default=Path.home() / "CraftRoot")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    parser.add_argument("--validate", action="store_true")
    args = parser.parse_args()
    if args.validate:
        validate(args.root.resolve(), args.craft_root.resolve())
    else:
        build(args.root.resolve(), args.craft_root.resolve(), args.jobs)
