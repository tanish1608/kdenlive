# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded instrumental generation; output destinations are issued by the editor."""

import hashlib
import json
import math
import os
import socket
import tempfile
import urllib.error
import urllib.request
import wave
from array import array
from pathlib import Path

from .gemini import AssistantError
from .protocol import decode, fields, frame

MUSIC_VERSION = "instrumental-1"
MAX_AUDIO_BYTES = 24 * 1024 * 1024
STYLES = {
    "postcard": "Warm uplifting instrumental travel music, organic percussion, mellow guitar, "
    "a gentle build and a resolved ending. Clear rhythm, no dramatic trailer hits.",
    "adventure": "Energetic instrumental travel music with crisp percussion and a clear pulse, "
    "a short opening, lively middle and resolved ending. No trailer braams.",
    "diary": "Reflective instrumental travel diary, warm piano and gentle organic textures, "
    "a restrained pulse and a peaceful resolved ending.",
}


class ElevenLabs:
    def __init__(self, key: str, model: str = "music_v2_5"):
        if type(key) is not str or not key.strip():
            raise AssistantError("missing_music_key")
        if type(model) is not str or model not in {
            "music_v1",
            "music_v2",
            "music_v2_5",
        }:
            raise AssistantError("invalid_settings")
        self._key = key.strip()
        self.model = model

    @classmethod
    def configured(cls):
        path = Path(
            os.environ.get("MONTAZ_ASSISTANT_CONFIG", Path.home() / ".config/montaz/assistant.json")
        )
        try:
            settings = decode(path.read_bytes()) if path.exists() else {}
        except (OSError, ValueError) as exc:
            raise AssistantError("invalid_settings") from exc
        if type(settings) is not dict:
            raise AssistantError("invalid_settings")
        return cls(
            os.environ.get("ELEVENLABS_API_KEY") or settings.get("elevenlabs_api_key", ""),
            os.environ.get("ELEVENLABS_MUSIC_MODEL")
            or settings.get("elevenlabs_music_model", "music_v2_5"),
        )

    def compose(self, prompt: str, milliseconds: int) -> bytes:
        request = urllib.request.Request(
            "https://api.elevenlabs.io/v1/music?output_format=auto",
            data=json.dumps(
                {
                    "prompt": prompt,
                    "music_length_ms": milliseconds,
                    "model_id": self.model,
                    "force_instrumental": True,
                }
            ).encode(),
            headers={"Content-Type": "application/json", "xi-api-key": self._key},
        )
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                data = response.read(MAX_AUDIO_BYTES + 1)
                if not data or len(data) > MAX_AUDIO_BYTES:
                    raise AssistantError("invalid_music_audio")
                return data
        except urllib.error.HTTPError as exc:
            code = "music_auth" if exc.code in (401, 403) else "music_unavailable"
            if exc.code == 422:
                code = "music_request_rejected"
            raise AssistantError(code) from None
        except (TimeoutError, socket.timeout) as exc:
            raise AssistantError("music_timeout") from exc
        except urllib.error.URLError as exc:
            if isinstance(exc.reason, (TimeoutError, socket.timeout)):
                raise AssistantError("music_timeout") from None
            raise AssistantError("music_unavailable") from None
        except OSError as exc:
            raise AssistantError("music_unavailable") from exc


def music_spec(snapshot, value: object) -> dict:
    spec = fields(
        value,
        {"path", "track_id", "start_frame", "duration_frames", "recipe", "instruction"},
    )
    start, duration = frame(spec["start_frame"]), frame(spec["duration_frames"], 1)
    frame(start + duration)
    if (
        type(spec["path"]) is not str
        or len(spec["path"]) > 4096
        or not Path(spec["path"]).is_absolute()
        or Path(spec["path"]).suffix != ".wav"
        or type(spec["recipe"]) is not str
        or spec["recipe"] not in STYLES
        or type(spec["instruction"]) is not str
        or len(spec["instruction"]) > 2000
        or not 3 * snapshot.frame_rate.numerator
        <= duration * snapshot.frame_rate.denominator
        <= 600 * snapshot.frame_rate.numerator
    ):
        raise AssistantError("invalid_music_request")
    tracks = {t.track_id: t for t in snapshot.tracks}
    track = tracks.get(spec["track_id"]) if type(spec["track_id"]) is str else None
    if not track or track.kind != "audio" or track.locked:
        raise AssistantError("no_audio_track")
    if any(
        x.track_id == track.track_id
        and start < x.start_frame + x.duration_frames
        and x.start_frame < start + duration
        for x in snapshot.occupied
    ):
        raise AssistantError("no_audio_track")
    video_ids = {t.track_id for t in snapshot.tracks if t.kind == "video"}
    visuals = [x for x in snapshot.occupied if x.track_id in video_ids]
    if (
        not visuals
        or start != min(x.start_frame for x in visuals)
        or start + duration != max(x.start_frame + x.duration_frames for x in visuals)
    ):
        raise AssistantError("invalid_music_request")
    return spec


