# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Reproduce real native match tracking, candidate selection, Undo, serialization
and export. The default run makes no provider call; --live-gemini adds one
separately labelled request against the same synthetic footage."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

# Fixture discs are 32 px in 320x180 footage. Outgoing window = source frames
# 30..59 of A (center 60+n), incoming = source frames 0..29 of B (center 100+n).
DISC = [
    [(90 - 16) / 320, (90 - 16) / 180, 32 / 320, 32 / 180],
    [(100 - 16) / 320, (90 - 16) / 180, 32 / 320, 32 / 180],
]
# Flat background ranks first by seed crop; the helper's variance gate rejects it.
FLAT = [[0.02, 0.05, 0.10, 32 / 180], [0.02, 0.05, 0.10, 32 / 180]]
# Compatible shape but ~1.5x zoom: ranks third, so the two-track ceiling skips it.
ZOOMED = [[0.70, 0.05, 0.10, 32 / 180], [0.70, 0.05, 0.15, 0.267]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--craft-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--live-gemini",
        action="store_true",
        help="Also send the synthetic seed frames to the configured Gemini model",
    )
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    sys.path[:0] = [str(source / "assistant/src"), str(source / "assistant/tests")]
    from test_native_match_tracker import make_fixture
    from montaz_assistant import match_cuts
    from montaz_assistant.contracts import (
        Asset,
        FrameRate,
        OccupiedRange,
        Snapshot,
        Track,
    )
    from montaz_assistant.gemini import Gemini
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
        calls = 0

        def generate(self, parts):
            # Known fixture seeds replace only semantic selection. Tracking/geometry remain real.
            self.calls += 1
            return dict(
                reason="Controlled candidates",
                candidates=[
                    dict(reason="Zoomed background corner", boxes=ZOOMED),
                    dict(reason="Controlled textured discs", boxes=DISC),
                    dict(reason="Flat background corner", boxes=FLAT),
                ],
            )

    class Counted:
        def __init__(self, provider):
            self.model, self.provider, self.calls = provider.model, provider, 0

        def generate(self, parts):
            self.calls += 1
            return self.provider.generate(parts)

    real_native = match_cuts.native_frames
    helper = dict(calls=0, seeds=[])

    def native_frames(*arguments):
        helper["calls"] += 1
        if len(arguments) > 6:
            helper["seeds"].append(arguments[6])
        return real_native(*arguments)

    match_cuts.native_frames = native_frames

    def run(provider, cache):
        helper.update(calls=0, seeds=[])
        before, report, started = provider.calls, {}, time.monotonic()
        result = match_cuts.propose_match(
            snapshot,
            "fixture",
            dict(width=1920, height=1080, clips=clips),
            {
                "1": str(root / "round-object-a.mp4"),
                "2": str(root / "round-object-b.mp4"),
            },
            cache,
            provider,
            MediaTools(),
            report=report,
        )
        frames = [
            dict(
                instance_id=o["instance_id"],
                clip_frames=[o["keyframes"][0]["frame"], o["keyframes"][-1]["frame"]],
                timeline_frames=[
                    c["start_frame"] + o["keyframes"][0]["frame"],
                    c["start_frame"] + o["keyframes"][-1]["frame"],
                ],
                source_frames=[
                    c["in_frame"] + o["keyframes"][0]["frame"],
                    c["in_frame"] + o["keyframes"][-1]["frame"],
                ],
            )
            for c, o in zip(clips, result["operations"])
        ]
        return result, dict(
            status=result["status"],
            reason=result["reason"],
            cached=result["cached"],
            elapsed_seconds=round(time.monotonic() - started, 3),
            provider_calls=provider.calls - before,
            helper_calls=helper["calls"],
            tracked_seeds=len(helper["seeds"]),
            cut_timeline_frame=clips[1]["start_frame"],
            keyframes=frames,
            target=result["target"],
            selection=report,
        )

    def fresh(name):
        cache = root / name
        shutil.rmtree(cache, ignore_errors=True)
        return cache

    fixture, cache = FixtureAnchors(), fresh("evidence")
    result, first = run(fixture, cache)
    _, reused = run(fixture, cache)
    selection = first["selection"]
    if (
        result["status"] != "matched"
        or selection["selected"] != 1
        or selection["ranked"] != [2, 1, 0]
        or [t["outcome"] for t in selection["tracked"]] != ["unstable", "valid"]
        or first["provider_calls"] != 1
        or first["helper_calls"] > 6
        or not reused["cached"]
        or reused["provider_calls"] + reused["helper_calls"] != 0
    ):
        raise RuntimeError(
            "The controlled fixture did not select the disc pair as expected"
        )
    (root / "controlled.json").write_text(json.dumps(result))
    evidence = dict(
        offline_fixture=dict(
            provider="offline fixture (no network); seeds are fixed, tracking is real",
            first_run=first,
            cache_reuse=reused,
        ),
        metric_definitions=dict(
            zoom="largest uniform Transform scale over both 30-frame windows",
            crop="mean over every keyframe of 1 - 1/scale^2, the source area discarded",
            motion="mean per-frame largest canvas displacement of any source corner "
            "added by the reframing, as a fraction of the canvas",
            cost=f"crop + {match_cuts.MOTION_WEIGHT:g} * motion; ties keep the "
            "earlier rank (seed crop, aspect, subject size, provider order)",
            budget=f"a further pair starts only with {match_cuts.CANDIDATE_SECONDS} s "
            f"left of {match_cuts.BUDGET_SECONDS:g} s",
        ),
    )
    if args.live_gemini:
        # Separate cache and label: a live answer never feeds the native checks.
        try:
            live = Counted(Gemini.configured())
            _, outcome = run(live, fresh("live-evidence"))
            outcome["model"] = live.model
        except ValueError as exc:
            outcome = dict(error=getattr(exc, "code", "invalid_request"))
        evidence["live_gemini"] = outcome
    (root / "selection-report.json").write_text(json.dumps(evidence, indent=2))
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
        "Native candidate selection, tracking, application, Undo/Redo, "
        "serialization and export passed."
    )
    print(f"Selection report: {root / 'selection-report.json'}")
    print(f"Review output: {root / 'native-alignment.mp4'}")


if __name__ == "__main__":
    main()
