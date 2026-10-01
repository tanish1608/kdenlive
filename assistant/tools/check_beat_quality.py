# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Reproduce local rhythm analysis and native retiming/export with synthetic audio."""

import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
import time
from array import array
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    sys.path[:0] = [str(source / "assistant/src"), str(source / "assistant/tests")]
    from montaz_assistant.contracts import (
        Asset,
        FrameRate,
        OccupiedRange,
        Snapshot,
        Track,
    )
    from montaz_assistant.planning import MediaTools
    from montaz_assistant.timing import refine_timing
    from test_beats import pulses, rhythm_audio

    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    craft, build = args.craft_root.resolve(), args.build_root.resolve()
    os.environ.update(
        DYLD_FALLBACK_LIBRARY_PATH=str(craft / "lib"),
        MLT_REPOSITORY=str(craft / "plugins/mlt"),
        MLT_DATA=str(craft / "share/mlt"),
        MLT_PROFILES_PATH=str(craft / "share/mlt/profiles"),
        QT_QPA_PLATFORM="offscreen",
        MONTAZ_FFMPEG=str(craft / "bin/ffmpeg"),
        MONTAZ_ANALYSIS_CACHE=str(root / "evidence"),
        MONTAZ_BEAT_QA_ROOT=str(root),
    )
    path = root / "rhythm.wav"
    path.write_bytes(rhythm_audio(pulses(seconds=6, phase=0), seconds=6))
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    for i, color in enumerate(("red", "green", "blue")):
        subprocess.run(
            [
                str(craft / "bin/ffmpeg"),
                "-v",
                "error",
                "-nostdin",
                "-y",
                "-f",
                "lavfi",
                "-i",
                f"color=c={color}:s=320x180:r=25:d=4",
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                str(root / f"shot-{i}.mp4"),
            ],
            capture_output=True,
            timeout=15,
            check=True,
        )
    starts, lengths = (0, 48, 102), (48, 54, 48)
    clips = [
        dict(
            instance_id=str(i),
            bin_id=str(i),
            track_id="1",
            start_frame=start,
            in_frame=0,
            duration_frames=length,
        )
        for i, (start, length) in enumerate(zip(starts, lengths))
    ]
    snapshot = Snapshot(
        "qa",
        "qa",
        0,
        FrameRate(25),
        tuple(Asset(str(i), 100) for i in range(3)),
        (Track("1", "video"), Track("2", "audio")),
        tuple(
            OccupiedRange(str(i), "1", start, length)
            for i, (start, length) in enumerate(zip(starts, lengths))
        )
        + (OccupiedRange("music", "2", 0, 150),),
    )
    spec = dict(
        clips=clips,
        max_shift_frames=6,
        music=dict(path=str(path), sha256=sha, instance_id="music"),
    )
    start = time.monotonic()
    result = refine_timing(snapshot, "beat-qa", spec, MediaTools())
    seconds = time.monotonic() - start
    cached = refine_timing(snapshot, "beat-qa-cached", spec, MediaTools())
    if result["analysis"]["mode"] != "beats" or not cached["analysis"]["cached"]:
        raise RuntimeError("Expected an estimated rhythm and a complete cache hit")
    if [o["start_frame"] for o in result["operations"]] != [0, 50, 100]:
        raise RuntimeError("Controlled music did not produce the expected cut frames")
    (root / "timing.json").write_text(json.dumps(result))
    with (root / "native-check.log").open("w") as log:
        subprocess.run(
            [
                str(build / "bin/assistanttest"),
                "Estimated beat timing survives native undo serialization and export",
            ],
            stdout=log,
            stderr=log,
            timeout=90,
            check=True,
        )

    def decode(name, audio=False):
        args = [
            str(craft / "bin/ffmpeg"),
            "-v",
            "error",
            "-nostdin",
            "-i",
            str(root / name),
        ]
        args += (
            ["-map", "0:a:0", "-f", "s16le", "-ac", "2", "-ar", "8000"]
            if audio
            else [
                "-map",
                "0:v:0",
                "-vf",
                "scale=1:1",
                "-pix_fmt",
                "rgb24",
                "-f",
                "rawvideo",
            ]
        )
        return subprocess.run(
            args + ["pipe:1"], capture_output=True, timeout=15, check=True
        ).stdout

    for name, expected in (
        ("before.mkv", [0] * 48 + [1] * 54 + [2] * 48),
        ("aligned.mkv", [0] * 50 + [1] * 50 + [2] * 50),
    ):
        raw = decode(name)
        colors = [
            max(range(3), key=lambda j: raw[i + j]) for i in range(0, len(raw), 3)
        ]
        if colors != expected:
            raise RuntimeError(f"Unexpected native export cut frames in {name}")
    before, after = decode("before.mkv", True), decode("aligned.mkv", True)
    if before != after or len(after) != 6 * 8000 * 4:
        raise RuntimeError(
            "Rendered audio changed or the export duration was incorrect"
        )
    samples = array("h")
    samples.frombytes(after)
    if sys.byteorder != "little":
        samples.byteswap()
    energy = [
        math.sqrt(sum(v * v for v in samples[i : i + 640]) / 640)
        for i in range(0, len(samples), 640)
    ]
    for cut in (50, 100):
        attack = max(range(cut - 2, cut + 3), key=lambda i: energy[i])
        if abs(attack - cut) > 1:
            raise RuntimeError(
                "Exported audio attacks disagree with proposed beat cuts"
            )
    if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
        raise RuntimeError("The source audio changed")
    report = dict(
        analysis=result["analysis"],
        analysis_seconds=round(seconds, 3),
        cache_reused=True,
        total_frames=150,
        before_cut_frames=[48, 102],
        after_cut_frames=[50, 100],
        rendered_audio_identical=True,
        one_undo_redo=True,
        serialized_export_checked=True,
    )
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
