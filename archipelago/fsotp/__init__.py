from typing import Any, Mapping

from BaseClasses import ItemClassification, LocationProgressType, Region, Tutorial
from worlds.AutoWorld import WebWorld, World

from .items import FILLER_ITEM, GAME, ITEM_NAME_TO_ID, ITEMS, FSotPItem
from .locations import LOCATION_NAME_TO_ID, LOCATIONS, FSotPLocation
from .options import FSotPOptions
from .rules import can_reach, evaluate, is_known_room

# The game refuses slot data of any other version.
SLOT_DATA_VERSION = 1


class FSotPWeb(WebWorld):
    theme = "grassFlowers"
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "How to play Four Swords of the Past in an Archipelago multiworld.",
        "English",
        "setup_en.md",
        "setup/en",
        ["fourSwordsFromThePast"],
    )]


class FSotPWorld(World):
    """
    A PC port of A Link to the Past for up to four players. The items in the
    chests (all but keys, maps and compasses) and the swords and shields from
    Link's uncle, the pedestal, the smiths and the fairies go into the
    multiworld. A co-op team plays as one slot.
    """
    game = GAME
    web = FSotPWeb()
    options_dataclass = FSotPOptions
    options: FSotPOptions
    item_name_to_id = ITEM_NAME_TO_ID
    location_name_to_id = LOCATION_NAME_TO_ID

    def generate_early(self) -> None:
        # Most of the game needs the Lamp, so it goes to a place of this game
        # that is open from the start (the port's own randomizer is stricter:
        # Link's house or the sanctuary).
        self.multiworld.local_early_items[self.player]["Lamp"] = 1

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        hyrule = Region("Hyrule", self.player, self.multiworld)
        menu.connect(hyrule)
        hyrule.add_locations(LOCATION_NAME_TO_ID, FSotPLocation)
        for name, (_, room) in LOCATIONS.items():
            if not is_known_room(room):
                # Out of the logic (the castle sewers, a few houses): filler only.
                self.get_location(name).progress_type = LocationProgressType.EXCLUDED
        ganon = FSotPLocation(self.player, "Ganon", None, hyrule)
        ganon.place_locked_item(FSotPItem("Victory", ItemClassification.progression, None, self.player))
        hyrule.locations.append(ganon)
        self.multiworld.regions += [menu, hyrule]

    def create_item(self, name: str) -> FSotPItem:
        _, _, classification = ITEMS[name]
        return FSotPItem(name, classification, ITEM_NAME_TO_ID[name], self.player)

    def create_items(self) -> None:
        self.multiworld.itempool += [self.create_item(name)
                                     for name, (_, count, _) in ITEMS.items() for _ in range(count)]

    def get_filler_item_name(self) -> str:
        return FILLER_ITEM

    def set_rules(self) -> None:
        player = self.player

        def room_rule(room: int):
            return lambda state: can_reach(room, evaluate(state.prog_items[player]))

        for name, (_, room) in LOCATIONS.items():
            self.get_location(name).access_rule = room_rule(room)
        self.get_location("Ganon").access_rule = lambda state: evaluate(state.prog_items[player]).win
        self.multiworld.completion_condition[player] = lambda state: state.has("Victory", player)

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {
            "version": SLOT_DATA_VERSION,
            "death_link": int(self.options.death_link.value),
            "goal": int(self.options.goal.value),
        }
