// Basic item randomizer: shuffles the contents of every chest in the game.
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

// Is the game in RAM randomized? Returns the seed, or 0.
uint32 Rando_CurrentSeed();

// The item in chest number idx (index into the chest table).
uint8 Rando_ChestItem(int idx, uint8 vanilla_item);
// Applied to an item received from a chest: gloves and mails are progressive.
uint8 Rando_AdjustReceivedItem(uint8 item);

// Tests: generates the placement for seed and checks it can be beaten.
// Prints the spoiler when verbose. Returns false if no placement was found.
bool Rando_Check(uint32 seed, bool verbose);

#endif  // ZELDA3_RANDOMIZER_H_
