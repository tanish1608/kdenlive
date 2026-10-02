# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Synthetic fixtures with known movement and coverage; no media or models."""

import random
import unittest

from montaz_assistant.motion_evidence import (
    MAX_FRAMES,
    Frame,
    MotionInputError,
    Window,
    analyse,
    assess_pair,
)

W, H = 64, 48


def texture(seed: int, size: int = 160) -> list[list[float]]:
    """Aperiodic blurred random texture, so translations have one clear match."""
    rng = random.Random(seed)
    raw = [[rng.uniform(30, 225) for _ in range(size)] for _ in range(size)]
    return [
        [
            sum(
                raw[min(size - 1, max(0, y + j))][min(size - 1, max(0, x + i))]
                for i in (-1, 0, 1)
                for j in (-1, 0, 1)
            )
            / 9
            for x in range(size)
        ]
        for y in range(size)
    ]


BASE, OTHER = texture(1), texture(2)


def crop(img, x0: int, y0: int) -> list[list[float]]:
    return [row[x0 : x0 + W] for row in img[y0 : y0 + H]]


def window(images, positions=None) -> Window:
    positions = positions or list(range(len(images)))
    return Window(
        W,
        H,
        tuple(
            Frame(p, bytes(max(0, min(255, round(v))) for row in img for v in row))
            for p, img in zip(positions, images)
        ),
    )


def pan(vx: int, vy: int, frames: int = 12) -> Window:
    # A camera moving by (-vx, -vy) makes the content move by (vx, vy).
    return window([crop(BASE, 48 - vx * t, 48 - vy * t) for t in range(frames)])


