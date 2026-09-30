# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Strict, bounded JSON contract shared with the native adapter."""

import json
from dataclasses import asdict

from .contracts import Asset, FrameRate, OccupiedRange, ProposalError, Snapshot, Track

MAX_MESSAGE_BYTES = 1024 * 1024
MAX_FRAME = 2**31 - 1


def fields(value: object, names: set[str]) -> dict:
    if type(value) is not dict or set(value) != names:
        raise ProposalError("Invalid message fields")
    return value


def frame(value: object, minimum: int = 0) -> int:
    if type(value) is not int or not minimum <= value <= MAX_FRAME:
        raise ProposalError("Frame value outside native integer range")
    return value


def decode(line: bytes) -> dict:
    if len(line) > MAX_MESSAGE_BYTES:
        raise ProposalError("Message too large")

    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProposalError("Duplicate JSON field")
            result[key] = value
        return result

    try:
        value = json.loads(line, object_pairs_hook=unique_pairs)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ProposalError("Malformed JSON") from exc
    if type(value) is not dict:
        raise ProposalError("Message must be an object")
    return value


def encode(value: dict) -> bytes:
    line = json.dumps(value, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(line) > MAX_MESSAGE_BYTES:
        raise ProposalError("Message too large")
    return line + b"\n"


def snapshot_from_wire(value: object) -> Snapshot:
    data = fields(
        value,
        {"project_id", "sequence_id", "revision", "frame_rate", "assets", "tracks", "occupied"},
    )
    rate = fields(data["frame_rate"], {"numerator", "denominator"})
    frame(rate["numerator"], 1)
    frame(rate["denominator"], 1)
    frame(data["revision"])
    for key in ("assets", "tracks", "occupied"):
        if type(data[key]) is not list or len(data[key]) > 10000:
            raise ProposalError("Invalid snapshot collection")
    assets = []
    for raw in data["assets"]:
        item = fields(raw, {"bin_id", "duration_frames", "has_video", "has_audio"})
        frame(item["duration_frames"], 1)
        assets.append(Asset(**item))
    tracks = [Track(**fields(raw, {"track_id", "kind", "locked"})) for raw in data["tracks"]]
    occupied = []
    for raw in data["occupied"]:
        item = fields(raw, {"instance_id", "track_id", "start_frame", "duration_frames"})
        start = frame(item["start_frame"])
        duration = frame(item["duration_frames"], 1)
        frame(start + duration)
        occupied.append(OccupiedRange(**item))
    return Snapshot(
        data["project_id"],
        data["sequence_id"],
        data["revision"],
        FrameRate(**rate),
        tuple(assets),
        tuple(tracks),
        tuple(occupied),
    )


def to_wire(value) -> dict:
    return asdict(value)
