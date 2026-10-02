# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Reproduce real native match tracking, candidate selection, Undo, serialization
and export. The default run makes no provider call; --live-gemini adds one
separately labelled request against the same synthetic footage.

A stability stage then solves every alignment model from the same real tracks,
exports each through the native check, and measures the decoded discs."""

import argparse
import hashlib
import json
import math
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
# Stability scenarios: outgoing fixture arguments and seed radius at source frame 30.
# "zoom" grows from radius 14.5 to 18.85 px over the outgoing window (+30%).
SCENARIOS = {
    "constant": (dict(center=60), 16.0),
    "zoom": (dict(center=60, radius=10, growth=0.15), 14.5),
}
# Yellow disc pixels, as tests/assistanttest.cpp's geometry check classifies them.
ABOVE_65 = bytes(1 if v > 65 else 0 for v in range(256))
BELOW_50 = bytes(1 if v < 50 else 0 for v in range(256))


def disc(frame: bytes, width: int = 640, height: int = 360) -> tuple:
    """Centroid (canvas fractions) and equivalent diameter (canvas heights)."""
    mask = (
        int.from_bytes(frame[0::3].translate(ABOVE_65), "big")
        & int.from_bytes(frame[1::3].translate(ABOVE_65), "big")
        & int.from_bytes(frame[2::3].translate(BELOW_50), "big")
    ).to_bytes(width * height, "big")
    count = sx = sy = 0
    for y in range(height):
        row = mask[y * width : (y + 1) * width]
        first = row.find(1)
        if first >= 0:
            # Fill each row's run: compressed checker cells can drop below the
            # color threshold inside the disc and must not read as size changes.
            last = row.rfind(1)
            n = last - first + 1
            count, sy = count + n, sy + y * n
            sx += (first + last) / 2 * n
    if count < 100:
        raise RuntimeError("The exported disc was not found")
    return (
        sx / count / width,
        sy / count / height,
        2 * math.sqrt(count / math.pi) / height,
    )


def truth_sweep(align, models, track, fixture: dict, offsets: int = 10) -> dict:
    """Shift both windows over the fixtures and score every model against the
    known disc centers/radii at every frame (transform level, no decode noise)."""
    radius, growth = fixture.get("radius", 16), fixture.get("growth", 0.0)

    def truth(start, center, r0, g):
        return [
            ((center + n) / 320, 0.5, (r0 + g * n) / 90)
            for n in range(start, start + 30)
        ]

    rows, rejected = {m: [] for m in models}, {"helper": 0, **{m: 0 for m in models}}
    for k in range(offsets):
        a, b = 30 - 3 * k, 3 * k
        ra = radius + growth * a
        seeds = (
            [(60 + a - ra) / 320, (90 - ra) / 180, ra / 160, ra / 90],
            [(100 + b - 16) / 320, 74 / 180, 0.1, 32 / 180],
        )
        try:
            tracks = [track("outgoing", a, seeds[0]), track("partner", b, seeds[1])]
        except ValueError as exc:
            # The helper's appearance/bounds gate is a valid negative outcome.
            if getattr(exc, "code", None) != "tracking_unavailable":
                raise
            rejected["helper"] += 1
            continue
        truths = [truth(a, 60, radius, growth), truth(b, 100, 16, 0.0)]
        for model in models:
            try:
                _, transforms = align(tracks, model)
            except ValueError as exc:
                if getattr(exc, "code", None) not in {
                    "no_match",
                    "tracking_unavailable",
                }:
                    raise
                rejected[model] += 1
                continue
            shown = [
                [
                    (
                        k["x"] + k["scale"] * t[0],
                        k["y"] + k["scale"] * t[1],
                        k["scale"] * t[2],
                    )
                    for k, t in zip(values, truth_values)
                ]
                for values, truth_values in zip(transforms, truths)
            ]
            last, first = shown[0][-1], shown[1][0]
            steps = [(p, q) for window in shown for p, q in zip(window, window[1:])]
            rows[model].append(
                (
                    math.hypot(last[0] - first[0], last[1] - first[1]),
                    abs(last[2] / first[2] - 1),
                    math.sqrt(
                        sum(math.hypot(q[0] - p[0], q[1] - p[1]) ** 2 for p, q in steps)
                        / len(steps)
                    ),
                    math.sqrt(
                        sum((q[2] / p[2] - 1) ** 2 for p, q in steps) / len(steps)
                    ),
                )
            )
    summary = {}
    for model, values in rows.items():
        if not values:
            continue
        columns = list(zip(*values))
        summary[model] = {
            f"{name}_{kind}": round(agg(column), 6)
            for name, column in zip(
                ("cut_center", "cut_size", "motion_rms", "size_change_rms"), columns
            )
            for kind, agg in (("mean", lambda c: sum(c) / len(c)), ("max", max))
        }
    return dict(windows=offsets, rejected=rejected, models=summary)


def decoded(video: Path) -> dict:
    """Measure the true subject on screen over both 30-frame windows (30..89)."""
    raw = subprocess.run(
        [
            os.environ["MONTAZ_FFMPEG"],
            "-v",
            "error",
            "-i",
            str(video),
            "-vf",
            "scale=640:360",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "pipe:1",
        ],
        capture_output=True,
        check=True,
    ).stdout
    size = 640 * 360 * 3
    frames = len(raw) // size
    shown = {n: disc(raw[n * size : (n + 1) * size]) for n in range(30, 90)}

    def rms(values):
        return math.sqrt(sum(v * v for v in values) / len(values))

    changes, accelerations, moves = [], [], []
    for window in (range(30, 60), range(60, 90)):
        d = [shown[n] for n in window]
        changes += [b[2] / a[2] - 1 for a, b in zip(d, d[1:])]
        accelerations += [
            (c[2] - 2 * b[2] + a[2]) / b[2] for a, b, c in zip(d, d[1:], d[2:])
        ]
        moves += [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(d, d[1:])]
    sizes = [shown[n][2] for n in range(30, 90)]
    return dict(
        frames=frames,
        size_change_rms=rms(changes),
        size_acceleration_rms=rms(accelerations),
        size_spread=max(sizes) / min(sizes) - 1,
        residual_motion_rms=rms(moves),
        boundary_center=math.hypot(
            shown[59][0] - shown[60][0], shown[59][1] - shown[60][1]
        ),
        boundary_size=abs(shown[59][2] / shown[60][2] - 1),
    )


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
    spec, rate = dict(width=1920, height=1080), FrameRate(30000, 1001)
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
    from montaz_assistant.match_stability import stability

    comparison, partner = {}, root / "round-object-b.mp4"
    for scenario, (fixture_args, radius) in SCENARIOS.items():
        folder = fresh(f"stability/{scenario}")
        folder.mkdir(parents=True)
        outgoing = folder / "outgoing.mp4"
        make_fixture(outgoing, **fixture_args)
        started = time.monotonic()
        seeds = [
            [(90 - radius) / 320, (90 - radius) / 180, radius / 160, radius / 90],
            DISC[1],
        ]
        # One real track per side; every model below reuses these boxes.
        tracks = [
            real_native(path, start, 30, spec, rate, MediaTools(), seed)["boxes"]
            for path, start, seed in ((outgoing, 30, seeds[0]), (partner, 0, seeds[1]))
        ]
        tracked = time.monotonic() - started
        results = {}
        for model in match_cuts.MODELS:
            used = []
            target, transforms = match_cuts.align(tracks, model, used)
            out = folder / model
            out.mkdir()
            shutil.copyfile(outgoing, out / "round-object-a.mp4")
            shutil.copyfile(partner, out / "round-object-b.mp4")
            review = dict(
                result,
                target=target,
                operations=[
                    dict(
                        op,
                        keyframes=[
                            dict(frame=o + i, **v) for i, v in enumerate(values)
                        ],
                    )
                    for op, o, values in zip(result["operations"], (30, 0), transforms)
                ],
                source_hashes={
                    key: hashlib.sha256((out / name).read_bytes()).hexdigest()
                    for key, name in (
                        ("1", "round-object-a.mp4"),
                        ("2", "round-object-b.mp4"),
                    )
                },
            )
            (out / "controlled.json").write_text(json.dumps(review))
            with (out / "native-check.log").open("w") as log:
                native = subprocess.run(
                    [str(build / "bin/assistanttest"), "[Montaz]"],
                    stdout=log,
                    stderr=log,
                    env=dict(os.environ, MONTAZ_MATCH_QA_ROOT=str(out)),
                    timeout=90,
                )
            scales = [k["scale"] for k in transforms[0]]
            results[model] = dict(
                fits=used,
                native_check="pass" if native.returncode == 0 else "fail",
                export=str(out / "native-alignment.mp4"),
                outgoing_scale=[scales[0], scales[-1]],
                transform=dict(
                    **match_cuts.metrics(transforms), **stability(transforms)
                ),
                decoded=decoded(out / "native-alignment.mp4")
                if native.returncode == 0
                else None,
            )
        sweep_started = time.monotonic()
        sweep = truth_sweep(
            match_cuts.align,
            match_cuts.MODELS,
            lambda side, start, box: real_native(
                outgoing if side == "outgoing" else partner,
                start,
                30,
                spec,
                rate,
                MediaTools(),
                box,
            )["boxes"],
            fixture_args,
        )
        sweep["seconds"] = round(time.monotonic() - sweep_started, 3)
        comparison[scenario] = dict(
            truth_sweep=sweep,
            measured_height_ratio=tracks[0][-1][3] / tracks[0][0][3],
            true_height_ratio=(radius + 29 * fixture_args.get("growth", 0)) / radius,
            helper_calls=2,
            provider_calls=0,
            tracking_seconds=round(tracked, 3),
            models=results,
        )
    evidence["stability"] = dict(
        comparison=comparison,
        definitions=dict(
            translation="RMS per-frame movement of the source frame center on canvas",
            scale_change="RMS per-frame change of the uniform Transform scale",
            scale_acceleration="RMS second difference of the scale",
            decoded="disc measured in the 640x360 decoded export, frames 30..89: "
            "relative size change/acceleration, centroid motion (canvas), and "
            "frame 59/60 center distance and size ratio",
            truth_sweep="ten window offsets of real tracks scored against the known "
            "disc center/radius on every frame: cut center distance and size ratio, "
            "on-screen motion RMS and relative size change RMS",
        ),
    )
    (root / "selection-report.json").write_text(json.dumps(evidence, indent=2))
    constant = comparison["constant"]["models"]
    zoom = comparison["zoom"]["models"]
    truth = comparison["constant"]["truth_sweep"]["models"]
    if (
        any(r["native_check"] != "pass" for r in constant.values())
        # Provisional S5 goal: halve RMS scale change on the constant-size fixture.
        or constant["stable"]["transform"]["scale_change"]
        > constant["baseline"]["transform"]["scale_change"] / 2
        or constant["stable"]["decoded"]["size_change_rms"]
        > constant["baseline"]["decoded"]["size_change_rms"] / 2
        # Without worsening boundary alignment against ground truth.
        or truth["stable"]["cut_center_max"]
        > max(0.007, truth["baseline"]["cut_center_max"])
        or truth["stable"]["cut_center_mean"]
        > truth["baseline"]["cut_center_mean"] * 1.05
        or truth["stable"]["cut_size_mean"] > truth["baseline"]["cut_size_mean"]
        # Genuine zoom keeps its direction.
        or zoom["stable"]["outgoing_scale"][0] <= zoom["stable"]["outgoing_scale"][1]
    ):
        raise RuntimeError("Stability comparison missed its engineering criteria")
    for scenario, data in comparison.items():
        for model, r in data["truth_sweep"]["models"].items():
            print(
                f"  truth {model:9} cut {r['cut_center_mean']:.4f}/{r['cut_center_max']:.4f} "
                f"size {r['cut_size_mean']:.4f}/{r['cut_size_max']:.4f} "
                f"motion {r['motion_rms_mean']:.4f} size change {r['size_change_rms_mean']:.4f}"
            )
        print(
            f"{scenario}: measured/true outgoing height ratio "
            f"{data['measured_height_ratio']:.3f}/{data['true_height_ratio']:.3f}"
        )
        for model, r in data["models"].items():
            t, d = r["transform"], r["decoded"] or {}
            print(
                f"  {model:9} {r['native_check']} zoom {t['zoom']:.3f} crop {t['crop']:.3f} "
                f"dS {t['scale_change']:.4f} d2S {t['scale_acceleration']:.4f} "
                f"move {t['translation']:.4f} | decoded size {d.get('size_change_rms', 0):.4f} "
                f"motion {d.get('residual_motion_rms', 0):.4f} "
                f"cut {d.get('boundary_center', 0):.4f}/{d.get('boundary_size', 0):.4f}"
            )
    print(
        "Native candidate selection, tracking, application, Undo/Redo, "
        "serialization, export and stability comparison passed."
    )
    print(f"Selection report: {root / 'selection-report.json'}")
    print(f"Review output: {root / 'native-alignment.mp4'}")


if __name__ == "__main__":
    main()
