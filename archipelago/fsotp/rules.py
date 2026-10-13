"""The logic of the port's own randomizer (Evaluate, RoomAccess and CanReach
in src/randomizer.c), ported line by line, so that the game and Archipelago
agree on what can be reached with what.

No Archipelago imports here: tools/logic_vectors.py loads this file on its
own to write test vectors for the C side.
"""
from functools import lru_cache

# Items the logic looks at. Everything else that matters (boots, book,
# flippers, mirror, flute, medallions) never moves, so Evaluate works out
# when the player has it.
SWORD = "Progressive Sword"
SHIELD = "Progressive Shield"
GLOVE = "Progressive Glove"
FLAGS = ("Lamp", "Bow", "Hookshot", "Hammer", "Fire Rod", "Ice Rod", "Moon Pearl", "Cane of Somaria", "Cape")
LOGIC_ITEMS = (SWORD, SHIELD, GLOVE) + FLAGS


class Ev:
    """What the player can do and where they can go (struct Ev in C)."""
    __slots__ = ("lift1", "lift2", "lamp", "bow", "hook", "hammer", "fire", "ice", "pearl", "somaria", "cape",
                 "sword1", "sword2", "shield1", "pedestal",
                 "ep", "boots", "book", "dp", "dm", "mirror", "dm_east", "hera", "ms", "aga", "dw_s", "dw_n",
                 "flippers", "flute", "ether", "quake", "dwdm",
                 "pod", "sp", "sw", "tt", "ip", "mm", "tr", "gt", "win")


def evaluate(count) -> Ev:
    """count: item name -> how many the player has (a dict or Counter).
    The result is shared between callers, so don't change it."""
    # Only these levels matter to the logic; capping them keeps the cache small.
    return _evaluate(min(count.get(SWORD, 0), 2), min(count.get(SHIELD, 0), 1), min(count.get(GLOVE, 0), 2),
                     *(count.get(name, 0) > 0 for name in FLAGS))


@lru_cache(maxsize=None)
def _evaluate(swords: int, shields: int, gloves: int, lamp: bool, bow: bool, hook: bool, hammer: bool,
              fire: bool, ice: bool, pearl: bool, somaria: bool, cape: bool) -> Ev:
    e = Ev()
    e.lift1 = gloves >= 1
    e.lift2 = gloves >= 2
    e.lamp = lamp
    e.bow = bow
    e.hook = hook
    e.hammer = hammer
    e.fire = fire
    e.ice = ice
    e.pearl = pearl
    e.somaria = somaria
    e.cape = cape
    e.sword1 = swords >= 1
    e.sword2 = swords >= 2                          # the Master Sword level
    e.shield1 = shields >= 1
    # Light World. Every dungeon needs a sword for its boss.
    e.ep = e.lamp and e.bow and e.sword1            # Eastern Palace
    e.boots = e.ep                                  # Sahasrahla, after the green pendant
    e.book = e.boots                                # library: dash into the shelf
    e.dp = e.book and e.lift1 and (e.lamp or e.fire) and e.sword1  # Desert Palace
    e.dm = e.lift1 and e.lamp                       # Death Mountain via the old man
    e.mirror = e.dm
    e.dm_east = e.dm and (e.hook or (e.mirror and e.hammer))
    e.hera = e.dm and (e.mirror or (e.hook and e.hammer)) and (e.lamp or e.fire) and e.sword1
    e.pedestal = e.ep and e.dp and e.hera           # three pendants
    e.ms = e.sword2
    # The barrier to the tower needs the Master Sword level, or the cape.
    e.aga = (e.sword2 or e.cape) and e.sword1 and e.lamp
    e.flippers = e.lift1                            # Zora
    # Dark World. Beating Agahnim only drops you at the pyramid: pegs block
    # the way south (the Hammer), a rock the way to the catfish's bank (a
    # glove or the Hammer), and the hookshot gap west starts from that bank.
    e.dw_n = e.pearl and (e.lift2 or (e.lift1 and e.hammer) or
                          (e.aga and e.hook and (e.lift1 or e.hammer or e.flippers)))
    e.dw_s = e.pearl and (e.lift2 or (e.lift1 and e.hammer) or (e.aga and (e.hammer or e.flippers)) or e.dw_n)
    e.flute = e.dw_s                                # shovel from the flute boy, then dig
    e.ether = e.dm and e.book and e.sword2          # tablets need the Master Sword
    e.quake = e.dw_n and e.lift1                    # catfish
    e.dwdm = e.dm_east and e.lift2 and e.pearl
    e.pod = e.dw_s and e.hammer and e.bow and e.lamp and e.sword1
    e.sp = e.dw_s and e.mirror and e.flippers and e.hook and e.hammer and e.sword1
    e.sw = e.dw_n and e.fire and e.sword1
    e.tt = e.dw_n and e.hammer and e.sword1
    e.ip = e.lift2 and e.flippers and e.pearl and e.fire and e.hammer and e.hook and e.sword1
    e.mm = (e.lift2 and e.flute and e.pearl and e.ether and e.somaria and e.lamp and e.fire and
            e.boots and e.hook)
    # (Quake only works with a sword.)
    e.tr = (e.dm_east and e.lift2 and e.hammer and e.pearl and e.somaria and e.quake and
            e.fire and e.ice and e.lamp and e.hook and e.sword1)
    crystals = e.pod and e.sp and e.sw and e.tt and e.ip and e.mm and e.tr
    e.gt = (crystals and e.dwdm and e.hook and e.hammer and e.bow and e.fire and e.somaria and
            e.lamp and e.boots)
    e.win = e.gt and e.aga
    return e


