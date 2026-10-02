# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import random
import unittest
from dataclasses import replace

from montaz_assistant.contracts import FrameRate
from montaz_assistant.shot_discovery import (
    DEFAULT_LIMITS,
    DiscoveryError,
    EvidenceWindow,
    Limits,
    discover,
    subject_terms,
)

RATE = FrameRate(30000, 1001)


def window(
    source,
    description,
    role="detail",
    quality=80,
    window_id=0,
    source_in=0,
    duration=90,
    content=None,
):
    return EvidenceWindow(
        source, window_id, source_in, duration, description, role, quality, content
    )


WHEELS = [
    window("1", "Close-up of a silver alloy wheel and tyre in the rain", content="a1"),
    window("2", "Gold spoked wheel on a white classic car, rim in focus", content="a2"),
    window("3", "Red sports car wheel with a five-spoke rim", content="a3"),
    window("4", "Black multi-spoke rim and tire on cobbles", content="a4"),
]
FRONTS = [
    window(
        "5",
        "Front of a matte Mercedes with headlights and grille",
        "establishing",
        content="b1",
    ),
    window(
        "6",
        "Front view of a white sports car, headlights and bumper wet",
        "establishing",
        content="b2",
    ),
]
TRAVEL = [
    window("7", "Waves breaking on an empty beach under a pale sky", "establishing"),
    window("8", "Snow-covered mountain ridge with clouds drifting", "establishing"),
    window("9", "Spices piled in baskets at a busy market stall", "detail"),
    window("10", "Golden temple roof against the evening sky", "closing"),
]


