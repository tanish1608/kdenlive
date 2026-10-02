# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""One described sound effect in an editor-issued, bounded video interval."""

import hashlib
import json
import os
import socket
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

from .gemini import AssistantError
from .music import _validate_wave, measure, unusable_provider_audio
from .protocol import decode, fields, frame

EFFECT_MODEL = "eleven_text_to_sound_v2"
EFFECT_VERSION = "effect-1"
MAX_EFFECT_BYTES = 3 * 1024 * 1024


class ElevenLabsEffects:
    model = EFFECT_MODEL

    def __init__(self, key: str):
        if type(key) is not str or not key.strip():
            raise AssistantError("missing_effect_key")
        self._key = key.strip()

    @classmethod
    def configured(cls):
        path = Path(
            os.environ.get(
                "MONTAZ_ASSISTANT_CONFIG", Path.home() / ".config/montaz/assistant.json"
            )
        )
        try:
            settings = decode(path.read_bytes()) if path.exists() else {}
        except (OSError, ValueError) as exc:
            raise AssistantError("invalid_settings") from exc
        if type(settings) is not dict:
            raise AssistantError("invalid_settings")
        return cls(
            os.environ.get("ELEVENLABS_API_KEY")
            or settings.get("elevenlabs_api_key", "")
        )

    def compose(self, description: str, seconds: float) -> bytes:
        request = urllib.request.Request(
            "https://api.elevenlabs.io/v1/sound-generation?output_format=mp3_44100_128",
            data=json.dumps(
                dict(
                    text=description,
                    duration_seconds=seconds,
                    model_id=self.model,
                    loop=False,
                    prompt_influence=0.3,
                )
            ).encode(),
            headers={"Content-Type": "application/json", "xi-api-key": self._key},
        )
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                audio = response.read(MAX_EFFECT_BYTES + 1)
                if not audio or len(audio) > MAX_EFFECT_BYTES:
                    raise AssistantError("invalid_effect_audio")
                return audio
        except urllib.error.HTTPError as exc:
            code = "effect_auth" if exc.code in (401, 403) else "effect_unavailable"
            if exc.code == 422:
                code = "effect_request_rejected"
            raise AssistantError(code) from None
        except (TimeoutError, socket.timeout) as exc:
            raise AssistantError("effect_timeout") from exc
        except urllib.error.URLError as exc:
            if isinstance(exc.reason, (TimeoutError, socket.timeout)):
                raise AssistantError("effect_timeout") from None
            raise AssistantError("effect_unavailable") from None
        except OSError as exc:
            raise AssistantError("effect_unavailable") from exc


def effect_spec(snapshot, raw):
    spec = fields(
        raw, {"path", "track_id", "start_frame", "duration_frames", "instruction"}
    )
    start, length = frame(spec["start_frame"]), frame(spec["duration_frames"], 1)
    frame(start + length)
    if (
        type(spec["path"]) is not str
        or len(spec["path"]) > 4096
        or not Path(spec["path"]).is_absolute()
        or Path(spec["path"]).suffix != ".wav"
        or type(spec["instruction"]) is not str
        or not spec["instruction"].strip()
        or len(spec["instruction"]) > 1000
        or not snapshot.frame_rate.numerator
        <= 2 * length * snapshot.frame_rate.denominator
        <= 20 * snapshot.frame_rate.numerator
    ):
        raise AssistantError("invalid_effect_request")
    tracks = {t.track_id: t for t in snapshot.tracks}
    track = tracks.get(spec["track_id"]) if type(spec["track_id"]) is str else None
    if not track or track.kind != "audio" or track.locked:
        raise AssistantError("no_audio_track")
    if any(
        x.track_id == track.track_id
        and start < x.start_frame + x.duration_frames
        and x.start_frame < start + length
        for x in snapshot.occupied
    ):
        raise AssistantError("no_audio_track")
    if not any(
        tracks[x.track_id].kind == "video"
        and x.start_frame <= start
        and start + length <= x.start_frame + x.duration_frames
        for x in snapshot.occupied
    ):
        raise AssistantError("invalid_effect_request")
    return spec


