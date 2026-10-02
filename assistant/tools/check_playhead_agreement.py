# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Private QA: do applied cuts show the same frames in every native path?

Generates lossless sources at mixed frame rates. Each encodes its own frame
number in the upper half and its identity in the lower half, as neutral grey. The native test applies them through
the assistant adapter and samples every project frame on the live timeline,
after Undo/Redo, from the reopened scene and from the export. This tool compares
those samples with each other and with the frame each source should show.
No provider, no user project; outputs stay in --output-dir.

  python3 assistant/tools/check_playhead_agreement.py --craft-root ... \\
      --build-root ... --output-dir ~/Movies/montaz-claude-maintenance/playhead
"""

import argparse
import json
import math
import os
import subprocess
from fractions import Fraction
from pathlib import Path

PROFILES = {"atsc_1080p_2997": Fraction(30000, 1001), "atsc_1080p_25": Fraction(25)}
# (source rate, in_frame, duration_frames in project frames)
CLIPS = [
    ("24", 7, 37),
    ("25", 11, 41),
    ("30000/1001", 5, 29),
    ("60000/1001", 13, 33),
    ("50", 3, 45),
]
STEP = 4  # Luma step per source frame.
WRAP = 50  # Frame numbers wrap every WRAP source frames.
CLIP_STEP = 40  # Luma step per clip identity.
FULL = 255 / 219  # Video-range luma as full-range RGB.


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--profile", choices=sorted(PROFILES), default="atsc_1080p_2997"
    )
    args = parser.parse_args()
    craft, build = args.craft_root.resolve(), args.build_root.resolve()
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    project = PROFILES[args.profile]
    environment = dict(
        os.environ,
        DYLD_FALLBACK_LIBRARY_PATH=str(craft / "lib"),
        MLT_REPOSITORY=str(craft / "plugins/mlt"),
        MLT_DATA=str(craft / "share/mlt"),
        MLT_PROFILES_PATH=str(craft / "share/mlt/profiles"),
        QT_QPA_PLATFORM="offscreen",
        MONTAZ_PLAYHEAD_QA_ROOT=str(root),
    )
    clips = []
    for index, (rate, in_frame, length) in enumerate(CLIPS):
        path = root / f"source-{index}.mp4"
        seconds = math.ceil((in_frame + length) / project) + 1
        subprocess.run(
            [
                str(craft / "bin/ffmpeg"),
                "-v",
                "error",
                "-y",
                "-f",
                "lavfi",
                "-i",
                f"nullsrc=s=320x180:r={rate}:d={seconds},"
                f"geq=lum='if(lt(Y\\,H/2)\\,16+{STEP}*mod(N\\,{WRAP})\\,16+{CLIP_STEP}*{index})'"
                ":cb=128:cr=128",
                "-c:v",
                "libx264",
                "-qp",
                "0",
                "-pix_fmt",
                "yuv420p",
                str(path),
            ],
            env=environment,
            check=True,
            timeout=120,
        )
        clips.append(dict(path=str(path), in_frame=in_frame, duration_frames=length))
    (root / "sources.json").write_text(
        json.dumps(dict(profile=args.profile, clips=clips))
    )
    with (root / "native-check.log").open("w") as log:
        subprocess.run(
            [str(build / "bin/assistanttest"), "Applied cuts agree*"],
            env=environment,
            stdout=log,
            stderr=log,
            check=True,
            timeout=600,
        )
    samples = json.loads((root / "native-samples.json").read_text())
    paths = ("timeline", "redo", "reopened", "export")
    # Encoders may rescale luma levels (range/matrix conversion). Fit each path's
    # levels to the live timeline (least squares) so only timing differences remain.
    levels = {}
    for path in paths:
        pairs = [
            (samples["timeline"][f][k], samples[path][f][k])
            for f in range(samples["total"])
            for k in (0, 1)
        ]
        n = len(pairs)
        mx, my = sum(x for x, _ in pairs) / n, sum(y for _, y in pairs) / n
        slope = sum((x - mx) * (y - my) for x, y in pairs) / sum(
            (x - mx) ** 2 for x, _ in pairs
        )
        levels[path] = (slope, my - slope * mx)

    def decoded(path, f):
        slope, offset = levels[path]
        upper, lower = ((v - offset) / slope for v in samples[path][f])
        return round(upper / (FULL * STEP)) % WRAP, round(lower / (FULL * CLIP_STEP))

    report = dict(profile=args.profile, project_fps=str(project), cuts=[], paths={})
    boundaries, start = [], 0
    for rate, in_frame, length in CLIPS:
        boundaries.append((start, start + length, Fraction(rate), in_frame))
        start += length
    for path in paths:
        mismatched, identity_errors, worst = [], 0, 0
        for f in range(samples["total"]):
            clip = next(i for i, b in enumerate(boundaries) if b[0] <= f < b[1])
            first, _, rate, in_frame = boundaries[clip]
            # MLT shows the nearest source frame at this project time.
            position = Fraction(in_frame + f - first) / project * rate
            expected = math.floor(position + Fraction(1, 2)) % WRAP
            number, identity = decoded(path, f)
            error = min((number - expected) % WRAP, (expected - number) % WRAP)
            worst = max(worst, error)
            if error:
                mismatched.append(
                    dict(frame=f, clip=clip, expected=expected, actual=number)
                )
            identity_errors += identity != clip
        report["paths"][path] = dict(
            frames=samples["total"],
            levels=[round(v, 4) for v in levels[path]],
            source_frame_mismatches=len(mismatched),
            worst_source_frame_error=worst,
            clip_identity_errors=identity_errors,
            first_mismatches=mismatched[:8],
        )
    for first, _, _, _ in boundaries[1:]:
        shown = {json.dumps([decoded(p, first - 1), decoded(p, first)]) for p in paths}
        report["cuts"].append(dict(frame=first, agree=len(shown) == 1))
    report["paths_identical"] = all(
        [decoded(p, f) for f in range(samples["total"])]
        == [decoded("timeline", f) for f in range(samples["total"])]
        for p in paths
    )
    (root / "playhead-report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if k != "paths"}, indent=1))
    for path, value in report["paths"].items():
        print(path, {k: v for k, v in value.items() if k != "first_mismatches"})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
