# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Private QA: decode bounded grayscale windows and report motion evidence.

Generates FFmpeg fixtures (outside Git) with known movement and coverage, and
optionally analyses caller-named clips. At most eight windows of at most 24
frames, each scaled to at most 96 pixels. No model calls; nothing is applied.

  python3 assistant/tools/check_motion_evidence.py --output-dir /private/qa \\
      [--synthetic] [--clip /path/a.mp4:START:COUNT[:STRIDE] ...]
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

MAX_WINDOWS = 8

# Each fixture: a filtergraph over a still 1280x720 texture, and the expected
# outcome it was built to show.
FIXTURES = {
    "static": ("[0:v]crop=640:360:320:180", "static"),
    "pan-left": ("[0:v]crop=640:360:'40+t*240':180", "content moves left"),
    "pan-up": ("[0:v]crop=640:360:320:'300-t*120'", "content moves down"),
    "fade-out": ("[0:v]crop=640:360:320:180,fade=t=out:st=0:d=0.9", "fade"),
    # drawbox's "t" is thickness, not time; overlay evaluates time per frame.
    "dark-wipe": (
        "[0:v]crop=640:360:320:180[b];color=c=black:s=640x360:r=24[c];"
        "[b][c]overlay=x='-640+t*1100':y=0:eval=frame",
        "complete occlusion candidate, textureless cover",
    ),
    "partial-cover": (
        "[0:v]crop=640:360:320:180[b];color=c=0x202020:s=300x360:r=24[c];"
        "[b][c]overlay=x='min(0,-300+t*700)':y=0:eval=frame",
        "partial occlusion candidate",
    ),
    "passing-object": (
        "[0:v]crop=640:360:320:180[b];[0:v]crop=180:180:0:0[o];[b][o]overlay=x='-180+t*900':y=90",
        "foreground motion, no occlusion",
    ),
    "cut": (
        "[0:v]crop=640:360:'if(lt(t,0.5),320,40)':'if(lt(t,0.5),180,20)'",
        "abrupt change",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--synthetic", action="store_true")
    parser.add_argument(
        "--clip", action="append", default=[], help="PATH:START:COUNT[:STRIDE]"
    )
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(source / "assistant/src"))
    from montaz_assistant.motion_evidence import MAX_FRAMES, Frame, Window, analyse
    from montaz_assistant.planning import MediaTools

    ffmpeg = os.environ.get("MONTAZ_FFMPEG", "ffmpeg")
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    jobs = []  # (name, path, start, count, stride, expected)
    if args.synthetic:
        # Random cells about 13 source pixels wide (about 2 analysis pixels after
        # scaling a 640-wide crop to 96): aperiodic, with no flat regions.
        import random

        rng = random.Random(7)
        cells = root / "cells.pgm"
        cells.write_bytes(
            b"P5\n100 56\n255\n"
            + bytes(rng.randrange(20, 236) for _ in range(100 * 56))
        )
        texture = root / "texture.png"
        subprocess.run(
            [
                ffmpeg,
                "-v",
                "error",
                "-y",
                "-i",
                str(cells),
                "-vf",
                "scale=1280:720:flags=bicubic",
                str(texture),
            ],
            check=True,
            timeout=60,
        )
        for name, (graph, expected) in FIXTURES.items():
            clip = root / f"{name}.mp4"
            subprocess.run(
                [
                    ffmpeg,
                    "-v",
                    "error",
                    "-y",
                    "-loop",
                    "1",
                    "-framerate",
                    "24",
                    "-i",
                    str(texture),
                    "-filter_complex",
                    graph,
                    "-t",
                    "1",
                    "-r",
                    "24",
                    "-pix_fmt",
                    "yuv420p",
                    "-c:v",
                    "libx264",
                    "-crf",
                    "16",
                    str(clip),
                ],
                check=True,
                timeout=60,
            )
            jobs.append((name, clip, 0, 24, 1, expected))
    for spec in args.clip:
        path, *numbers = (
            spec.rsplit(":", 3) if spec.count(":") >= 3 else spec.rsplit(":", 2)
        )
        start, count, stride = (list(map(int, numbers)) + [1])[:3]
        if not 2 <= count <= MAX_FRAMES or stride < 1 or start < 0:
            parser.error(f"invalid clip window: {spec}")
        jobs.append((Path(path).name, Path(path), start, count, stride, "unknown"))
    if not jobs or len(jobs) > MAX_WINDOWS:
        parser.error(f"analyse between 1 and {MAX_WINDOWS} windows")

    media, report = MediaTools(), []
    for name, path, start, count, stride, expected in jobs:
        started = time.monotonic()
        width, height = 96, 54
        raw = media._output(
            [
                ffmpeg,
                "-v",
                "error",
                "-nostdin",
                "-i",
                str(path),
                "-vf",
                f"trim=start_frame={start}:end_frame={start + count * stride},"
                f"select='not(mod(n\\,{stride}))',scale={width}:{height},format=gray",
                "-fps_mode",
                "passthrough",
                "-frames:v",
                str(count),
                "-f",
                "rawvideo",
                "pipe:1",
            ],
            maximum_bytes=width * height * MAX_FRAMES,
        )
        size = width * height
        frames = tuple(
            Frame(start + i * stride, raw[i * size : (i + 1) * size])
            for i in range(len(raw) // size)
        )
        decoded = time.monotonic() - started
        evidence = analyse(Window(width, height, frames), check=media.check)
        report.append(
            dict(
                window=name,
                expected=expected,
                frames=len(frames),
                positions=[frames[0].position, frames[-1].position],
                direction=evidence.direction,
                velocity_px_per_frame=evidence.velocity,
                consistency=evidence.consistency,
                steps={
                    s: sum(x.status == s for x in evidence.steps)
                    for s in ("measured", "textureless", "ambiguous", "unmatched")
                },
                fade=evidence.fade,
                abrupt_change=evidence.abrupt_change,
                local_motion=evidence.local_motion,
                occlusions=[
                    dict(
                        kind=o.kind,
                        start=o.start,
                        end=o.end,
                        peak=o.peak_coverage,
                        textured_cover=o.textured_cover,
                        verified=o.verified,
                    )
                    for o in evidence.occlusions
                ],
                notes=evidence.notes,
                decode_seconds=round(decoded, 3),
                analyse_seconds=round(time.monotonic() - started - decoded, 3),
            )
        )
        print(
            f"{name:24} expected {expected:46} -> {evidence.direction:12} "
            f"fade={bool(evidence.fade)} cut={bool(evidence.abrupt_change)} "
            f"local={evidence.local_motion} occlusions={[o.kind for o in evidence.occlusions]}"
        )
    (root / "motion-evidence-report.json").write_text(json.dumps(report, indent=2))
    print(f"Report: {root / 'motion-evidence-report.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