def measure(path: Path, rate, duration: int, media) -> dict:
    """Find audible energy accents, not a claim of musical beats or downbeats."""
    raw = media._output(
        [
            os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
            "-v",
            "error",
            "-nostdin",
            "-i",
            str(path),
            "-ac",
            "1",
            "-ar",
            "8000",
            "-f",
            "s16le",
            "pipe:1",
        ],
        maximum_bytes=10 * 1024 * 1024,
    )
    samples = array("h")
    samples.frombytes(raw)
    if not samples or max(abs(x) for x in samples) < 100:
        raise AssistantError("invalid_music_audio")
    energies = [
        math.sqrt(sum(x * x for x in samples[i : i + 160]) / len(samples[i : i + 160]))
        for i in range(0, len(samples), 160)
    ]
    accents = []
    for i in range(1, len(energies) - 1):
        rise = energies[i] - energies[i - 1]
        if rise > 100 and rise > 0.25 * max(energies[i - 1], 1) and energies[i] >= energies[i + 1]:
            point = rate.seconds_to_frame(str(i / 50))
            if 0 < point < duration and (
                not accents or point - accents[-1] >= rate.seconds_to_frame("0.3")
            ):
                accents.append(point)
    return {
        "accents": accents[:128],
        "peak_dbfs": round(20 * math.log10(max(abs(x) for x in samples) / 32768), 2),
    }


def compose_music(snapshot, request_id: str, raw_spec, provider, media, progress) -> dict:
    spec = music_spec(snapshot, raw_spec)
    path = Path(spec["path"])
    if path.exists() or not path.parent.is_dir():
        raise AssistantError("invalid_music_request")
    media.check()
    seconds = (
        spec["duration_frames"] * snapshot.frame_rate.denominator / snapshot.frame_rate.numerator
    )
    prompt = (
        STYLES[spec["recipe"]]
        + " Instrumental only. A complete short musical arc, no vocals or speech."
    )
    if spec["instruction"].strip():
        prompt += " Editorial mood: " + spec["instruction"].strip()
    identity = {
        "version": MUSIC_VERSION,
        "model": provider.model,
        "prompt": prompt,
        "milliseconds": math.ceil(seconds * 1000),
        "fps": [snapshot.frame_rate.numerator, snapshot.frame_rate.denominator],
    }
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    cached_path = path.parent / ("music-" + digest + ".wav")
    raw_path = path.parent / ("music-" + digest + ".mp3")
    cache_hit = cached_path.is_file()
    reused = cache_hit or raw_path.is_file()
    progress("generation", reused)
    if not cache_hit:
        if not raw_path.is_file():
            audio = provider.compose(prompt, identity["milliseconds"])
            # Keep the paid response before preparation or cancellation. A local
            # encoding retry must not silently submit another generation.
            with tempfile.NamedTemporaryFile(
                prefix=".music-", dir=path.parent, delete=False
            ) as raw:
                raw.write(audio)
                temporary_raw = Path(raw.name)
            os.replace(temporary_raw, raw_path)
        media.check()
        with tempfile.TemporaryDirectory(prefix=".music-", dir=path.parent) as temporary:
            target = Path(temporary) / "normalized.wav"
            # Loudness preparation and soft edges belong to this generated asset;
            # the live timeline still uses native audio insertion and volume controls.
            fade = min(0.5, seconds / 4)
            arguments = [
                os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
                "-v",
                "error",
                "-nostdin",
                "-i",
                str(raw_path),
                "-t",
                f"{seconds:.9f}",
                "-af",
                "loudnorm=I=-18:TP=-1:LRA=7",
                "-map_metadata",
                "-1",
                "-ac",
                "2",
                "-ar",
                "48000",
                "-c:a",
                "pcm_s16le",
                str(target),
            ]
            media._output(arguments)
            # MP3 music can end one codec block early. Only a shortfall of at
            # most one project frame may be padded; never stretch a short track.
            tolerance = snapshot.frame_rate.denominator / snapshot.frame_rate.numerator
            _validate_wave(target, seconds, tolerance)
            prepared = Path(temporary) / "prepared.wav"
            media._output(
                [
                    os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
                    "-v",
                    "error",
                    "-nostdin",
                    "-i",
                    str(target),
                    "-t",
                    f"{seconds:.9f}",
                    "-af",
                    f"apad=pad_dur={tolerance},afade=t=in:d={fade},afade=t=out:st={seconds - fade}:d={fade}",
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
    _validate_wave(cached_path, seconds)
    media.check()
    progress("measurement", reused)
    measurements = measure(cached_path, snapshot.frame_rate, spec["duration_frames"], media)
    media.check()
    # Exclusive creation: a cancelled/replayed request cannot overwrite an asset.
    sha = hashlib.sha256()
    with cached_path.open("rb") as source, path.open("xb") as output:
        path.chmod(0o600)
        while chunk := source.read(1024 * 1024):
            media.check()
            output.write(chunk)
            sha.update(chunk)
    return {
        "request_id": request_id,
        "project_id": snapshot.project_id,
        "sequence_id": snapshot.sequence_id,
        "base_revision": snapshot.revision,
        "frame_rate": {
            "numerator": snapshot.frame_rate.numerator,
            "denominator": snapshot.frame_rate.denominator,
        },
        **{k: spec[k] for k in ("path", "track_id", "start_frame", "duration_frames")},
        "sha256": sha.hexdigest(),
        "accents": measurements["accents"],
        "cached": reused,
    }


def _validate_wave(path: Path, expected_seconds: float, maximum_shortfall: float = 1 / 48000):
    try:
        with wave.open(str(path), "rb") as audio:
            actual = audio.getnframes() / audio.getframerate()
            if (
                audio.getnchannels() != 2
                or audio.getsampwidth() != 2
                or audio.getframerate() != 48000
                or actual + maximum_shortfall < expected_seconds
                or actual > expected_seconds + 0.1
            ):
                raise AssistantError("invalid_music_audio")
    except (OSError, ValueError, EOFError, wave.Error) as exc:
        raise AssistantError("invalid_music_audio") from exc
