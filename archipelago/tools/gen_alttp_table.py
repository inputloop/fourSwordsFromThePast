#!/usr/bin/env python3
"""Writes src/ap_alttp_data.h: the location ids of Archipelago's own
*A Link to the Past* world for the places of this game.

    python3 archipelago/tools/gen_alttp_table.py zelda3.sfc path/to/worlds/alttp

The ROM is only read for the chest table (room, item), to put the chests in
the order the game's chest list has them (see gen_locations.py); nothing of
it is copied. The worlds/alttp folder is Archipelago's
(https://github.com/ArchipelagoMW/Archipelago): Regions.py has the ids,
Client.py the save flags each place sets.
"""
import re
import sys

CHEST_TABLE = 0xE96E
NUM_CHESTS = 168
DEAD_ROOMS = {0x005, 0x016, 0x05B, 0x12D, 0x13D}

# The places outside chests, in the order of their bits in the save (after
# the chests; the first six are the randomizer's own, see kRando_* in
# src/randomizer.h). kind: how the game tells the place (see ApLocKind in
# src/archipelago.h), a/b: its key.
#   npc     a hook of its own in the game (a, b unused)
#   owheart a heart piece on overworld screen a
#   uwflag  a room's saved flag: room a, bit b (heart pieces, torch keys, the
#           generous guys, the chest game's prize)
#   boss    the heart container of dungeon a (the game's dungeon index)
SPECIALS = [
    ("Link's Uncle", 'npc', 0, 0),           # kRando_StartSword
    (None, 'none', 0, 0),                    # kRando_StartShield: this game only
    ("Master Sword Pedestal", 'npc', 0, 0),  # kRando_Pedestal
    ("Blacksmith", 'npc', 0, 0),             # kRando_Smiths
    (None, 'none', 0, 0),                    # kRando_PyramidFairy: this game only
    (None, 'none', 0, 0),                    # kRando_WaterfallFairy: this game only
    ("King Zora", 'npc', 0, 0),
    ("Sahasrahla", 'npc', 0, 0),
    ("Sick Kid", 'npc', 0, 0),
    ("Hobo", 'npc', 0, 0),
    ("Bottle Merchant", 'npc', 0, 0),
    ("Purple Chest", 'npc', 0, 0),
    ("Old Man", 'npc', 0, 0),
    ("Stumpy", 'npc', 0, 0),
    ("Potion Shop", 'npc', 0, 0),
    ("Library", 'npc', 0, 0),
    ("Mushroom", 'npc', 0, 0),
    ("Magic Bat", 'npc', 0, 0),
    ("Ether Tablet", 'npc', 0, 0),
    ("Bombos Tablet", 'npc', 0, 0),
    ("Catfish", 'npc', 0, 0),
    ("Flute Spot", 'npc', 0, 0),
    ("Sunken Treasure", 'owheart', 0, 0),
    ("Zora's Ledge", 'owheart', 0, 0),
    ("Lake Hylia Island", 'owheart', 0, 0),
    ("Maze Race", 'owheart', 0, 0),
    ("Desert Ledge", 'owheart', 0, 0),
    ("Spectacle Rock", 'owheart', 0, 0),
    ("Pyramid", 'owheart', 0, 0),
    ("Digging Game", 'owheart', 0, 0),
    ("Bumper Cave Ledge", 'owheart', 0, 0),
    ("Floating Island", 'owheart', 0, 0),
    ("Lost Woods Hideout", 'uwflag', 0, 0),
    ("Lumberjack Tree", 'uwflag', 0, 0),
    ("Cave 45", 'uwflag', 0, 0),
    ("Graveyard Cave", 'uwflag', 0, 0),
    ("Checkerboard Cave", 'uwflag', 0, 0),
    ("Spectacle Rock Cave", 'uwflag', 0, 0),
    ("Peg Cave", 'uwflag', 0, 0),
    ("Chest Game", 'uwflag', 0, 0),
    ("Mini Moldorm Cave - Generous Guy", 'uwflag', 0, 0),
    ("Hype Cave - Generous Guy", 'uwflag', 0, 0),
    ("Desert Palace - Torch", 'uwflag', 0, 0),
    ("Tower of Hera - Basement Cage", 'uwflag', 0, 0),
    ("Ganons Tower - Bob's Torch", 'uwflag', 0, 0),
    ("Eastern Palace - Boss", 'boss', 2, 0),
    ("Desert Palace - Boss", 'boss', 3, 0),
    ("Tower of Hera - Boss", 'boss', 10, 0),
    ("Palace of Darkness - Boss", 'boss', 6, 0),
    ("Swamp Palace - Boss", 'boss', 5, 0),
    ("Skull Woods - Boss", 'boss', 8, 0),
    ("Thieves' Town - Boss", 'boss', 11, 0),
    ("Ice Palace - Boss", 'boss', 9, 0),
    ("Misery Mire - Boss", 'boss', 7, 0),
    ("Turtle Rock - Boss", 'boss', 12, 0),
    # The original game has no chests in the fairies' caves (the randomizer
    # puts them there): the fairy of the Pyramid hands these out for the
    # first two things thrown in, the one of the Waterfall of Wishing likewise.
    ("Pyramid Fairy - Left", 'npc', 0, 0),
    ("Pyramid Fairy - Right", 'npc', 0, 0),
    ("Waterfall Fairy - Left", 'npc', 0, 0),
    ("Waterfall Fairy - Right", 'npc', 0, 0),
]
# The keys enemies drop and the ones under pots, places with that world's
# key_drop_shuffle (on by default): the key sets its room's flag like the
# torch keys. (Their ids are the second number of Regions.py's key_drop_data.)
KEY_DROPS = [
    "Hyrule Castle - Map Guard Key Drop", "Hyrule Castle - Boomerang Guard Key Drop",
    "Sewers - Key Rat Key Drop", "Hyrule Castle - Big Key Drop",
    "Eastern Palace - Dark Square Pot Key", "Eastern Palace - Dark Eyegore Key Drop",
    "Desert Palace - Desert Tiles 1 Pot Key", "Desert Palace - Beamos Hall Pot Key",
    "Desert Palace - Desert Tiles 2 Pot Key",
    "Castle Tower - Dark Archer Key Drop", "Castle Tower - Circle of Pots Key Drop",
    "Swamp Palace - Pot Row Pot Key", "Swamp Palace - Trench 1 Pot Key", "Swamp Palace - Hookshot Pot Key",
    "Swamp Palace - Trench 2 Pot Key", "Swamp Palace - Waterway Pot Key",
    "Skull Woods - West Lobby Pot Key", "Skull Woods - Spike Corner Key Drop",
    "Ice Palace - Jelly Key Drop", "Ice Palace - Conveyor Key Drop", "Ice Palace - Hammer Block Key Drop",
    "Ice Palace - Many Pots Pot Key",
    "Misery Mire - Spikes Pot Key", "Misery Mire - Fishbone Pot Key", "Misery Mire - Conveyor Crystal Key Drop",
    "Turtle Rock - Pokey 1 Key Drop", "Turtle Rock - Pokey 2 Key Drop",
    "Ganons Tower - Conveyor Cross Pot Key", "Ganons Tower - Double Switch Pot Key",
    "Ganons Tower - Conveyor Star Pits Pot Key", "Ganons Tower - Mini Helmasaur Key Drop",
]
SPECIALS += [(name, 'uwflag', 0, 0) for name in KEY_DROPS]

