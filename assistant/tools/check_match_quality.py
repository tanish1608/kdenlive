# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Reproduce real native match tracking, Undo, serialization and export without API calls."""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    sys.path[:0] = [str(source / "assistant/src"), str(source / "assistant/tests")]
    from test_native_match_tracker import make_fixture
    from montaz_assistant.contracts import (
        Asset,
        FrameRate,
        OccupiedRange,
        Snapshot,
        Track,
    )
    from montaz_assistant.match_cuts import propose_match
    from montaz_assistant.planning import MediaTools

    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    craft, build = args.craft_root.resolve(), args.build_root.resolve()
    os.environ.update(
        DYLD_FALLBACK_LIBRARY_PATH=str(craft / "lib"),
        MLT_REPOSITORY=str(craft / "plugins/mlt"),
        MLT_DATA=str(craft / "share/mlt"),
        MLT_PROFILES_PATH=str(craft / "share/mlt/profiles"),
        QT_QPA_PLATFORM="offscreen",
        MONTAZ_TRACKER=str(build / "bin/montaz-tracker"),
        MONTAZ_FFMPEG=str(craft / "bin/ffmpeg"),
        MONTAZ_FFPROBE=str(craft / "bin/ffprobe"),
        MONTAZ_MATCH_QA_ROOT=str(root),
    )
    for i, name in enumerate(("round-object-a.mp4", "round-object-b.mp4")):
        make_fixture(
            root / name,
            center=60 if i == 0 else 100,
            background=(12, 30, 45) if i == 0 else (35, 14, 42),
        )
    clips = [
        dict(
            instance_id=str(11 + i),
            bin_id=str(1 + i),
            track_id="1",
            start_frame=i * 60,
            in_frame=0,
            duration_frames=60,
        )
        for i in range(2)
    ]
    snapshot = Snapshot(
        "qa",
        "qa",
        0,
        FrameRate(30000, 1001),
        (Asset("1", 90), Asset("2", 90)),
        (Track("1", "video"),),
        tuple(
            OccupiedRange(c["instance_id"], "1", c["start_frame"], 60) for c in clips
        ),
    )

    class FixtureAnchors:
        model = "gemini-fixture"

        def generate(self, parts):
            # Known fixture seeds replace only semantic selection. Tracking/geometry remain real.
            return dict(
                reason="Controlled textured discs",
                boxes=[
                    [(90 - 16) / 320, (90 - 16) / 180, 32 / 320, 32 / 180],
                    [(100 - 16) / 320, (90 - 16) / 180, 32 / 320, 32 / 180],
                ],
            )

    result = propose_match(
        snapshot,
        "fixture",
        dict(width=1920, height=1080, clips=clips),
        {"1": str(root / "round-object-a.mp4"), "2": str(root / "round-object-b.mp4")},
        root / "evidence",
        FixtureAnchors(),
        MediaTools(),
    )
    if result["status"] != "matched":
        raise RuntimeError("The controlled fixture failed to produce a match")
    (root / "controlled.json").write_text(json.dumps(result))
    with (root / "native-check.log").open("w") as log:
        subprocess.run(
            [str(build / "bin/assistanttest"), "[Montaz]"],
            stdout=log,
            stderr=log,
            env=os.environ.copy(),
            timeout=90,
            check=True,
        )
    print(
        "Native object tracking, application, Undo/Redo, serialization and export passed."
    )
    print(f"Review output: {root / 'native-alignment.mp4'}")


if __name__ == "__main__":
    main()
