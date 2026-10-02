# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import unittest
from dataclasses import replace
from fractions import Fraction

from montaz_assistant import planning
from montaz_assistant.contracts import FrameRate
from montaz_assistant.travel_recipes import (
    RECIPES,
    SLOTS,
    TRANSITIONS,
    RecipeError,
    plan_slots,
    recipe,
    resolve_transitions,
    validate,
)

NTSC = FrameRate(30000, 1001)
# What the editor can produce and review today.
TODAY = frozenset({"hard_cut", "beat_cut", "match_cut"})
FULL = {"establishing": 3, "other": 2, "action": 2, "detail": 2, "closing": 1}


class TravelRecipeTests(unittest.TestCase):
    def test_catalog_is_versioned_and_preserves_planner_shot_rules(self):
        self.assertEqual(set(RECIPES), {"postcard", "adventure", "diary"})
        for name, r in RECIPES.items():
            with self.subTest(name=name):
                validate(r)
                self.assertEqual(r.id, f"{name}@1")
                self.assertEqual(tuple(s.name for s in r.slots), SLOTS)
                # The planner's minimum/maximum shot seconds and story are unchanged.
                minimum, maximum, story = planning.RECIPES[name]
                self.assertEqual(r.shot_seconds, (minimum, maximum))
                self.assertEqual(r.story, story)
                self.assertTrue(r.distinct_sources)
                self.assertGreaterEqual(r.shot_seconds[0], 1)
                low = sum(s.share[0] for s in r.slots)
                high = sum(s.share[1] for s in r.slots)
                self.assertLessEqual(low, 1)
                self.assertGreaterEqual(high, 1)

    def test_music_arcs_and_beat_preferences_differ_by_recipe(self):
        energy = {n: [m.energy for m in r.music] for n, r in RECIPES.items()}
        self.assertEqual(max(energy["adventure"]), 5)
        self.assertEqual(energy["adventure"].index(5), SLOTS.index("detail_or_action"))
        self.assertLessEqual(max(energy["diary"]), 2)
        self.assertEqual(
            {n: r.beat_cuts for n, r in RECIPES.items()},
            {"postcard": "allow", "adventure": "prefer", "diary": "avoid"},
        )

    def test_quick_match_montage_is_not_a_hidden_recipe(self):
        for name in ("quick_match", "montage", "Postcard", None):
            with self.subTest(name=name), self.assertRaises(RecipeError) as caught:
                recipe(name)
            self.assertEqual(caught.exception.code, "unknown_recipe")
        # No recipe allows sub-second shots.
        self.assertTrue(all(r.shot_seconds[0] >= 1 for r in RECIPES.values()))

    def test_unimplemented_transitions_fall_back_to_hard_cuts_with_reasons(self):
        resolved = {t.slot: t for t in resolve_transitions(recipe("adventure"), TODAY)}
        self.assertEqual(resolved["detail_or_action"].chosen, "beat_cut")
        self.assertIsNone(resolved["detail_or_action"].note)
        nothing = {
            t.slot: t for t in resolve_transitions(recipe("adventure"), frozenset())
        }
        self.assertEqual({t.chosen for t in nothing.values()}, {"hard_cut"})
        self.assertEqual(
            nothing["detail_or_action"].note,
            "beat cut, speed ramp, whip pan are not implemented; using a hard cut instead.",
        )
        diary = {t.slot: t for t in resolve_transitions(recipe("diary"), TODAY)}
        self.assertEqual(
            (diary["ending"].preferred, diary["ending"].chosen),
            ("dissolve", "hard_cut"),
        )
        self.assertEqual(
            diary["ending"].note,
            "dissolve is not implemented; using a hard cut instead.",
        )
        everything = resolve_transitions(recipe("adventure"), set(TRANSITIONS))
        self.assertTrue(
            all(t.chosen == t.preferred and t.note is None for t in everything)
        )
        for bad in ({"teleport"}, ["hard_cut"], "hard_cut"):
            with self.subTest(bad=bad), self.assertRaises(RecipeError):
                resolve_transitions(recipe("postcard"), bad)

    def test_full_inventory_plan_uses_exact_frames_at_fractional_rates(self):
        plan = plan_slots(recipe("postcard"), NTSC, 20, FULL)
        self.assertTrue(plan.feasible)
        self.assertEqual(
            plan.requested_frames, 599
        )  # 20 s at 30000/1001, rounded half up.
        self.assertEqual(plan.achievable_frames, 599)
        self.assertEqual(plan.shot_frames, (60, 120))
        self.assertEqual(plan.max_shots, 9)
        self.assertEqual(plan.explanation, "All story slots have fitting footage.")
        for slot, recipe_slot in zip(plan.slots, recipe("postcard").slots):
            self.assertEqual(
                slot.frames,
                (int(recipe_slot.share[0] * 599), -(-recipe_slot.share[1] * 599 // 1)),
            )
            self.assertIsNone(slot.note)
        self.assertLessEqual(sum(s.frames[0] for s in plan.slots), 599)
        self.assertGreaterEqual(sum(s.frames[1] for s in plan.slots), 599)
        # Opening and ending each take one source; the middle shares the rest.
        counts = {s.slot: s.available_sources for s in plan.slots}
        self.assertEqual((counts["opening"], counts["ending"]), (1, 1))
        self.assertEqual(sum(counts.values()), sum(FULL.values()))

    def test_short_requests_collapse_slots_or_are_impossible(self):
        short = plan_slots(recipe("postcard"), NTSC, 3, FULL)
        self.assertTrue(short.feasible)
        self.assertEqual(short.max_shots, 1)
        self.assertIn("at most 1 shot of 2-4 s", short.explanation)
        self.assertIn("rather than using shorter shots", short.explanation)
        impossible = plan_slots(recipe("postcard"), NTSC, 1, FULL)
        self.assertFalse(impossible.feasible)
        self.assertEqual(impossible.slots, ())
        self.assertIn(
            "shorter than this recipe's 2 s minimum shot", impossible.explanation
        )
        # Adventure allows one-second shots, so the same request is possible.
        self.assertTrue(plan_slots(recipe("adventure"), NTSC, 1, FULL).feasible)

    def test_requests_beyond_distinct_footage_are_not_padded(self):
        plan = plan_slots(
            recipe("postcard"), FrameRate(25), 60, {"establishing": 2, "detail": 1}
        )
        self.assertTrue(plan.feasible)
        self.assertEqual(
            plan.achievable_frames, 3 * 100
        )  # Three sources of at most 4 s.
        self.assertIn(
            "3 distinct sources allow at most 12.0 s without repeating footage",
            plan.explanation,
        )
        self.assertLess(plan.achievable_frames, plan.requested_frames)

    def test_missing_roles_are_explained_honestly(self):
        plan = plan_slots(recipe("postcard"), FrameRate(24), 20, {"other": 3})
        notes = {s.slot: s.note for s in plan.slots}
        self.assertIn("No establishing view was observed", notes["opening"])
        self.assertIn("No detail or activity was observed", notes["detail_or_action"])
        self.assertIn("rather than inventing an ending", notes["ending"])
        self.assertIsNone(notes["development"])
        for note in filter(None, notes.values()):
            self.assertIn(note, plan.explanation)
        nothing = plan_slots(recipe("diary"), FrameRate(24), 20, {})
        self.assertFalse(nothing.feasible)
        self.assertIn("No usable footage", nothing.explanation)

    def test_plans_are_deterministic(self):
        first = plan_slots(recipe("adventure"), NTSC, 45, FULL)
        reordered = dict(reversed(list(FULL.items())))
        self.assertEqual(plan_slots(recipe("adventure"), NTSC, 45, reordered), first)

    def test_action_footage_can_fill_both_middle_slots_without_repeats(self):
        plan = plan_slots(
            recipe("adventure"), NTSC, 12, {"establishing": 2, "action": 3}
        )
        counts = {s.slot: s.available_sources for s in plan.slots}
        self.assertGreater(counts["development"], 0)
        self.assertGreater(counts["detail_or_action"], 0)
        self.assertEqual(sum(counts.values()), 5)
        self.assertTrue(all(s.note is None for s in plan.slots))

    def test_invalid_requests_and_records_are_rejected(self):
        r = recipe("postcard")
        for args, code in (
            ((r, 30, 20, FULL), "invalid_rate"),
            ((r, NTSC, 0, FULL), "invalid_duration"),
            ((r, NTSC, 601, FULL), "invalid_duration"),
            ((r, NTSC, True, FULL), "invalid_duration"),
            ((r, NTSC, 20.0, FULL), "invalid_duration"),
            ((r, NTSC, 20, {"hero": 1}), "invalid_inventory"),
            ((r, NTSC, 20, {"detail": -1}), "invalid_inventory"),
            ((r, NTSC, 20, {"detail": True}), "invalid_inventory"),
            ((r, NTSC, 20, [("detail", 1)]), "invalid_inventory"),
        ):
            with self.subTest(args=args[1:]), self.assertRaises(RecipeError) as caught:
                plan_slots(*args)
            self.assertEqual(caught.exception.code, code)
        broken = (
            replace(r, version=0),
            replace(r, shot_seconds=(0, 4)),
            replace(r, distinct_sources=False),
            replace(r, slots=r.slots[::-1]),
            replace(
                r,
                slots=(replace(r.slots[0], transitions_in=("whip_pan",)),)
                + r.slots[1:],
            ),
            replace(r, slots=(replace(r.slots[0], roles=("hero",)),) + r.slots[1:]),
            replace(
                r,
                slots=(replace(r.slots[0], share=(Fraction(0), Fraction(1, 2))),)
                + r.slots[1:],
            ),
            replace(r, beat_cuts="always"),
        )
        for bad in broken:
            with self.subTest(bad=bad.name), self.assertRaises(RecipeError):
                validate(bad)


if __name__ == "__main__":
    unittest.main()
