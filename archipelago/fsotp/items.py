from BaseClasses import Item, ItemClassification as IC

GAME = "Four Swords of the Past"
# "FS" in the top bytes; item ids are BASE_ID + the code the game hands to
# Link_ReceiveItem, so the client turns an id into an item without a table.
BASE_ID = 0x46530000

# name -> (item code, how many in the pool, classification)
# Swords, shields, gloves, mails and boomerangs are progressive in the game
# itself (each one received is the next level), so one code does for all.
ITEMS: dict[str, tuple[int, int, IC]] = {
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
    # 2 from chests, and 2 for the extra Lamps of the original game
    "Rupees (300)": (0x46, 4, IC.filler),
    "Single Arrow": (0x43, 1, IC.filler),
    "Arrows (10)": (0x44, 10, IC.filler),
}

FILLER_ITEM = "Rupees (20)"

ITEM_NAME_TO_ID = {name: BASE_ID + code for name, (code, _, _) in ITEMS.items()}


class FSotPItem(Item):
    game = GAME
