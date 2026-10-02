# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Editor-issued replacement scope; the model never chooses clips to delete."""

from dataclasses import replace

from .contracts import Proposal, ProposalError, Snapshot, validate_proposal
from .protocol import fields, frame


def draft_context(snapshot: Snapshot, spec: dict) -> tuple[Snapshot, dict]:
    clips = fields(spec, {"clips"})["clips"]
    if type(clips) is not list or not 1 <= len(clips) <= 256:
        raise ProposalError("Invalid draft scope")
    occupied = {c.instance_id: c for c in snapshot.occupied}
    assets = {a.bin_id: a for a in snapshot.assets}
    tracks = {t.track_id: t for t in snapshot.tracks}
    seen, end, track_id, start = set(), None, None, None
    for raw in clips:
        c = fields(
            raw,
            {
                "instance_id",
                "bin_id",
                "track_id",
                "start_frame",
                "in_frame",
                "duration_frames",
            },
        )
        if any(type(c[k]) is not str for k in ("instance_id", "bin_id", "track_id")):
            raise ProposalError("Invalid draft identity")
        at, source, length = (
            frame(c["start_frame"]),
            frame(c["in_frame"]),
            frame(c["duration_frames"], 1),
        )
        frame(at + length)
        original, asset, track = (
            occupied.get(c["instance_id"]),
            assets.get(c["bin_id"]),
            tracks.get(c["track_id"]),
        )
        if (
            original is None
            or asset is None
            or track is None
            or c["instance_id"] in seen
            or track.locked
            or track.kind != "video"
            or not asset.has_video
            or (original.track_id, original.start_frame, original.duration_frames)
            != (c["track_id"], at, length)
            or source + length > asset.duration_frames
            or (end is not None and (at != end or c["track_id"] != track_id))
        ):
            raise ProposalError("Draft scope does not match current clips")
        seen.add(c["instance_id"])
        if start is None:
            start, track_id = at, c["track_id"]
        end = at + length
    planning = replace(
        snapshot,
        occupied=tuple(c for c in snapshot.occupied if c.instance_id not in seen),
    )
    return planning, dict(
        track_id=track_id, start_frame=start, duration_frames=end - start
    )


def validate_draft_proposal(snapshot: Snapshot, spec: dict, proposal: Proposal) -> None:
    planning, placement = draft_context(snapshot, spec)
    validate_proposal(planning, proposal)
    existing = {c.instance_id for c in snapshot.occupied}
    position = placement["start_frame"]
    for op in proposal.operations:
        if (
            op.instance_id in existing
            or op.track_id != placement["track_id"]
            or op.start_frame != position
        ):
            raise ProposalError("Revision escapes the editor-issued draft")
        position += op.duration_frames
    if position != placement["start_frame"] + placement["duration_frames"]:
        raise ProposalError("Revision must preserve the draft's exact span")
