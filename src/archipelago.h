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
#include "ap_alttp_data.h"
#include <stddef.h>

// The Archipelago world a file (or the connection) plays. Archipelago's own
// "A Link to the Past" world (the one its Launcher knows, no extra files)
// is the default; "Four Swords of the Past" is this game's own
// (archipelago/fsotp), with fewer locations.
typedef enum ApWorld {
  kApWorld_Fsotp = 0,  // (0: files from before the choice existed)
  kApWorld_Alttp = 1,
} ApWorld;
const char *Ap_WorldGameName(ApWorld w);

// Save file bytes (offsets in the 7EF000 save area).
enum {
  kApSave_Marker = 0x407,     // 'A' in an Archipelago file
  kApSave_Bind = 0x408,       // 4 bytes: the room the file belongs to (ApRoom.bind_hash)
  kApSave_ItemCount = 0x410,  // 2 bytes: how many of the server's items were received
  kApSave_State = 0x412,      // kApState_* bits
  kApSave_Deaths = 0x413,     // game overs since the file was loaded (for DeathLink)
  kApSave_World = 0x414,      // ApWorld
  // "A Link to the Past" rooms tell these with the kSessEv_ApRoom events:
  kApSave_Medallions = 0x415, // the medallion that opens Misery Mire (low nibble) and
                              // Turtle Rock (high): 0 Bombos, 1 Ether, 2 Quake, 0xf any
  kApSave_CrystalsGt = 0x416, // dungeons beaten before Ganon's tower opens
  kApSave_CrystalsGanon = 0x417,  // ... before the pyramid's hole can be entered
  kApSave_Checked = 0x418,    // 40 bytes: a bit per location (synced as flags between worlds)
  kApSave_CheckedSize = 40,
  kApSave_Goal = 0x440,       // kApGoal_* (low nibble) | kApGoalFlag_* (high); 0xff: not told yet
  kApSave_PiecesNeeded = 0x441,  // 2 bytes: Triforce pieces for a Triforce hunt
  kApSave_Pieces = 0x443,     // Triforce pieces received
  kApSave_End = 0x444,
  kApMarker = 0x41,
  kApPlaceholderItem = 0x4C,  // the item a location hands out
  kApSpecialBit = 168,        // bit of special place 0 (after the chests)
};
enum {
  kApState_Goal = 1,           // the goal was reached
  kApState_DeathPending = 4,   // a DeathLink still has Links to take down
  kApState_GanonBeaten = 8,    // (A Link to the Past: goals that need more than Ganon)
  kApState_Triforce = 16,      // the Triforce item came (the pedestal goal)
};
// A Link to the Past's goals (its Goal option).
enum {
  kApGoal_Ganon = 0, kApGoal_Crystals = 1, kApGoal_Bosses = 2, kApGoal_Pedestal = 3,
  kApGoal_GanonPedestal = 4, kApGoal_TriforceHunt = 5, kApGoal_LocalTriforceHunt = 6,
  kApGoal_GanonTriforceHunt = 7, kApGoal_LocalGanonTriforceHunt = 8,
  kApGoalFlag_OpenPyramid = 0x10,  // the pyramid's hole is open from the start
};

// Netplay session events (session.h kSessEv_*): [type, a, b, c].
enum {
  kSessEv_ApItem = 3,   // [3, item code, index lo, index hi]
  kSessEv_ApDeath = 4,  // [4, 0, 0, 0]
  kSessEv_ApRoom = 5,   // [5, medallions, crystals for GT, crystals for Ganon]
  kSessEv_ApRoom2 = 6,  // [6, goal | flags, pieces needed lo, hi]
};

// The kinds of places outside chests (kAlttpSpecials[].kind).
enum ApLocKind {
  kApLocKind_None = 0,
  kApLocKind_Npc = 1,      // a hook of its own in the game
  kApLocKind_OwHeart = 2,  // a heart piece on overworld screen a
  kApLocKind_UwFlag = 3,   // a room's saved flag: room a, bit b
  kApLocKind_Boss = 4,     // the heart container of dungeon a
};

