# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Local, conservative cut refinement against supported beats or energy accents."""

import os
import re
from pathlib import Path

from .beats import MAX_ANCHORS, analyze, music_hash
from .gemini import AssistantError
from .music import measure
from .protocol import fields, frame


def timing_spec(snapshot, raw):
    spec = fields(raw, {"clips", "music", "max_shift_frames"})
    music = fields(spec["music"], {"path", "sha256", "instance_id"})
    if (
        type(music["path"]) is not str
        or not Path(music["path"]).is_absolute()
        or Path(music["path"]).suffix != ".wav"
        or type(music["sha256"]) is not str
        or not re.fullmatch(r"[a-f0-9]{64}", music["sha256"])
        or type(spec["clips"]) is not list
        or not 2 <= len(spec["clips"]) <= 256
        or frame(spec["max_shift_frames"], 1)
        != max(
            1, snapshot.frame_rate.numerator // (4 * snapshot.frame_rate.denominator)
        )
    ):
        raise AssistantError("invalid_timing_request")
    assets = {a.bin_id: a for a in snapshot.assets}
    tracks = {t.track_id: t for t in snapshot.tracks}
    occupied = {o.instance_id: o for o in snapshot.occupied}
    previous, track_id, seen = None, None, set()
    minimum = (
        snapshot.frame_rate.numerator + snapshot.frame_rate.denominator - 1
    ) // snapshot.frame_rate.denominator
    for clip in spec["clips"]:
        fields(
            clip,
            {
                "instance_id",
                "bin_id",
                "track_id",
                "start_frame",
                "in_frame",
                "duration_frames",
            },
        )
        if any(type(clip[k]) is not str for k in ("instance_id", "bin_id", "track_id")):
            raise AssistantError("invalid_timing_request")
        item, asset, track = (
            occupied.get(clip["instance_id"]),
            assets.get(clip["bin_id"]),
            tracks.get(clip["track_id"]),
        )
        start, source, length = (
            frame(clip["start_frame"]),
            frame(clip["in_frame"]),
            frame(clip["duration_frames"], minimum),
        )
        frame(start + length)
        if (
            not item
            or not asset
            or not asset.has_video
            or not track
            or track.kind != "video"
            or track.locked
            or source + length > asset.duration_frames
            or item.track_id != track.track_id
            or item.start_frame != start
            or item.duration_frames != length
            or clip["instance_id"] in seen
            or (previous is not None and previous != start)
            or (track_id is not None and track_id != track.track_id)
        ):
            raise AssistantError("invalid_timing_request")
        seen.add(clip["instance_id"])
        previous, track_id = start + length, track.track_id
    audio = (
        occupied.get(music["instance_id"])
        if type(music["instance_id"]) is str
        else None
    )
    if (
        not audio
        or tracks[audio.track_id].kind != "audio"
        or audio.start_frame != spec["clips"][0]["start_frame"]
        or audio.start_frame + audio.duration_frames != previous
        or set(occupied) != seen | {audio.instance_id}
        or not 3 * snapshot.frame_rate.numerator
        <= audio.duration_frames * snapshot.frame_rate.denominator
        <= 600 * snapshot.frame_rate.numerator
    ):
        raise AssistantError("invalid_timing_request")
    return spec


def refine(snapshot, request_id, spec, accents, *, maximum_anchors=128):
    """Maximize feasible anchor matches, then minimize movement with dynamic programming."""
    spec = timing_spec(snapshot, spec)
    clips = spec["clips"]
    assets = {a.bin_id: a for a in snapshot.assets}
    start, end = (
        clips[0]["start_frame"],
        clips[-1]["start_frame"] + clips[-1]["duration_frames"],
    )
    if (
        type(accents) is not list
        or len(accents) > maximum_anchors
        or any(type(x) is not int or not 0 < x < end - start for x in accents)
        or accents != sorted(set(accents))
    ):
        raise AssistantError("invalid_timing_request")
    points = {start + x for x in accents}
    minimum = (
        snapshot.frame_rate.numerator + snapshot.frame_rate.denominator - 1
    ) // snapshot.frame_rate.denominator
    states = {start: ((0, 0), [start])}
    for index, clip in enumerate(clips):
        original = clip["start_frame"] + clip["duration_frames"]
        options = (
            {end}
            if index == len(clips) - 1
            else {original}
            | {p for p in points if abs(p - original) <= spec["max_shift_frames"]}
        )
        following = {}
        for point in sorted(options):
            for before, (score, path) in states.items():
                if (
                    not minimum
                    <= point - before
                    <= assets[clip["bin_id"]].duration_frames - clip["in_frame"]
                ):
                    continue
                cost = (
                    score[0] - int(index < len(clips) - 1 and point in points),
                    score[1] + abs(point - original),
                )
                if point not in following or cost < following[point][0]:
                    following[point] = (cost, path + [point])
        states = following
    if end not in states:
        raise AssistantError("invalid_timing_request")
    boundaries = states[end][1]
    operations = [
        dict(
            c,
            start_frame=boundaries[i],
            duration_frames=boundaries[i + 1] - boundaries[i],
        )
        for i, c in enumerate(clips)
    ]
    if operations == clips:
        raise AssistantError("no_alignment_needed")
    return dict(
        request_id=request_id,
        project_id=snapshot.project_id,
        sequence_id=snapshot.sequence_id,
        base_revision=snapshot.revision,
        frame_rate=dict(
            numerator=snapshot.frame_rate.numerator,
            denominator=snapshot.frame_rate.denominator,
        ),
        protocol_version=2,
        music_sha256=spec["music"]["sha256"],
        accents=accents,
        operations=operations,
    )


def refine_timing(snapshot, request_id, raw_spec, media):
    spec = timing_spec(snapshot, raw_spec)
    path = Path(spec["music"]["path"])
    if music_hash(path, media) != spec["music"]["sha256"]:
        raise AssistantError("media_changed")
    audio = next(
        o for o in snapshot.occupied if o.instance_id == spec["music"]["instance_id"]
    )
    cache = Path(
        os.environ.get("MONTAZ_ANALYSIS_CACHE", Path.home() / ".cache/montaz/analysis")
    )
    analysis = analyze(
        path,
        snapshot.frame_rate,
        audio.duration_frames,
        spec["music"]["sha256"],
        cache / "beats",
        media,
    )
    anchors = analysis.pop("beats")
    if analysis["mode"] == "accents":
        anchors = measure(path, snapshot.frame_rate, audio.duration_frames, media)[
            "accents"
        ]
    if music_hash(path, media) != spec["music"]["sha256"]:
        raise AssistantError("media_changed")
    media.check()
    proposal = refine(snapshot, request_id, spec, anchors, maximum_anchors=MAX_ANCHORS)
    proposal.update(
        protocol_version=4, anchors=proposal.pop("accents"), analysis=analysis
    )
    return proposal
