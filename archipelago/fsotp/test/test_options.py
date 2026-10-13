from BaseClasses import CollectionState, LocationProgressType
from Fill import distribute_items_restrictive
from worlds.AutoWorld import call_all

from . import FSotPTestBase


class TestDeathLinkOff(FSotPTestBase):
    def test_slot_data(self) -> None:
        self.assertEqual(self.world.fill_slot_data(), {"version": 1, "death_link": 0, "goal": 0})


class TestDeathLinkOn(FSotPTestBase):
    options = {"death_link": True}

    def test_slot_data(self) -> None:
        self.assertEqual(self.world.fill_slot_data(), {"version": 1, "death_link": 1, "goal": 0})


class TestFill(FSotPTestBase):
    def fill(self, seed: int) -> None:
        self.world_setup(seed)
        distribute_items_restrictive(self.multiworld)
        call_all(self.multiworld, "post_fill")

    def test_excluded_get_filler(self) -> None:
        for seed in range(10):
            with self.subTest(seed=seed):
                self.fill(seed)
                excluded = [loc for loc in self.multiworld.get_locations(self.player)
                            if loc.progress_type == LocationProgressType.EXCLUDED]
                self.assertEqual(len(excluded), 9)
                for loc in excluded:
                    self.assertFalse(loc.item.advancement or loc.item.useful, f"{loc.name}: {loc.item.name}")

    def test_lamp_reachable_from_the_start(self) -> None:
        for seed in range(10):
            with self.subTest(seed=seed):
                self.fill(seed)
                lamp = next(loc for loc in self.multiworld.get_filled_locations() if loc.item.name == "Lamp")
                self.assertEqual(lamp.player, self.player)
                self.assertTrue(lamp.can_reach(CollectionState(self.multiworld)), lamp.name)

    def test_beatable(self) -> None:
        for seed in range(10):
            with self.subTest(seed=seed):
                self.fill(seed)
                self.assertTrue(self.multiworld.can_beat_game(CollectionState(self.multiworld)))
