# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""One deterministic diagnostic request per process; no provider calls."""

import sys

from .contracts import InsertClip, Proposal, ProposalError, validate_proposal
from .lifecycle import Run, Status
from .protocol import MAX_MESSAGE_BYTES, decode, encode, fields, frame, snapshot_from_wire, to_wire


def propose_demo(snapshot, request_id: str) -> Proposal:
    """Append up to six distinct visual sources to an unlocked video track."""
    tracks = [track for track in snapshot.tracks if track.kind == "video" and not track.locked]
    assets = [asset for asset in snapshot.assets if asset.has_video][:6]
    if not tracks or not assets:
        raise ProposalError("Import video and unlock a video track before requesting a preview")
    target = tracks[0]
    position = max(
        (item.start_frame + item.duration_frames for item in snapshot.occupied), default=0
    )
    duration = max(1, snapshot.frame_rate.seconds_to_frame("2"))
    operations = []
    for index, asset in enumerate(assets):
        length = min(duration, asset.duration_frames)
        frame(position + length)
        operations.append(
            InsertClip(f"{request_id}:{index}", asset.bin_id, target.track_id, position, 0, length)
        )
        position += length
    proposal = Proposal(
        request_id,
        snapshot.project_id,
        snapshot.sequence_id,
        snapshot.revision,
        snapshot.frame_rate,
        tuple(operations),
    )
    validate_proposal(snapshot, proposal)
    return proposal


def main() -> int:
    output = sys.stdout.buffer
    output.write(encode({"version": 1, "type": "ready"}))
    output.flush()
    try:
        line = sys.stdin.buffer.readline(MAX_MESSAGE_BYTES + 1)
        request = fields(decode(line), {"version", "type", "request_id", "snapshot"})
        if (
            type(request["version"]) is not int
            or request["version"] != 1
            or request["type"] != "plan_demo"
        ):
            raise ProposalError("Unsupported request")
        # Validate request IDs through the same typed proposal constructor.
        snapshot = snapshot_from_wire(request["snapshot"])
        proposal = propose_demo(snapshot, request["request_id"])
        run = Run(proposal.request_id, ("planner",))
        run.transition("planner", Status.RUNNING)
        run.transition("planner", Status.COMPLETED)
        run.finish()
        for event in run.events:
            output.write(encode({"version": 1, "type": "event", **to_wire(event)}))
        output.write(encode({"version": 1, "type": "proposal", "proposal": to_wire(proposal)}))
        output.flush()
        return 0
    except (ProposalError, TypeError, KeyError, AttributeError, RecursionError):
        # Avoid reflecting arbitrary input, credentials, or source paths in diagnostics.
        output.write(encode({"version": 1, "type": "error", "code": "invalid_request"}))
        output.flush()
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