KINDS = {'none': 0, 'npc': 1, 'owheart': 2, 'uwflag': 3, 'boss': 4}
# Where the check tracker counts each place (bosses and the keys in
# dungeons: by their room). 0 Light World, 1 Death Mountain, 2 Dark World.
AREAS = {'lw': 0, 'dm': 1, 'dw': 2, 'dungeon': 3}
AREA_OF = {"Link's Uncle": 'lw', 'Master Sword Pedestal': 'lw', 'Blacksmith': 'dw', 'King Zora': 'lw', 'Sahasrahla': 'lw', 'Sick Kid': 'lw', 'Hobo': 'lw', 'Bottle Merchant': 'lw', 'Purple Chest': 'dw', 'Old Man': 'dm', 'Stumpy': 'dw', 'Potion Shop': 'lw', 'Library': 'lw', 'Mushroom': 'lw', 'Magic Bat': 'lw', 'Ether Tablet': 'dm', 'Bombos Tablet': 'lw', 'Catfish': 'dw', 'Flute Spot': 'lw', 'Sunken Treasure': 'lw', "Zora's Ledge": 'lw', 'Lake Hylia Island': 'lw', 'Maze Race': 'lw', 'Desert Ledge': 'lw', 'Spectacle Rock': 'dm', 'Pyramid': 'dw', 'Digging Game': 'dw', 'Bumper Cave Ledge': 'dw', 'Floating Island': 'dm', 'Lost Woods Hideout': 'lw', 'Lumberjack Tree': 'lw', 'Cave 45': 'lw', 'Graveyard Cave': 'lw', 'Checkerboard Cave': 'lw', 'Spectacle Rock Cave': 'dm', 'Peg Cave': 'dw', 'Chest Game': 'dw', 'Mini Moldorm Cave - Generous Guy': 'lw', 'Hype Cave - Generous Guy': 'dw', 'Desert Palace - Torch': 'dungeon', 'Tower of Hera - Basement Cage': 'dungeon', "Ganons Tower - Bob's Torch": 'dungeon', 'Pyramid Fairy - Left': 'dw', 'Pyramid Fairy - Right': 'dw', 'Waterfall Fairy - Left': 'lw', 'Waterfall Fairy - Right': 'lw'}


