# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Stage upstream runtime data in a development bundle; no SDK or user settings edits."""

import argparse
import shutil
from pathlib import Path


def prepare(bundle: Path) -> None:
    source = Path(__file__).resolve().parents[2]
    resources = bundle / "Contents/Resources"
    if not (bundle / "Contents/MacOS/kdenlive").is_file():
        raise SystemExit("Build the native editor before running the development launcher.")
    resources.mkdir(parents=True, exist_ok=True)
    # Match upstream's macOS DATA_INSTALL_PREFIX="": Qt locates these under
    # Contents/Resources through AppDataLocation. Do not write to user data paths.
    for directory in (
        "effects",
        "transitions",
        "layouts",
        "profiles",
        "resourceproviders",
        "shortcuts",
        "titles",
        "luts",
        "lumas",
        "scripts",
    ):
        shutil.copytree(
            source / "data" / directory,
            resources / directory,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns("CMakeLists.txt", "*.md"),
        )
    for name in (
        "meta_magiclantern.png",
        "encodingprofiles.rc",
        "externalproxies.rc",
        "kdenliveeffectscategory.rc",
        "kdenlivetranscodingrc",
        "slideanimations.rc",
        "clipjobsettings.rc",
        "kdenlive.notifyrc",
    ):
        shutil.copy2(source / "data" / name, resources / name)
    (resources / "export").mkdir(exist_ok=True)
    shutil.copy2(source / "data/profiles.xml", resources / "export/profiles.xml")
    # Wizard::fixKdenliveRenderPath first looks beside the editor binary.
    renderer = bundle.parent / "kdenlive_render"
    if not renderer.is_file():
        raise SystemExit(
            "Build the kdenlive_render target before running the development launcher."
        )
    shutil.copy2(renderer, bundle / "Contents/MacOS/kdenlive_render")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    prepare(parser.parse_args().bundle.resolve())
