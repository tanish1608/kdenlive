# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from dataclasses import replace
from fractions import Fraction
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import Asset, FrameRate, Snapshot, Track
from montaz_assistant.gemini import AssistantError, Gemini
from montaz_assistant.planning import (
    MediaTools,
    analyze,
    brief_from_wire,
    candidates,
    plan_media,
    proposal_from_plan,
    validate_analysis,
)
from montaz_assistant.protocol import decode, encode, to_wire


class PlanningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "source.mp4"
        self.path.write_bytes(b"synthetic-content")
        self.asset = Asset("1", 50, True, False)
        self.rate = FrameRate(25)
        self.snapshot = Snapshot(
            "project", "sequence", 1, self.rate, (self.asset,), (Track("1", "video"),)
        )
        self.brief = {
            "duration_seconds": 60,
            "recipe": "postcard",
            "instruction": "Beach trip",
        }
        self.observation = {
            "windows": [
                {
                    "window_id": 0,
                    "description": "A red test image",
                    "role": "other",
                    "quality": 80,
                    "trim_in": 0,
                    "trim_out": 50,
                }
            ]
        }
        self.provider = Mock(model="test-model")
        self.kept = {"decision": "keep", "reason": "The observed detail provides a coherent opening", "shots": []}
        self.provider.generate.return_value = self.observation
        self.media = MediaTools()
        self.media.image = Mock(return_value={"text": "synthetic frame"})
        self.media.video = Mock(return_value={"text": "synthetic video"})
        self.media.visual_frames = Mock(side_effect=lambda path, rate, maximum: maximum)

    def analysis(self, **changes):
        values = dict(
            asset=self.asset,
            rate=self.rate,
            source=str(self.path),
            cache_dir=self.root / "cache",
            provider=self.provider,
            media=self.media,
        )
        values.update(changes)
        return analyze(**values)

    def test_cached_content_is_reused_after_rename_and_bin_id_change(self):
        first, hit = self.analysis()
        self.assertFalse(hit)
        new = self.root / "renamed.mp4"
        self.path.rename(new)
        second, hit = self.analysis(source=str(new), asset=replace(self.asset, bin_id="9"))
        self.assertTrue(hit)
        self.assertEqual(first, second)
        self.assertEqual(self.provider.generate.call_count, 1)
        self.assertEqual(self.media.video.call_count, 1)
        cache = next((self.root / "cache").glob("*.json"))
        self.assertEqual(cache.stat().st_mode & 0o777, 0o600)
        self.assertNotIn(str(new), cache.read_text())

    def test_content_model_and_frame_rate_invalidate_the_cache(self):
        self.analysis()
        self.path.write_bytes(b"changed-content")
        self.analysis()
        self.provider.model = "another-model"
        self.analysis()
        self.analysis(rate=FrameRate(30))
        self.assertEqual(self.provider.generate.call_count, 4)

    def test_invalid_analysis_and_provider_failures_are_never_cached(self):
        for result in (
            {"windows": []},
            {"windows": [dict(self.observation["windows"][0], window_id=True)]},
        ):
            self.provider.generate.return_value = result
            with self.assertRaises(AssistantError):
                self.analysis()
        self.provider.generate.side_effect = AssistantError("provider_unavailable")
        with self.assertRaisesRegex(AssistantError, "provider_unavailable"):
            self.analysis()
        self.assertFalse((self.root / "cache").exists())

    def test_changed_media_is_rejected_before_success_can_be_cached(self):
        def change(_):
            self.path.write_bytes(b"replaced-during-analysis")
            return self.observation

        self.provider.generate.side_effect = change
        with self.assertRaisesRegex(AssistantError, "media_changed"):
            self.analysis()
        self.assertFalse((self.root / "cache").exists())

    def test_malformed_cache_is_reanalyzed(self):
        self.analysis()
        next((self.root / "cache").glob("*.json")).write_text("not-json")
        _, hit = self.analysis()
        self.assertFalse(hit)
        self.assertEqual(self.provider.generate.call_count, 2)

    @unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is required for the media regression")
    def test_audio_tail_does_not_break_visual_analysis(self):
        # The muxed asset extends beyond the last video frame, as real phone clips can.
        subprocess.run(
            [
                shutil.which("ffmpeg"),
                "-v",
                "error",
                "-y",
                "-f",
                "lavfi",
                "-i",
                "testsrc2=size=64x64:rate=25:duration=1",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=440:duration=1.08",
                "-c:v",
                "mpeg4",
                "-c:a",
                "aac",
                "-threads",
                "1",
                str(self.path),
            ],
            check=True,
            capture_output=True,
        )
        media = MediaTools()
        self.provider.generate.return_value = {
            "windows": [dict(self.observation["windows"][0], trim_out=25)]
        }
        with self.assertRaisesRegex(AssistantError, "media_unreadable"):
            media.image(self.path, Fraction(26, 25))
        result, hit = self.analysis(asset=replace(self.asset, duration_frames=27), media=media)
        self.assertFalse(hit)
        self.assertEqual(result[0]["duration_frames"], 25)
        proxy = media.video(self.path, result[0], self.rate)
        self.assertEqual(proxy["video_metadata"], {"fps": 4})
        probe = subprocess.run(
            [shutil.which("ffprobe"), "-v", "error", "-show_streams", "-of", "json", "pipe:0"],
            input=base64.b64decode(proxy["inline_data"]["data"]),
            capture_output=True,
            check=True,
        )
        self.assertEqual([s["codec_type"] for s in json.loads(probe.stdout)["streams"]], ["video"])

    def test_short_sources_produce_a_feasible_plan_without_repetition(self):
        evidence = {
            "1": [
                {
                    **candidates(self.asset, self.rate)[0],
                    **self.observation["windows"][0],
                }
            ]
        }
        proposal = proposal_from_plan(
            self.snapshot,
            "request",
            {
                "shots": [
                    {
                        "bin_id": "1",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail connects the scene",
                    }
                ]
            },
            evidence,
            self.brief,
        )
        self.assertEqual(len(proposal.operations), 1)
        self.assertEqual(proposal.operations[0].duration_frames, 50)

    def test_subsecond_travel_cuts_are_rejected(self):
        asset = replace(self.asset, duration_frames=12)
        evidence = {"1": [{**candidates(asset, self.rate)[0], **self.observation["windows"][0]}]}
        with self.assertRaisesRegex(AssistantError, "invalid_plan"):
            proposal_from_plan(
                replace(self.snapshot, assets=(asset,)),
                "r",
                {
                    "shots": [
                        {
                            "bin_id": "1",
                            "window_id": 0,
                            "duration_frames": 12,
                            "reason": "A visible detail connects the scene",
                        }
                    ]
                },
                evidence,
                self.brief,
            )

    def test_planner_receives_feasible_target_instead_of_only_a_duration_cap(self):
        self.provider.generate.side_effect = [
            self.observation,
            {
                "shots": [
                    {
                        "bin_id": "1",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail connects the scene",
                    }
                ]
            },
            self.kept,
        ]
        plan_media(
            self.snapshot,
            "r",
            {"1": str(self.path)},
            self.brief,
            self.root / "cache",
            self.provider,
            self.media,
            lambda *_: None,
        )
        prompt = self.provider.generate.call_args_list[1].args[0][0]["text"]
        self.assertIn("feasible target from distinct usable windows is 50 frames", prompt)

    def test_duplicate_unknown_unusable_or_excessive_shots_are_rejected(self):
        evidence = {
            "1": [
                {
                    **candidates(self.asset, self.rate)[0],
                    **self.observation["windows"][0],
                }
            ]
        }
        shot = {
            "bin_id": "1",
            "window_id": 0,
            "duration_frames": 50,
            "reason": "A visible detail connects the scene",
        }
        invalid = [
            [shot, shot],
            [dict(shot, bin_id="invented")],
            [dict(shot, window_id=1)],
            [dict(shot, duration_frames=51)],
            [dict(shot, duration_frames=True)],
            [dict(shot, duration_frames=10)],
        ]
        for shots in invalid:
            with self.subTest(shots=shots), self.assertRaises(ValueError):
                proposal_from_plan(self.snapshot, "r", {"shots": shots}, evidence, self.brief)
        evidence["1"][0]["quality"] = 0
        with self.assertRaisesRegex(AssistantError, "invalid_plan"):
            proposal_from_plan(self.snapshot, "r", {"shots": [shot]}, evidence, self.brief)
        evidence["1"][0]["quality"] = 80
        with self.assertRaisesRegex(AssistantError, "invalid_plan"):
            proposal_from_plan(
                self.snapshot,
                "r",
                {"shots": [shot]},
                evidence,
                dict(self.brief, duration_seconds=1),
            )

    def test_analysis_progress_and_cache_hits_are_reported_on_repeated_runs(self):
        shots = {
            "shots": [
                {
                    "bin_id": "1",
                    "window_id": 0,
                    "duration_frames": 50,
                    "reason": "A visible detail connects the scene",
                }
            ]
        }
        events = []
        self.provider.generate.side_effect = [self.observation, shots, self.kept, shots, self.kept]
        for _ in range(2):
            plan_media(
                self.snapshot,
                "r",
                {"1": str(self.path)},
                self.brief,
                self.root / "cache",
                self.provider,
                self.media,
                lambda *event: events.append(event),
            )
        self.assertIn(("planning", 1, 1, 1), events)
        self.assertIn(("reviewing", 1, 1, 1), events)
        self.assertEqual(self.provider.generate.call_count, 5)  # one analysis, two plans/reviews

    def test_duplicate_content_in_parallel_is_analyzed_only_once(self):
        second = self.root / "copy.mp4"
        second.write_bytes(self.path.read_bytes())
        snapshot = replace(self.snapshot, assets=(self.asset, replace(self.asset, bin_id="2")))
        self.provider.generate.side_effect = [
            self.observation,
            {
                "shots": [
                    {
                        "bin_id": "1",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail connects the scene",
                    }
                ]
            },
            self.kept,
        ]
        proposal = plan_media(
            snapshot,
            "r",
            {"1": str(self.path), "2": str(second)},
            self.brief,
            self.root / "cache",
            self.provider,
            self.media,
            lambda *_: None,
        )
        self.assertEqual(self.provider.generate.call_count, 3)  # analysis + plan + review
        self.assertEqual(len(proposal.operations), 1)

    def test_two_video_analysis_requests_overlap(self):
        second = self.root / "second.mp4"
        second.write_bytes(b"another-content")
        snapshot = replace(self.snapshot, assets=(self.asset, replace(self.asset, bin_id="2")))
        barrier = threading.Barrier(2, timeout=3)

        def generate(parts):
            if any(p.get("text", "").startswith("Analyze") for p in parts):
                barrier.wait()
                return self.observation
            if parts[0]["text"].startswith("Review"):
                return self.kept
            return {
                "shots": [
                    {
                        "bin_id": "1",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail connects the scene",
                    },
                    {
                        "bin_id": "2",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail connects the scene",
                    },
                ]
            }

        self.provider.generate.side_effect = generate
        proposal = plan_media(
            snapshot,
            "r",
            {"1": str(self.path), "2": str(second)},
            self.brief,
            self.root / "cache",
            self.provider,
            self.media,
            lambda *_: None,
        )
        self.assertEqual(len(proposal.operations), 2)

    def test_missing_key_fails_before_extracting_any_media(self):
        request = {
            "version": 1,
            "type": "plan_media",
            "request_id": "request",
            "snapshot": to_wire(self.snapshot),
            "sources": {"1": "private-path"},
            "brief": self.brief,
        }
        env = dict(
            os.environ,
            GEMINI_API_KEY="",
            MONTAZ_ASSISTANT_CONFIG=str(self.root / "empty"),
        )
        result = subprocess.run(
            [sys.executable, "-m", "montaz_assistant.worker"],
            input=encode(request),
            capture_output=True,
            env=env,
            timeout=5,
        )
        messages = [decode(x) for x in result.stdout.splitlines()]
        self.assertEqual(messages[-1]["code"], "missing_api_key")
        self.assertNotIn(b"private-path", result.stdout + result.stderr)
        self.assertNotIn("progress", [m["type"] for m in messages])

    def test_stop_kills_owned_media_process(self):
        child = Mock()
        child.poll.return_value = None
        media = MediaTools()
        media._children.add(child)
        media.cancel()
        child.kill.assert_called_once()
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            media.check()

    def test_stop_reaps_a_real_media_child_even_if_it_ignores_termination(self):
        child = subprocess.Popen(
            [
                sys.executable,
                "-c",
                "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);"
                "print('ready',flush=True);time.sleep(60)",
            ],
            stdout=subprocess.PIPE,
        )
        try:
            self.assertEqual(child.stdout.readline(), b"ready\n")
            media = MediaTools()
            media._children.add(child)
            media.cancel()
            self.assertIsNotNone(child.wait(timeout=2))
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=2)
            child.stdout.close()

    def test_configuration_rejects_blank_keys_and_nonmodel_paths(self):
        for key, model in ((" ", "gemini-3.8-flash"), ("secret", "../../anything")):
            with self.assertRaises(AssistantError):
                Gemini(key, model)
        with self.assertRaises(AssistantError):
            brief_from_wire(dict(self.brief, duration_seconds=True))

    def test_temporal_trims_exclude_unusable_leading_and_trailing_frames(self):
        window = {
            **candidates(self.asset, self.rate)[0],
            **self.observation["windows"][0],
            "trim_in": 5,
            "trim_out": 45,
        }
        shot = {
            "bin_id": "1",
            "window_id": 0,
            "duration_frames": 40,
            "reason": "The camera has settled on the visible detail",
        }
        proposal = proposal_from_plan(
            self.snapshot, "r", {"shots": [shot]}, {"1": [window]}, self.brief
        )
        self.assertEqual(proposal.operations[0].in_frame, 5)
        self.assertEqual(proposal.operations[0].duration_frames, 40)
        with self.assertRaisesRegex(AssistantError, "invalid_plan"):
            proposal_from_plan(
                self.snapshot,
                "r",
                {"shots": [dict(shot, duration_frames=41)]},
                {"1": [window]},
                self.brief,
            )

    def test_trim_offsets_cannot_escape_the_analyzed_window(self):
        original = self.observation["windows"][0]
        for changes in (
            {"trim_in": -1},
            {"trim_in": True},
            {"trim_out": 51},
            {"trim_out": 0},
            {"trim_out": 49.5},
            {"trim_in": 50},
        ):
            with self.subTest(changes=changes), self.assertRaises(AssistantError):
                validate_analysis(
                    {"windows": [{**original, **changes}]}, candidates(self.asset, self.rate)
                )

    def test_micro_clips_skip_provider_work_and_cannot_generate_an_edit(self):
        snapshot = replace(self.snapshot, assets=(replace(self.asset, duration_frames=12),))
        with self.assertRaisesRegex(AssistantError, "insufficient_footage"):
            plan_media(
                snapshot,
                "r",
                {"1": str(self.path)},
                self.brief,
                self.root / "cache",
                self.provider,
                self.media,
                lambda *_: None,
            )
        self.provider.generate.assert_not_called()
        self.media.video.assert_not_called()

    def test_review_reports_usable_duration_and_excluded_sources_without_paths(self):
        snapshot = replace(
            self.snapshot, assets=(self.asset, replace(self.asset, bin_id="2", duration_frames=12))
        )
        self.provider.generate.side_effect = [
            self.observation,
            {
                "shots": [
                    {
                        "bin_id": "1",
                        "window_id": 0,
                        "duration_frames": 50,
                        "reason": "A visible detail opens the story",
                    }
                ]
            },
            self.kept,
        ]
        reviews = []
        proposal = plan_media(
            snapshot,
            "r",
            {"1": str(self.path), "2": str(self.path)},
            self.brief,
            self.root / "cache",
            self.provider,
            self.media,
            lambda *_: None,
            reviews.append,
        )
        review = reviews[0]
        self.assertEqual(review["available_frames"], 50)
        self.assertEqual(review["skipped_assets"], 1)
        self.assertEqual(review["assessment"]["status"], "kept")
        self.assertEqual(review["shots"][0]["instance_id"], proposal.operations[0].instance_id)
        self.assertNotIn(str(self.path), json.dumps(review))

    def test_revised_plan_and_metadata_use_cached_evidence_and_same_span(self):
        second = self.root / "second.mp4"
        second.write_bytes(b"another source")
        snapshot = replace(self.snapshot, assets=(self.asset, replace(self.asset, bin_id="2")))
        self.analysis()
        self.analysis(source=str(second), asset=snapshot.assets[1])
        draft = {"shots": [dict(bin_id=bid, window_id=0, duration_frames=50,
                                reason="An observed detail connects the coast") for bid in ("1", "2")]}
        self.provider.generate.reset_mock()
        self.media.video.reset_mock()
        self.provider.generate.side_effect = [draft, dict(decision="revise", reason="Open with the second observed view",
                                                        shots=list(reversed(draft["shots"])))]
        reviews, events = [], []
        proposal = plan_media(snapshot, "r", {"1": str(self.path), "2": str(second)}, self.brief,
                              self.root / "cache", self.provider, self.media,
                              lambda *event: events.append(event), reviews.append)
        self.assertEqual([op.bin_id for op in proposal.operations], ["2", "1"])
        self.assertEqual(sum(op.duration_frames for op in proposal.operations), 100)
        self.assertEqual(reviews[0]["assessment"]["status"], "revised")
        self.assertEqual(reviews[0]["shots"][0]["instance_id"], proposal.operations[0].instance_id)
        self.assertIn(("reviewing", 2, 2, 2), events)
        self.assertEqual(self.provider.generate.call_count, 2)
        self.media.video.assert_not_called()

    def test_unwatchable_ranges_and_empty_rationales_are_rejected(self):
        window = {**candidates(self.asset, self.rate)[0], **self.observation["windows"][0]}
        shot = {"bin_id": "1", "window_id": 0, "duration_frames": 50, "reason": "Visible detail"}
        for quality, reason in ((49, "Visible detail"), (80, "  "), (80, "x" * 301)):
            with (
                self.subTest(quality=quality, reason=reason),
                self.assertRaisesRegex(AssistantError, "invalid_plan"),
            ):
                proposal_from_plan(
                    self.snapshot,
                    "r",
                    {"shots": [dict(shot, reason=reason)]},
                    {"1": [dict(window, quality=quality)]},
                    self.brief,
                )

    def test_rest_key_is_in_header_and_errors_do_not_reflect_the_key(self):
        provider = Gemini("private-key")
        payload = {
            "candidates": [
                {
                    "finishReason": "STOP",
                    "content": {"parts": [{"text": json.dumps(self.observation)}]},
                }
            ]
        }
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=None)
        response.read.return_value = json.dumps(payload).encode()
        with patch("urllib.request.urlopen", return_value=response) as http:
            self.assertEqual(provider.generate([{"text": "test"}]), self.observation)
        request = http.call_args.args[0]
        self.assertNotIn("private-key", request.full_url)
        self.assertEqual(request.get_header("X-goog-api-key"), "private-key")


if __name__ == "__main__":
    unittest.main()
