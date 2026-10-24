// "Legend of Links": a co-op roguelite run. A run is one randomized save file
// with a shared pool of team lives. A team wipe (the game over that happens when
// every Link is down and no fairy helps) costs one life; at zero the run is over.
// See docs/designs/05-coop-roguelite.md in the mixlab repository.
#ifndef ZELDA3_RUN_H_
#define ZELDA3_RUN_H_

#include "types.h"

// Run header in the save file (offsets in the 7EF000 save area, which every world
// syncs after each frame, see instance.c). 0x444 is kApSave_End: the bytes up to
// 0x4EF were unclaimed. Document every byte here (freeze at first release).
enum {
  kRunSave_Marker = 0x444,      // kRunMarker in a run file
  kRunSave_Version = 0x445,     // kRunFormatVersion
  kRunSave_RunId = 0x446,       // 4 bytes, reserved (profile-wide run number)
  kRunSave_Lives = 0x44A,       // team lives left (a counter: summed across worlds, see Run_IsCounterAddr)
  kRunSave_LivesStart = 0x44B,  // team lives at the start
  kRunSave_Mutators = 0x450,    // 4 bytes, reserved
  kRunSave_Goal = 0x454,        // reserved: goal kind
  kRunSave_GoalNeeded = 0x455,  // reserved: bosses or crystals needed
  kRunSave_Wipes = 0x45A,       // team wipes so far (a counter, saturates at 255)
  kRunSave_Revives = 0x45C,     // revives so far (a counter), reserved
  kRunSave_State = 0x467,       // kRunState_* bits
  kRunSave_End = 0x468,
};
enum {
  kRunMarker = 0x55,  // 'U'
  kRunFormatVersion = 1,
  kRunState_Over = 1,  // the lives ran out
  kRunState_Won = 2,   // reserved: the goal was reached
  kRun_StartLives = 3,    // for one player; one more for each extra player
  kRun_MaxLives = 9,
};

// New save files are run files (the host decides; see mptest "run").
void Run_SetNewGameSetting(bool on);
bool Run_GetNewGameSetting();
// Called when a new save file is made (after the randomizer set its seed).
void Run_OnNewFile(uint8 *sram_slot);

// The loaded game (RAM) is a run file.
bool Run_IsRunFile();
int Run_Lives();
int Run_Wipes();
bool Run_IsOver();
// The same, from a save area (Inst_SaveArea(): 0x500 bytes), for the display.
bool Run_AreaIsRunFile(const uint8 *save_area);
int Run_AreaLives(const uint8 *save_area);
bool Run_AreaIsOver(const uint8 *save_area);

// A game over starts (Death_Func1): costs the team a life, or ends the run.
void Run_OnGameOver();
// The game over menu chose something (GameOver_SaveAndOrContinue): in a run, every
// choice saves, so the lives just spent cannot be taken back by quitting without
// saving; and an ended run saves and quits to the title.
void Run_FilterDeathChoice();
// Is this save-area address (0xF000 + offset) one that worlds add up like a counter?
bool Run_IsCounterAddr(int addr);

#endif  // ZELDA3_RUN_H_
