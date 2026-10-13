"""Spot checks of the logic against the rules in src/randomizer.c, worked
out by hand from Evaluate and RoomAccess."""
import random

from . import FSotPTestBase
from ..locations import LOCATIONS
from ..rules import LOGIC_ITEMS, can_reach, evaluate

EASTERN = ["Eastern Palace - Big Chest", "Eastern Palace - Cannonball Chest"]
EASTERN_ITEMS = {"Lamp": 1, "Bow": 1, "Progressive Sword": 1}
# Hera: a glove and the Lamp for the old man, then the mirror he leads to.
HERA_ITEMS = {"Lamp": 1, "Progressive Glove": 1, "Progressive Sword": 1}
# The Desert Palace: the book (dash into the shelf with the boots that
# Sahasrahla gives for the Eastern Palace's pendant) and a glove.
DESERT_ITEMS = {**EASTERN_ITEMS, "Progressive Glove": 1}
# All three pendants.
PEDESTAL_ITEMS = DESERT_ITEMS
# Ganon's Tower needs all seven crystals and then some: every item the logic
# knows except the Cape (only another way past Agahnim's barrier) and the
# shields (only for the waterfall fairy).
TOWER_ITEMS = {"Progressive Sword": 2, "Progressive Glove": 2, "Moon Pearl": 1, "Hammer": 1, "Hookshot": 1,
               "Bow": 1, "Lamp": 1, "Fire Rod": 1, "Ice Rod": 1, "Cane of Somaria": 1}
MAX_ITEMS = {**TOWER_ITEMS, "Progressive Sword": 4, "Progressive Shield": 3, "Cape": 1}

# Open from the start of an open-mode game.
START = {"Link's House", "Secret Passage", "Sanctuary", "Aginah's Cave", "Ice Rod Cave", "Floodgate Chest",
         "Link's Uncle", "Link's Uncle - Second Item",
         *(f"Kakariko Well - {s}" for s in ("Top", "Left", "Middle", "Right", "Bottom")),
         *(f"Blind's Hideout - {s}" for s in ("Top", "Left", "Right", "Far Left", "Far Right")),
         *(f"Sahasrahla's Hut - {s}" for s in ("Left", "Middle", "Right")),
         *(f"Mini Moldorm Cave - {s}" for s in ("Far Left", "Left", "Right", "Far Right"))}


def less(items: dict[str, int], name: str) -> dict[str, int]:
    """items with one copy of name taken away."""
    out = dict(items)
    out[name] -= 1
    return out


