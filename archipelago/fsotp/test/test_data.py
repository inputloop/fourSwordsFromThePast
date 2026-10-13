from collections import Counter

from BaseClasses import ItemClassification as IC, LocationProgressType

from . import FSotPTestBase
from ..location_data import CHESTS

BASE = 0x46530000

# The item table of the spec, written out again on purpose: the world's own
# table must match it, not itself.
SPEC_ITEMS = {
    "Progressive Sword": (0x49, 4, IC.progression),
    "Progressive Shield": (0x04, 3, IC.progression),
    "Progressive Glove": (0x1B, 2, IC.progression),
    "Progressive Mail": (0x22, 2, IC.useful),
    "Progressive Boomerang": (0x0C, 3, IC.useful),
    "Bow": (0x0B, 1, IC.progression),
    "Hookshot": (0x0A, 1, IC.progression),
    "Hammer": (0x09, 1, IC.progression),
    "Fire Rod": (0x07, 1, IC.progression),
    "Ice Rod": (0x08, 1, IC.progression),
    "Lamp": (0x12, 1, IC.progression),
    "Cane of Somaria": (0x15, 1, IC.progression),
    "Cape": (0x19, 1, IC.progression),
    "Moon Pearl": (0x1F, 1, IC.progression),
    "Bottle": (0x16, 1, IC.useful),
    "Piece of Heart": (0x17, 6, IC.useful),
    "Cane of Byrna": (0x18, 1, IC.useful),
    "Heart Container": (0x3F, 1, IC.useful),
    "Bombs (3)": (0x28, 17, IC.filler),
    "Rupee (1)": (0x34, 2, IC.filler),
    "Rupees (5)": (0x35, 2, IC.filler),
    "Rupees (20)": (0x36, 30, IC.filler),
    "Rupees (50)": (0x41, 7, IC.filler),
    "Rupees (100)": (0x40, 1, IC.filler),
    "Rupees (300)": (0x46, 4, IC.filler),
    "Single Arrow": (0x43, 1, IC.filler),
    "Arrows (10)": (0x44, 10, IC.filler),
}

SPEC_SPECIALS = {
    "Link's Uncle": 0,
    "Link's Uncle - Second Item": 1,
    "Master Sword Pedestal": 2,
    "Blacksmith": 3,
    "Pyramid Fairy - Sword Upgrade": 4,
    "Waterfall Fairy - Shield Upgrade": 5,
}

# Rooms the logic doesn't know: the castle sewers and basement, and houses
# the port's randomizer leaves out.
EXCLUDED = {
    "Sewers - Secret Room - Left", "Sewers - Secret Room - Middle", "Sewers - Secret Room - Right",
    "Hyrule Castle - Boomerang Chest", "Hyrule Castle - Zelda's Chest",
    "Kakariko Tavern", "Brewery", "Chicken House", "Bonk Rock Cave",
}

LOGIC_ITEMS = ["Progressive Sword", "Progressive Shield", "Progressive Glove", "Bow", "Hookshot", "Hammer",
               "Fire Rod", "Ice Rod", "Lamp", "Cane of Somaria", "Cape", "Moon Pearl"]


class TestData(FSotPTestBase):
    def test_item_ids(self) -> None:
        self.assertEqual(self.world.item_name_to_id, {name: BASE + code for name, (code, _, _) in SPEC_ITEMS.items()})

    def test_pool(self) -> None:
        pool = self.multiworld.itempool
        self.assertEqual(len(pool), 106)
        self.assertEqual(Counter(item.name for item in pool),
                         Counter({name: count for name, (_, count, _) in SPEC_ITEMS.items()}))
        for item in pool:
            self.assertEqual(item.classification, SPEC_ITEMS[item.name][2], item.name)
            self.assertEqual(item.code, BASE + SPEC_ITEMS[item.name][0], item.name)

    def test_progression_in_pool(self) -> None:
        names = {item.name for item in self.multiworld.itempool if item.advancement}
        for name in LOGIC_ITEMS:
            self.assertIn(name, names)

    def test_filler(self) -> None:
        self.assertEqual(self.world.get_filler_item_name(), "Rupees (20)")
        self.assertEqual(self.world.create_filler().code, BASE + 0x36)

    def test_location_ids(self) -> None:
        expected = {name: BASE + chest for chest, _, _, name in CHESTS}
        expected.update({name: BASE + 0x100 + which for name, which in SPEC_SPECIALS.items()})
        self.assertEqual(len(expected), 106)
        self.assertEqual(self.world.location_name_to_id, expected)
        real = [loc for loc in self.multiworld.get_locations(self.player) if loc.address is not None]
        self.assertEqual({loc.name: loc.address for loc in real}, expected)

    def test_item_count_matches_locations(self) -> None:
        real = [loc for loc in self.multiworld.get_locations(self.player) if loc.address is not None]
        self.assertEqual(len(real), len(self.multiworld.itempool))

    def test_goal_event(self) -> None:
        ganon = self.world.get_location("Ganon")
        self.assertIsNone(ganon.address)
        self.assertEqual(ganon.item.name, "Victory")
        self.assertIsNone(ganon.item.code)

    def test_excluded_locations(self) -> None:
        excluded = {loc.name for loc in self.multiworld.get_locations(self.player)
                    if loc.progress_type == LocationProgressType.EXCLUDED}
        self.assertEqual(excluded, EXCLUDED)

    def test_lamp_is_local_early(self) -> None:
        self.assertEqual(self.multiworld.local_early_items[self.player].get("Lamp"), 1)
