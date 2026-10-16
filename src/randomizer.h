// Basic item randomizer: shuffles the contents of every chest in the game.
//
// A randomized game starts in "open mode": no intro and no escape from
// Hyrule Castle; Link can go anywhere. Swords and shields are randomized too:
// the uncle (waiting in the secret passage) gives two random items instead
// of his sword and shield, and the pedestal, the smiths and the fairies give
// random items.
//
// Whether a new game is randomized is decided when the save file is created
// (Gameplay menu > Randomizer). The seed is stored in the save file, so the
// game stays randomized the same way after saving and loading. Small keys,
// big keys, maps and compasses stay in their dungeons; everything else in a
// chest (Bow, Hookshot, Hammer, gloves, Moon Pearl, rods, Lamp, mails,
// shields, heart pieces, rupees, ...) is shuffled with a logic check that
// every seed can be finished without glitches.
#ifndef ZELDA3_RANDOMIZER_H_
#define ZELDA3_RANDOMIZER_H_

#include "types.h"

enum {
  kRando_Off = 0,
  kRando_RandomSeed = 1,  // any other value is a fixed seed
};

// Setting for games created from now on (applied deterministically, so it
// comes with the netplay frame like other session settings).
void Rando_SetNewGameSetting(uint32 setting);
uint32 Rando_GetNewGameSetting();

// Called when the file select screen creates a new save file.
void Rando_OnNewFile(uint8 *sram_slot);
// Called when a save file was loaded into RAM.
void Rando_OnFileLoaded();

// Is the game in RAM randomized? Returns the seed, or 0.
uint32 Rando_CurrentSeed();

// The item in chest number idx (index into the chest table).
uint8 Rando_ChestItem(int idx, uint8 vanilla_item);
// Does chest number idx take part in the shuffle (and so is an Archipelago
// location)? Dungeon items stay, and some table entries have no chest.
bool Rando_IsApLocation(int idx);
// Applied to every item Link receives: swords, shields, gloves and mails are
// progressive (the next level of what Link has). (Not in a file of
// Archipelago's "A Link to the Past" world: its codes are final.)
uint8 Rando_AdjustReceivedItem(uint8 item);
// Link's sword level (0-4), also while the smiths have it.
int Rando_SwordLevel();

// Places outside chests that give swords and shields.
enum {
  kRando_StartSword, kRando_StartShield, kRando_Pedestal, kRando_Smiths,
  kRando_PyramidFairy, kRando_WaterfallFairy,
  kRando_NumSpecial,
};
// The item given at one of those places instead of vanilla_item.
// Marks the place as used.
uint8 Rando_SpecialItem(int which, uint8 vanilla_item);
bool Rando_SpecialDone(int which);
// The smiths keep Link's sword while tempering it; remember its level.
void Rando_OnSmithsTakeSword();
void Rando_OnSmithsReturnSword();
// The uncle in the secret passage hands over his two (random) items.
void Rando_UncleGivesItems();
// Hands out the uncle's second item as soon as Link is free to receive it.
void Rando_GiveStartItems();

// Writes a spoiler log for seed to path: where the important items are, in
// the order they can be collected, then every location. False on failure.
bool Rando_WriteSpoiler(uint32 seed, const char *path);

// The check tracker (the overlay's page after a map closes): the locations of
// the randomized file in a save area (0x500 bytes from 7EF000), by area, as
// found, reachable with the items the file has (the randomizer's own logic,
// which is on the safe side) or locked. Locations in rooms the logic doesn't
// know (only filler goes there) are neither reachable nor locked. Only reads
// save. False for a file that isn't randomized.
typedef struct RandoTrackerArea {
  const char *name;
  int total, found, reachable, locked;
} RandoTrackerArea;
typedef struct RandoTracker {
  bool archipelago;
  uint32 seed;
  int total, found, reachable, locked;
  int nareas;
  RandoTrackerArea area[16];  // Light World, Death Mountain, Dark World, the dungeons
  // Items (or the next level of one) that would make more reachable, the
  // most first: how many more. When no single item would, pairs (name2).
  int nneeded;
  struct { const char *name, *name2; int more; } needed[8];
} RandoTracker;
bool Rando_Tracker(const uint8 *save, RandoTracker *t);

// Tests: the logic for a set of items (count by item code): can a chest in
// room be reached (1, 0, -1 for a room the logic doesn't know), and can the
// game be finished.
int Rando_TestRoomAccess(const uint8 counts[256], int room);
bool Rando_TestWin(const uint8 counts[256]);

// Tests: generates the placement for seed and checks it can be beaten.
// Prints the spoiler when verbose. Returns false if no placement was found.
bool Rando_Check(uint32 seed, bool verbose);

#endif  // ZELDA3_RANDOMIZER_H_
