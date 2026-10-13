// Archipelago (archipelago.gg) multiworld support: the game side.
//
// An Archipelago file is a randomized file (open start, progressive items)
// whose chests and special places are locations of an Archipelago multiworld.
// They hand out a placeholder item and mark the location as checked; the
// server decides what was there and sends every item of our slot, our own
// finds included. Only the host (or the single player) talks to the server.
// The items enter the game as netplay frame events, so every computer gets
// them in the same frame, and the checks travel back from the save area of
// the final (confirmed) frames.
#ifndef ZELDA3_ARCHIPELAGO_H_
#define ZELDA3_ARCHIPELAGO_H_

#include "types.h"
#include <stddef.h>

// Save file bytes (offsets in the 7EF000 save area).
enum {
  kApSave_Marker = 0x407,     // 'A' in an Archipelago file
  kApSave_Bind = 0x408,       // 4 bytes: the room the file belongs to (ApRoom.bind_hash)
  kApSave_ItemCount = 0x410,  // 2 bytes: how many of the server's items were received
  kApSave_State = 0x412,      // kApState_* bits
  kApSave_Deaths = 0x413,     // game overs so far (for DeathLink)
  kApSave_Checked = 0x418,    // 32 bytes: a bit per location (synced as flags between worlds)
  kApSave_CheckedSize = 32,
  kApMarker = 0x41,
  kApPlaceholderItem = 0x4C,  // the item a location hands out
  kApSpecialBit = 168,        // bit of special place 0 (after the chests)
};
enum {
  kApState_Goal = 1,           // the goal was reached
  kApState_LinkedDeath = 2,    // the next game over came from DeathLink (not sent on)
  kApState_DeathPending = 4,   // a DeathLink still has Links to take down
};

// Netplay session events (session.h kSessEv_*): [type, a, b, c].
enum {
  kSessEv_ApItem = 3,   // [3, item code, index lo, index hi]
  kSessEv_ApDeath = 4,  // [4, 0, 0, 0]
};

// --- The simulation (deterministic, runs on every computer) ---

// New files are Archipelago files of this room (0: not). Comes with every
// netplay frame from the host.
void Ap_SetNewGameBind(uint32 bind);
uint32 Ap_GetNewGameBind();
// Called by the randomizer when a new file is made / was loaded.
void Ap_OnNewFile(uint8 *sram_slot);
void Ap_OnFileLoaded();
// The file in RAM is an Archipelago file. Its room, or 0.
bool Ap_IsApFile();
uint32 Ap_FileBind();
// A chest or special place was used (randomizer.c and dungeon.c).
void Ap_OnChestOpened(int chest);
void Ap_OnSpecialPlace(int which);
// The game is over / the Triforce was reached.
void Ap_OnGameOver();
void Ap_OnGoal();
// The events of a netplay frame, before the frame is run.
void Ap_ApplyEvent(const uint8 *ev);
// Before every frame: hands out what DeathLink left to do.
void Ap_BeforeFrame();
// Location ids of the checked locations in a save area (0x500 bytes).
int Ap_CheckedLocations(const uint8 *save, int64 *ids, int max);

// --- The host (not part of the simulation) ---

// Settings from the menu / zelda3.ini.
typedef struct ApSettings {
  char server[128];
  char slot[64];
  char password[64];
  int death_link;  // 0: as the room says, 1: on, 2: off
} ApSettings;
extern ApSettings g_ap_settings;

void Ap_Connect();
void Ap_Disconnect();
bool Ap_IsConnecting();
// Talks to the server: sends checks, the goal and deaths of the confirmed
// frames. Once per display frame, on the host or offline.
void Ap_Tick();
// For the host's next frame: an item or DeathLink event to add.
bool Ap_NextEvent(uint8 ev[4]);
// The bind hash new files get (0 while not connected to a room we can play).
uint32 Ap_HostBind();
// One line about the connection for the menu.
void Ap_StatusLine(char *buf, size_t size);
// Lines to show on the screen, oldest first (kind: ApTextKind).
bool Ap_PollNotice(char *buf, size_t size, int *kind);

// Tests: a pretend room instead of a server (bind 0: off). What the game
// would send is recorded.
void Ap_TestRoom(uint32 bind, bool death_link);
void Ap_TestGiveItem(int64 item);
void Ap_TestDeathLink();
typedef struct ApTestSent {
  int checks;    // locations sent (the latest list)
  int64 check[256];
  bool goal;
  int deaths;
} ApTestSent;
const ApTestSent *Ap_TestSent();

#endif  // ZELDA3_ARCHIPELAGO_H_
