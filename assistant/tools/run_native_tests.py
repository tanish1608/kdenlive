# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Compile Qt-only transport tests without downloading the full KDE/MLT SDK."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--qmake", default=shutil.which("qmake") or "/opt/homebrew/opt/qtbase/bin/qmake"
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="montaz-native-tests-") as build:
        subprocess.run(
            [args.qmake, str(root / "assistant/tests/native/clienttest.pro")], cwd=build, check=True
        )
        subprocess.run(["make", "-s", "-j2"], cwd=build, check=True)
        environment = os.environ.copy()
        environment["MONTAZ_TEST_PYTHON"] = sys.executable
        environment["MONTAZ_TEST_SOURCE"] = str(root / "assistant/src")
        subprocess.run(
            [str(Path(build) / "montaz-client-test")], env=environment, cwd=build, check=True
        )


if __name__ == "__main__":
    main()
