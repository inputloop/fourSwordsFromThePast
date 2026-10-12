// World instances: independent roaming for co-op.
//
// The original game simulates one world around one camera. To let players go
// wherever they want, every group of players that is in a different place
// gets its own copy of the game (RAM, PPU and DMA state plus the co-op state
// in g_mp). Each frame, every instance is simulated in turn.
//
// - Split: a player who goes through a door/exit or walks out of the view of
//   their group continues in a new instance cloned from the group's world.
// - Merge: when players of two instances stand in the same room or screen,
//   within view of each other, the instances become one again.
// - Shared progress: items, rupees, keys and world/dungeon flags (the save
//   file area of RAM) are synced between instances after every frame.
//
// Everything here is deterministic, so netplay keeps working: the snapshot
// and the state hash cover every instance.
#ifndef ZELDA3_INSTANCE_H_
#define ZELDA3_INSTANCE_H_

#include "types.h"
#include "mp.h"

enum {
  kInstMax = 2 * kMpMaxPlayers,  // a world for every Link plus one for its menu
};

typedef struct ByteArray ByteArray;

// Call once after ZeldaInitialize: the current world becomes instance 0.
void Inst_Init();
bool Inst_IsActive(int i);
int Inst_Count();
int Inst_Current();
// Makes instance i the one the game code (g_ram, g_zenv, g_mp) works on.
void Inst_Switch(int i);
// The instance player p is in, or -1 if p isn't playing.
int Inst_OfPlayer(int p);      // where the player's Link is
int Inst_ViewOfPlayer(int p);  // what the player sees and controls (its item menu or map, if open)
bool Inst_PlayerInMenu(int p);
uint8 Inst_PlayerViewModule(int p);       // main_module_index of what p sees
uint8 Inst_PlayerViewSubmodule(int p);    // and its submodule_index
const uint8 *Inst_PlayerWorldRam(int p);  // the memory of the world p's Link is in (read only)
bool Inst_IsJoined(int p);
int Inst_NumJoined();

// Drop-in/out. A new player appears next to the lowest numbered player.
void Inst_Join(int p);
void Inst_Leave(int p);

// Session settings, applied to all instances.
void Inst_SetFriendlyFire(bool on);
bool Inst_GetFriendlyFire();
// true: players roam independently. false: one shared screen with group exits.
void Inst_SetIndependent(bool on);
bool Inst_GetIndependent();

// Simulates one frame of every instance. Returns the replay flag of ZeldaRunFrame.
bool Inst_RunFrame(const uint16 *inputs);
// The player whose instance is heard; the camera instance for local display.
void Inst_SetAudioPlayer(int p);

// Display only: Links of other worlds that are in the same room or overworld
// area as the current world, so players see each other before their worlds
// merge. Their sprites are in screen coordinates of their own world's camera;
// (dx, dy) moves them to the current world's camera.
typedef struct InstRemoteLink {
  int player;
  bool downed;
  const MpRenderPlayer *rp;
  const uint16 *tiles;  // Link tiles (0x200 words of VRAM from 0x4000)
  int dx, dy;
} InstRemoteLink;
int Inst_LinksFromOtherWorlds(InstRemoteLink *out, int max);

// Netplay snapshots and state hashing cover every instance.
void Inst_SaveState(ByteArray *arr);
bool Inst_LoadState(const uint8 *data, size_t size);
uint32 Inst_HashState();

// Fast in-memory copies of every world, for rolling back predicted frames.
typedef struct InstSnapshot InstSnapshot;
InstSnapshot *Inst_SnapshotNew();
void Inst_SnapshotFree(InstSnapshot *s);
void Inst_SnapshotSave(InstSnapshot *s);
void Inst_SnapshotLoad(const InstSnapshot *s);
// Test instrumentation: prints the sprite tables of every world in s.
void Inst_SnapshotPrintSprites(const InstSnapshot *s);
// false: frames don't send their sound to the audio thread (frames that are
// simulated again after a rollback were already heard).
void Inst_SetAudioOutput(bool on);

// Per player queries that work whatever instance the player is in.
uint8 Inst_PlayerHealth(int p);
uint8 Inst_PlayerMagic(int p);
// Where player p is: returns the world, or -1 if p isn't playing.
int Inst_PlayerPos(int p, uint16 *x, uint16 *y);
// Where player p is, for pointing at it from another player's screen. space
// says which coordinates the position is in: the light or dark world, a
// dungeon (its rooms share coordinates) or a single house or cave room.
// cam_x/cam_y is the camera of the player's world. False if p isn't playing
// or its world is in a transition.
typedef struct InstPlace {
  int world;
  uint16 x, y, cam_x, cam_y;
  uint32 space;
  bool downed;
} InstPlace;
bool Inst_PlayerPlace(int p, InstPlace *out);
// Player p's ping marker, while it shows and its world is still in the area
// it was placed in. space is as for Inst_PlayerPlace.
typedef struct InstPing {
  int world;
  uint16 x, y;
  uint32 space;
  uint8 ttl, kind;
} InstPing;
bool Inst_PlayerPing(int p, InstPing *out);
bool Inst_PlayerDowned(int p);
// The HUD item box tiles for player p's selected item (false: none), and
// which item that is (hud_cur_item).
bool Inst_PlayerItemIcon(int p, uint32 rgba[16 * 16]);  // 0: see-through
uint8 Inst_PlayerItem(int p);
// Who holds player p over their head (-1: nobody); whether p flies from a
// throw, and whether p holds someone.
int Inst_PlayerCarry(int p, bool *thrown, bool *carrying);
bool Inst_PlayerCanSendHeart(int p);  // downed, and A would send a teammate a heart
uint8 Inst_PlayerReviveCtr(int p);
// The player whose world p's screen shows: p, or while p is a ghost in a
// world that is paused (only ghosts there), a standing teammate.
int Inst_SpectateTarget(int p);
// Main module of the instance player p is in (0 if not playing).
uint8 Inst_PlayerModule(int p);
// Time left on the "everyone has to head that way" hint in p's instance.
uint8 Inst_ExitBlockedTimer(int p);

#endif  // ZELDA3_INSTANCE_H_
