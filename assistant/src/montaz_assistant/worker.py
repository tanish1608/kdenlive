# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""One bounded planning request per process; stdout is protocol only."""

import os
import signal
import sys
from pathlib import Path

from .contracts import InsertClip, Proposal, ProposalError, validate_proposal
from .gemini import AssistantError, Gemini
from .lifecycle import Run, Status
from .music import ElevenLabs, compose_music
from .timing import refine_timing
from .planning import MediaTools, brief_from_wire, plan_media
from .protocol import (
    MAX_MESSAGE_BYTES,
    decode,
    encode,
    fields,
    frame,
    snapshot_from_wire,
    to_wire,
)


def propose_demo(snapshot, request_id: str) -> Proposal:
    """Append up to six distinct visual sources to an unlocked video track."""
    tracks = [track for track in snapshot.tracks if track.kind == "video" and not track.locked]
    assets = [asset for asset in snapshot.assets if asset.has_video][:6]
    if not tracks or not assets:
        raise ProposalError("Import video and unlock a video track before requesting a preview")
    target = tracks[0]
    position = max(
        (item.start_frame + item.duration_frames for item in snapshot.occupied),
        default=0,
    )
    duration = max(1, snapshot.frame_rate.seconds_to_frame("2"))
    operations = []
    for index, asset in enumerate(assets):
        length = min(duration, asset.duration_frames)
        frame(position + length)
        operations.append(
            InsertClip(
                f"{request_id}:{index}",
                asset.bin_id,
                target.track_id,
                position,
                0,
                length,
            )
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
    media = MediaTools()
    signal.signal(signal.SIGTERM, lambda *_: media.cancel())

    def send_event(event):
        output.write(encode({"version": 1, "type": "event", **to_wire(event)}))
        output.flush()

    def progress(phase, completed, cached, total):
        output.write(
            encode(
                {
                    "version": 1,
                    "type": "progress",
                    "phase": phase,
                    "completed": completed,
                    "cached": cached,
                    "total": total,
                    "model": provider.model,
                }
            )
        )
        output.flush()

    try:
        line = sys.stdin.buffer.readline(MAX_MESSAGE_BYTES + 1)
        request = decode(line)
        keys = {"version", "type", "request_id", "snapshot"}
        real = request.get("type") == "plan_media"
        musical = request.get("type") == "compose_music"
        timing = request.get("type") == "refine_timing"
        fields(
            request,
            keys | {"timing"}
            if timing
            else keys | {"music"}
            if musical
            else keys | {"sources", "brief"}
            if real
            else keys,
        )
        if (
            type(request["version"]) is not int
            or request["version"] != 1
            or request["type"] not in {"plan_demo", "plan_media", "compose_music", "refine_timing"}
        ):
            raise ProposalError("Unsupported request")
        # Validate request IDs through the same typed proposal constructor.
        snapshot = snapshot_from_wire(request["snapshot"])
        request_id = request["request_id"]
        if type(request_id) is not str or not request_id or len(request_id) > 128:
            raise ProposalError("Invalid request ID")
        stage = "timing" if timing else "music" if musical else "planner"
        run = Run(request_id, (stage,))
        send_event(run.events[-1])
        run.transition(stage, Status.RUNNING)
        send_event(run.events[-1])
        if timing:
            proposal = refine_timing(snapshot, request_id, request["timing"], media)
        elif musical:
            provider = ElevenLabs.configured()

            def music_progress(phase, cached):
                output.write(
                    encode(
                        {
                            "version": 1,
                            "type": "music_progress",
                            "phase": phase,
                            "model": provider.model,
                            "cached": cached,
                        }
                    )
                )
                output.flush()

            soundtrack = compose_music(
                snapshot, request_id, request["music"], provider, media, music_progress
            )
        elif real:
            brief = brief_from_wire(request["brief"])
            provider = Gemini.configured()  # Missing keys fail before media/provider work.
            cache = Path(
                os.environ.get("MONTAZ_ANALYSIS_CACHE", Path.home() / ".cache/montaz/analysis")
            )
            reviews = []
            proposal = plan_media(
                snapshot,
                request_id,
                request["sources"],
                brief,
                cache,
                provider,
                media,
                progress,
                reviews.append,
            )
        else:
            proposal = propose_demo(snapshot, request_id)
        run.transition(stage, Status.COMPLETED)
        send_event(run.events[-1])
        run.finish()
        send_event(run.events[-1])
        message = (
            {"version": 1, "type": "timing", "proposal": proposal}
            if timing
            else {"version": 1, "type": "soundtrack", "soundtrack": soundtrack}
            if musical
            else {"version": 1, "type": "proposal", "proposal": to_wire(proposal)}
        )
        if real:
            message["review"] = reviews[0]
        output.write(encode(message))
        output.flush()
        return 0
    except (
        ProposalError,
        AssistantError,
        OSError,
        TypeError,
        KeyError,
        AttributeError,
        RecursionError,
    ) as exc:
        media.cancel()
        # Avoid reflecting arbitrary input, credentials, or source paths in diagnostics.
        code = exc.code if isinstance(exc, AssistantError) else "invalid_request"
        output.write(encode({"version": 1, "type": "error", "code": code}))
        output.flush()
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
