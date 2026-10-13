from BaseClasses import Location

from .items import BASE_ID, GAME
from .location_data import CHESTS

# Swords and shields that aren't in chests. `which` and the made-up room
# numbers are the port's (kSpecialLocations in src/randomizer.c).
# (which, room, item code in the original game, name)
SPECIALS = [
    (0, 0x1000, 0x49, "Link's Uncle"),
    (1, 0x1001, 0x04, "Link's Uncle - Second Item"),
    (2, 0x1002, 0x01, "Master Sword Pedestal"),
    (3, 0x1003, 0x02, "Blacksmith"),
    (4, 0x1004, 0x03, "Pyramid Fairy - Sword Upgrade"),
    (5, 0x1005, 0x05, "Waterfall Fairy - Shield Upgrade"),
]

# name -> (id, room the logic knows the location by)
LOCATIONS: dict[str, tuple[int, int]] = {
    **{name: (BASE_ID + chest, room) for chest, room, _, name in CHESTS},
    **{name: (BASE_ID + 0x100 + which, room) for which, room, _, name in SPECIALS},
}

LOCATION_NAME_TO_ID = {name: loc_id for name, (loc_id, _) in LOCATIONS.items()}


class FSotPLocation(Location):
    game = GAME