class ShotDiscoveryTests(unittest.TestCase):
    def test_unicode_identifiers_do_not_get_parsed_as_decimal_native_ids(self):
        windows = [window("²", "wheel and tyre"), window("3", "wheel and tyre")]
        result = discover(windows, RATE)
        self.assertEqual(len(result.pairs), 1)
        self.assertEqual(discover(windows[::-1], RATE), result)

    def test_wheels_and_car_fronts_form_separate_unverified_groups(self):
        result = discover(WHEELS + FRONTS, RATE)
        subjects = [g.subject for g in result.groups]
        self.assertEqual(subjects[0], "wheel")
        self.assertIn("car front", subjects)
        wheel_pairs = [p for p in result.pairs if p.subject == "wheel"]
        self.assertEqual(len(wheel_pairs), 6)  # Four distinct wheel sources.
        for p in result.pairs:
            self.assertFalse(p.verified)
            self.assertIn("unverified", p.reason)
            self.assertNotEqual(p.first.source_id, p.second.source_id)
        # A wide car front is never paired with a close wheel detail.
        self.assertFalse(
            any(
                {p.first.source_id, p.second.source_id} & {"5", "6"}
                and p.subject == "wheel"
                for p in result.pairs
            )
        )
        self.assertIsNone(result.empty_reason)

    def test_unrelated_travel_scenes_suggest_nothing(self):
        result = discover(TRAVEL, RATE)
        self.assertEqual((result.groups, result.pairs), ((), ()))
        self.assertEqual(result.empty_reason, "no_shared_subject")
        reasons = {e.reason for e in result.exclusions}
        self.assertEqual(reasons, {"no_matchable_subject"})

    def test_backgrounds_and_negated_subjects_are_not_matchable(self):
        self.assertEqual(subject_terms("Sand, sea and sky with grass on the dunes"), {})
        self.assertEqual(
            subject_terms("Empty road with no people or cars in sight"), {}
        )
        self.assertEqual(subject_terms("A man without a hat"), {"person": ("man",)})
        self.assertEqual(
            subject_terms("Car wheel; the tyre is wet"),
            {"wheel": ("wheel", "tyre"), "car": ("car",)},
        )

    def test_shared_subject_in_one_source_only_is_not_a_cut(self):
        same = [
            window("1", "A red boat moored at the pier", window_id=0, source_in=0),
            window("1", "The red boat drifting away", window_id=1, source_in=200),
        ]
        result = discover(same, RATE)
        self.assertEqual(result.pairs, ())
        self.assertEqual(result.empty_reason, "no_shared_subject")

    def test_content_identity_collapses_duplicates_but_unknown_identity_does_not(self):
        twins = [
            window("1", "Sailboat on calm water", quality=70, content="same"),
            window("2", "Sailboat on calm water", quality=90, content="same"),
            window("3", "A ferry crossing the bay"),
        ]
        result = discover(twins, RATE)
        excluded = {(e.source_id, e.reason) for e in result.exclusions}
        self.assertIn(
            ("1", "duplicate_content"), excluded
        )  # Lower-quality copy dropped.
        self.assertEqual(
            {(p.first.source_id, p.second.source_id) for p in result.pairs},
            {("2", "3")},
        )
        # Same identity, separate ranges: still one source, so never paired.
        apart = [replace(twins[0], source_in=500), twins[1]]
        self.assertEqual(discover(apart, RATE).pairs, ())
        # No identity: two bins are two sources, even with identical text.
        unknown = [
            replace(twins[0], content_id=None),
            replace(twins[1], content_id=None),
        ]
        self.assertEqual(len(discover(unknown, RATE).pairs), 1)

    def test_role_scale_and_quality_and_duration_gate_pairs(self):
        mixed = [
            window("1", "Close detail of a brass door handle", "detail"),
            window("2", "Wide view of a village gate at dawn", "establishing"),
            window("3", "Someone opens a wooden door", "action"),
            window("4", "Blurry door", "detail", quality=40),
            window("5", "Door slams", "action", duration=20),
        ]
        result = discover(mixed, RATE)
        pairs = {(p.first.source_id, p.second.source_id) for p in result.pairs}
        self.assertEqual(pairs, {("1", "3"), ("2", "3")})
        reasons = {(e.source_id, e.reason) for e in result.exclusions}
        self.assertIn(("4", "low_quality"), reasons)
        self.assertIn(("5", "too_short"), reasons)
        # The one-second floor follows the project rate: 20 frames pass at 15 FPS.
        slow = discover(mixed, FrameRate(15))
        self.assertNotIn(
            ("5", "too_short"), {(e.source_id, e.reason) for e in slow.exclusions}
        )

    def test_a_pair_is_suggested_once_under_its_most_specific_subject(self):
        result = discover(
            [
                window("1", "Car wheel and tyre", content="x"),
                window("2", "Car wheel spinning", content="y"),
            ],
            RATE,
        )
        self.assertEqual([p.subject for p in result.pairs], ["wheel"])
        self.assertEqual({g.subject for g in result.groups}, {"wheel", "car"})

    def test_ceilings_reject_or_truncate_explicitly(self):
        many = [
            window(str(i), f"Bicycle number {i}", window_id=0, content=f"c{i}")
            for i in range(25)
        ]
        with self.assertRaises(DiscoveryError) as caught:
            discover(many, RATE)
        self.assertEqual(caught.exception.code, "too_many_windows")
        with self.assertRaises(DiscoveryError):
            discover(many[:4], RATE, limits=Limits(windows=25))
        full = discover(many[:24], RATE)
        self.assertEqual(
            len(full.groups[0].members), DEFAULT_LIMITS.candidates_per_group
        )
        self.assertEqual(len(full.pairs), 15)  # Six candidates: 6 choose 2.
        self.assertTrue(full.truncated)
        self.assertEqual(
            sum(e.reason == "candidate_limit:bicycle" for e in full.exclusions), 18
        )
        # Seven distinct subjects, each in two sources, exceed six groups.
        subjects = ["boat", "door", "lamp", "statue", "ball", "bicycle", "mug"]
        spread = [
            window(f"{s}{k}", f"A {s} here", content=f"{s}{k}")
            for s in subjects
            for k in (1, 2)
        ]
        result = discover(spread, RATE)
        self.assertEqual(len(result.groups), 6)
        self.assertTrue(
            any(e.reason.startswith("group_limit:") for e in result.exclusions)
        )
        small = discover(spread, RATE, limits=Limits(pairs=2))
        self.assertEqual(len(small.pairs), 2)
        self.assertTrue(small.truncated)

    def test_malformed_input_is_rejected_whole(self):
        good = WHEELS[0]
        for bad in (
            replace(good, window_id=True),
            replace(good, window_id=-1),
            replace(good, source_in=1.5),
            replace(good, duration_frames=0),
            replace(good, description=""),
            replace(good, description="x" * 601),
            replace(good, role="hero"),
            replace(good, quality=101),
            replace(good, quality=True),
            replace(good, source_id=""),
            replace(good, content_id=""),
        ):
            with self.subTest(bad=bad), self.assertRaises(DiscoveryError) as caught:
                discover([bad], RATE)
            self.assertEqual(caught.exception.code, "invalid_evidence")
        with self.assertRaises(DiscoveryError) as caught:
            discover([good, replace(good, description="again")], RATE)
        self.assertEqual(caught.exception.code, "duplicate_window")
        for windows, rate in (
            ({"1": good}, RATE),
            ([good], 30),
            ([dict(vars(good))], RATE),
        ):
            with self.subTest(windows=type(windows)), self.assertRaises(DiscoveryError):
                discover(windows, rate)

    def test_output_is_stable_for_any_input_order_and_ties_use_native_ids(self):
        ties = [
            window(str(i), "A lantern glowing", quality=80, content=f"l{i}")
            for i in (100, 25, 300, 11)
        ]
        expected = discover(ties, RATE)
        self.assertEqual(
            [(p.first.source_id, p.second.source_id) for p in expected.pairs],
            [
                # Numeric native order (11 < 25 < 100 < 300), not text order.
                ("11", "25"),
                ("11", "100"),
                ("11", "300"),
                ("25", "100"),
                ("25", "300"),
                ("100", "300"),
            ],
        )
        rng = random.Random(3)
        everything = WHEELS + FRONTS + TRAVEL + ties
        baseline = discover(everything, RATE)
        for _ in range(10):
            rng.shuffle(everything)
            self.assertEqual(discover(everything, RATE), baseline)

    def test_cancellation_is_checked_inside_bounded_loops(self):
        calls = []

        def check():
            calls.append(1)
            if len(calls) == 5:
                raise RuntimeError("cancelled")

        with self.assertRaises(RuntimeError):
            discover(WHEELS + FRONTS, RATE, check=check)
        counter = []
        discover(WHEELS + FRONTS + TRAVEL, RATE, check=lambda: counter.append(1))
        # Each loop is bounded by the window/group/candidate ceilings.
        self.assertLess(len(counter), 24 * 3 + 6 + 6 * 15)


if __name__ == "__main__":
    unittest.main()