class TestLogic(FSotPTestBase):
    def assertNeedsExactly(self, locations: list[str], items: dict[str, int]) -> None:
        """Reachable with items, and not when any one of them is missing."""
        for location in locations:
            self.assertTrue(self.reachable(location, items), location)
            for name in items:
                self.assertFalse(self.reachable(location, less(items, name)), f"{location} without one {name}")

    def test_start(self) -> None:
        self.assertEqual(len(START), 25)
        for location in self.world.get_locations():
            self.assertEqual(self.reachable(location.name, {}), location.name in START, location.name)

    def test_eastern_palace(self) -> None:
        self.assertNeedsExactly(EASTERN, EASTERN_ITEMS)

    def test_pendant_dungeons(self) -> None:
        self.assertNeedsExactly(["Desert Palace - Big Chest"], DESERT_ITEMS)
        self.assertNeedsExactly(["Tower of Hera - Big Chest"], HERA_ITEMS)
        # The torches there take the Lamp or the Fire Rod, but the way there
        # needs the Lamp anyway.
        for location in ("Desert Palace - Big Chest", "Tower of Hera - Big Chest"):
            self.assertFalse(self.reachable(location, {**less(DESERT_ITEMS, "Lamp"), "Fire Rod": 1}))

    def test_pedestal(self) -> None:
        self.assertNeedsExactly(["Master Sword Pedestal"], PEDESTAL_ITEMS)
        # Fewer pendants are not enough.
        self.assertFalse(self.reachable("Master Sword Pedestal", EASTERN_ITEMS))
        self.assertFalse(self.reachable("Master Sword Pedestal", HERA_ITEMS))

    def test_ganons_tower(self) -> None:
        tower = [name for name in LOCATIONS if name.startswith("Ganons Tower")]
        self.assertEqual(len(tower), 20)
        self.assertNeedsExactly(tower, TOWER_ITEMS)

    def test_ganon(self) -> None:
        self.assertNeedsExactly(["Ganon"], TOWER_ITEMS)
        self.assertTrue(self.reachable("Ganon", MAX_ITEMS))

    def test_excluded_need_the_win(self) -> None:
        for location in ("Kakariko Tavern", "Hyrule Castle - Zelda's Chest", "Sewers - Secret Room - Left"):
            self.assertNeedsExactly([location], TOWER_ITEMS)

    def test_specials(self) -> None:
        self.assertNeedsExactly(["Waterfall Fairy - Shield Upgrade"], {"Progressive Glove": 1, "Progressive Shield": 1})
        self.assertNeedsExactly(["Blacksmith"], {"Moon Pearl": 1, "Progressive Glove": 2, "Progressive Sword": 1})
        # Crystals 5 and 6: Ice Palace and Misery Mire (Ether needs the Master Sword).
        self.assertNeedsExactly(["Pyramid Fairy - Sword Upgrade"],
                                {"Progressive Sword": 2, "Progressive Glove": 2, "Moon Pearl": 1, "Fire Rod": 1,
                                 "Hammer": 1, "Hookshot": 1, "Cane of Somaria": 1, "Lamp": 1, "Bow": 1})

    def test_caves(self) -> None:
        self.assertNeedsExactly(["Spike Cave"],
                                {"Progressive Glove": 2, "Lamp": 1, "Moon Pearl": 1, "Hammer": 1, "Cape": 1})
        self.assertNeedsExactly(["King's Tomb"], {**EASTERN_ITEMS, "Progressive Glove": 2})
        self.assertNeedsExactly(["Mire Shed - Left"], {"Progressive Glove": 2, "Moon Pearl": 1})
        self.assertNeedsExactly(["Hookshot Cave - Top Left"],
                                {"Progressive Glove": 2, "Lamp": 1, "Moon Pearl": 1, "Hookshot": 1})
        self.assertNeedsExactly(["Spiral Cave", "Paradox Cave Upper - Left"],
                                {"Progressive Glove": 1, "Lamp": 1, "Hookshot": 1})
        self.assertTrue(self.reachable("Spiral Cave", {"Progressive Glove": 1, "Lamp": 1, "Hammer": 1}))

    def test_dark_world(self) -> None:
        # North: the Pearl and the Titan's Mitt, or a glove and the Hammer.
        self.assertNeedsExactly(["C-Shaped House"], {"Moon Pearl": 1, "Progressive Glove": 2})
        self.assertNeedsExactly(["C-Shaped House"], {"Moon Pearl": 1, "Progressive Glove": 1, "Hammer": 1})
        # Or past Agahnim with the Hookshot from the catfish's bank.
        self.assertNeedsExactly(["C-Shaped House"], {"Moon Pearl": 1, "Progressive Sword": 1, "Cape": 1, "Lamp": 1,
                                                     "Hookshot": 1, "Hammer": 1})
        # South: after Agahnim, the Hammer or the flippers (a glove) will do.
        self.assertNeedsExactly(["Hype Cave - Top"],
                                {"Moon Pearl": 1, "Progressive Sword": 2, "Lamp": 1, "Hammer": 1})
        self.assertFalse(self.reachable("Hype Cave - Top", {"Moon Pearl": 1, "Progressive Sword": 2, "Lamp": 1}))

    def test_rules_follow_rooms(self) -> None:
        # Each location's rule is the logic for its own room, whatever the state.
        rng = random.Random(7)
        for _ in range(200):
            items = {name: rng.randint(0, cap) for name, cap in zip(LOGIC_ITEMS, (4, 3, 2) + (1,) * 9)}
            state = self.state_with(items)
            e = evaluate(items)
            for name, (_, room) in LOCATIONS.items():
                self.assertEqual(self.world.get_location(name).can_reach(state), can_reach(room, e), name)
            self.assertEqual(self.world.get_location("Ganon").can_reach(state), e.win)
