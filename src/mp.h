// Four player co-op support.
//
// The original game has exactly one Link whose state lives at fixed RAM
// addresses. Co-op works by giving every player a private copy of the
// "Link context" (the RAM ranges listed in mp.c) and swapping the context
// of whichever player is acting into g_ram. Everything else (the world,
// enemies, inventory, progress) stays shared.
//
// - The leader is the player whose context is resident at frame
//   boundaries. The camera, room transitions and menus follow the leader.
//   Whenever a player's actions change the game module (opening the menu,
//   taking stairs, talking to someone) that player becomes the leader.
// - Enemies act against the nearest Link; damage checks loop over all Links.
// - Ancillae (arrows, bombs, boomerangs...) run in their owner's context.
//
// All of the state in MpState is part of the deterministic simulation and is
// included in netplay snapshots. Render-only data lives in MpRenderState.
#ifndef ZELDA3_MP_H_
#define ZELDA3_MP_H_

#include "types.h"
#include "snes/saveload.h"

enum {
  kMpMaxPlayers = 4,
  kMpCtxMaxBytes = 512,
  kMpReviveFrames = 90,
  kMpMaxExtraOam = 64,
  kMpMaxAncillaOam = 24,
};

typedef struct MpPlayer {
  uint8 joined;
  uint8 downed;
  uint8 revive_ctr;
  uint8 pad;
  uint8 ctx[kMpCtxMaxBytes];
} MpPlayer;

typedef struct MpState {
  uint8 leader;
  uint8 resident;
  uint8 friendly_fire;
  uint8 in_other_update;
  uint8 after_death;
  uint8 pad[3];
  uint32 area_key;
  uint8 ancilla_owner[10];
  uint8 ancilla_seen[10];
  uint8 sprite_owner[16];
  uint8 sprite_seen_state[16];
  uint8 module_snapshot[2];
  uint8 exit_blocked_timer;  // > 0 shows the "everyone must head that way" hint
  uint8 regroup_grace;       // frames before regrouped Links can split off by leaving the view
  uint8 group_exits;         // 1: rooms change only when everyone heads the same way
  uint8 independent;         // 1: players roam freely; splitting off asks for a new world instance
  int8 split_request;        // player that leaves this world instance this frame, or -1
  uint8 split_travel;        // 1: it went through an exit, 2: it opened a menu or map
                             // (the others replay the frame without it)
  uint16 inputs[kMpMaxPlayers];
  MpPlayer p[kMpMaxPlayers];
} MpState;

typedef struct MpRenderPlayer {
  uint8 count;
  uint8 visible;
  uint16 oam[kMpMaxExtraOam * 2];
  uint8 high[kMpMaxExtraOam];
  // Sprites of this player's projectiles (hookshot, ...) that use Link's own
  // tiles, so they are drawn with this player's tiles instead of the leader's.
  uint8 anc_count;
  uint16 anc_oam[kMpMaxAncillaOam * 2];
  uint8 anc_high[kMpMaxAncillaOam];
  uint16 tiles[0x200];
} MpRenderPlayer;

typedef struct MpRenderState {
  MpRenderPlayer p[kMpMaxPlayers];
} MpRenderState;

extern MpState g_mp;
extern MpRenderState g_mp_render;
// Display only: shifts how another player's Link is drawn (netplay smooths
// out corrections of predicted movement with it).
extern int16 g_mp_draw_offset[kMpMaxPlayers][2];
// Display only: the players playing on this computer (bit per player); a
// screen shows the HUD item of the first of them in the world it shows.
extern uint8 g_mp_local_mask;

void Mp_Reset();
// True when more than one player is in the game.
bool Mp_Active();
int Mp_NumJoined();
void Mp_Join(int p);
void Mp_Leave(int p);
void Mp_SetInputs(const uint16 *inputs);
void Mp_OnStateLoaded();
void Mp_SaveLoad(SaveLoadFunc *func, void *ctx);

// Context access
void Mp_SwapTo(int p);
void Mp_SwapToLeader();
uint8 *Mp_PlayerRam(int p, uint32 addr);
uint16 Mp_PlayerX(int p);
uint16 Mp_PlayerY(int p);
uint8 Mp_PlayerHealth(int p);
bool Mp_IsAlive(int p);

// Hooks called from the game code
void Mp_BeginFrame();
// Runs one frame of the game's module routing, applying the group exit rule.
void Mp_RunRouting(void (*routing)(void));
void Mp_EndFrame();
void Mp_RunOtherPlayers(void (*control)(void));
void Mp_DrawOtherPlayers();
void Mp_ReadJoypads();
bool Mp_OnFatalDamage();
void Mp_BeginObject();
void Mp_EndObject();
void Mp_PreSprite(int k);
void Mp_PreAncilla(int k);
void Mp_PostAncilla(int k);
bool Mp_SuppressCamera();
// Runs the camera scroll of the gameplay modules. A player who just got their
// own world instance may stand away from the center; the camera catches up.
void Mp_CameraCatchUp(void (*scroll)(void));
typedef bool MpSpriteCheckFunc(int k);
bool Mp_AnyPlayer(int k, MpSpriteCheckFunc *func);
typedef uint8 MpSpriteCheckFuncU8(int k);
uint8 Mp_AnyPlayerU8(int k, MpSpriteCheckFuncU8 *func);
void Mp_ClearPerFrameSpriteFlags();
void Mp_FriendlyFire();
void Mp_ForEachAlivePlayer(int k, void (*func)(int k));

// World instance support (see instance.c)
int Mp_CtxSize();
void Mp_ExportPlayer(int p, uint8 *ctx_out);
void Mp_ImportPlayer(int p, const uint8 *ctx, uint8 downed, uint8 revive_ctr);
void Mp_RemovePlayer(int p);
void Mp_SetLeader(int p);
uint32 Mp_AreaKeyPublic();
bool Mp_InStableGameplay();
// Set by the instance code: is anyone standing in another world instance?
extern bool (*g_mp_other_instance_alive)(void);
// Called when a regular save state was loaded (all players come back together).
extern void (*g_mp_state_loaded_hook)(void);
// True when more than one player is in the game in total (all instances).
extern bool g_mp_coop;

typedef struct Ppu Ppu;
void Mp_PrepareRender(Ppu *ppu);
void Mp_FinishRender(Ppu *ppu);

#endif  // ZELDA3_MP_H_
