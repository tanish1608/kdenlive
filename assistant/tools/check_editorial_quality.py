# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Render a captured proposal natively; never call a provider or edit a user project.

Input directory: result.json contains proposal/review; private-sources.json maps
original bin IDs to absolute media paths. Outputs are generated alongside these
private inputs. Use separate directories for baseline and candidate captures.
"""

import argparse
import json
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--input-dir", type=Path, required=True)
    args = parser.parse_args()
    root, craft, build = (
        p.resolve() for p in (args.input_dir, args.craft_root, args.build_root)
    )
    result = json.loads((root / "result.json").read_text())
    sources = json.loads((root / "private-sources.json").read_text())
    proposal = result["proposal"]
    for path in sources.values():
        if not Path(path).is_absolute() or not Path(path).is_file():
            raise ValueError("A captured source is unavailable")
    frames = sum(op["duration_frames"] for op in proposal["operations"])
    rate = proposal["frame_rate"]
    environment = dict(
        os.environ,
        DYLD_FALLBACK_LIBRARY_PATH=str(craft / "lib"),
        MLT_REPOSITORY=str(craft / "plugins/mlt"),
        MLT_DATA=str(craft / "share/mlt"),
        MLT_PROFILES_PATH=str(craft / "share/mlt/profiles"),
        QT_QPA_PLATFORM="offscreen",
        MONTAZ_EDITORIAL_QA_ROOT=str(root),
    )
    with (root / "native-check.log").open("w") as log:
        subprocess.run(
            [str(build / "bin/assistanttest"), "Editorial proposals*"],
            env=environment,
            stdout=log,
            stderr=log,
            check=True,
            timeout=120,
        )
    video = root / "native-editorial.mp4"
    probe = subprocess.run(
        [
            str(craft / "bin/ffprobe"),
            "-v",
            "error",
            "-count_frames",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=nb_read_frames,r_frame_rate,width,height",
            "-of",
            "json",
            str(video),
        ],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    stream = json.loads(probe.stdout)["streams"][0]
    if (
        int(stream["nb_read_frames"]) != frames
        or stream["r_frame_rate"] != f"{rate['numerator']}/{rate['denominator']}"
    ):
        raise ValueError("Decoded export timing differs from the proposal")
    report = dict(
        total_frames=frames,
        seconds=frames * rate["denominator"] / rate["numerator"],
        shots=len(proposal["operations"]),
        cut_frames=[o["start_frame"] for o in proposal["operations"][1:]],
        requested_frames=result["review"]["requested_frames"],
        available_frames=result["review"]["available_frames"],
        assessment=result["review"]["assessment"]["status"],
        decoded_export=stream,
        native_apply_undo_redo_serialization=True,
        provider_calls=0,
    )
    (root / "export-report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