def covered(t: int, frames: int, cover, reach: float) -> list[list[float]]:
    """BASE with `cover` sliding in from the left to `reach` of the width by mid-window."""
    img = [row[:] for row in crop(BASE, 48, 48)]
    edge = round(W * reach * min(1.0, t / (frames // 2)))
    for y in range(H):
        for x in range(edge):
            img[y][x] = cover(x, y)
    return img


class MotionEvidenceTests(unittest.TestCase):
    def test_static_texture_measures_no_movement(self):
        result = analyse(window([crop(BASE, 48, 48)] * 8))
        self.assertEqual(result.direction, "static")
        for s in result.steps:
            self.assertEqual(s.status, "measured")
            self.assertAlmostEqual(s.dx, 0, delta=0.05)
            self.assertAlmostEqual(s.dy, 0, delta=0.05)
        self.assertEqual(
            (result.occlusions, result.fade, result.abrupt_change), ((), None, None)
        )

    def test_translation_directions_and_speed_are_measured(self):
        for (vx, vy), label in (
            ((-2, 0), "left"),
            ((3, 0), "right"),
            ((0, -2), "up"),
            ((0, 2), "down"),
            ((2, -2), "up-right"),
        ):
            with self.subTest(label=label):
                result = analyse(pan(vx, vy))
                self.assertEqual(result.direction, label)
                # Sub-pixel estimates: within a tenth of a pixel per frame.
                self.assertAlmostEqual(result.velocity[0], vx, delta=0.1)
                self.assertAlmostEqual(result.velocity[1], vy, delta=0.1)
                self.assertEqual(result.consistency, 1.0)
                self.assertEqual(result.occlusions, ())

    def test_sparse_positions_report_speed_per_project_frame(self):
        frames = [crop(BASE, 48 + 3 * t, 48) for t in range(8)]
        result = analyse(window(frames, positions=[100 + 2 * t for t in range(8)]))
        self.assertEqual(result.direction, "left")
        self.assertAlmostEqual(
            result.velocity[0], -1.5, delta=0.05
        )  # 3 px per 2-frame step.
        self.assertAlmostEqual(result.velocity[1], 0, delta=0.05)
        self.assertEqual((result.first, result.last), (100, 114))

    def test_independent_foreground_is_not_an_occlusion(self):
        frames = []
        for t in range(12):
            img = crop(BASE, 48, 48)
            x0 = 18 + 2 * t
            for y in range(14, 34):
                for x in range(x0, x0 + 20):
                    img[y][x] = OTHER[y][x + 40]
            frames.append(img)
        result = analyse(window(frames))
        self.assertEqual(result.direction, "static")  # The background dominates.
        self.assertEqual(result.occlusions, ())
        self.assertTrue(result.local_motion)

    def test_object_entering_from_an_edge_and_passing_through_is_not_an_occlusion(self):
        frames = []
        for t in range(16):
            img = crop(BASE, 48, 48)
            x0 = (
                -20 + 6 * t
            )  # Enters at the left edge and leaves the left blocks behind.
            for y in range(8, 40):
                for x in range(max(0, x0), min(W, x0 + 22)):
                    img[y][x] = OTHER[y][x + 40]
            frames.append(img)
        result = analyse(window(frames))
        self.assertEqual(result.occlusions, ())
        self.assertTrue(result.local_motion)

    def test_noise_is_unavailable_and_claims_nothing(self):
        rng = random.Random(9)
        frames = [
            [[rng.uniform(0, 255) for _ in range(W)] for _ in range(H)]
            for _ in range(10)
        ]
        result = analyse(window(frames))
        self.assertEqual(result.direction, "unavailable")
        self.assertEqual(
            (result.occlusions, result.abrupt_change, result.fade), ((), None, None)
        )
        self.assertTrue(any("not coherent" in n for n in result.notes))

    def test_fade_to_black_is_a_fade_not_an_occlusion(self):
        frames = [
            [[v * (1 - t / 11) for v in row] for row in crop(BASE, 48, 48)]
            for t in range(12)
        ]
        result = analyse(window(frames))
        self.assertIsNotNone(result.fade)
        self.assertEqual(result.occlusions, ())
        self.assertIsNone(result.abrupt_change)

    def test_partial_occlusion_from_an_edge(self):
        frames = [covered(t, 12, lambda x, y: 12.0, 0.5) for t in range(12)]
        result = analyse(window(frames))
        (occlusion,) = result.occlusions
        self.assertEqual(occlusion.kind, "partial")
        self.assertFalse(occlusion.verified)
        self.assertGreaterEqual(occlusion.peak_coverage, 0.4)
        self.assertLess(occlusion.peak_coverage, 0.9)
        self.assertIsNone(result.fade)

    def test_complete_occlusion_is_only_ever_a_candidate(self):
        textured = [covered(t, 12, lambda x, y: OTHER[y][x], 1.0) for t in range(12)]
        (occlusion,) = analyse(window(textured)).occlusions
        self.assertEqual(
            (occlusion.kind, occlusion.textured_cover, occlusion.verified),
            ("complete", True, False),
        )
        self.assertIn("not established", occlusion.note)
        flat = [covered(t, 12, lambda x, y: 10.0, 1.0) for t in range(12)]
        result = analyse(window(flat))
        (occlusion,) = result.occlusions
        self.assertEqual(
            (occlusion.kind, occlusion.textured_cover, occlusion.verified),
            ("complete", False, False),
        )
        self.assertIn("cannot be told from a dark object", occlusion.note)
        self.assertIsNone(result.fade)  # Spreading from an edge is not a uniform fade.

    def test_a_wipe_that_covers_everything_then_moves_on_stays_a_candidate(self):
        frames = []
        for t in range(14):
            img = crop(BASE, 48, 48)
            left, right = (
                max(0, (t - 7) * 10),
                min(W, t * 10),
            )  # In from the left, out to the right.
            for y in range(H):
                for x in range(left, right):
                    img[y][x] = OTHER[y][x]
            frames.append(img)
        (occlusion,) = analyse(window(frames)).occlusions
        self.assertEqual((occlusion.kind, occlusion.verified), ("complete", False))

    def test_a_zoom_or_dolly_is_not_reported_as_occlusion(self):
        # Content scales about the centre by 2% per frame: edges decorrelate first.
        frames = []
        for t in range(16):
            k = 1 + 0.02 * t
            frames.append(
                [
                    [
                        BASE[round(80 + (y - H / 2) / k)][round(80 + (x - W / 2) / k)]
                        for x in range(W)
                    ]
                    for y in range(H)
                ]
            )
        result = analyse(window(frames))
        self.assertEqual(result.occlusions, ())
        self.assertTrue(any("zoom or dolly" in n for n in result.notes))

    def test_abrupt_full_change_is_a_cut_not_a_wipe(self):
        frames = [crop(BASE, 48, 48)] * 6 + [crop(OTHER, 48, 48)] * 6
        result = analyse(window(frames))
        self.assertEqual(result.abrupt_change, (5, 6))
        self.assertEqual(result.occlusions, ())

    def test_textureless_window_proves_nothing(self):
        result = analyse(window([[[128.0] * W for _ in range(H)]] * 8))
        self.assertEqual(result.direction, "unavailable")
        self.assertTrue(all(s.status == "textureless" for s in result.steps))
        self.assertEqual(result.occlusions, ())
        self.assertTrue(any("Textureless reference" in n for n in result.notes))

    def test_pair_assessment_is_evidence_with_a_caveat(self):
        left, left_fast, right, still = (
            analyse(pan(-2, 0)),
            analyse(pan(-3, 0)),
            analyse(pan(2, 0)),
            analyse(pan(0, 0)),
        )
        same = assess_pair(left, left_fast)
        self.assertEqual(same.outcome, "same_direction")
        self.assertAlmostEqual(same.turn_degrees, 0.0, delta=2)
        self.assertAlmostEqual(same.speed_ratio, 1.5, delta=0.05)
        self.assertIn("not distinguished", same.note)
        self.assertEqual(same.boundary, (left.last, left_fast.first))
        self.assertEqual(assess_pair(left, right).outcome, "different_direction")
        self.assertEqual(assess_pair(left, still).outcome, "unavailable")
        self.assertEqual(
            assess_pair(left, analyse(pan(-7, 0, frames=6))).outcome,
            "different_direction",
        )  # Speed ratio 3.5.

    def test_invalid_inputs_are_rejected(self):
        good = window([crop(BASE, 48, 48)] * 2)
        frame = good.frames[0]
        cases = {
            "invalid_window": [dict(width=W)],
            "invalid_size": [
                Window(97, H, good.frames),
                Window(W, 31, good.frames),
                Window(64.0, H, good.frames),
            ],
            "invalid_frame_count": [
                Window(W, H, good.frames[:1]),
                Window(
                    W, H, tuple(Frame(i, frame.pixels) for i in range(MAX_FRAMES + 1))
                ),
                Window(W, H, list(good.frames)[:0]),
            ],
            "invalid_positions": [
                Window(W, H, (Frame(5, frame.pixels), Frame(5, frame.pixels))),
                Window(W, H, (Frame(5, frame.pixels), Frame(4, frame.pixels))),
                Window(W, H, (Frame(-1, frame.pixels), Frame(4, frame.pixels))),
                Window(W, H, (Frame(True, frame.pixels), Frame(4, frame.pixels))),
            ],
            "invalid_pixels": [
                Window(W, H, (Frame(0, frame.pixels[:-1]), frame)),
                Window(
                    W, H, (Frame(0, [float("nan")] * (W * H)), Frame(1, frame.pixels))
                ),
                Window(
                    W, H, (Frame(0, [float("inf")] * (W * H)), Frame(1, frame.pixels))
                ),
                Window(W, H, (Frame(0, [True] * (W * H)), Frame(1, frame.pixels))),
                Window(W, H, (Frame(0, [256] * (W * H)), Frame(1, frame.pixels))),
                Window(W, H, (Frame(0, "x" * (W * H)), Frame(1, frame.pixels))),
            ],
        }
        for code, windows in cases.items():
            for bad in windows:
                with (
                    self.subTest(code=code),
                    self.assertRaises(MotionInputError) as caught,
                ):
                    analyse(bad)
                self.assertEqual(caught.exception.code, code)

    def test_cancellation_stops_the_search(self):
        calls = []

        def check():
            calls.append(1)
            if len(calls) == 20:
                raise RuntimeError("cancelled")

        with self.assertRaises(RuntimeError):
            analyse(pan(-2, 0), check=check)
        self.assertEqual(len(calls), 20)

    def test_largest_window_stays_within_the_fixed_search(self):
        size = 96
        tex = texture(5, 300)
        frames = [
            Frame(
                t,
                bytes(
                    round(v)
                    for row in tex[40 : 40 + size]
                    for v in row[190 - 8 * t : 190 - 8 * t + size]
                ),
            )
            for t in range(MAX_FRAMES)
        ]
        calls = []
        result = analyse(
            Window(size, size, tuple(frames)), check=lambda: calls.append(1)
        )
        # 8 px per frame sits exactly on the search radius and is still measured.
        self.assertEqual(result.direction, "right")
        self.assertAlmostEqual(result.velocity[0], 8.0, delta=0.1)
        # Per step: one call, nine coarse rows; plus one per frame for coverage.
        self.assertEqual(len(calls), (MAX_FRAMES - 1) * 10 + MAX_FRAMES)


if __name__ == "__main__":
    unittest.main()
