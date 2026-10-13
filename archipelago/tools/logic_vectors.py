#!/usr/bin/env python3
"""Writes test vectors for checking the APWorld's logic (fsotp/rules.py)
against the game's own (Evaluate and RoomAccess in src/randomizer.c).

    python3 archipelago/tools/logic_vectors.py OUT [COUNT] [SEED]

One line per item state:

    items 49=2 04=1 1b=0 ... 1f=1 | rooms 0011=-1 0012=1 ... 1005=0 | win=0

items: how many of each item the logic looks at, by the code the game gives
it (hex). rooms: every room with a location, and RoomAccess for it (1, 0, or
-1 for a room the logic doesn't know). win: whether Ganon can be beaten.
The first two lines have nothing and everything; the rest are random.
Needs no Archipelago: it loads rules.py and location_data.py by path.
"""
import importlib.util
import os
import random
import sys

FSOTP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fsotp')

# (item code, name in the APWorld, most the game can have)
ITEMS = [
    (0x49, 'Progressive Sword', 4),
    (0x04, 'Progressive Shield', 3),
    (0x1B, 'Progressive Glove', 2),
    (0x0B, 'Bow', 1),
    (0x0A, 'Hookshot', 1),
    (0x09, 'Hammer', 1),
    (0x07, 'Fire Rod', 1),
    (0x08, 'Ice Rod', 1),
    (0x12, 'Lamp', 1),
    (0x15, 'Cane of Somaria', 1),
    (0x19, 'Cape', 1),
    (0x1F, 'Moon Pearl', 1),
]


def load(name):
    spec = importlib.util.spec_from_file_location('fsotp_' + name, os.path.join(FSOTP, name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def random_state(rng):
    # Plain random states rarely get deep into the game, so some start from
    # everything and lose a few items, and some start from nothing and gain
    # a few: that covers the edges of each rule.
    kind = rng.randrange(3)
    if kind == 0:
        return tuple(rng.randint(0, most) for _, _, most in ITEMS)
    counts = [most if kind == 1 else 0 for _, _, most in ITEMS]
    for _ in range(rng.randint(1, 4)):
        i = rng.randrange(len(ITEMS))
        counts[i] = max(0, counts[i] - 1) if kind == 1 else min(ITEMS[i][2], counts[i] + 1)
    return tuple(counts)


def main():
    if not 2 <= len(sys.argv) <= 4:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    rules = load('rules')
    chests = load('location_data').CHESTS
    rooms = sorted({room for _, room, _, _ in chests} | set(range(0x1000, 0x1006)))
    # Every item the logic looks at must be in the vectors.
    missing = set(rules.LOGIC_ITEMS) - {name for _, name, _ in ITEMS}
    if missing:
        raise SystemExit(f'items missing from the vectors: {sorted(missing)}')

    rng = random.Random(seed)
    states = [tuple(0 for _ in ITEMS), tuple(most for _, _, most in ITEMS)]
    seen = set(states)
    # There are 5*4*3*2**9 = 30720 states; stop short of them all.
    count = min(count, 30720)
    while len(states) < count:
        state = random_state(rng)
        if state not in seen:
            seen.add(state)
            states.append(state)

    lines = []
    for state in states[:count]:
        e = rules.evaluate({name: n for (_, name, _), n in zip(ITEMS, state)})
        items = ' '.join(f'{code:02x}={n}' for (code, _, _), n in zip(ITEMS, state))
        access = ' '.join(f'{room:04x}={rules.room_access(room, e)}' for room in rooms)
        lines.append(f'items {items} | rooms {access} | win={int(e.win)}\n')
    with open(out, 'w', newline='\n') as f:
        f.writelines(lines)
    print(f'{len(lines)} states, {len(rooms)} rooms -> {out}')


if __name__ == '__main__':
    main()