def compose_effect(snapshot, request_id, raw_spec, provider, media, progress):
    spec = effect_spec(snapshot, raw_spec)
    path = Path(spec["path"])
    if path.exists() or not path.parent.is_dir():
        raise AssistantError("invalid_effect_request")
    media.check()
    seconds = (
        spec["duration_frames"]
        * snapshot.frame_rate.denominator
        / snapshot.frame_rate.numerator
    )
    prompt = (
        spec["instruction"].strip()
        + ". A sound effect only, no music, voices or speech."
    )
    identity = dict(
        version=EFFECT_VERSION, model=provider.model, prompt=prompt, seconds=seconds
    )
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    cached_path = path.parent / ("effect-" + digest + ".wav")
    raw_path = path.parent / ("effect-" + digest + ".mp3")
    reused = cached_path.is_file() or raw_path.is_file()
    progress("generation", reused)
    if not cached_path.is_file():
        if not raw_path.is_file():
            audio = provider.compose(prompt, seconds)
            with tempfile.NamedTemporaryFile(
                prefix=".effect-", dir=path.parent, delete=False
            ) as file:
                file.write(audio)
                temporary_raw = Path(file.name)
            os.replace(temporary_raw, raw_path)
        media.check()
        with tempfile.TemporaryDirectory(
            prefix=".effect-", dir=path.parent
        ) as temporary:
            normalized = Path(temporary) / "normalized.wav"
            tolerance = snapshot.frame_rate.denominator / snapshot.frame_rate.numerator
            with unusable_provider_audio(raw_path):
                media._output(
                    [
                        os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
                        "-v",
                        "error",
                        "-nostdin",
                        "-i",
                        str(raw_path),
                        "-t",
                        f"{seconds:.9f}",
                        "-af",
                        "loudnorm=I=-26:TP=-6:LRA=7",
                        "-map_metadata",
                        "-1",
                        "-ac",
                        "2",
                        "-ar",
                        "48000",
                        "-c:a",
                        "pcm_s16le",
                        str(normalized),
                    ]
                )
                _validate_wave(normalized, seconds, tolerance)
            prepared = Path(temporary) / "prepared.wav"
            fade = min(0.1, seconds / 4)
            media._output(
                [
                    os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
                    "-v",
                    "error",
                    "-nostdin",
                    "-i",
                    str(normalized),
                    "-t",
                    f"{seconds:.9f}",
                    "-af",
                    f"apad=pad_dur={tolerance},afade=t=in:d=0.005,afade=t=out:st={seconds - fade}:d={fade}",
                    "-map_metadata",
                    "-1",
                    "-c:a",
                    "pcm_s16le",
                    str(prepared),
                ]
            )
            _validate_wave(prepared, seconds)
            media.check()
            prepared.chmod(0o600)
            os.replace(prepared, cached_path)
    with unusable_provider_audio(cached_path):
        _validate_wave(cached_path, seconds)
    media.check()
    progress("measurement", reused)
    with unusable_provider_audio(cached_path, raw_path):
        measure(cached_path, snapshot.frame_rate, spec["duration_frames"], media)
    sha = hashlib.sha256()
    with cached_path.open("rb") as source, path.open("xb") as output:
        path.chmod(0o600)
        while chunk := source.read(1024 * 1024):
            media.check()
            output.write(chunk)
            sha.update(chunk)
    return dict(
        request_id=request_id,
        project_id=snapshot.project_id,
        sequence_id=snapshot.sequence_id,
        base_revision=snapshot.revision,
        frame_rate=dict(
            numerator=snapshot.frame_rate.numerator,
            denominator=snapshot.frame_rate.denominator,
        ),
        **{k: spec[k] for k in ("path", "track_id", "start_frame", "duration_frames")},
        sha256=sha.hexdigest(),
        accents=[],
        cached=reused,
    )
