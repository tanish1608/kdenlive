# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Audio quality regressions through the public beat and caption helpers.

Synthetic band-split PCM drives the real onset/tempo code; only decoder and
provider boundaries are mocked. No credentials, network or real speech.
The reported bugs now run as ordinary regressions after their fixes.
"""

import hashlib
import math
import random
import tempfile
import unittest
from array import array
from fractions import Fraction
from pathlib import Path
from unittest.mock import Mock

from montaz_assistant.beats import analyze, estimate, onset_envelope
from montaz_assistant.captions import is_silent, propose_captions
from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools

SR = 8000  # beats.SAMPLE_RATE: the decoder's band-split output rate.
LABELLED = {
    "insufficient_onsets",
    "weak_periodicity",
    "irregular_onsets",
    "unstable_tempo",
}
VOICES = {
    "kick": (0, 800, 300, 80),
    "snare": (1, 900, 250, None),
    "hat": (2, 300, 60, None),
}


def render(seconds, events, pad=0.0, noise=0.0, seed=1) -> bytes:
    """Three interleaved bands (low, mid, high) of s16 at 8 kHz, as beats.analyze decodes."""
    n = int(seconds * SR)
    rng = random.Random(seed)
    bands = [[0.0] * n for _ in range(3)]
    if pad or noise:
        for i in range(n):
            bands[0][i] += pad * math.sin(2 * math.pi * 110 * i / SR)
            bands[1][i] += pad * 0.6 * math.sin(2 * math.pi * 440 * i / SR)
            if noise:
                for b in range(3):
                    bands[b][i] += rng.gauss(0, noise)
    for t, voice, amp in events:
        band, length, decay, freq = VOICES[voice]
        start = int(t * SR)
        for k in range(min(length, n - start)):
            wave = math.sin(2 * math.pi * freq * k / SR) if freq else rng.uniform(-1, 1)
            bands[band][start + k] += amp * math.exp(-k / decay) * wave
    out = array(
        "h",
        (
            max(-32767, min(32767, round(bands[b][i])))
            for i in range(n)
            for b in range(3)
        ),
    )
    return out.tobytes()


def grid(bpm, seconds, phase=0.1):
    times, k = [], 0
    while phase + k * 60 / bpm < seconds - 0.1:
        times.append(phase + k * 60 / bpm)
        k += 1
    return times


def syncopated(bars=6, bpm=120, phase=0.1):
    """Steady pulse; kicks on 1, the "and" of 2 and 4, snares on 2 and 4."""
    beat, events, pulse = 60 / bpm, [], []
    for bar in range(bars):
        b = [phase + (4 * bar + k) * beat for k in range(4)]
        pulse += b
        events += [
            (b[0], "kick", 16000),
            (b[1] + beat / 2, "kick", 14000),
            (b[3], "kick", 16000),
        ]
        events += [(b[1], "snare", 12000), (b[3], "snare", 12000)]
    return events, pulse


def score(result, truth, tolerance=0.03):
    beats = [t / 100 for t in result["beats"]]
    precision = sum(any(abs(b - t) <= tolerance for t in truth) for b in beats) / len(
        beats
    )
    recall = sum(any(abs(b - t) <= tolerance for b in beats) for t in truth) / len(
        truth
    )
    error = sum(min(abs(b - t) for t in truth) for b in beats) / len(beats)
    return precision, recall, error


def run(raw):
    return estimate(onset_envelope(raw, MediaTools()), MediaTools())


class BeatRegressionTests(unittest.TestCase):
    def test_steady_tempos_with_offbeat_hats_are_measured_and_supported(self):
        for bpm in (97.5, 120, 128):
            with self.subTest(bpm=bpm):
                pulse = grid(bpm, 12)
                hats = [(t + 30 / bpm, "hat", 4000) for t in pulse]
                result = run(render(12, [(t, "kick", 16000) for t in pulse] + hats))
                self.assertEqual(result["mode"], "beats", result)
                self.assertAlmostEqual(result["bpm"], bpm, delta=0.5)
                precision, recall, error = score(result, pulse)
                self.assertEqual(precision, 1.0)  # Every claimed beat is a real kick.
                self.assertGreaterEqual(recall, 0.9)
                self.assertLessEqual(error, 0.01)  # The 10 ms analysis tick.

    def test_half_time_kicks_over_double_time_hats_claim_only_supported_beats(self):
        pulse = grid(70, 12)
        result = run(
            render(
                12,
                [(t, "kick", 16000) for t in pulse]
                + [(t, "hat", 5000) for t in grid(140, 12)],
            )
        )
        self.assertEqual(result["mode"], "beats")
        self.assertAlmostEqual(result["bpm"], 70, delta=0.5)
        self.assertEqual(score(result, pulse)[0], 1.0)

    def test_quiet_sustained_music_under_kicks_keeps_the_pulse(self):
        pulse = grid(110, 12)
        result = run(render(12, [(t, "kick", 16000) for t in pulse], pad=1000))
        self.assertEqual(result["mode"], "beats")
        precision, recall, _ = score(result, pulse)
        self.assertEqual(precision, 1.0)
        self.assertGreaterEqual(recall, 0.9)

    @staticmethod
    def continuous_beds():
        """Kicks under a bed that is already audible at the first sample."""
        pad = grid(110, 12)
        noisy = grid(120, 12)
        return {
            "pad 5000": (
                render(12, [(t, "kick", 16000) for t in pad], pad=5000),
                pad,
                110,
            ),
            "pad 2500": (
                render(12, [(t, "kick", 16000) for t in pad], pad=2500),
                pad,
                110,
            ),
            "noise 1000": (
                render(12, [(t, "kick", 16000) for t in noisy], noise=1000),
                noisy,
                120,
            ),
            "noise 1500": (
                render(12, [(t, "kick", 16000) for t in noisy], noise=1500),
                noisy,
                120,
            ),
        }

    def test_known_bug_music_audible_from_the_first_sample_hides_every_onset(self):
        """onset_envelope starts each band's previous level at zero, so audio that
        is already sounding in the first 10 ms block scores a huge onset (19.7
        against about 1.9 per kick under a pad). estimate() normalises by that peak:
        quieter kicks drop below its 0.15 floor ("insufficient_onsets"), or half of
        them do and the tempo is halved with an unsupported beat at tick 0 (noise
        1000: 60 instead of 120 BPM). Proposed fix: treat the first block as the
        baseline (no rise) in onset_envelope."""
        for name, (raw, pulse, bpm) in self.continuous_beds().items():
            with self.subTest(name=name):
                result = run(raw)
                self.assertEqual(result["mode"], "beats")
                self.assertAlmostEqual(result["bpm"], bpm, delta=0.5)
                self.assertEqual(score(result, pulse)[0], 1.0)

    def test_initial_audio_is_baseline_and_attacks_remain_detectable(self):
        # Evidence for the proposed fix: each bed is fully detected once the first
        # block's artificial rise from silence is removed, and nothing else changes.
        for name, (raw, pulse, bpm) in self.continuous_beds().items():
            with self.subTest(name=name):
                envelope = onset_envelope(raw, MediaTools())
                self.assertEqual(envelope[0], 0.0)
                result = estimate(envelope, MediaTools())
                self.assertEqual(result["mode"], "beats")
                self.assertAlmostEqual(result["bpm"], bpm, delta=0.5)
                self.assertEqual(score(result, pulse)[:2], (1.0, 1.0))

    def test_syncopated_steady_pulse_falls_back_with_a_label(self):
        # Measured recall loss: a common pop pattern over a steady 120 BPM pulse
        # is not recognised. The fallback is honest and labelled, not wrong.
        events, _ = syncopated()
        result = run(render(12, events))
        self.assertEqual((result["mode"], result["beats"]), ("accents", []))
        self.assertIn(result["reason"], LABELLED)

    def test_variable_jittered_weak_and_unpulsed_audio_never_claim_beats(self):
        accelerando, t = [], 0.1
        while t < 11.9:
            accelerando.append(t)
            t += 60 / (100 + 30 * t / 12)
        rng = random.Random(4)
        jitter = [t + rng.uniform(-0.04, 0.04) for t in grid(100, 12)]
        cases = {
            "accelerando 100-130": render(
                12, [(t, "kick", 16000) for t in accelerando]
            ),
            "jitter +-40 ms": render(12, [(t, "kick", 16000) for t in jitter]),
            "weak kicks in noise": render(
                12, [(t, "kick", 1500) for t in grid(120, 12)], noise=900
            ),
            "music pad only": render(12, [], pad=6000),
            "noise only": render(12, [], noise=2000),
            "2.5 s of steady kicks": render(
                2.5, [(t, "kick", 16000) for t in grid(120, 2.5)]
            ),
        }
        for name, raw in cases.items():
            with self.subTest(name=name):
                result = run(raw)
                self.assertEqual(
                    (result["mode"], result["beats"], result["bpm"]), ("accents", [], 0)
                )
                self.assertIn(result["reason"], LABELLED)

    def test_digital_silence_is_unavailable_music_not_an_empty_beat_grid(self):
        with self.assertRaises(AssistantError) as caught:
            onset_envelope(render(12, []), MediaTools())
        self.assertEqual(caught.exception.code, "invalid_music_audio")

    def test_beat_frames_respect_rational_fps_and_source_bounds(self):
        pulse = grid(120, 12)
        raw = render(12, [(t, "kick", 16000) for t in pulse])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            music = root / "music.wav"
            music.write_bytes(b"synthetic music")
            sha = hashlib.sha256(music.read_bytes()).hexdigest()
            for rate in (
                FrameRate(24000, 1001),
                FrameRate(25),
                FrameRate(30000, 1001),
                FrameRate(60000, 1001),
            ):
                fps = Fraction(rate.numerator, rate.denominator)
                # Only the first 9.5 s is placed: later attacks must not appear.
                duration = math.floor(Fraction(19, 2) * fps)
                media = MediaTools()
                media._output = Mock(return_value=raw[: int(9.5 * SR) * 6])
                with self.subTest(fps=str(fps)):
                    result = analyze(
                        music,
                        rate,
                        duration,
                        sha,
                        root / f"cache-{rate.numerator}",
                        media,
                    )
                    self.assertEqual(result["mode"], "beats")
                    frames = result["beats"]
                    self.assertTrue(all(0 < f < duration for f in frames))
                    self.assertEqual(frames, sorted(set(frames)))
                    for f in frames:
                        # Within half a frame plus the 10 ms tick of a real attack.
                        nearest = min(
                            abs(Fraction(f) / fps - Fraction(t).limit_denominator(1000))
                            for t in pulse
                        )
                        self.assertLessEqual(nearest, 1 / (2 * fps) + Fraction(1, 100))
                    expected = sum(1 for t in pulse if 0 < round(t * fps) < duration)
                    self.assertGreaterEqual(len(frames), expected - 1)


def tone(dbfs, kind="noise", seconds=1.0) -> bytes:
    """16 kHz mono s16 noise or a music-like chord at an RMS-ish level in dBFS."""
    rng, amp = random.Random(2), 32768 * 10 ** (dbfs / 20)
    n = int(seconds * 16000)
    if kind == "noise":
        values = (rng.gauss(0, amp) for _ in range(n))
    else:
        values = (
            amp
            * (
                math.sin(2 * math.pi * 220 * i / 16000)
                + math.sin(2 * math.pi * 330 * i / 16000)
            )
            / 1.4
            for i in range(n)
        )
    return array("h", (max(-32767, min(32767, round(v))) for v in values)).tobytes()


class CaptionRegressionTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.path = self.root / "speech.wav"
        self.path.write_bytes(b"synthetic source")

    def captions(
        self, pcm, segments, rate=FrameRate(30000, 1001), length=90, cache="cache"
    ):
        snapshot = Snapshot(
            "p",
            "s",
            1,
            rate,
            (Asset("1", 1000, False, True),),
            (Track("2", "audio"),),
            (OccupiedRange("a", "2", 0, length),),
        )
        spec = dict(
            clips=[
                dict(
                    instance_id="a",
                    bin_id="1",
                    track_id="2",
                    start_frame=0,
                    in_frame=0,
                    duration_frames=length,
                    audio_stream=0,
                )
            ]
        )
        media = MediaTools()
        expected = length * rate.denominator * 16000 // rate.numerator
        media._output = Mock(
            return_value=(pcm * (expected // (len(pcm) // 2) + 1))[: expected * 2]
        )
        provider = Mock(model="gemini-test")
        provider.generate.return_value = dict(segments=segments)
        factory = Mock(return_value=provider)
        result = propose_captions(
            snapshot,
            "r",
            spec,
            {"1": str(self.path)},
            self.root / cache,
            media,
            Mock(),
            factory,
        )
        return result, factory, provider

    def test_silence_below_the_energy_gate_never_requests_speech(self):
        for dbfs in (-90, -70, -62):
            with self.subTest(dbfs=dbfs):
                self.assertTrue(is_silent(tone(dbfs)))
                result, factory, _ = self.captions(tone(dbfs), [])
                factory.assert_not_called()  # No provider is even configured.
                self.assertEqual((result["captions"], result["skipped_clips"]), ([], 1))

    def test_audible_non_speech_is_sent_for_recognition_and_may_be_empty(self):
        # The gate is energy, not voice detection: noise and music above about
        # -60 dBFS are recognised. Empty answers finish without captions; no
        # claim is made that a provider never hallucinates speech from them.
        for kind, dbfs in (("noise", -50), ("noise", -20), ("music", -20)):
            with self.subTest(kind=kind, dbfs=dbfs):
                self.assertFalse(is_silent(tone(dbfs, kind)))
                result, factory, provider = self.captions(
                    tone(dbfs, kind), [], cache=f"{kind}{dbfs}"
                )
                self.assertEqual(provider.generate.call_count, 1)
                self.assertEqual(result["captions"], [])

    def test_caption_frames_cover_speech_at_rational_rates(self):
        rng = random.Random(11)
        for rate in (
            FrameRate(24000, 1001),
            FrameRate(25),
            FrameRate(30000, 1001),
            FrameRate(60000, 1001),
        ):
            fps = Fraction(rate.numerator, rate.denominator)
            length = int(4 * fps)
            frame_ms = 1000 / fps
            starts = sorted(rng.sample(range(0, 3700, 10), 6))
            segments, cursor = [], 0
            for s in starts:
                # Keep at least two frames between segments so rounding cannot merge them.
                start = max(s, cursor + int(2 * frame_ms) + 1)
                end = min(start + rng.randint(120, 600), int(length * frame_ms) - 1)
                if end - start < 50:
                    break
                segments.append(
                    dict(start_ms=start, end_ms=end, text=f"Words {len(segments)}")
                )
                cursor = end
            with self.subTest(fps=str(fps)):
                result, _, _ = self.captions(
                    tone(-20),
                    segments,
                    rate=rate,
                    length=length,
                    cache=f"c{rate.numerator}",
                )
                captions = result["captions"]
                self.assertEqual(len(captions), len(segments))
                previous = 0
                for c, s in zip(captions, segments):
                    first, last = (
                        c["start_frame"],
                        c["start_frame"] + c["duration_frames"],
                    )
                    # The frame interval covers the spoken interval, rounding outward by under one frame.
                    self.assertLessEqual(
                        Fraction(first) / fps, Fraction(s["start_ms"], 1000)
                    )
                    self.assertGreaterEqual(
                        Fraction(last) / fps, Fraction(s["end_ms"], 1000)
                    )
                    self.assertLess(
                        Fraction(s["start_ms"], 1000) - Fraction(first) / fps, 1 / fps
                    )
                    self.assertLess(
                        Fraction(last) / fps - Fraction(s["end_ms"], 1000), 1 / fps
                    )
                    self.assertGreaterEqual(first, previous)
                    self.assertLessEqual(last, length)
                    previous = last

    def test_known_bug_sub_frame_segment_fails_the_whole_request(self):
        """Valid non-overlapping segments inside one frame collapse to zero length
        after outward rounding; propose_captions raises invalid_captions for the
        entire request. Proposed fix: extend a collapsed caption to one frame when
        it still fits the clip, otherwise merge its text into the previous caption."""
        segments = [
            dict(start_ms=0, end_ms=1010, text="We made it."),
            dict(start_ms=1010, end_ms=1030, text="Oh!"),
        ]
        result, _, _ = self.captions(tone(-20), segments)
        self.assertEqual(
            [c["text"] for c in result["captions"]], ["We made it.", "Oh!"]
        )

    def test_known_bug_unmappable_answer_is_cached_and_never_retried(self):
        """A genuine mapping failure must not poison subsequent recognition.

        Two full-length phrases fit in milliseconds but not in one native frame;
        their combined text also exceeds the caption limit, so neither extension
        nor merging can preserve them. A later valid answer must be requested.
        """
        bad = [
            dict(start_ms=0, end_ms=20, text="a" * 84),
            dict(start_ms=20, end_ms=30, text="b" * 84),
        ]
        with self.assertRaises(AssistantError):
            self.captions(tone(-20), bad, length=1, cache="shared")
        self.assertEqual(list((self.root / "shared").glob("*.json")), [])
        _, _, provider = self.captions(
            tone(-20),
            [dict(start_ms=0, end_ms=20, text="Hi.")],
            length=1,
            cache="shared",
        )
        self.assertEqual(
            provider.generate.call_count, 1
        )  # A fresh answer must be requested.

    def test_boundary_phrases_merge_without_losing_words_or_exceeding_the_clip(self):
        result, _, _ = self.captions(
            tone(-20),
            [
                dict(start_ms=0, end_ms=20, text="We made it."),
                dict(start_ms=20, end_ms=30, text="Oh!"),
            ],
            length=1,
        )
        self.assertEqual(
            result["captions"],
            [
                dict(
                    instance_id="a",
                    start_frame=0,
                    duration_frames=1,
                    text="We made it. Oh!",
                )
            ],
        )

    def test_unmappable_cached_answer_is_discarded_before_a_fresh_request(self):
        result, _, _ = self.captions(tone(-20), [], length=1, cache="corrupt")
        self.assertEqual(result["captions"], [])
        for target in (self.root / "corrupt").glob("*.json"):
            target.write_text(
                '{"segments":['
                '{"start_ms":0,"end_ms":20,"text":"' + "a" * 84 + '"},'
                '{"start_ms":20,"end_ms":30,"text":"' + "b" * 84 + '"}]}'
            )
        result, _, provider = self.captions(
            tone(-20),
            [
                dict(start_ms=0, end_ms=20, text="Hi."),
            ],
            length=1,
            cache="corrupt",
        )
        self.assertEqual(provider.generate.call_count, 1)
        self.assertEqual(result["cached_clips"], 0)
        self.assertEqual(result["captions"][0]["text"], "Hi.")


if __name__ == "__main__":
    unittest.main()
