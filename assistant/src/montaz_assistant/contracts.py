# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Immutable proposals against editor-owned snapshots; no editor mutations."""

from dataclasses import dataclass
from fractions import Fraction
from typing import Literal


class ProposalError(ValueError):
    """A proposal cannot safely apply to the supplied editor snapshot."""


def _integer(value: int, minimum: int = 0) -> None:
    if type(value) is not int or value < minimum:
        raise ProposalError(f"Expected an integer >= {minimum}")


def _identifier(value: str) -> None:
    if not isinstance(value, str) or not value.strip() or len(value) > 128:
        raise ProposalError("Expected a nonempty identifier of at most 128 characters")
    if any(ord(c) < 32 for c in value):
        raise ProposalError("Control characters are not allowed in identifiers")


@dataclass(frozen=True)
class FrameRate:
    numerator: int
    denominator: int = 1

    def __post_init__(self):
        _integer(self.numerator, 1)
        _integer(self.denominator, 1)
        rate = Fraction(self.numerator, self.denominator)
        object.__setattr__(self, "numerator", rate.numerator)
        object.__setattr__(self, "denominator", rate.denominator)

    def seconds_to_frame(self, seconds: str) -> int:
        """Round a nonnegative decimal provider timestamp to nearest frame, half up."""
        if not isinstance(seconds, str):
            raise ProposalError("Provider timestamp must be a string")
        try:
            value = Fraction(seconds) * self.numerator / self.denominator
        except (ValueError, TypeError, ZeroDivisionError) as exc:
            raise ProposalError("Invalid provider timestamp") from exc
        if value < 0:
            raise ProposalError("Provider timestamp must be nonnegative")
        return (2 * value.numerator + value.denominator) // (2 * value.denominator)


@dataclass(frozen=True)
class Asset:
    bin_id: str
    duration_frames: int  # Kdenlive-conformed duration at the project frame rate.
    has_video: bool = True
    has_audio: bool = True

    def __post_init__(self):
        _identifier(self.bin_id)
        _integer(self.duration_frames, 1)
        if type(self.has_video) is not bool or type(self.has_audio) is not bool:
            raise ProposalError("Asset stream flags must be booleans")


@dataclass(frozen=True)
class Track:
    track_id: str
    kind: Literal["video", "audio"]
    locked: bool = False

    def __post_init__(self):
        _identifier(self.track_id)
        if self.kind not in {"video", "audio"} or type(self.locked) is not bool:
            raise ProposalError("Invalid track kind or lock state")


@dataclass(frozen=True)
class OccupiedRange:
    instance_id: str
    track_id: str
    start_frame: int
    duration_frames: int

    def __post_init__(self):
        _identifier(self.instance_id)
        _identifier(self.track_id)
        _integer(self.start_frame)
        _integer(self.duration_frames, 1)


@dataclass(frozen=True)
class Snapshot:
    project_id: str
    sequence_id: str
    revision: int
    frame_rate: FrameRate
    assets: tuple[Asset, ...]
    tracks: tuple[Track, ...]
    occupied: tuple[OccupiedRange, ...] = ()

    def __post_init__(self):
        _identifier(self.project_id)
        _identifier(self.sequence_id)
        _integer(self.revision)
        if not isinstance(self.frame_rate, FrameRate):
            raise ProposalError("Snapshot requires a rational frame rate")
        for values, cls, key in (
            (self.assets, Asset, "bin_id"),
            (self.tracks, Track, "track_id"),
            (self.occupied, OccupiedRange, "instance_id"),
        ):
            if type(values) is not tuple or any(not isinstance(x, cls) for x in values):
                raise ProposalError("Snapshot collections must be immutable typed tuples")
            ids = [getattr(x, key) for x in values]
            if len(ids) != len(set(ids)):
                raise ProposalError(f"Duplicate snapshot {key}")
        tracks = {t.track_id for t in self.tracks}
        if any(item.track_id not in tracks for item in self.occupied):
            raise ProposalError("Occupied range references an unknown track")


@dataclass(frozen=True)
class InsertClip:
    instance_id: str
    bin_id: str
    track_id: str
    start_frame: int
    in_frame: int
    duration_frames: int

    def __post_init__(self):
        for value in (self.instance_id, self.bin_id, self.track_id):
            _identifier(value)
        for value in (self.start_frame, self.in_frame):
            _integer(value)
        _integer(self.duration_frames, 1)


@dataclass(frozen=True)
class Proposal:
    request_id: str
    project_id: str
    sequence_id: str
    base_revision: int
    frame_rate: FrameRate
    operations: tuple[InsertClip, ...]
    protocol_version: int = 1

    def __post_init__(self):
        for value in (self.request_id, self.project_id, self.sequence_id):
            _identifier(value)
        _integer(self.base_revision)
        _integer(self.protocol_version, 1)
        if self.protocol_version != 1:
            raise ProposalError("Unsupported protocol version")
        if not isinstance(self.frame_rate, FrameRate):
            raise ProposalError("Proposal requires a rational frame rate")
        if type(self.operations) is not tuple or not 1 <= len(self.operations) <= 256:
            raise ProposalError("A proposal requires 1–256 immutable operations")
        if any(type(op) is not InsertClip for op in self.operations):
            raise ProposalError("Protocol v1 supports InsertClip only")


def validate_proposal(
    snapshot: Snapshot,
    proposal: Proposal,
    applied_request_ids: frozenset[str] = frozenset(),
) -> tuple[InsertClip, ...]:
    """Reject incompatible insertions. Success does not apply or acknowledge them.

    Kdenlive must repeat these checks on the GUI thread against its current
    state immediately before applying the native command batch.
    """
    if (proposal.project_id, proposal.sequence_id) != (snapshot.project_id, snapshot.sequence_id):
        raise ProposalError("Proposal targets another project or sequence")
    if proposal.base_revision != snapshot.revision:
        raise ProposalError("Stale proposal: the editor revision changed")
    if proposal.frame_rate != snapshot.frame_rate:
        raise ProposalError("Project frame rate changed")
    if proposal.request_id in applied_request_ids:
        raise ProposalError("Request already applied")

    assets = {a.bin_id: a for a in snapshot.assets}
    tracks = {t.track_id: t for t in snapshot.tracks}
    instance_ids = {r.instance_id for r in snapshot.occupied}
    ranges: dict[str, list[tuple[int, int]]] = {}
    for item in snapshot.occupied:
        ranges.setdefault(item.track_id, []).append(
            (item.start_frame, item.start_frame + item.duration_frames)
        )

    for op in proposal.operations:
        if op.instance_id in instance_ids:
            raise ProposalError("Duplicate clip instance ID")
        instance_ids.add(op.instance_id)
        asset, track = assets.get(op.bin_id), tracks.get(op.track_id)
        if asset is None or track is None:
            raise ProposalError("Unknown imported asset or track")
        if track.locked:
            raise ProposalError("Target track is locked")
        if (track.kind == "video" and not asset.has_video) or (
            track.kind == "audio" and not asset.has_audio
        ):
            raise ProposalError("Asset has no matching stream for the target track")
        if op.in_frame + op.duration_frames > asset.duration_frames:
            raise ProposalError("Clip source range exceeds the imported asset")
        start, end = op.start_frame, op.start_frame + op.duration_frames
        occupied = ranges.setdefault(op.track_id, [])
        if any(
            start < previous_end and previous_start < end
            for previous_start, previous_end in occupied
        ):
            raise ProposalError("Insertion overlaps occupied frames; use explicit edit semantics")
        occupied.append((start, end))

    return proposal.operations
