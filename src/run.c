#include "mods.h"  // (mods/SPEC.md: this file is the roguelite mod)
#if MOD_ROGUELITE
#include "run.h"
#include "mp.h"
#include "variables.h"
#include <string.h>

static bool g_new_game_run;

void Run_SetNewGameSetting(bool on) { g_new_game_run = on; }
bool Run_GetNewGameSetting() { return g_new_game_run; }

void Run_OnNewFile(uint8 *sram_slot) {
  // The run header always starts clean, so a file that isn't a run carries nothing of one.
  memset(sram_slot + kRunSave_Marker, 0, kRunSave_End - kRunSave_Marker);
  if (!g_new_game_run)
    return;
  int players = Mp_NumJoined();
  int lives = kRun_StartLives + (players > 1 ? players - 1 : 0);
  if (lives > kRun_MaxLives)
    lives = kRun_MaxLives;
  sram_slot[kRunSave_Marker] = kRunMarker;
  sram_slot[kRunSave_Version] = kRunFormatVersion;
  sram_slot[kRunSave_Lives] = sram_slot[kRunSave_LivesStart] = lives;
}

bool Run_AreaIsRunFile(const uint8 *a) { return a[kRunSave_Marker] == kRunMarker; }
int Run_AreaLives(const uint8 *a) { return a[kRunSave_Lives]; }
bool Run_AreaIsOver(const uint8 *a) { return (a[kRunSave_State] & kRunState_Over) != 0; }

bool Run_IsRunFile() { return Run_AreaIsRunFile(g_ram + 0xF000); }
int Run_Lives() { return Run_AreaLives(g_ram + 0xF000); }
int Run_Wipes() { return g_ram[0xF000 + kRunSave_Wipes]; }
bool Run_IsOver() { return Run_AreaIsOver(g_ram + 0xF000); }

void Run_OnGameOver() {
  if (!Run_IsRunFile() || Run_IsOver())
    return;
  // A fairy in a bottle brings the team back (GameOver_SplatAndFade): not a wipe.
  for (int i = 0; i < 4; i++)
    if (link_bottle_info[i] == 6)
      return;
  uint8 *r = g_ram + 0xF000;
  if (r[kRunSave_Wipes] < 255)
    r[kRunSave_Wipes]++;
  if (r[kRunSave_Lives] > 0)
    r[kRunSave_Lives]--;
  if (r[kRunSave_Lives] == 0)
    r[kRunSave_State] |= kRunState_Over;
}

void Run_FilterDeathChoice() {
  if (!Run_IsRunFile())
    return;
  // Choice 2 ("Continue") plays on from RAM without saving: quitting afterwards would
  // reload the older copy in the save slot and give the spent life back. In a run every
  // choice saves first.
  if (subsubmodule_index == 2)
    subsubmodule_index = 0;
  // An ended run saves and quits to the title.
  if (Run_IsOver())
    subsubmodule_index = 1;
}

bool Run_IsCounterAddr(int addr) {
  return addr == 0xF000 + kRunSave_Lives || addr == 0xF000 + kRunSave_Wipes ||
         addr == 0xF000 + kRunSave_Revives;
}
#endif  // MOD_ROGUELITE
