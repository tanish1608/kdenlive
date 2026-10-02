# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Generate independent moving-subject fixtures and check native short montages.

No provider calls or user footage. Generated media, MLT scenes and evidence stay
in the required output directory outside the repository.
"""

import argparse
import json
import os
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    craft, build, root = (
        path.expanduser().resolve()
        for path in (args.craft_root, args.build_root, args.output_dir)
    )
    repository = Path(__file__).resolve().parents[2]
    if root == repository or repository in root.parents:
        parser.error("Keep generated media outside the repository")
    root.mkdir(parents=True, exist_ok=True)
    (root / "report.json").unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(
        DYLD_LIBRARY_PATH=str(craft / "lib"),
        DYLD_FALLBACK_LIBRARY_PATH=str(craft / "lib"),
        MLT_REPOSITORY=str(craft / "plugins/mlt"),
        MLT_DATA=str(craft / "share/mlt"),
        MLT_PROFILES_PATH=str(craft / "share/mlt/profiles"),
        QT_QPA_PLATFORM="offscreen",
        MONTAZ_MONTAGE_QA_ROOT=str(root),
    )
    ffmpeg, ffprobe = craft / "bin/ffmpeg", craft / "bin/ffprobe"
    with (root / "fixture.log").open("w") as log:
        for i in range(3):
            # Full subject is 216/1080 high; centers move exactly one pixel per
            # frame from x=900, 960, 1020. Native tests solve a common 1.25x crop.
            subprocess.run(
                [
                    str(ffmpeg),
                    "-v",
                    "error",
                    "-y",
                    "-f",
                    "lavfi",
                    "-i",
                    "color=black:s=1920x1080:r=30000/1001",
                    "-f",
                    "lavfi",
                    "-i",
                    "color=yellow:s=216x216:r=30000/1001",
                    "-filter_complex",
                    f"[0:v][1:v]overlay=x='{900 + i * 60 - 108}+n-1':y=432:"
                    "format=rgb,format=yuv420p",
                    "-frames:v",
                    "60",
                    "-c:v",
                    "libx264",
                    "-crf",
                    "18",
                    "-preset",
                    "fast",
                    "-an",
                    str(root / f"subject-{i}.mp4"),
                ],
                env=env,
                stdout=log,
                stderr=log,
                timeout=60,
                check=True,
            )
    with (root / "native-check.log").open("w") as log:
        subprocess.run(
            [str(build / "bin/assistanttest"), "[MontageRender]"],
            cwd=build,
            env=env,
            stdout=log,
            stderr=log,
            timeout=120,
            check=True,
        )
    outputs = []
    for length in (8, 10, 12):
        video = root / f"montage-{length}.mp4"
        probe = json.loads(
            subprocess.run(
                [
                    str(ffprobe),
                    "-v",
                    "error",
                    "-select_streams",
                    "v:0",
                    "-count_frames",
                    "-show_entries",
                    "stream=nb_read_frames,r_frame_rate,width,height",
                    "-of",
                    "json",
                    str(video),
                ],
                env=env,
                capture_output=True,
                timeout=30,
                check=True,
            ).stdout
        )["streams"][0]
        if int(probe["nb_read_frames"]) != 3 * length:
            raise RuntimeError(f"Wrong decoded frame count for {length}-frame shots")
        if probe["r_frame_rate"] != "30000/1001":
            raise RuntimeError("Export changed the fractional project frame rate")
        outputs.append(dict(shot_frames=length, cuts=[length, length * 2], **probe))
    report = dict(
        status="pass",
        provider_calls=0,
        scope="Synthetic native geometry, frame count, Undo/Redo, MLT reload and export; "
        "does not assess AI subject detection or real-footage perceptual quality",
        outputs=outputs,
        tolerances=dict(center_x=0.004, center_y=0.006, subject_height=0.007),
        evidence="native-check.log",
    )
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