# room -> what reaching a chest there takes (the switch in RoomAccess).
ROOM_RULES = {}


def _rooms(rooms, rule):
    for room in rooms:
        ROOM_RULES[room] = rule


# Places open from the start (bombs can be bought). The game starts after
# the escape, so the castle basement and sewers (rooms 0x11, 0x32, 0x71,
# 0x72, 0x80), whose keys come from the escape's guards, only get filler.
_rooms((0x104, 0x55, 0x12, 0x2F, 0x11D, 0x105, 0x10A, 0x123, 0x120, 0x10B), lambda e: True)
_rooms((0xE0, 0xD0), lambda e: e.aga)               # Agahnim's tower
_rooms((0xA8, 0xA9, 0xAA, 0xB8, 0xB9), lambda e: e.ep)
_rooms((0x73, 0x74, 0x75, 0x85), lambda e: e.dp)
_rooms((0x27, 0x77, 0x87), lambda e: e.hera)
_rooms((0x09, 0x0A, 0x19, 0x1A, 0x2A, 0x2B, 0x3A, 0x6A), lambda e: e.pod)
_rooms((0x16, 0x28, 0x34, 0x35, 0x36, 0x37, 0x46, 0x66, 0x76), lambda e: e.sp)
_rooms((0x57, 0x58, 0x59, 0x67, 0x68), lambda e: e.sw)
_rooms((0x44, 0x45, 0x65, 0xCB, 0xDB, 0xDC), lambda e: e.tt)
_rooms((0x1F, 0x2E, 0x3F, 0x5F, 0x7E, 0x9E, 0xAE), lambda e: e.ip)
_rooms((0xA2, 0xB3, 0xC1, 0xC2, 0xC3, 0xD1), lambda e: e.mm)
_rooms((0x04, 0x14, 0x24, 0xB6, 0xB7, 0xD5, 0xD6), lambda e: e.tr)
_rooms((0x1C, 0x3D, 0x4D, 0x5B, 0x7B, 0x7C, 0x7D, 0x8B, 0x8C, 0x8D, 0x9D), lambda e: e.gt)
_rooms((0x11C,), lambda e: e.dw_n)                  # C-shaped house
_rooms((0x11E,), lambda e: e.dw_s)                  # hype cave
_rooms((0x113,), lambda e: e.boots and e.lift2)     # King's tomb
_rooms((0x117,), lambda e: e.dwdm and e.hammer and e.cape)  # spike cave
_rooms((0x10D,), lambda e: e.lift2 and e.flute and e.pearl)  # Mire shed
_rooms((0xEF, 0xFF, 0xFE), lambda e: e.dm_east)     # paradox cave, spiral cave
_rooms((0x10C,), lambda e: e.tr)                    # mimic cave
_rooms((0xF8,), lambda e: e.dwdm)                   # superbunny cave
_rooms((0x3C,), lambda e: e.dwdm and e.hook and e.lift1)  # hookshot cave
# Swords and shields outside chests
_rooms((0x1000, 0x1001), lambda e: True)            # the uncle, at the start
_rooms((0x1002,), lambda e: e.pedestal)
_rooms((0x1003,), lambda e: e.dw_n and e.lift2 and e.sword1)  # frog smith, then hand over a sword
_rooms((0x1004,), lambda e: e.dw_s and e.ip and e.mm and e.sword1)  # crystals 5 and 6, throw a sword
_rooms((0x1005,), lambda e: e.flippers and e.shield1)  # throw a shield into the waterfall


def room_access(room: int, e: Ev) -> int:
    """1 if a chest in room can be reached, 0 if not, -1 if the logic doesn't
    know the room (then only filler goes there)."""
    rule = ROOM_RULES.get(room)
    if rule is None:
        return -1
    return 1 if rule(e) else 0


def can_reach(room: int, e: Ev) -> bool:
    a = room_access(room, e)
    return e.win if a < 0 else a != 0


def is_known_room(room: int) -> bool:
    return room in ROOM_RULES