// --- The simulation (deterministic, runs on every computer) ---

// New files are Archipelago files of this room (0: not), of this world.
// Comes with every netplay frame from the host.
void Ap_SetNewGameBind(uint32 bind);
uint32 Ap_GetNewGameBind();
void Ap_SetNewGameWorld(ApWorld w);
// Called by the randomizer when a new file is made / was loaded.
void Ap_OnNewFile(uint8 *sram_slot);
void Ap_OnFileLoaded();
// The file in RAM is an Archipelago file. Its room, or 0.
bool Ap_IsApFile();
uint32 Ap_FileBind();
// ... of the "A Link to the Past" world.
bool Ap_IsAlttpFile();
// A chest or special place was used (randomizer.c and dungeon.c).
void Ap_OnChestOpened(int chest);
void Ap_OnSpecialPlace(int which);

// --- Places of the "A Link to the Past" world (its locations outside the
// chests the randomizer shuffles). Each hands out the placeholder instead
// of vanilla_item and is marked checked, in a file of that world; in any
// other file vanilla_item comes back untouched. ---
// A place with a hook of its own (kApLoc_* in ap_alttp_data.h).
uint8 Ap_PlaceItem(int loc, uint8 vanilla_item);
// A heart piece: outdoors on screen `where`, indoors in room `where` with
// the room's saved bit `mask` (0x200 or 0x400). -1: not a place.
int Ap_HeartPiecePlace(bool indoors, int where, int mask);
// A room's saved flag (a torch key, a generous guy, the chest game): -1 if not a place.
int Ap_RoomFlagPlace(int room, int mask);
// The heart container of a dungeon (the game's dungeon index): -1 if not a place.
int Ap_BossPlace(int dungeon);
// The fairy of the Pyramid (dark world) or of the Waterfall of Wishing:
// the next of her two places still to hand out, -1 when both are done.
int Ap_FairyPlace(bool dark_world);
// Pendants and crystals are placed where only the patched ROM would know:
// in a file of that world the gates they open count beaten dungeons
// instead (never stricter than that world's logic). Elsewhere: vanilla.
enum { kApGate_Sahasrahla, kApGate_PyramidFairy, kApGate_Pedestal, kApGate_GanonsTower, kApGate_Pyramid };
bool Ap_PrizeGate(int gate, bool vanilla);
// Does casting medallion (0 Bombos, 1 Ether, 2 Quake) open the entrance on
// overworld screen 0x70 (Misery Mire) or 0x47 (Turtle Rock)?
bool Ap_MedallionOpens(int medallion, int screen);
// Received items of that world: swords, shields, gloves, mails and
// boomerangs by name keep the best level Link has (item: the code to hand
// over, or the same).
uint8 Ap_AlttpBestLevel(uint8 item);
// The last Link fell (counted unless a bottled fairy will bring it back) /
// the Triforce was reached.
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
  int world;       // ApWorld to connect as
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
// The bind hash new files get (0 while not connected to a room we can play),
// and the world of that room.
uint32 Ap_HostBind();
ApWorld Ap_HostWorld();
// The host loaded a saved state (or reset the game) by itself.
void Ap_OnStateLoaded();
// One line about the connection for the menu.
void Ap_StatusLine(char *buf, size_t size);
// Lines to show on the screen, oldest first (kind: ApTextKind).
bool Ap_PollNotice(char *buf, size_t size, int *kind);

// Tests: a pretend room instead of a server (bind 0: off). What the game
// would send is recorded.
void Ap_TestRoom(uint32 bind, bool death_link, ApWorld world);
void Ap_TestGiveItem(int64 item);
// An option of the pretend "A Link to the Past" room (kApAlttp_*).
void Ap_TestSetOption(int which, int value);
void Ap_TestDeathLink();
typedef struct ApTestSent {
  int checks;    // locations sent (the latest list)
  int64 check[256];
  bool goal;
  int deaths;
} ApTestSent;
const ApTestSent *Ap_TestSent();

#endif  // ZELDA3_ARCHIPELAGO_H_
