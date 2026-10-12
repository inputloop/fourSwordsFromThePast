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

enum {
  kMpPingBit = 0x1000,     // the Ping button in a joypad word (the game never sees it)
  kMpPingFrames = 180,     // how long a ping marker shows
};

// A ping marker: "look here", or "help".
typedef struct MpPing {
  uint16 x, y;             // where (the middle of the ring), in the world's coordinates
  uint32 key;              // the area or room it was placed in (Mp_AreaKeyPublic)
  uint8 ttl;               // frames left to show, 0 = none
  uint8 kind;              // 0 look here, 1 help
} MpPing;

typedef struct MpState {
  uint8 leader;
  uint8 resident;
  uint8 friendly_fire;
  uint8 in_other_update;
  uint8 after_death;
  uint8 menu_leader;         // shared screen: who led (+1) before a teammate opened a menu, 0 = nobody
  uint8 switch_presser;      // the Link (+1) standing on the floor switch last found pressed, 0 = the resident
  uint8 pad[1];
  uint32 area_key;
  uint8 ancilla_owner[10];
  uint8 ancilla_seen[10];
  uint8 sprite_owner[16];
  uint8 sprite_seen_state[16];
  uint8 module_snapshot[2];
  uint8 exit_blocked_timer;  // > 0 shows the "everyone must head that way" hint
  uint8 regroup_grace;       // frames before regrouped Links can split off by leaving the view
  uint8 at_title;            // on the title / file select screens: the save file isn't loaded yet
  uint8 carried_by[kMpMaxPlayers];  // the teammate (+1) holding this Link over its head, 0 = none
  uint8 merge_grace;         // frames after a Link walked out of view before the worlds may merge again
  uint8 away[kMpMaxPlayers];     // in the item menu or a map, shown in a world of its own:
                                 // this Link stands still here meanwhile
  uint8 thrown[kMpMaxPlayers];   // in flight after a teammate threw it: flies over ledges
  uint16 ghost_cooldown[kMpMaxPlayers];  // frames before a downed player can send a heart again
  uint16 ghost_prev_input[kMpMaxPlayers];
  uint8 quad_valid;          // quad_offset is known for quad_room
  uint8 quad_offset;         // dungeon quadrant XOR the half of the room the leader stands in
  uint16 quad_room;
  uint8 group_exits;         // 1: rooms change only when everyone heads the same way
  uint8 independent;         // 1: players roam freely; splitting off asks for a new world instance
  int8 split_request;        // player that leaves this world instance this frame, or -1
  uint8 split_travel;        // 1: it went through an exit, 2: it opened a menu or map
                             // (the others replay the frame without it)
  uint16 inputs[kMpMaxPlayers];
  MpPing ping[kMpMaxPlayers];
  uint8 ping_hold[kMpMaxPlayers];      // frames the Ping button has been held
  uint8 ping_cooldown[kMpMaxPlayers];  // frames before the next ping
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
  // The leader's own Link sprites (drawn through the regular OAM), so other
  // worlds in the same place can show this world's leader too.
  MpRenderPlayer self;
  int self_player;  // whose sprites those are (the leader can change after they're drawn)
} MpRenderState;

extern MpState g_mp;
extern MpRenderState g_mp_render;
// Display only: shifts how another player's Link is drawn (netplay smooths
// out corrections of predicted movement with it).
extern int16 g_mp_draw_offset[kMpMaxPlayers][2];
// Display only: the players playing on this computer (bit per player); a
// screen shows the HUD item of the first of them in the world it shows.
extern uint8 g_mp_local_mask;
// The camera of a shared world follows the middle of its players (1) or the leader (0).
extern uint8 g_mp_group_camera;
// Player colors (display only): 0 standard, 1 color-blind friendly.
extern uint8 g_mp_color_scheme;
uint32 Mp_PlayerColorRgb(int p);  // 0xRRGGBB, for the overlay

void Mp_Reset();
// True when more than one player is in the game.
bool Mp_Active();
// A standing Link other than the one in RAM has less health than this.
bool Mp_OtherLinkBelow(uint8 limit);
// The HUD item box tiles for player p's selected item (false: no item).
bool Mp_ItemIconTiles(int p, uint16 out[4]);
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
uint8 Mp_PlayerMagic(int p);
bool Mp_IsAlive(int p);
// The Link whose context is resident is flying from a throw (tile detection
// then treats ledges as open ground, so it can land on top of one).
bool Mp_ResidentIsThrown();
// The glove level that counts when the resident Link lifts something: one
// more when a teammate next to it helps (holds A).
int Mp_GloveLevelForLift();
enum { kMpGhostHeartCooldown = 20 * 60 };

// Hooks called from the game code
void Mp_BeginFrame();
// Runs one frame of the game's module routing, applying the group exit rule.
void Mp_RunRouting(void (*routing)(void));
void Mp_EndFrame();
void Mp_RunOtherPlayers(void (*control)(void));
void Mp_DrawOtherPlayers();
// Called around drawing the leader's Link: remembers its sprites.
void Mp_BeginLeaderOam();
void Mp_EndLeaderOam();
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
// Remembers the hearts and magic player p leaves the game with (Mp_Join
// gives them back).
void Mp_RecordLeaving(int p);
// Gets a downed player back on its feet with that much health.
void Mp_Revive(int p, uint8 health);
// Floor switches: runs check (one of the game's "is Link on a switch"
// tests) for the resident Link and then every other standing one; true
// when some Link stands on a switch, which is remembered as the presser.
bool Mp_AnyLinkOnSwitch(bool (*check)(uint8 *out), uint8 *out);
// Moves the Link that pressed the switch by dy (the plate sinking under it).
void Mp_NudgeSwitchPresser(int dy);
// The teammate carrier holds over its head, or -1.
int Mp_CarriedBy(int carrier);
void Mp_SetLeader(int p);
uint32 Mp_AreaKeyPublic();
bool Mp_InStableGameplay();
// Set by the instance code: is anyone standing in another world instance?
extern bool (*g_mp_other_instance_alive)(void);
// The camera of another world in this area that has this world's leader in
// view (a lone player walking back to a group), or false.
extern bool (*g_mp_neighbour_camera)(uint16 *cx, uint16 *cy);
// Called when a regular save state was loaded (all players come back together).
extern void (*g_mp_state_loaded_hook)(void);
// True when more than one player is in the game in total (all instances).
extern bool g_mp_coop;

typedef struct Ppu Ppu;
void Mp_PrepareRender(Ppu *ppu);
void Mp_FinishRender(Ppu *ppu);

#endif  // ZELDA3_MP_H_