def cname(name):
    s = re.sub(r"[^A-Za-z0-9]+", " ", name).title().replace(" ", "")
    return "kApLoc_" + s


def main():
    rom = open(sys.argv[1], 'rb').read()
    if len(rom) & 0x200:
        rom = rom[0x200:]
    regions = open(sys.argv[2] + '/Regions.py').read()
    client = open(sys.argv[2] + '/Client.py').read()
    ids = {}
    for m in re.finditer(r"""^\s*\{?(['"])(.+?)\1:\s*\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+|None)""", regions, re.M):
        ids[m.group(2)] = int(m.group(3), 16)
    for m in re.finditer(r"""^\s*(['"])(.+?)\1:\s*\[(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)""",
                         regions[regions.index('key_drop_data = {'):], re.M):
        ids[m.group(2)] = int(m.group(4), 16)
    uw = {}
    for m in re.finditer(r"""(['"])(.+?)\1:\s*\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)""",
                         client[client.index('location_table_uw ='):client.index('location_table_npc =')]):
        uw[m.group(2)] = (int(m.group(3), 16), int(m.group(4), 16))
    ow = {}
    for m in re.finditer(r"""(['"])(.+?)\1:\s*(0x[0-9a-fA-F]+)""",
                         client[client.index('location_table_ow ='):client.index('location_table_misc =')]):
        ow[m.group(2)] = int(m.group(3), 16)

    ent = []
    for i in range(NUM_CHESTS):
        a = CHEST_TABLE + 3 * i
        w = rom[a] | rom[a + 1] << 8
        ent.append((CHEST_TABLE + 3 * i, w & 0x7FFF))
    order = sorted(range(NUM_CHESTS), key=lambda i: ent[i][1])
    by_addr = {addr: name for name, addr in ids.items()}
    chests = []
    for idx, ri in enumerate(order):
        addr, room = ent[ri]
        name = by_addr.get(addr)
        if room in DEAD_ROOMS:
            chests.append((0, None))
            continue
        if not name:
            # (The original game's table has an entry that no chest uses,
            # the second one for room 0x7e; that world has no place for it.)
            print(f'chest {idx} (room {room:#x}, {addr:#x}) is no place of that world', file=sys.stderr)
            chests.append((0, None))
            continue
        chests.append((addr, name))

    out = ['// Generated by archipelago/tools/gen_alttp_table.py; do not edit.',
           '// The location ids of Archipelago\'s own "A Link to the Past" world for the',
           '// places of this game. (A chest\'s id is the address of its entry in the',
           '// chest table of the original game; 0: an entry without a chest.)',
           '#ifndef ZELDA3_AP_ALTTP_DATA_H_', '#define ZELDA3_AP_ALTTP_DATA_H_', '',
           f'#define kAlttpNumChests {NUM_CHESTS}',
           'static const uint32 kAlttpChestIds[kAlttpNumChests] = {']
    for idx, (addr, name) in enumerate(chests):
        out.append(f'  {addr:#07x},  // {idx}: {name}' if name else f'  0,  // {idx}: no chest')
    out += ['};', '', '// The places outside chests, by their bit after the chests (kApSpecialBit).',
            'enum {']
    for i, (name, kind, a, b) in enumerate(SPECIALS):
        if name:
            out.append(f'  {cname(name)} = {i},')
    out += [f'  kApNumSpecials = {len(SPECIALS)},', '};', '',
            'typedef struct ApAlttpSpecial {', '  uint32 id;    // the location id, 0: not a place of that world',
            '  uint8 kind;   // ApLocKind', '  uint8 area;   // for the check tracker: 0 Light World, 1 Death Mountain, 2 Dark World, 3 by room (b)',
            '  uint16 a, b;  // its key (see gen_alttp_table.py; a boss: its dungeon and its room)',
            '} ApAlttpSpecial;', 'static const ApAlttpSpecial kAlttpSpecials[kApNumSpecials] = {']
    for name, kind, a, b in SPECIALS:
        if not name:
            out.append('  {0, 0, 0, 0, 0},')
            continue
        if kind == 'owheart':
            a, b = ow[name], 0x40
        elif kind == 'uwflag':
            a, b = uw[name]
        elif kind == 'boss':
            b = uw[name][0]
        area = AREAS[AREA_OF.get(name, 'dungeon')] if kind != 'boss' else AREAS['dungeon']
        out.append(f'  {{{ids[name]:#x}, {KINDS[kind]}, {area}, {a:#x}, {b:#x}}},  // {name}')
    out += ['};', '', '#endif  // ZELDA3_AP_ALTTP_DATA_H_']
    open('src/ap_alttp_data.h', 'w').write('\n'.join(out) + '\n')
    n = sum(1 for addr, _ in chests if addr)
    print(n, 'chests,', sum(1 for s in SPECIALS if s[0]), 'other places')


if __name__ == '__main__':
    main()
