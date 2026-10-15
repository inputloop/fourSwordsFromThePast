#include "mp.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "player.h"
#include "player_oam.h"
#include "sprite.h"
#include "ancilla.h"
#include "misc.h"
#include "hud.h"
#include "dungeon.h"
#include "overworld.h"
#include "instance.h"
#include "features.h"
#include "assets.h"
#include "tagalong.h"
#include "load_gfx.h"

MpState g_mp;
MpRenderState g_mp_render;
int16 g_mp_draw_offset[kMpMaxPlayers][2];
bool (*g_mp_other_instance_alive)(void);
bool (*g_mp_neighbour_camera)(uint16 *cx, uint16 *cy);
void (*g_mp_state_loaded_hook)(void);
bool g_mp_coop;

// RAM ranges that make up one Link. Everything not listed here is shared.
typedef struct MpRange {
  uint32 addr;
  uint16 len;
} MpRange;

static const MpRange kMpCtxRanges[] = {
  {0x20, 0x52},    // coords, velocities, direction, sword/button state, tile detection
  {0x76, 4},       // interacting tile, spin attack counter
  {0xEE, 1},       // link_is_on_lower_level
  {0xF0, 0x0B},    // joypad state
  {0x100, 0x0A},   // Link graphics dma indexes
  {0x202, 1},      // hud_cur_item (selected Y item)
  {0x2C0, 0x0F},   // staircase/pushing/recoil/swimming
  {0x2D8, 0x1A},   // item receipt, bunny, sword delay, misc tile detection
  {0x2F3, 6},
  {0x2FA, 1},
  {0x300, 0x10},   // handler timer, item in hand, current_item_y, state bits
  {0x314, 0x3E},   // pickup, spin attack, blink, swimming, palette bits...
  {0x354, 0x2C},   // oam value, grass timers, damage, dash, hookshot...
  {0x399, 5},      // boomerang temp coords, hookshot effect
  {0x3E9, 1},
  {0x3EF, 1},
  {0x3F3, 1},
  {0x3F5, 7},      // temp bunny timer .. bit9_of_xcoord
  {0x476, 1},      // link_is_on_lower_level_mirror
  {0x47A, 1},      // about_to_jump_off_ledge
  {0x4CA, 1},      // low health beep
  {0x656, 3},      // hud_cur_item_x/l/r (item switching enhancement)
  {0xB7B, 5},      // link_prevent_from_moving, drag_player
  {0xFC1, 5},      // flag_unk1, prev coords
  {0xF34F, 1},     // selected bottle
  {0xF36D, 2},     // link_health_current, link_magic_power
  {0xF372, 2},     // link_hearts_filler, link_magic_filler
};

// Fields of the context that stay personal when a player is moved to the leader.
static const MpRange kMpPersonalRanges[] = {
  {0xF0, 0x0B},
  {0x202, 1},
  {0x656, 3},
  {0x303, 1},
  {0x307, 1},
  {0xF34F, 1},
  {0xF36D, 2},
  {0xF372, 2},
};

static bool Mp_IsTravel(uint8 m0, uint8 m1, uint8 s1);
static bool Mp_LeavesRoom(uint8 m0, uint8 m1, uint8 s1);

static int Mp_CtxOffset(uint32 addr) {
  int off = 0;
  for (size_t i = 0; i < countof(kMpCtxRanges); i++) {
    if (addr >= kMpCtxRanges[i].addr && addr < kMpCtxRanges[i].addr + kMpCtxRanges[i].len)
      return off + (addr - kMpCtxRanges[i].addr);
    off += kMpCtxRanges[i].len;
  }
  return -1;
}

static void Mp_SaveCtx(uint8 *dst) {
  for (size_t i = 0; i < countof(kMpCtxRanges); i++) {
    memcpy(dst, g_ram + kMpCtxRanges[i].addr, kMpCtxRanges[i].len);
    dst += kMpCtxRanges[i].len;
  }
}

static void Mp_LoadCtx(const uint8 *src) {
  for (size_t i = 0; i < countof(kMpCtxRanges); i++) {
    memcpy(g_ram + kMpCtxRanges[i].addr, src, kMpCtxRanges[i].len);
    src += kMpCtxRanges[i].len;
  }
}

void Mp_Reset() {
  memset(&g_mp, 0, sizeof(g_mp));
  memset(&g_mp_render, 0, sizeof(g_mp_render));
  g_mp.p[0].joined = 1;
  g_mp.friendly_fire = 0;
  g_mp.group_exits = 1;
  g_mp.split_request = -1;
  memset(g_mp.ancilla_owner, 0, sizeof(g_mp.ancilla_owner));
}

int Mp_NumJoined() {
  int n = 0;
  for (int i = 0; i < kMpMaxPlayers; i++)
    n += g_mp.p[i].joined;
  return n;
}

bool Mp_Active() {
  return Mp_NumJoined() > 1;
}

uint8 *Mp_PlayerRam(int p, uint32 addr) {
  if (p == g_mp.resident)
    return &g_ram[addr];
  int off = Mp_CtxOffset(addr);
  assert(off >= 0);
  return &g_mp.p[p].ctx[off];
}

uint16 Mp_PlayerX(int p) { return WORD(*Mp_PlayerRam(p, 0x22)); }
uint16 Mp_PlayerY(int p) { return WORD(*Mp_PlayerRam(p, 0x20)); }
uint8 Mp_PlayerHealth(int p) { return *Mp_PlayerRam(p, 0xF36D); }
uint8 Mp_PlayerMagic(int p) { return *Mp_PlayerRam(p, 0xF36E); }

bool Mp_IsAlive(int p) {
  return g_mp.p[p].joined && !g_mp.p[p].downed;
}

bool Mp_MovesFreely(uint8 handler_state) {
  return handler_state == kPlayerState_Ground || handler_state == kPlayerState_Swimming ||
         handler_state == kPlayerState_PermaBunny || handler_state == kPlayerState_TempBunny;
}

// Would the game take this Link on to the next area at the area's edge, if
// it led? Walking, swimming, as a bunny and dashing (17 is the dash itself):
// not while hopping down a ledge, knocked back or flying from a throw.
static bool Mp_MovesOnAtEdge(uint8 handler_state) {
  return Mp_MovesFreely(handler_state) || handler_state == kPlayerState_StartDash;
}

// Assigns ownership of newly spawned ancillae and newly lifted sprites to
// the player whose context is currently resident.
void Mp_AncillaSlotTaken(int k) {
  if (k >= 0 && k < 10)
    g_mp.ancilla_seen[k] = 0;  // (so the next look sees a new object)
}

static void Mp_AttributeObjects() {
  for (int i = 0; i < 10; i++) {
    if (ancilla_type[i] != g_mp.ancilla_seen[i]) {
      if (ancilla_type[i] && g_mp.ancilla_seen[i] == 0)
        g_mp.ancilla_owner[i] = g_mp.resident;
      g_mp.ancilla_seen[i] = ancilla_type[i];
    }
  }
  for (int k = 0; k < 16; k++) {
    if (sprite_state[k] != g_mp.sprite_seen_state[k]) {
      if (sprite_state[k] == 10)
        g_mp.sprite_owner[k] = g_mp.resident;
      g_mp.sprite_seen_state[k] = sprite_state[k];
    }
  }
}

void Mp_SwapTo(int p) {
  if (p == g_mp.resident)
    return;
  Mp_AttributeObjects();
  Mp_SaveCtx(g_mp.p[g_mp.resident].ctx);
  Mp_LoadCtx(g_mp.p[p].ctx);
  g_mp.resident = p;
}

void Mp_SwapToLeader() {
  Mp_SwapTo(g_mp.leader);
}

// Copies the leader's Link onto player p, keeping p's personal fields.
static void Mp_RegroupPlayer(int p) {
  if (p == g_mp.leader)
    return;
  int r = g_mp.resident;
  Mp_SwapTo(g_mp.leader);
  uint8 personal[64], *pp = personal;
  for (size_t i = 0; i < countof(kMpPersonalRanges); i++)
    for (int j = 0; j < kMpPersonalRanges[i].len; j++)
      *pp++ = *Mp_PlayerRam(p, kMpPersonalRanges[i].addr + j);
  Mp_SaveCtx(g_mp.p[p].ctx);
  pp = personal;
  for (size_t i = 0; i < countof(kMpPersonalRanges); i++)
    for (int j = 0; j < kMpPersonalRanges[i].len; j++)
      *Mp_PlayerRam(p, kMpPersonalRanges[i].addr + j) = *pp++;
  // Don't inherit whatever the leader was holding or doing: nor being
  // hidden and held still (the bird is still setting the leader down after
  // a flute flight, and only lets go of the leader), nor a knockback.
  static const uint16 kClear[] = {
    0x301, 0x308, 0x309, 0x37A, 0x314, 0x2F4, 0x376, 0x373, 0x3C, 0x3A, 0x5D,
    0x4B,                // link_visibility_status
    0x2E4,               // flag_is_link_immobilized
    0x37B,               // link_disable_sprite_damage
    0x2DA,               // link_pose_for_item
    0x4D, 0x46, 0x2C6,   // link_auxiliary_state, link_incapacitated_timer, link_recoilmode_timer
    0x27, 0x28, 0x29,    // link_actual_vel_y/x/z
    0x24, 0x25,          // link_z_coord
    0x3F5, 0x3F6, 0x3F7, 0x2E2,  // a bunny spell's timer, a transformation under way
  };
  for (size_t i = 0; i < countof(kClear); i++)
    *Mp_PlayerRam(p, kClear[i]) = 0;
  *Mp_PlayerRam(p, 0x31F) = 0x20;  // countdown_for_blink
  // It swims in the leader's water, and is a bunny like the leader in the
  // Dark World without the Moon Pearl; a bunny spell that hit the leader is
  // the leader's own. (The game starts swimming only on entering deep water,
  // and keeps a bunny one only in the bunny's own state.)
  uint8 st = link_player_handler_state;  // (the leader's)
  if (st == kPlayerState_Swimming) {
    *Mp_PlayerRam(p, 0x5D) = st;
    // Its own strokes, from rest (as Link_ResetSwimmingState): the countdown,
    // hard stroke and faster flags, and the accelerations (swimcoll_var3, 5,
    // 1, 9 and 7, two words each).
    static const uint16 kSwimFresh[] = { 0x2CB, 0x34F, 0x32A };
    static const uint16 kSwimAccel[] = { 0x326, 0x32B, 0x32F, 0x334, 0x33C };
    for (size_t i = 0; i < countof(kSwimFresh); i++)
      *Mp_PlayerRam(p, kSwimFresh[i]) = 0;
    for (size_t i = 0; i < countof(kSwimAccel); i++)
      for (int j = 0; j < 4; j++)
        *Mp_PlayerRam(p, kSwimAccel[i] + j) = 0;
  } else if (st == kPlayerState_PermaBunny) {
    *Mp_PlayerRam(p, 0x5D) = st;
  } else {
    *Mp_PlayerRam(p, 0x345) = 0;  // link_is_in_deep_water
    *Mp_PlayerRam(p, 0x56) = 0;   // link_is_bunny
    *Mp_PlayerRam(p, 0x2E0) = 0;  // link_is_bunny_mirror
  }
  Mp_SwapTo(r);
}

// Save-area bytes (shared, saved, synced between worlds) that remember the
// hearts and magic a player left the game with, so it comes back with them.
enum { kRam_LeftHealth = 0xF4E8, kRam_LeftMagic = 0xF4EC };

void Mp_RecordLeaving(int p) {
  if (p < 0 || p >= kMpMaxPlayers || !g_mp.p[p].joined)
    return;
  uint8 h = Mp_PlayerHealth(p);
  g_ram[kRam_LeftHealth + p] = g_mp.p[p].downed || !h ? 0x18 : h;  // a ghost gets three hearts
  g_ram[kRam_LeftMagic + p] = Mp_PlayerMagic(p);
}

bool Mp_AnyLinkOnSwitch(bool (*check)(uint8 *out), uint8 *out) {
  bool hit = check(out);
  // (Who pressed it: the plate's sinking moves that Link, also if the lead
  // changes meanwhile. 0: a game for one.)
  g_mp.switch_presser = hit && Mp_Active() ? g_mp.resident + 1 : 0;
  if (hit || !Mp_Active())
    return hit;
  // Any Link's weight counts: one can hold a plate down while the others
  // go through the door it opens. (Not a ghost, nor a Link held over a
  // teammate's head or flying from a throw.)
  int r = g_mp.resident;
  for (int p = 0; p < kMpMaxPlayers && !hit; p++) {
    if (p == r || !Mp_IsAlive(p) || g_mp.carried_by[p] || g_mp.thrown[p])
      continue;
    Mp_SwapTo(p);
    if ((hit = check(out)))
      g_mp.switch_presser = p + 1;
  }
  Mp_SwapTo(r);
  return hit;
}

void Mp_NudgeSwitchPresser(int dy) {
  int p = g_mp.switch_presser - 1;
  if (p < 0)
    link_y_coord += dy;
  else if (g_mp.p[p].joined)  // (gone meanwhile: nobody to move)
    WORD(*Mp_PlayerRam(p, 0x20)) += dy;  // link_y_coord
}

// Forgets what the co-op layer knows about a player who leaves this world
// (or has just arrived): nothing carried, thrown, away or cooling down, and
// whoever it was holding is set down where it is.
// Hands what player p was doing in this world over to the leader, as p
// leaves it. Not the bird p called with the flute, though: it goes too (here
// it would pick up whoever leads now). (One setting p down after a flight
// still lands, as it lets go of its Link and brings the follower along.)
static void Mp_HandOverObjects(int p) {
  for (int i = 0; i < 10; i++) {
    if (g_mp.ancilla_owner[i] != p)
      continue;
    if (ancilla_type[i] == 0x27 && !ancilla_L[i]) {  // the bird, on its way to p or off with it
      if (ancilla_step[i])
        tagalong_var5 = 0;  // (it had picked p up and hidden the follower)
      ancilla_type[i] = 0;
    } else if (ancilla_type[i] == 0x27 && ancilla_step[i]) {
      // Setting p down: it lands without anyone and flies off, and the
      // follower comes along to the leader as it would to p. (Not the rest of
      // its letting go: the leader wasn't hidden or held.)
      ancilla_step[i] = 0;
      tagalong_var5 = 0;
      g_mp.ancilla_owner[i] = g_mp.leader;
      if (!((follower_indicator == 12 || follower_indicator == 13) && follower_dropped)) {
        uint8 speed = link_speed_setting, state = link_player_handler_state, running = link_is_running;
        Follower_Initialize();
        link_speed_setting = speed, link_player_handler_state = state, link_is_running = running;
      }
    } else {
      g_mp.ancilla_owner[i] = g_mp.leader;
    }
  }
  for (int i = 0; i < 16; i++)
    if (g_mp.sprite_owner[i] == p)
      g_mp.sprite_owner[i] = g_mp.leader;
}

// Player p (a bunny?) is leaving this world: if no bunny is left, the
// world's Link colors go back to the gear's (a bunny spell puts the bunny's
// into the world's palette, and only puts them back where it ends).
static void Mp_BunnyColorsLeave(bool was_bunny) {
  if (!was_bunny)
    return;
  for (int q = 0; q < kMpMaxPlayers; q++)
    if (g_mp.p[q].joined && *Mp_PlayerRam(q, 0x56))  // link_is_bunny
      return;
  LoadActualGearPalettes();
}

static void Mp_ClearCoopState(int p) {
  g_mp.carried_by[p] = 0;
  g_mp.thrown[p] = 0;
  g_mp.away[p] = 0;
  g_mp.ghost_cooldown[p] = 0;
  for (int q = 0; q < kMpMaxPlayers; q++) {
    if (g_mp.carried_by[q] == p + 1) {
      g_mp.carried_by[q] = 0;
      if (g_mp.p[q].joined)
        WORD(*Mp_PlayerRam(q, 0x24)) = 0;  // the carrier left: set down where it is
    }
  }
}

void Mp_Join(int p) {
  if (p < 0 || p >= kMpMaxPlayers || g_mp.p[p].joined)
    return;
  int r = g_mp.resident;
  Mp_SwapTo(g_mp.leader);
  memset(g_mp.p[p].ctx, 0, sizeof(g_mp.p[p].ctx));
  g_mp.p[p].joined = 1;
  g_mp.p[p].downed = 0;
  g_mp.p[p].revive_ctr = 0;
  Mp_ClearCoopState(p);
  Mp_RegroupPlayer(p);
  // Full hearts for a newcomer; what it left with for one coming back.
  uint8 cap = link_health_capacity ? link_health_capacity : 0x18;
  uint8 left = g_ram[kRam_LeftHealth + p];
  *Mp_PlayerRam(p, 0xF36D) = left && left <= cap ? left : cap;
  *Mp_PlayerRam(p, 0xF36E) = left ? g_ram[kRam_LeftMagic + p] : link_magic_power;
  g_ram[kRam_LeftHealth + p] = 0;
  *Mp_PlayerRam(p, 0x202) = hud_cur_item;
  *Mp_PlayerRam(p, 0x303) = current_item_y;
  *Mp_PlayerRam(p, 0xF34F) = link_item_bottle_index;  // (0 would be no bottle at all)
  Mp_SwapTo(r);
}

static int Mp_FindNewLeader(int except) {
  // Someone standing and playing first (not in a menu: the camera would park
  // on a Link that can't move), then anyone standing, then anyone at all.
  for (int i = 0; i < kMpMaxPlayers; i++)
    if (i != except && Mp_IsAlive(i) && !g_mp.away[i])
      return i;
  for (int i = 0; i < kMpMaxPlayers; i++)
    if (i != except && Mp_IsAlive(i))
      return i;
  for (int i = 0; i < kMpMaxPlayers; i++)
    if (i != except && g_mp.p[i].joined)
      return i;
  return -1;
}

void Mp_Leave(int p) {
  if (p < 0 || p >= kMpMaxPlayers || !g_mp.p[p].joined || Mp_NumJoined() <= 1)
    return;
  Mp_ClearCoopState(p);
  if (p == g_mp.leader) {
    int nl = Mp_FindNewLeader(p);
    Mp_SwapTo(nl);
    g_mp.leader = nl;
  } else if (p == g_mp.resident) {
    Mp_SwapTo(g_mp.leader);
  }
  bool was_bunny = *Mp_PlayerRam(p, 0x56) != 0;  // link_is_bunny
  g_mp.p[p].joined = 0;
  Mp_HandOverObjects(p);
  Mp_BunnyColorsLeave(was_bunny);
  g_mp_render.p[p].count = 0;
}

// After loading a regular save state, RAM holds a single Link. Treat it as the
// leader and pull everybody else over to it.
void Mp_OnStateLoaded() {
  if (g_mp_state_loaded_hook)
    g_mp_state_loaded_hook();
  g_mp.resident = g_mp.leader;
  g_mp.area_key = 0xffffffff;
  // (What the frame before the load was in the middle of is over: a split
  // off, the quadrant a room was entered in, the waits.)
  g_mp.split_request = -1;
  g_mp.split_travel = 0;
  g_mp.quad_valid = 0;
  g_mp.quad_room = 0;
  g_mp.merge_grace = g_mp.regroup_grace = g_mp.exit_blocked_timer = 0;
  memcpy(g_mp.ancilla_seen, ancilla_type, 10);
  memcpy(g_mp.sprite_seen_state, sprite_state, 16);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    // Nobody is held or in the air any more: everybody starts over at the
    // leader. (A carry otherwise lasts through module changes.)
    bool held = g_mp.carried_by[p] || g_mp.thrown[p];
    g_mp.carried_by[p] = 0;
    g_mp.thrown[p] = 0;
    if (g_mp.p[p].joined && p != g_mp.leader) {
      if (!*Mp_PlayerRam(p, 0xF36D))
        *Mp_PlayerRam(p, 0xF36D) = link_health_current;
      g_mp.p[p].downed = 0;
      if (held)
        WORD(*Mp_PlayerRam(p, 0x24)) = 0;
    }
  }
}

void Mp_SetInputs(const uint16 *inputs) {
  memcpy(g_mp.inputs, inputs, sizeof(g_mp.inputs));
}

void Mp_SaveLoad(SaveLoadFunc *func, void *ctx) {
  func(ctx, &g_mp, sizeof(g_mp));
}

static void Mp_ReadJoypadInto(uint16 input, uint8 *last_h, uint8 *last_l, uint8 *filt_h, uint8 *filt_l, uint8 *last2_h, uint8 *last2_l) {
  if ((input & 0x30) == 0x30) input ^= 0x30;
  if ((input & 0xc0) == 0xc0) input ^= 0xc0;
  uint16 both = input, reversed = 0;
  for (int i = 0; i < 16; i++, both >>= 1)
    reversed = reversed * 2 + (both & 1);
  uint8 r0 = reversed, r1 = reversed >> 8;
  *last_l = r0;
  *filt_l = (r0 ^ *last2_l) & r0;
  *last2_l = r0;
  *last_h = r1;
  *filt_h = (r1 ^ *last2_h) & r1;
  *last2_h = r1;
}

static void Mp_HandleCarry();
static void Mp_GhostHelp();

void Mp_ReadJoypads() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    // (The Ping button is the mod's: the game never sees it.)
    Mp_ReadJoypadInto(g_mp.p[p].downed || g_mp.away[p] ? 0 : g_mp.inputs[p] & 0x0fff,
                      Mp_PlayerRam(p, 0xF0), Mp_PlayerRam(p, 0xF2), Mp_PlayerRam(p, 0xF4),
                      Mp_PlayerRam(p, 0xF6), Mp_PlayerRam(p, 0xF8), Mp_PlayerRam(p, 0xFA));
  }
  Mp_HandleCarry();
  Mp_GhostHelp();
}

// A downed player isn't only waiting: A sends a small heart to the nearest
// living teammate in the same world (it drops from above next to them), once
// every 20 seconds.
static void Mp_GhostHelp() {
  bool stable = Mp_Active() && submodule_index == 0 && (main_module_index == 7 || main_module_index == 9);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    uint16 in = g_mp.p[p].joined && g_mp.p[p].downed ? g_mp.inputs[p] : 0;
    bool a = (in & ~g_mp.ghost_prev_input[p]) & 0x100;
    g_mp.ghost_prev_input[p] = in;
    if (g_mp.ghost_cooldown[p])
      g_mp.ghost_cooldown[p]--;
    if (!a || !stable || g_mp.ghost_cooldown[p])
      continue;
    int best = -1, best_d = 0;
    for (int q = 0; q < kMpMaxPlayers; q++) {
      if (!Mp_IsAlive(q))
        continue;
      int d = abs((int16)(Mp_PlayerX(q) - Mp_PlayerX(p))) + abs((int16)(Mp_PlayerY(q) - Mp_PlayerY(p)));
      if (best < 0 || d < best_d)
        best = q, best_d = d;
    }
    int k = -1;
    for (int j = 15; j >= 0 && k < 0; j--)
      if (sprite_state[j] == 0)
        k = j;
    if (best < 0 || k < 0)
      continue;
    // Like an enemy's prize drop, without the enemy.
    sprite_type[k] = 0xd8;  // small heart
    sprite_state[k] = 9;
    SpritePrep_LoadProperties(k);
    Sprite_SetX(k, Mp_PlayerX(best));
    Sprite_SetY(k, Mp_PlayerY(best) + 8);
    sprite_floor[k] = *Mp_PlayerRam(best, 0xEE);
    sprite_z[k] = 24;
    sprite_z_vel[k] = 0;
    sprite_ignore_projectile[k]++;
    sprite_delay_aux4[k] = 21;
    sprite_stunned[k] = 255;
    g_mp.ghost_cooldown[p] = kMpGhostHeartCooldown;
  }
}

// ---------------------------------------------------------------------------
// Lifting and throwing teammates (like Four Swords): A next to a teammate
// you face lifts it over your head, A again throws it the way you face. The
// Link being carried can wriggle free with B.

enum {
  kMpCarryHeight = 14,  // how high a carried Link is held
  kMpThrowSpeed = 40,   // horizontal speed of a throw (like a knockback, only stronger)
  kMpThrowZSpeed = 24,
  kMpThrowTime = 30,
};

// Can p do something with its hands right now?
static bool Mp_IsFreeToCarry(int p) {
  return Mp_IsAlive(p) && !g_mp.carried_by[p] &&
         *Mp_PlayerRam(p, 0x5D) == 0 &&             // link_player_handler_state: on the ground
         *Mp_PlayerRam(p, 0x4D) == 0 &&             // link_auxiliary_state: not hurt
         (uint16)(WORD(*Mp_PlayerRam(p, 0x24)) + 1) <= 1 &&  // link_z_coord: 0 or 0xffff on the ground
         *Mp_PlayerRam(p, 0x308) == 0 &&            // link_state_bits: not carrying anything
         *Mp_PlayerRam(p, 0x301) == 0 &&            // link_item_in_hand
         *Mp_PlayerRam(p, 0x345) == 0;              // not swimming
}

int Mp_CarriedBy(int carrier) {
  for (int q = 0; q < kMpMaxPlayers; q++)
    if (g_mp.carried_by[q] == carrier + 1)
      return q;
  return -1;
}

// The teammate right in front of p, if any.
static int Mp_TeammateInFront(int p) {
  uint16 x = Mp_PlayerX(p), y = Mp_PlayerY(p);
  uint8 dir = *Mp_PlayerRam(p, 0x2F);  // link_direction_facing: 0 up, 2 down, 4 left, 6 right
  for (int q = 0; q < kMpMaxPlayers; q++) {
    if (q == p || !Mp_IsFreeToCarry(q) || Mp_CarriedBy(q) >= 0)
      continue;
    int dx = (int16)(Mp_PlayerX(q) - x), dy = (int16)(Mp_PlayerY(q) - y);
    int along = dir == 0 ? -dy : dir == 2 ? dy : dir == 4 ? -dx : dx;
    int across = dir < 4 ? dx : dy;
    if (along > 0 && along <= 20 && abs(across) <= 10 && *Mp_PlayerRam(q, 0xEE) == *Mp_PlayerRam(p, 0xEE))
      return q;
  }
  return -1;
}

static void Mp_Release(int q, bool throw_it) {
  int c = g_mp.carried_by[q] - 1;
  g_mp.carried_by[q] = 0;
  uint8 dir = c >= 0 ? *Mp_PlayerRam(c, 0x2F) : 2;
  int r = g_mp.resident;
  Mp_SwapTo(q);
  // Off it goes, in the arc of a knockback (which also handles walls, pits
  // and ledges), or with a little hop when it wriggles free.
  int speed = throw_it ? kMpThrowSpeed : 8;
  link_actual_vel_x = dir == 4 ? -speed : dir == 6 ? speed : 0;
  link_actual_vel_y = dir == 0 ? -speed : dir == 2 ? speed : 0;
  link_actual_vel_z = throw_it ? kMpThrowZSpeed : 12;
  link_incapacitated_timer = throw_it ? kMpThrowTime : 12;
  link_auxiliary_state = 1;
  link_give_damage = 0;
  link_direction_facing = dir;
  // A Link held overhead isn't standing in a doorway, whatever the flag kept
  // from before it was lifted says (in a doorway, knockback only goes
  // along the door, so a sideways throw went nowhere).
  is_standing_in_doorway = 0;
  Mp_SwapTo(r);
  g_mp.thrown[q] = throw_it;
}

// Heavy rocks: two Links together are one glove stronger. A teammate next to
// the Link that tries to lift (within 24 pixels), free and holding A, helps.
int Mp_GloveLevelForLift() {
  int gloves = link_item_gloves;
  if (!g_mp_coop || !Mp_Active() || gloves >= 2)
    return gloves;
  int me = g_mp.resident;
  for (int q = 0; q < kMpMaxPlayers; q++) {
    if (q == me || !Mp_IsAlive(q) || g_mp.carried_by[q] || Mp_CarriedBy(q) >= 0 || !(g_mp.inputs[q] & 0x100) ||
        *Mp_PlayerRam(q, 0x4D) != 0 || (uint16)(WORD(*Mp_PlayerRam(q, 0x24)) + 1) > 1)
      continue;
    int dx = (int16)(Mp_PlayerX(q) - link_x_coord), dy = (int16)(Mp_PlayerY(q) - link_y_coord);
    if (abs(dx) <= 24 && abs(dy) <= 24)
      return gloves + 1;
  }
  return gloves;
}

bool Mp_ResidentIsThrown() {
  return g_mp_coop && g_mp.thrown[g_mp.resident] && link_auxiliary_state == 1;
}

static void Mp_PlaceCarried();

// Modules a carried Link stays on its carrier's head through: plain
// gameplay and the trips it makes (doors, stairs, exits and entrances, edge
// scrolling, falling through a hole, the mirror) and text boxes and menus.
static bool Mp_CarryGoesAlong() {
  uint8 m = main_module_index;
  return m == 6 || m == 7 || m == 8 || m == 9 || m == 11 || m == 14 || m == 15 || m == 16 || m == 17 || m == 21;
}

static void Mp_HandleCarry() {
  bool stable = Mp_Active() && submodule_index == 0 && (main_module_index == 7 || main_module_index == 9);
  for (int q = 0; q < kMpMaxPlayers; q++)
    if (g_mp.thrown[q] && (!g_mp.p[q].joined || *Mp_PlayerRam(q, 0x4D) != 1))
      g_mp.thrown[q] = 0;  // landed
  bool any = false;
  for (int q = 0; q < kMpMaxPlayers; q++) {
    int c = g_mp.carried_by[q] - 1;
    if (c < 0)
      continue;
    // Let go when either one is gone, down or hurt, or the game does
    // something else than gameplay and its trips (dying, the ending...).
    // Through a door, an exit or stairs the carried Link comes along on its
    // carrier's head: set down at the start of the trip, it was left in the
    // doorway, right on the way back out.
    // (Hopping down a ledge, 11-16, or through the mirror, 20, the carrier
    // takes the carried Link along too.)
    uint8 cs = *Mp_PlayerRam(c, 0x5D);  // link_player_handler_state
    bool hopping = cs >= 11 && cs <= 16;
    bool carrier_busy = cs != 0 && !hopping && cs != 20;
    bool carrier_hit = *Mp_PlayerRam(c, 0x4D) != 0 && !hopping;  // link_auxiliary_state (2 while hopping)
    // (Nothing is let go in mid-air while the carrier hops down a ledge:
    // the carried Link would start from the cliff face. It waits for the
    // landing.)
    if (!Mp_Active() || !Mp_CarryGoesAlong() || !g_mp.p[q].joined || !Mp_IsAlive(c) || g_mp.p[q].downed ||
        (stable && !hopping && (carrier_hit || carrier_busy || *Mp_PlayerRam(q, 0x4D) != 0))) {
      g_mp.carried_by[q] = 0;
      if (g_mp.p[q].joined && *Mp_PlayerRam(q, 0x4D) == 0)
        WORD(*Mp_PlayerRam(q, 0x24)) = 0;  // set down where it is
      continue;
    }
    any = true;
    // The carried Link can only wriggle free.
    bool b = *Mp_PlayerRam(q, 0xF4) & 0x80;  // filtered_joypad_H: B
    *Mp_PlayerRam(q, 0xF0) = *Mp_PlayerRam(q, 0xF2) = *Mp_PlayerRam(q, 0xF4) = *Mp_PlayerRam(q, 0xF6) = 0;
    if (b && stable && !hopping)
      Mp_Release(q, false);
  }
  if (!stable) {
    // On the way: the carried Link moves with its carrier.
    if (any)
      Mp_PlaceCarried();
    return;
  }
  // A Link with a teammate over its head can walk, open the menu and throw
  // (A); no sword, items or dash. (Bits: B 0x80, Y 0x40, Select 0x20, Start
  // 0x10 and the directions in joypad1H_last/filtered_joypad_H; A 0x80, X
  // 0x40, L 0x20, R 0x10 in joypad1L_last/filtered_joypad_L.)
  for (int q = 0; q < kMpMaxPlayers; q++) {
    int c = g_mp.carried_by[q] - 1;
    if (c < 0)
      continue;
    *Mp_PlayerRam(c, 0xF0) &= 0x3f;
    *Mp_PlayerRam(c, 0xF4) &= 0x3f;
    *Mp_PlayerRam(c, 0xF2) &= 0x80;
    *Mp_PlayerRam(c, 0xF6) &= 0x80;
  }
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p) || !(*Mp_PlayerRam(p, 0xF6) & 0x80))  // filtered_joypad_L: A
      continue;
    int q = Mp_CarriedBy(p);
    uint8 ps = *Mp_PlayerRam(p, 0x5D);
    if (q >= 0) {
      *Mp_PlayerRam(p, 0xF6) &= ~0x80;
      if (!(ps >= 11 && ps <= 16))  // (not while hopping down a ledge)
        Mp_Release(q, true);
    } else if (Mp_IsFreeToCarry(p) && (q = Mp_TeammateInFront(p)) >= 0) {
      *Mp_PlayerRam(p, 0xF6) &= ~0x80;
      g_mp.carried_by[q] = p + 1;
    }
  }
}

// After everybody moved: carried Links are where their carriers hold them.
static void Mp_PlaceCarried() {
  for (int q = 0; q < kMpMaxPlayers; q++) {
    int c = g_mp.carried_by[q] - 1;
    if (c < 0)
      continue;
    uint16 x = Mp_PlayerX(c), y = Mp_PlayerY(c);
    uint8 floor = *Mp_PlayerRam(c, 0xEE), dir = *Mp_PlayerRam(c, 0x2F);
    int16 cz = WORD(*Mp_PlayerRam(c, 0x24));  // the carrier's height (hopping down a ledge)
    WORD(*Mp_PlayerRam(q, 0x22)) = x;
    WORD(*Mp_PlayerRam(q, 0x20)) = y + 1;  // drawn in front of the carrier
    WORD(*Mp_PlayerRam(q, 0x24)) = kMpCarryHeight + (cz > 0 ? cz : 0);
    *Mp_PlayerRam(q, 0xEE) = floor;
    *Mp_PlayerRam(q, 0x2F) = dir;
  }
}

void Mp_BeginFrame() {
  if (g_mp.resident != g_mp.leader)
    Mp_SwapToLeader();
  for (int p = 0; p < kMpMaxPlayers; p++)
    g_mp_render.p[p].visible = 0, g_mp_render.p[p].anc_count = 0;
}

static uint32 Mp_AreaKey() {
  return player_is_indoors << 16 | (player_is_indoors ? dungeon_room_index : overworld_screen_index);
}

// Which quarter of a dungeon room "Link" is in belongs to the world, but
// the game only updates it as the leader walks from one half of the room to
// the other. After the leader changes, it still says where the old leader
// was: walking out of the room's exit then looks like walking into the other
// half, and the game scrolls instead of leaving the room. The quadrant isn't
// simply the half Link stands in (in some rooms, like Link's house, it isn't),
// but how the two differ stays the same within a room. So that difference is
// remembered per room, and the quadrant is kept in line with the leader's
// half through it. (Not while the leader stands in a doorway: there the
// game's door code moves the quadrant on its own time.)
static void Mp_SyncQuadrantToLeader() {
  if (!player_is_indoors)
    g_mp.quad_valid = 0;  // (a room entered again may be entered elsewhere)
  if (!g_mp_coop || !player_is_indoors || main_module_index != 7 || submodule_index != 0 ||
      is_standing_in_doorway)
    return;
  uint8 page = ((link_x_coord >> 8) & 1) | ((link_y_coord >> 8) & 1) << 1;
  uint8 quad = link_quadrant_x | link_quadrant_y;
  if (!g_mp.quad_valid || g_mp.quad_room != dungeon_room_index) {
    g_mp.quad_valid = 1;
    g_mp.quad_room = dungeon_room_index;
    g_mp.quad_offset = page ^ quad;
    return;
  }
  uint8 want = page ^ g_mp.quad_offset, d = want ^ quad;
  if (d & 1) {
    if (want & 1)
      AdjustQuadrantAndCamera_right();
    else
      AdjustQuadrantAndCamera_left();
  }
  if (d & 2) {
    if (want & 2)
      AdjustQuadrantAndCamera_down();
    else
      AdjustQuadrantAndCamera_up();
  }
}

void Mp_TakeLead() {
  g_mp.leader = g_mp.resident;
  // (Only with the room's difference known: learning it now would take the
  // old leader's quadrant for this Link's.)
  if (g_mp.quad_valid && g_mp.quad_room == dungeon_room_index)
    Mp_SyncQuadrantToLeader();
}

static void Mp_RestoreMenuLeader();

// The Ping button: a tap marks "look here" a little ahead of the Link, held
// for a third of a second it marks "help" where the Link is (a ghost always
// calls for help). The marker is part of the world's state, so it shows the
// same on every computer; everybody in the same area or room sees it.
void Mp_HandlePings() {
  enum { kHelpHold = 20, kCooldown = 30 };
  bool playing = Mp_IsPlayModule(main_module_index);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    MpPing *pg = &g_mp.ping[p];
    if (pg->ttl)
      pg->ttl--;
    if (g_mp.ping_cooldown[p])
      g_mp.ping_cooldown[p]--;
    bool can = g_mp.p[p].joined && !g_mp.away[p] && playing;
    bool held = can && (g_mp.inputs[p] & kMpPingBit);
    uint8 hold = g_mp.ping_hold[p];
    g_mp.ping_hold[p] = held ? (hold < 255 ? hold + 1 : 255) : 0;
    int kind = -1;
    if (held && g_mp.ping_hold[p] == kHelpHold)
      kind = 1;
    else if (!held && hold && hold < kHelpHold && can)
      kind = g_mp.p[p].downed;
    if (kind < 0 || g_mp.ping_cooldown[p])
      continue;
    uint16 x = Mp_PlayerX(p) + 8, y = Mp_PlayerY(p) + 12;
    if (kind == 0) {
      static const int8 kAheadX[4] = { 0, 0, -24, 24 }, kAheadY[4] = { -24, 24, 0, 0 };
      int dir = (*Mp_PlayerRam(p, 0x2F) >> 1) & 3;  // link_direction_facing
      x += kAheadX[dir], y += kAheadY[dir];
    }
    pg->x = x, pg->y = y;
    uint32 key = Mp_AreaKeyPublic();
    pg->area = (uint16)key, pg->indoors = (uint8)(key >> 16), pg->pad = 0;
    pg->ttl = kMpPingFrames;
    pg->kind = kind;
    g_mp.ping_cooldown[p] = kCooldown;
    if (kind)
      sound_effect_1 = 43;  // (the low-health beep)
    else
      sound_effect_2 = 32;  // (a menu tick)
  }
}

void Mp_EndFrame() {
  if (g_mp_coop)
    Mp_HandlePings();
  if (g_mp.merge_grace)
    g_mp.merge_grace--;
  // (Not in module 5 itself: a mirror warp out of a dungeon and a continue
  // after game over load through it in the middle of the game.)
  if (main_module_index < 5)
    g_mp.at_title = 1;
  // A Link playing alone in a world keeps track of where it is, so players
  // who join this world later aren't mistaken for a group that just arrived.
  if (!Mp_Active()) {
    Mp_SyncQuadrantToLeader();
    // (Also once a regroup was pending when it split off, or it would never
    // count as settled and could never meet up with anyone again.)
    bool in_bed = link_player_handler_state == kPlayerState_AsleepInBed ||
                  (g_mp.area_key == 0xffffffff && link_auxiliary_state);
    if (!in_bed && Mp_IsPlayModule(main_module_index) && submodule_index == 0)
      g_mp.area_key = Mp_AreaKey();
    // (A Link alone in its world in a co-op game picks items in a menu world
    // of its own; the box here still shows the old one.)
    if (g_mp_coop && Mp_IsPlayModule(main_module_index))
      Hud_SyncItemBox();
    return;
  }
  Mp_SwapToLeader();
  // Players who joined on the title or file select screen got hearts, magic
  // and a Y item before there was a save file; start them like the leader
  // instead.
  if (g_mp.at_title && Mp_IsPlayModule(main_module_index)) {
    g_mp.at_title = 0;
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (g_mp.p[p].joined && p != g_mp.leader) {
        *Mp_PlayerRam(p, 0xF36D) = link_health_current;
        *Mp_PlayerRam(p, 0xF36E) = link_magic_power;
        *Mp_PlayerRam(p, 0x202) = hud_cur_item;
        *Mp_PlayerRam(p, 0x303) = current_item_y;
        *Mp_PlayerRam(p, 0xF34F) = link_item_bottle_index;
      }
    }
  }
  Mp_RestoreMenuLeader();
  Mp_SyncQuadrantToLeader();
  Mp_AttributeObjects();
  if (main_module_index == 18 || main_module_index == 23)  // death / game over
    g_mp.after_death = 1;
  // A downed leader hands over the camera to someone who is still standing.
  // (Not in the middle of a screen or room transition: those finish by where
  // the leader stands, and with someone else's position the camera would
  // scroll on forever. A text box can't wait, though: a ghost can't close it.)
  uint8 m = main_module_index;
  bool in_transition = (Mp_IsPlayModule(m) && submodule_index != 0) ||
                       m == 6 || m == 8 || m == 10 || m == 15 || m == 16 || m == 17;
  if (g_mp.p[g_mp.leader].downed && !in_transition) {
    int nl = Mp_FindNewLeader(g_mp.leader);
    if (nl >= 0 && Mp_IsAlive(nl)) {
      Mp_SwapTo(nl);
      g_mp.leader = nl;
    }
  }
  // The HUD's item box shows the leader's item: the other Links' control code
  // may have drawn theirs there this frame, or the leader may have changed
  // (also into a text box or a map; only the item menu owns the box itself).
  if (Mp_IsPlayModule(m) || (m == 14 && submodule_index != 1))
    Hud_SyncItemBox();
}

void Mp_BeginObject() {
  g_mp.module_snapshot[0] = main_module_index;
  g_mp.module_snapshot[1] = submodule_index;
}

// Called after running a piece of game code under some player's context. If
// that code switched the game module, that player now leads.
void Mp_EndObject() {
  // (Not into a text box or menu for a player in the item menu, whose Link
  // only stands here: the world would wait for its buttons. A fall or other
  // trip that Link makes on its own takes it, as the leader, elsewhere.)
  if (g_mp.resident != g_mp.leader && !(g_mp.away[g_mp.resident] && main_module_index == 14) &&
      (g_mp.module_snapshot[0] != main_module_index || g_mp.module_snapshot[1] != submodule_index)) {
    // A menu or map (module 14) opened in a shared screen: the one who
    // opened it leads while it is open (its item choices land in its own
    // state), and the camera's player gets the lead back when it closes.
    // Not the flute's map: the bird takes everybody to the one who played
    // the flute, who leads on from there.
    if (main_module_index == 14 && submodule_index != 10 && !g_mp.independent && !g_mp.menu_leader)
      g_mp.menu_leader = g_mp.leader + 1;
    g_mp.leader = g_mp.resident;
  }
  Mp_SwapToLeader();
}

// Gives the lead back after a menu opened by a teammate closed (see
// Mp_EndObject).
static void Mp_RestoreMenuLeader() {
  if (!g_mp.menu_leader)
    return;
  if (main_module_index == 14)
    return;
  int q = g_mp.menu_leader - 1;
  g_mp.menu_leader = 0;
  if ((main_module_index == 7 || main_module_index == 9) && submodule_index == 0 &&
      Mp_IsAlive(q) && !g_mp.away[q] && q != g_mp.leader) {
    Mp_SwapTo(q);
    g_mp.leader = q;
  }
}

static int Mp_NearestAlive(uint16 x, uint16 y) {
  int best = g_mp.leader, best_d = 0x7fffffff;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p))
      continue;
    int d = abs((int16)(Mp_PlayerX(p) - x)) + abs((int16)(Mp_PlayerY(p) - y));
    if (d < best_d)
      best_d = d, best = p;
  }
  return best;
}

static bool Mp_LeaderInCutscene() {
  uint8 *state = Mp_PlayerRam(g_mp.leader, 0x5D);         // link_player_handler_state
  uint8 *immobilized = Mp_PlayerRam(g_mp.leader, 0x2E4);  // flag_is_link_immobilized
  return *state == kPlayerState_AsleepInBed || *immobilized;
}

void Mp_PreSprite(int k) {
  if (!Mp_Active())
    return;
  Mp_BeginObject();
  int target;
  uint8 st = sprite_state[k];
  if ((st == 10 || st == 11) && g_mp.p[g_mp.sprite_owner[k]].joined)
    target = g_mp.sprite_owner[k];
  else
    target = Mp_NearestAlive(sprite_x_lo[k] | sprite_x_hi[k] << 8, sprite_y_lo[k] | sprite_y_hi[k] << 8);
  // During story scenes (asleep in bed, cutscenes freezing Link) everything
  // talks to the leader, so scripted state lands on the right Link.
  if (Mp_LeaderInCutscene()) {
    Mp_SwapTo(g_mp.leader);
    return;
  }
  // Many enemies have their own sword handling, so an enemy that is inside
  // some Link's sword swing deals with that Link this frame.
  if (st >= 9) {
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (!Mp_IsAlive(p))
        continue;
      Mp_SwapTo(p);
      if (player_oam_y_offset != 0x80 && sprite_floor[k] == link_is_on_lower_level) {
        SpriteHitBox hb;
        Player_SetupActionHitBox(&hb);
        Sprite_SetupHitBox(k, &hb);
        if (CheckIfHitBoxesOverlap(&hb)) {
          target = p;
          break;
        }
      }
    }
  }
  Mp_SwapTo(target);
}

static OamEnt g_mp_oam_before[128];
// The camera of a shared world follows the middle of its players instead of
// the leader. (A setting, the same on every machine; 1 by default.)
uint8 g_mp_group_camera = 1;

void Mp_BeginLeaderOam() {
  if (g_mp_coop)
    memcpy(g_mp_oam_before, oam_buf, sizeof(g_mp_oam_before));
}

void Mp_EndLeaderOam() {
  MpRenderPlayer *rp = &g_mp_render.self;
  rp->count = 0;
  rp->visible = 0;
  g_mp_render.self_player = g_mp.leader;
  if (!g_mp_coop)
    return;
  for (int i = 0; i < 128 && rp->count < kMpMaxExtraOam; i++) {
    OamEnt *e = &oam_buf[i];
    if (e->y == 0xf0 || !memcmp(e, &g_mp_oam_before[i], sizeof(OamEnt)))
      continue;
    rp->oam[rp->count * 2 + 0] = e->x | e->y << 8;
    rp->oam[rp->count * 2 + 1] = e->charnum | e->flags << 8;
    rp->high[rp->count] = bytewise_extended_oam[i];
    rp->count++;
  }
  rp->visible = 1;
}

bool Mp_PreAncilla(int k) {
  if (!Mp_Active())
    return true;
  Mp_BeginObject();
  // (One added since the last swap is its maker's, the resident: otherwise
  // its first frame ran as whoever owned that slot before.)
  Mp_AttributeObjects();
  int owner = g_mp.ancilla_owner[k];
  if (!g_mp.p[owner].joined)
    owner = g_mp.leader;
  Mp_SwapTo(owner);
  memcpy(g_mp_oam_before, oam_buf, sizeof(g_mp_oam_before));
  // The flute's bird on its way to the one who called it:
  if (ancilla_type[k] == 0x27 && !ancilla_L[k] && !ancilla_step[k]) {
    // Gone down meanwhile: the bird doesn't come (it would carry the ghost
    // off, and everybody here along into the flute's map).
    if (g_mp.p[owner].downed) {
      ancilla_type[k] = 0;
      return false;
    }
    // In the item menu or a map (its Link only stands here): the bird waits
    // out of sight, and flies in again afterwards, or it would carry off the
    // Link standing in for that player and the whole world into a map that
    // nobody here can use.
    if (g_mp.away[owner] && ancilla_timer[k] < 2)
      ancilla_timer[k] = 2;
  }
  return true;
}

// Some projectiles (the hookshot's head and chain, ...) are drawn with tiles
// from Link's own graphics, which are only uploaded for the leader. The
// sprites another player's projectile just added that use those tiles are
// moved over to that player's own sprite set, which has its own Link tiles.
// Can a Link stand at x,y (its feet not over a pit or deep water)?
static bool Mp_SafeGround(uint16 x, uint16 y, uint8 floor) {
  static const int8 kFeet[4][2] = { {2, 16}, {13, 16}, {2, 23}, {13, 23} };
  uint8 saved = sprite_tiletype;  // (GetTileAttribute leaves its result there)
  bool ok = true;
  for (int i = 0; i < 4 && ok; i++) {
    uint16 px = x + kFeet[i][0];
    uint8 t = GetTileAttribute(floor, &px, y + kFeet[i][1]);
    ok = !(t == 0x08 || t == 0x20 || (t >= 0xb0 && t <= 0xbd));
  }
  sprite_tiletype = saved;
  return ok;
}

// The hookshot latches onto a teammate the way it does onto a block, and
// its owner is pulled over to them (over pits too, as always), ending
// beside them, or where they stand when the spot beside them is a pit or
// deep water. Runs in the owner's context right after its hookshot moved.
static void Mp_HookTeammate(int k) {
  int o = g_mp.resident;
  if (ancilla_type[k] != 0x1f || related_to_hookshot || ancilla_step[k] || ancilla_L[k] || hookshot_effect_index != k ||
      ancilla_item_to_link[k] < 4 || !Mp_IsAlive(o))
    return;
  uint16 ax = Ancilla_GetX(k), ay = Ancilla_GetY(k), ox = link_x_coord, oy = link_y_coord;
  uint8 floor = link_is_on_lower_level;
  for (int q = 0; q < kMpMaxPlayers; q++) {
    if (q == o || !Mp_IsAlive(q) || g_mp.carried_by[q] || g_mp.thrown[q] || *Mp_PlayerRam(q, 0xEE) != floor)
      continue;
    // Only a teammate standing on the ground (not hopping down a ledge or
    // flying from a hit).
    uint16 tz = WORD(*Mp_PlayerRam(q, 0x24));
    if (*Mp_PlayerRam(q, 0x5D) != 0 || *Mp_PlayerRam(q, 0x4D) != 0 || (tz != 0 && tz != 0xffff))
      continue;
    uint16 tx = Mp_PlayerX(q), ty = Mp_PlayerY(q);
    if ((uint16)(tx + 12 - ax) >= 24 || (uint16)(ty + 20 - ay) >= 28)
      continue;
    int dir = ancilla_dir[k] & 3;  // up, down, left, right
    static const int8 kBesideX[4] = { 0, 0, 16, -16 }, kBesideY[4] = { 16, -16, 0, 0 };
    // The pull only goes the way the hook flew: beside the teammate, or
    // else where they stand (when beside them is a pit or deep water). The
    // spot has to be on solid ground and ahead (a teammate overlapping the
    // owner would pull it backwards, then past them): otherwise the hook
    // flies on as if they weren't there.
    uint16 ex = dir >= 2 ? tx + kBesideX[dir] : ox, ey = dir < 2 ? ty + kBesideY[dir] : oy;
    if (!Mp_SafeGround(ex, ey, floor)) {
      // (Right where the teammate stands is as safe as it is for them; a
      // spot off to the side of it has to be checked.)
      ex = dir >= 2 ? tx : ox, ey = dir < 2 ? ty : oy;
      if ((ex != tx || ey != ty) && !Mp_SafeGround(ex, ey, floor))
        continue;
    }
    int ahead = dir == 0 ? (int16)(oy - ey) : dir == 1 ? (int16)(ey - oy) : dir == 2 ? (int16)(ox - ex) : (int16)(ex - ox);
    if (ahead < 2)
      continue;
    // The pull heads for the hook plus an offset (kHookshotArrA/B in
    // player.c), 4 pixels a frame, for as many frames as the hook flew out:
    // enough of them to get all the way.
    static const int8 kPullX[4] = { 0, 0, 4, -12 }, kPullY[4] = { -8, -16, 0, 0 };
    Ancilla_SetXY(k, ex - kPullX[dir], ey - kPullY[dir]);
    int frames = abs((int16)(dir >= 2 ? ex - ox : ey - oy)) / 4 + 4;
    if (ancilla_item_to_link[k] < frames)
      ancilla_item_to_link[k] = frames < 127 ? frames : 127;
    ancilla_x_vel[k] = ancilla_y_vel[k] = 0;
    related_to_hookshot = 1;
    hookshot_effect_index = k;
    return;
  }
}

void Mp_PostAncilla(int k) {
  int owner = g_mp.resident;
  if (Mp_Active())
    Mp_HookTeammate(k);
  if (!Mp_Active() || owner == g_mp.leader)
    return;
  MpRenderPlayer *rp = &g_mp_render.p[owner];
  for (int i = 0; i < 128; i++) {
    OamEnt *e = &oam_buf[i];
    if (e->y == 0xf0 || !memcmp(e, &g_mp_oam_before[i], sizeof(OamEnt)))
      continue;
    if ((e->flags & 1) || e->charnum >= 0x20 || rp->anc_count >= kMpMaxAncillaOam)
      continue;
    rp->anc_oam[rp->anc_count * 2 + 0] = e->x | e->y << 8;
    rp->anc_oam[rp->anc_count * 2 + 1] = e->charnum | e->flags << 8;
    rp->anc_high[rp->anc_count] = bytewise_extended_oam[i];
    rp->anc_count++;
    e->y = 0xf0;
  }
}

bool Mp_SuppressCamera() {
  return g_mp.in_other_update != 0;
}

// The point the camera of a shared world heads for: the middle of the
// players playing in it (standing, not in a menu). Returns how many there
// are; *fast says whether one of them is dashing.
static int Mp_GroupTarget(uint16 *tx, uint16 *ty, bool *fast) {
  int n = 0, minx = 0, maxx = 0, miny = 0, maxy = 0;
  *fast = false;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p) || g_mp.away[p])
      continue;
    int x = Mp_PlayerX(p), y = Mp_PlayerY(p);
    if (n == 0 || x < minx) minx = x;
    if (n == 0 || x > maxx) maxx = x;
    if (n == 0 || y < miny) miny = y;
    if (n == 0 || y > maxy) maxy = y;
    *fast |= *Mp_PlayerRam(p, 0x5D) == 17;  // link_player_handler_state: dashing
    n++;
  }
  *tx = (minx + maxx) / 2;
  *ty = (miny + maxy) / 2;
  return n;
}

// Is the leader about to walk out of the overworld area (within |margin|
// pixels of the line where the game moves on to the next area, heading
// there)? The transition needs the camera at the area's edge, so the camera
// then follows the leader alone.
static bool Mp_LeaderAtAreaEdge(int margin) {
  if (main_module_index != 9 || player_is_indoors)
    return false;
  int area = BYTE(overworld_area_index) & 0x3f;
  int rb = overworld_right_bottom_bound_for_scroll;
  int tx = (int16)(link_x_coord - kOverworld_OffsetBaseX[area]);
  int ty = (int16)(link_y_coord - kOverworld_OffsetBaseY[area]);
  int8 vx = link_x_vel, vy = link_y_vel;
  return (vx < 0 && tx < 6 + margin) || (vx > 0 && tx >= rb + 4 - margin) ||
         (vy < 0 && ty < 4 + margin) || (vy > 0 && ty >= rb - margin);
}

// How fast the camera slides toward a point |d| pixels off its centre.
// (1 for the last few pixels, so the camera comes to rest exactly there.)
static uint8 Mp_SlideSpeed(int d, bool fast) {
  int a = abs(d);
  int s = a > 64 || fast ? 4 : a > 24 ? 3 : a > 4 ? 2 : 1;
  return d > 0 ? s : -s;
}

void Mp_CameraCatchUp(void (*scroll)(void)) {
  if (!g_mp_coop || g_mp.p[g_mp.leader].downed) {
    scroll();
    return;
  }
  uint16 tx, ty;
  bool fast;
  int n = g_mp_group_camera ? Mp_GroupTarget(&tx, &ty, &fast) : 0;
  // A group plays in this world: the camera keeps its middle in view, in
  // place of the leader. (Not while the leader is held still by a cutscene
  // or a text box, and not when it is about to leave the area: the screen
  // transition needs the camera at the area's edge, where the leader is.)
  if (n >= 2 && flag_is_link_immobilized) {
    scroll();
    return;
  }
  bool steer = n >= 2 && !Mp_LeaderAtAreaEdge(40);
  // A lone player walking back into a group's view: the camera closes in on
  // the group's camera, so the worlds can merge without the view jumping.
  if (!steer && n == 1 && g_mp_group_camera && g_mp.independent && g_mp_neighbour_camera &&
      !flag_is_link_immobilized && !Mp_LeaderAtAreaEdge(40)) {
    uint16 cx, cy;
    if (g_mp_neighbour_camera(&cx, &cy)) {
      tx = cx + 128 - 8, ty = cy + 116 - 12;
      fast = true;
      steer = true;
    }
  }
  if (steer) {
    uint8 vx = link_x_vel, vy = link_y_vel;
    uint16 lx = link_x_coord, ly = link_y_coord, lz = link_z_coord;
    // The leader stays well inside the view (a teammate further off than
    // that gets a world of its own soon, as before).
    int d = (int16)(tx - lx);
    tx = d > 104 ? lx + 104 : d < -104 ? lx - 104 : tx;
    d = (int16)(ty - ly);
    ty = d > 88 ? ly + 88 : d < -88 ? ly - 88 : ty;
    int sx = (int16)(tx + 8 - BG2HOFS_copy2) - 128, sy = (int16)(ty + 12 - BG2VOFS_copy2) - 116;
    link_x_coord = tx, link_y_coord = ty, link_z_coord = 0;
    link_x_vel = sx ? Mp_SlideSpeed(sx, fast) : 0;
    link_y_vel = sy ? Mp_SlideSpeed(sy, fast) : 0;
    scroll();
    link_x_coord = lx, link_y_coord = ly, link_z_coord = lz;
    link_x_vel = vx, link_y_vel = vy;
    return;
  }
  if (!g_mp.independent) {
    scroll();
    return;
  }
  uint8 vx = link_x_vel, vy = link_y_vel;
  int sx = (int16)(link_x_coord + 8 - BG2HOFS_copy2), sy = (int16)(link_y_coord + 12 - BG2VOFS_copy2);
  // Near or past the screen's edge (the camera still sliding over after a
  // split, say), the camera heads for the leader whichever way it walks.
  bool far_x = sx < 24 || sx > 232, far_y = sy < 24 || sy > 200;
  if (far_x || (vx == 0 && (sx > 136 || sx < 120)))
    link_x_vel = sx > 128 ? (far_x ? 3 : 2) : (far_x ? 0xfd : 0xfe);
  if (far_y || (vy == 0 && (sy > 124 || sy < 108)))
    link_y_vel = sy > 116 ? (far_y ? 3 : 2) : (far_y ? 0xfd : 0xfe);
  scroll();
  link_x_vel = vx;
  link_y_vel = vy;
}

// Runs |func| for each standing player, starting with the resident one. The
// first player for which it returns true stays resident.
bool Mp_AnyPlayer(int k, MpSpriteCheckFunc *func) {
  if (!Mp_Active())
    return func(k);
  int r = g_mp.resident;
  if (Mp_IsAlive(r) && func(k))
    return true;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p == r || !Mp_IsAlive(p))
      continue;
    Mp_SwapTo(p);
    if (func(k))
      return true;
  }
  Mp_SwapTo(r);
  return false;
}

uint8 Mp_AnyPlayerU8(int k, MpSpriteCheckFuncU8 *func) {
  if (!Mp_Active())
    return func(k);
  int r = g_mp.resident;
  uint8 rv;
  if (Mp_IsAlive(r) && (rv = func(k)) != 0)
    return rv;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p == r || !Mp_IsAlive(p))
      continue;
    Mp_SwapTo(p);
    if ((rv = func(k)) != 0)
      return rv;
  }
  Mp_SwapTo(r);
  return 0;
}

void Mp_ClearPerFrameSpriteFlags() {
  if (!Mp_Active())
    return;
  // What Sprite_Main resets every frame for the resident Link: the flags a
  // sprite sets on the Link it touched (pick-up, pedestal pose, pull-switch
  // lean, "can't move", platform drag). Sprites set them in the touched
  // player's context, so the others need the same reset or they stay stuck.
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined || p == g_mp.resident)
      continue;
    *Mp_PlayerRam(p, 0x314) = 0;  // flag_is_sprite_to_pick_up
    *Mp_PlayerRam(p, 0x377) = 0;  // link_unk_master_sword
    *Mp_PlayerRam(p, 0x379) = 0;  // byte_7E0379 (pull switch)
    *Mp_PlayerRam(p, 0xB7B) = 0;  // link_prevent_from_moving
    if (submodule_index == 0) {
      WORD(*Mp_PlayerRam(p, 0xB7C)) = 0;  // drag_player_x
      WORD(*Mp_PlayerRam(p, 0xB7E)) = 0;  // drag_player_y
    }
  }
}

// Called when the resident Link takes fatal damage. Returns true if co-op
// handled it (the Link went down, or a bottled fairy saved it); the Link's
// new health is in link_health_current. Returns false for a regular death,
// which also handles fairies the original way.
// A Link going down lets go of what it holds: a lifted bush or pot falls
// (the game's own drop, as when a carrying Link gets hit), a carried teammate
// is set down, and the Link is in no carrying pose any more.
static void Mp_LetGo(int me) {
  uint8 timer = link_incapacitated_timer;
  link_incapacitated_timer = 1;  // (what makes the game drop a carried object)
  for (int k = 0; k < 16; k++)
    if (sprite_state[k] == 10 && g_mp.sprite_owner[k] == me)
      CarriedSprite_CheckForThrow(k);
  link_incapacitated_timer = timer;
  int q = Mp_CarriedBy(me);
  if (q >= 0) {
    g_mp.carried_by[q] = 0;
    WORD(*Mp_PlayerRam(q, 0x24)) = 0;
  }
  link_state_bits = 0;
  link_picking_throw_state = 0;
  link_item_in_hand = 0;
  link_position_mode = 0;
}

// The resident Link takes a fatal hit: it goes down (true) unless it is the
// last one standing in the whole game (false: the caller ends the game).
static bool Mp_GoDown(bool fairy_helps) {
  int me = g_mp.resident;
  bool other_alive = g_mp_other_instance_alive && g_mp_other_instance_alive();
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if ((p != me && Mp_IsAlive(p)) || other_alive) {
      // A fairy in a bottle (shared by the team) saves the Link right away.
      for (int i = 0; i < 4 && fairy_helps; i++) {
        if (link_bottle_info[i] == 6) {
          link_bottle_info[i] = 2;
          link_health_current = link_health_capacity < 0x38 ? link_health_capacity : 0x38;
          link_hearts_filler = 0;
          countdown_for_blink = 0x60;
          sound_effect_2 = 0x31;
          Hud_Rebuild();
          return true;
        }
      }
      g_mp.p[me].downed = 1;
      g_mp.p[me].revive_ctr = 0;
      // Whatever the Link carried (a bush, a pot, a teammate) is let go.
      Mp_LetGo(me);
      // The bird it called with the flute doesn't come (see Mp_PreAncilla).
      for (int i = 0; i < 10; i++)
        if (g_mp.ancilla_owner[i] == me && ancilla_type[i] == 0x27 && !ancilla_L[i] && !ancilla_step[i])
          ancilla_type[i] = 0;
      // A ghost lies still where it fell: no knockback, no hop.
      link_player_handler_state = 0;
      link_auxiliary_state = 0;
      link_incapacitated_timer = 0;
      link_z_coord = 0;
      link_actual_vel_x = link_actual_vel_y = link_actual_vel_z = 0;
      link_x_vel = link_y_vel = 0;
      link_health_current = 0;
      countdown_for_blink = 0;
      link_hearts_filler = 0;
      return true;
    }
  }
  return false;
}

bool Mp_OnFatalDamage() {
  return Mp_GoDown(true);
}

bool Mp_DeathLinkFall() {
  if (Mp_GoDown(false))
    return false;
  // The last Link standing: game over, as when it takes a fatal hit.
  mapbak_TM = TM_copy;
  mapbak_TS = TS_copy;
  saved_module_for_menu = main_module_index;
  main_module_index = 18;
  submodule_index = 1;
  countdown_for_blink = 0;
  link_hearts_filler = 0;
  link_health_current = 0;
  Mp_SetLeader(g_mp.resident);
  return true;
}

void Mp_Revive(int p, uint8 health) {
  g_mp.p[p].downed = 0;
  g_mp.p[p].revive_ctr = 0;
  *Mp_PlayerRam(p, 0xF36D) = health;
  *Mp_PlayerRam(p, 0x31F) = 0x60;  // countdown_for_blink
  *Mp_PlayerRam(p, 0x373) = 0;     // link_give_damage
}

static void Mp_HandleRevives() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined || !g_mp.p[p].downed)
      continue;
    bool near = false;
    for (int q = 0; q < kMpMaxPlayers; q++) {
      if (q != p && Mp_IsAlive(q) &&
          abs((int16)(Mp_PlayerX(q) - Mp_PlayerX(p))) < 24 &&
          abs((int16)(Mp_PlayerY(q) - Mp_PlayerY(p))) < 24)
        near = true;
    }
    if (!near) {
      g_mp.p[p].revive_ctr = 0;
    } else if (++g_mp.p[p].revive_ctr >= kMpReviveFrames) {
      Mp_Revive(p, 4);  // half a heart
      sound_effect_2 = 0x2f;
    }
  }
}

enum {
  kMpTetherX = 104,  // how far (in pixels) a Link may be from the leader
  kMpTetherY = 76,
};

static int Abs16(uint16 a, uint16 b) {
  return abs((int16)(a - b));
}

// Undoes movement of the resident Link that takes it further than allowed
// from (lx, ly), or further off screen. This never moves a Link anywhere it
// wasn't already, so nobody gets pushed into walls.
static void Mp_LimitMovement(uint16 old_x, uint16 old_y, uint16 lx, uint16 ly) {
  int extra = (enhanced_features0 & kFeatures0_ExtendScreen64) ? 40 : 0;
  int sx = (int16)(link_x_coord - BG2HOFS_copy2), old_sx = (int16)(old_x - BG2HOFS_copy2);
  int sy = (int16)(link_y_coord - BG2VOFS_copy2), old_sy = (int16)(old_y - BG2VOFS_copy2);
  bool bad_x = (Abs16(link_x_coord, lx) > kMpTetherX && Abs16(link_x_coord, lx) > Abs16(old_x, lx)) ||
               (sx < -extra && sx < old_sx) || (sx > 256 - 16 + extra && sx > old_sx);
  bool bad_y = (Abs16(link_y_coord, ly) > kMpTetherY && Abs16(link_y_coord, ly) > Abs16(old_y, ly)) ||
               (sy < -8 && sy < old_sy) || (sy > 224 - 32 && sy > old_sy);
  if (bad_x)
    link_x_coord = old_x;
  if (bad_y)
    link_y_coord = old_y;
}

// The leader may not walk away from the others either.
static void Mp_LimitLeaderMovement() {
  uint16 old_x = link_x_coord_prev, old_y = link_y_coord_prev;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p == g_mp.leader || !Mp_IsAlive(p))
      continue;
    uint16 px = Mp_PlayerX(p), py = Mp_PlayerY(p);
    if (Abs16(link_x_coord, px) > kMpTetherX && Abs16(link_x_coord, px) > Abs16(old_x, px))
      link_x_coord = old_x;
    if (Abs16(link_y_coord, py) > kMpTetherY && Abs16(link_y_coord, py) > Abs16(old_y, py))
      link_y_coord = old_y;
  }
}

// Keeps the resident Link inside the overworld area its world has loaded,
// short of the line where the leader would move on to the next area.
// Returns true if it was at (or past) that line. The game only checks the
// leader for leaving the area, and only as it crosses the line: a teammate
// walking (or being thrown) past it ended up outside the area, where the
// camera can't follow and from where it never moved on either.
static bool Mp_KeepInOverworldArea() {
  if (!Mp_IsOverworldModule(main_module_index) || submodule_index != 0 || player_is_indoors)
    return false;
  // (The area loaded, by its top left corner; the game's per-frame "area of
  // the player" can be stale right after coming out of a building. A special
  // area has no entry in the tables: it is where the game put it.)
  int area = BYTE(overworld_area_index) & 0x3f;
  int bx = kOverworld_OffsetBaseX[area], by = kOverworld_OffsetBaseY[area];
  if (main_module_index == 11)
    bx = overworld_offset_base_x << 3, by = overworld_offset_base_y;
  int rb = overworld_right_bottom_bound_for_scroll;
  int tx = (int16)(link_x_coord - bx), ty = (int16)(link_y_coord - by);
  int cx = tx < 6 ? 6 : tx > rb + 3 ? rb + 3 : tx;
  int cy = ty < 4 ? 4 : ty > rb - 1 ? rb - 1 : ty;
  // Only a nudge back from just past the edge, never a teleport.
  if ((cx == tx && cy == ty) || abs(cx - tx) > 48 || abs(cy - ty) > 48)
    return false;
  link_x_coord = bx + cx;
  link_y_coord = by + cy;
  return true;
}

// Called by the gameplay modules after the leader's player control ran.
void Mp_RunOtherPlayers(void (*control)(void)) {
  if (!Mp_Active() || submodule_index != 0 || !Mp_IsPlayModule(main_module_index))
    return;
  assert(g_mp.resident == g_mp.leader);

  // While the leader sleeps through the intro the others aren't around yet.
  // They join the leader once it is standing on the floor.
  if (link_player_handler_state == kPlayerState_AsleepInBed) {
    g_mp.area_key = 0xffffffff;
    return;
  }
  // Getting out of bed: wait until the leader has hopped onto the floor.
  if (g_mp.area_key == 0xffffffff && link_auxiliary_state)
    return;

  uint32 key = Mp_AreaKey();
  if (g_mp.regroup_grace)
    g_mp.regroup_grace--;
  if (key != g_mp.area_key || g_mp.after_death) {
    g_mp.regroup_grace = 30;
    extern bool g_inst_trace;
    if (g_inst_trace)
      printf("  regroup: area %x -> %x after_death=%d leader=P%d\n", g_mp.area_key, key, g_mp.after_death, g_mp.leader + 1);
    g_mp.area_key = key;
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (g_mp.p[p].joined && p != g_mp.leader) {
        Mp_RegroupPlayer(p);
        if (g_mp.after_death)
          Mp_Revive(p, link_health_current ? link_health_current : 0x18);
      }
    }
    g_mp.after_death = 0;
  }

  if (!g_mp.independent)
    Mp_LimitLeaderMovement();
  uint16 lx = link_x_coord, ly = link_y_coord;

  uint8 m = main_module_index, s = submodule_index;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p == g_mp.leader || !g_mp.p[p].joined)
      continue;
    // Downed Links stay where they fell until someone revives them.
    if (g_mp.p[p].downed)
      continue;
    if (main_module_index != m || submodule_index != s)
      break;
    Mp_SwapTo(p);
    // Teleport stragglers back to the group.
    if (abs((int16)(link_x_coord - BG2HOFS_copy2 - 128)) > 400 || abs((int16)(link_y_coord - BG2VOFS_copy2 - 112)) > 400) {
      Mp_SwapToLeader();
      Mp_RegroupPlayer(p);
      Mp_SwapTo(p);
    }
    Mp_BeginObject();
    uint16 old_x = link_x_coord, old_y = link_y_coord;
    g_mp.in_other_update = 1;
    control();
    g_mp.in_other_update = 0;
    if (main_module_index == m && submodule_index == s) {
      // A teammate at the area's edge gets a world of its own, as the one
      // leading it, so its next step there takes it on to the next area.
      if (g_mp.independent && Mp_KeepInOverworldArea() && g_mp.split_request < 0 &&
          Mp_MovesOnAtEdge(*Mp_PlayerRam(p, 0x5D))) {
        g_mp.split_request = p;
        g_mp.split_travel = 0;
      }
      // So does one on the tile that leads into a special area (Zora's
      // domain, say) or out of it: the game only looks at the leader for
      // that, so a teammate walked right over it, on into the cliffs.
      if (g_mp.independent && g_mp.split_request < 0 && Mp_IsOverworldModule(main_module_index) &&
          Mp_MovesOnAtEdge(*Mp_PlayerRam(p, 0x5D)) && Overworld_OnSpecialSwitchTile()) {
        g_mp.split_request = p;
        g_mp.split_travel = 0;
      }
      if (!g_mp.independent) {
        Mp_LimitMovement(old_x, old_y, lx, ly);
      } else if (g_mp.split_request < 0 && !g_mp.regroup_grace) {
        // Heading out of view: this Link gets its own world and camera.
        // (Not right after a regroup: Links put next to the leader at, say, an
        // entrance look out of view until the camera has caught up.)
        // Only once it has mostly left the screen: a Link standing on stairs
        // or in a doorway at the screen's edge is still in view.
        int extra = (enhanced_features0 & kFeatures0_ExtendScreen64) ? 40 : 0;
        int sx = (int16)(link_x_coord - BG2HOFS_copy2), sy = (int16)(link_y_coord - BG2VOFS_copy2);
        if (sx < -8 - extra || sx > 256 - 8 + extra || sy < -16 || sy > 224 - 8) {
          g_mp.split_request = p;
          g_mp.split_travel = 0;
        }
      }
    }
    // Hud_RefillLogic only runs for the leader, so apply pending hearts directly
    // and refill magic the same way it does.
    if (link_hearts_filler) {
      int h = link_health_current + link_hearts_filler;
      link_health_current = h > link_health_capacity ? link_health_capacity : h;
      link_hearts_filler = 0;
    }
    if (link_magic_filler) {
      if (link_magic_power >= 128) {
        link_magic_power = 128;
        link_magic_filler = 0;
      } else {
        link_magic_filler--;
        link_magic_power++;
      }
    }
    Mp_EndObject();
  }
  Mp_SwapToLeader();
  // A leader knocked back or thrown doesn't move on to the next area either:
  // keep it inside too. (Swimming, as a bunny or dashing it does.)
  if (g_mp.independent && !Mp_MovesOnAtEdge(link_player_handler_state))
    Mp_KeepInOverworldArea();
  Mp_PlaceCarried();
  Mp_HandleRevives();
  if (g_mp.friendly_fire)
    Mp_FriendlyFire();
}

void Mp_ForEachAlivePlayer(int k, void (*func)(int k)) {
  int r = g_mp.resident;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p))
      continue;
    Mp_SwapTo(p);
    func(k);
  }
  Mp_SwapTo(r);
}

static void Mp_HitBoxFromLink(SpriteHitBox *hb) {
  uint16 x = link_x_coord + 4, y = link_y_coord + 8;
  hb->r4_spr_xlo = x;
  hb->r10_spr_xhi = x >> 8;
  hb->r5_spr_ylo = y;
  hb->r11_spr_yhi = y >> 8;
  hb->r6_spr_xsize = 8;
  hb->r7_spr_ysize = 8;
}

static void Mp_HurtLink(uint16 from_x, uint16 from_y, uint8 damage) {
  int dx = (int16)(link_x_coord - from_x), dy = (int16)(link_y_coord - from_y);
  link_actual_vel_x = abs(dx) >= abs(dy) ? (dx < 0 ? -24 : 24) : 0;
  link_actual_vel_y = abs(dx) < abs(dy) ? (dy < 0 ? -24 : 24) : 0;
  link_actual_vel_z = 12;
  link_z_coord = 0;
  link_incapacitated_timer = 19;
  link_auxiliary_state = 1;
  link_give_damage = damage;
}

// Projectiles (arrows, rods, sword beams, boomerang, hookshot) hitting teammates.
static void Mp_FriendlyFireProjectiles() {
  for (int k = 0; k < 10; k++) {
    uint8 t = ancilla_type[k];
    uint8 dmg;
    switch (t) {
    case 0x01: case 0x02: case 0x09: case 0x0B: case 0x0C: dmg = 4; break;
    case 0x05: dmg = 2; break;  // (the hookshot latches onto a teammate instead)
    default: continue;
    }
    int owner = g_mp.ancilla_owner[k];
    uint16 ax = Ancilla_GetX(k), ay = Ancilla_GetY(k);
    for (int v = 0; v < kMpMaxPlayers; v++) {
      if (v == owner || !Mp_IsAlive(v))
        continue;
      Mp_SwapTo(v);
      if (countdown_for_blink || link_disable_sprite_damage || link_auxiliary_state || ancilla_floor[k] != link_is_on_lower_level)
        continue;
      if ((uint16)(link_x_coord + 12 - ax) < 24 && (uint16)(link_y_coord + 20 - ay) < 28) {
        Mp_HurtLink(ax, ay, dmg);
        if (t == 0x09 || t == 0x02 || t == 0x0B || t == 0x01)
          ancilla_type[k] = 0;
        break;
      }
    }
  }
  Mp_SwapToLeader();
}

// Sword hits between players.
void Mp_FriendlyFire() {
  Mp_FriendlyFireProjectiles();
  for (int a = 0; a < kMpMaxPlayers; a++) {
    if (!Mp_IsAlive(a))
      continue;
    Mp_SwapTo(a);
    if (player_oam_y_offset == 0x80)
      continue;  // not swinging
    SpriteHitBox hb;
    Player_SetupActionHitBox(&hb);
    uint8 floor = link_is_on_lower_level;
    uint16 ax = link_x_coord, ay = link_y_coord;
    for (int v = 0; v < kMpMaxPlayers; v++) {
      if (v == a || !Mp_IsAlive(v))
        continue;
      Mp_SwapTo(v);
      if (link_is_on_lower_level == floor && !countdown_for_blink && !link_disable_sprite_damage) {
        Mp_HitBoxFromLink(&hb);
        if (CheckIfHitBoxesOverlap(&hb))
          Mp_HurtLink(ax, ay, 4);
      }
      Mp_SwapTo(a);
    }
  }
  Mp_SwapToLeader();
}

// Draws the non-leader Links into private sprite lists for the renderer.
void Mp_DrawOtherPlayers() {
  if (!Mp_Active() || g_mp.area_key == 0xffffffff)
    return;
  uint8 saved[0x2A0];
  memcpy(saved, &g_ram[0x800], sizeof(saved));
  int r = g_mp.resident;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    MpRenderPlayer *rp = &g_mp_render.p[p];
    if (p == r || !g_mp.p[p].joined)
      continue;
    rp->count = 0;
    Mp_SwapTo(p);
    for (int i = 0; i < 128; i++)
      oam_buf[i].y = 0xf0;
    g_mp.in_other_update = 1;
    // A blinking Link (just hurt, or just put next to the leader) is hidden
    // every other frame. Without the widescreen fixes the game does that by
    // moving its sprites past the right edge with the 9th x bit, which the
    // x recomputed below undoes: the Link showed as broken 8x8 pieces. The
    // widescreen way hides them below the screen instead.
    uint32 features = enhanced_features0;
    enhanced_features0 |= kFeatures0_WidescreenVisualFixes;
    LinkOam_MainOne();
    enhanced_features0 = features;
    g_mp.in_other_update = 0;
    // The game's Link drawing only handles a Link near the screen; for one
    // off to the side it gets the 9th bit of the sprites' x wrong, and they
    // wrapped around onto the screen. Each sprite's x is worked out again
    // from where this Link really is, and sprites off the screen are left out.
    int sx = (int16)(link_x_coord - BG2HOFS_copy2), sy = (int16)(link_y_coord - BG2VOFS_copy2);
    bool far = sy < -48 || sy > 224 + 32;
    for (int i = 0; i < 128 && rp->count < kMpMaxExtraOam && !far; i++) {
      OamEnt *e = &oam_buf[i];
      if (e->y == 0xf0)
        continue;
      int x = e->x | (bytewise_extended_oam[i] & 1) << 8;
      x = x >= 256 ? x - 512 : x;
      while (x < sx - 128) x += 256;
      while (x > sx + 128) x -= 256;
      if (x < -32 || x >= 256)
        continue;
      rp->oam[rp->count * 2 + 0] = (x & 0xff) | e->y << 8;
      rp->oam[rp->count * 2 + 1] = e->charnum | e->flags << 8;
      rp->high[rp->count] = (bytewise_extended_oam[i] & ~1) | (x < 0);
      rp->count++;
    }
    rp->visible = 1;
    // Build the dynamically uploaded Link tiles for this player.
    uint16 srcs[12];
    Link_CalcDmaSources(srcs);
    uint16 *t = rp->tiles;  // t[0] corresponds to vram 0x4000
    memcpy(t, &g_zenv.vram[0x4000], 0x200 * 2);
    memcpy(&t[0x100], &kLinkGraphics[srcs[0] - 0x8000], 0x40);
    memcpy(&t[0x120], &kLinkGraphics[srcs[1] - 0x8000], 0x40);
    memcpy(&t[0x140], &kLinkGraphics[srcs[2] - 0x8000], 0x20);
    memcpy(&t[0x000], &kLinkGraphics[srcs[3] - 0x8000], 0x40);
    memcpy(&t[0x020], &kLinkGraphics[srcs[4] - 0x8000], 0x40);
    memcpy(&t[0x040], &kLinkGraphics[srcs[5] - 0x8000], 0x20);
    memcpy(&t[0x050], &g_ram[srcs[6]], 0x40);
    memcpy(&t[0x070], &g_ram[srcs[7]], 0x40);
    memcpy(&t[0x090], &g_ram[srcs[8]], 0x40);
    memcpy(&t[0x150], &g_ram[srcs[9]], 0x40);
    memcpy(&t[0x170], &g_ram[srcs[10]], 0x40);
    memcpy(&t[0x190], &g_ram[srcs[11]], 0x40);
  }
  memcpy(&g_ram[0x800], saved, sizeof(saved));
  Mp_SwapTo(r);
}

// ---------------------------------------------------------------------------
// Rendering glue (not part of the simulation)

#include "snes/ppu.h"

// Tunic colors for P1..P4 (5-bit RGB). Standard: green, red, blue, purple.
// Color-blind friendly (from the Okabe-Ito palette, told apart with any
// kind of color blindness): bluish green, orange, blue, reddish purple.
static const uint8 kMpPlayerColors[2][kMpMaxPlayers][3] = {
  { {6, 26, 8}, {31, 6, 6}, {7, 13, 31}, {24, 8, 31} },
  { {0, 20, 14}, {29, 20, 0}, {0, 14, 22}, {25, 15, 21} },
};
static const uint32 kMpPlayerColorsRgb[2][kMpMaxPlayers] = {
  { 0x40d040, 0xf04040, 0x4878ff, 0xc050ff },
  { 0x00b080, 0xf0a000, 0x2890e0, 0xd080b0 },
};
uint8 g_mp_color_scheme;

uint32 Mp_PlayerColorRgb(int p) {
  return kMpPlayerColorsRgb[g_mp_color_scheme & 1][p & 3];
}
static uint16 g_mp_saved_cgram[16];
static uint16 g_mp_tunic_mask;

static uint16 Mp_TunicMask() {
  if (!g_mp_tunic_mask) {
    // The colors that differ between green and blue mail are the tunic.
    const uint16 *a = kPalette_ArmorAndGloves, *b = kPalette_ArmorAndGloves + 15;
    for (int i = 0; i < 15; i++)
      if (a[i] != b[i])
        g_mp_tunic_mask |= 1 << (i + 1);
    if (!g_mp_tunic_mask)
      g_mp_tunic_mask = 0x8000;
  }
  return g_mp_tunic_mask;
}

static void Mp_MakePalette(uint16 *dst, const uint16 *src, int player) {
  uint16 mask = Mp_TunicMask();
  const uint8 *col = kMpPlayerColors[g_mp_color_scheme & 1][player];
  for (int i = 0; i < 16; i++) {
    uint16 c = src[i];
    if (mask & (1 << i)) {
      int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
      int v = r > g ? r : g;
      v = v > b ? v : b;
      v = v < 6 ? 6 : v;
      c = (col[0] * v / 31) | (col[1] * v / 31) << 5 | (col[2] * v / 31) << 10;
    }
    dst[i] = c;
  }
}

// A downed Link is drawn steadily as a pale ghost. (It used to blink, which
// looked like the ghost kept popping up out of nowhere.)
static void Mp_MakeGhostPalette(uint16 *dst, const uint16 *src, int player) {
  Mp_MakePalette(dst, src, player);
  for (int i = 1; i < 16; i++) {
    uint16 c = dst[i];
    int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
    int l = (r * 3 + g * 6 + b) / 10;
    r = (l * 2 + r) / 3 + 5, g = (l * 2 + g) / 3 + 6, b = (l * 2 + b) / 3 + 9;
    dst[i] = (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10;
  }
}

static void Mp_MakePlayerPalette(uint16 *dst, const uint16 *src, int player, bool downed) {
  if (downed)
    Mp_MakeGhostPalette(dst, src, player);
  else
    Mp_MakePalette(dst, src, player);
}

uint8 g_mp_local_mask = 1;
static uint8 g_mp_render_ram[0x20000];
static MpState g_mp_render_mp;
static uint16 g_mp_saved_hud[165], g_mp_saved_hud_addr;
static bool g_mp_hud_swapped;

// The HUD's item box shows the leader's Y item. On a screen that belongs to
// another player, show that player's own item instead (drawing only: the
// game's memory is put back right after).
// Is a standing Link other than the one in RAM below this health? (The
// low-health beep is for everybody in the world.)
bool Mp_OtherLinkBelow(uint8 limit) {
  if (!Mp_Active())
    return false;
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (p != g_mp.resident && Mp_IsAlive(p) && Mp_PlayerHealth(p) < limit && Mp_PlayerHealth(p) < link_health_capacity)
      return true;
  return false;
}

// The HUD's item box (2x2 tilemap words) as the HUD would draw it for
// player p's selected item. Display only: everything is put back.
bool Mp_ItemIconTiles(int p, uint16 out[4]) {
  if (!g_mp.p[p].joined || !*Mp_PlayerRam(p, 0x202))  // hud_cur_item: none
    return false;
  static uint8 saved[0x20000];
  memcpy(saved, g_ram, sizeof(saved));
  MpState mp = g_mp;
  Mp_SwapTo(p);
  Hud_RefreshIcon();
  for (int i = 0; i < 4; i++)
    out[i] = hud_tile_indices_buffer[(1 + (i >> 1)) * 32 + 5 + (i & 1)];
  memcpy(g_ram, saved, sizeof(saved));
  g_mp = mp;
  return true;
}

static void Mp_ShowViewersItem() {
  g_mp_hud_swapped = false;
  if (!Mp_Active() || !Mp_IsPlayModule(main_module_index))
    return;
  int v = -1;
  for (int p = 0; p < kMpMaxPlayers && v < 0; p++)
    if ((g_mp_local_mask >> p & 1) && g_mp.p[p].joined)
      v = p;
  if (v < 0 || v == g_mp.leader ||
      (*Mp_PlayerRam(v, 0x202) == hud_cur_item && Mp_PlayerMagic(v) == link_magic_power))
    return;
  memcpy(g_mp_render_ram, g_ram, sizeof(g_mp_render_ram));
  g_mp_render_mp = g_mp;
  g_mp_saved_hud_addr = word_7E0219;
  memcpy(g_mp_saved_hud, &g_zenv.vram[g_mp_saved_hud_addr], sizeof(g_mp_saved_hud));
  Mp_SwapTo(v);
  Hud_RefreshIcon();
  // Only the item icon (2x2 tiles at column 5, row 1 of the HUD) and the
  // magic meter (column 3, rows 1 to 4).
  for (int y = 1; y <= 2; y++)
    for (int x = 5; x <= 6; x++)
      g_zenv.vram[g_mp_saved_hud_addr + y * 32 + x] = hud_tile_indices_buffer[y * 32 + x];
  for (int y = 1; y <= 4; y++)
    g_zenv.vram[g_mp_saved_hud_addr + y * 32 + 3] = hud_tile_indices_buffer[y * 32 + 3];
  memcpy(g_ram, g_mp_render_ram, sizeof(g_mp_render_ram));
  g_mp = g_mp_render_mp;
  g_mp_hud_swapped = true;
}

int8 g_mp_extra_set_player[kPpuMaxExtraSpriteSets];
uint8 g_mp_extra_set_link_count[kPpuMaxExtraSpriteSets];

void Mp_PrepareRender(Ppu *ppu) {
  ppu->numExtraSpriteSets = 0;
  if (!Mp_Active() && !g_mp_coop)
    return;
  Mp_ShowViewersItem();
  memcpy(g_mp_saved_cgram, &ppu->cgram[0xf0], sizeof(g_mp_saved_cgram));
  int n = 0;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    MpRenderPlayer *rp = &g_mp_render.p[p];
    if (p == g_mp.leader || !g_mp.p[p].joined || !rp->visible)
      continue;
    PpuExtraSpriteSet *set = &ppu->extraSprites[n];
    set->count = rp->count;
    set->bank = n + 1;
    memcpy(set->oam, rp->oam, rp->count * 4);
    memcpy(set->high, rp->high, rp->count);
    int na = rp->anc_count < kPpuMaxExtraSprites - rp->count ? rp->anc_count : kPpuMaxExtraSprites - rp->count;
    memcpy(set->oam + rp->count * 2, rp->anc_oam, na * 4);
    memcpy(set->high + rp->count, rp->anc_high, na);
    set->count += na;
    int dx = g_mp_draw_offset[p][0], dy = g_mp_draw_offset[p][1];
    if (dx | dy) {
      // (The projectiles too: a hookshot chain stays on its Link.)
      for (int i = 0; i < set->count; i++) {
        int x = (set->oam[i * 2] & 0xff) | (set->high[i] & 1) << 8;
        int y = set->oam[i * 2] >> 8;
        x = (x + dx) & 0x1ff, y = (y + dy) & 0xff;
        set->oam[i * 2] = (x & 0xff) | y << 8;
        set->high[i] = (set->high[i] & ~1) | x >> 8;
      }
    }
    memcpy(set->tiles, rp->tiles, sizeof(set->tiles));
    Mp_MakePlayerPalette(ppu->extraPalettes[n + 1], g_mp_saved_cgram, p, g_mp.p[p].downed);
    g_mp_extra_set_player[n] = p;
    g_mp_extra_set_link_count[n] = rp->count;
    n++;
  }
  // Links of other worlds in this same room or area, so players approaching
  // each other see each other before their worlds merge.
  InstRemoteLink rl[kMpMaxPlayers * 2];
  int nr = Inst_LinksFromOtherWorlds(rl, countof(rl));
  for (int k = 0; k < nr && n < kPpuMaxExtraSpriteSets; k++) {
    PpuExtraSpriteSet *set = &ppu->extraSprites[n];
    const MpRenderPlayer *rp = rl[k].rp;
    set->count = 0;
    set->bank = n + 1;
    for (int i = 0; i < rp->count && set->count < kPpuMaxExtraSprites; i++) {
      int x = (rp->oam[i * 2] & 0xff) | (rp->high[i] & 1) << 8, y = rp->oam[i * 2] >> 8;
      x = (x >= 256 ? x - 512 : x) + rl[k].dx;
      y = (y >= 240 ? y - 256 : y) + rl[k].dy;
      // OAM y has 8 bits: above -16 it would wrap to the bottom of the screen.
      if (x < -64 || x > 320 || y < -16 || y >= 224)
        continue;
      set->oam[set->count * 2] = (x & 0xff) | (y & 0xff) << 8;
      set->oam[set->count * 2 + 1] = rp->oam[i * 2 + 1];
      set->high[set->count] = (rp->high[i] & ~1) | (x >> 8 & 1);
      set->count++;
    }
    if (!set->count)
      continue;
    memcpy(set->tiles, rl[k].tiles, sizeof(set->tiles));
    Mp_MakePlayerPalette(ppu->extraPalettes[n + 1], g_mp_saved_cgram, rl[k].player, rl[k].downed);
    g_mp_extra_set_player[n] = rl[k].player;
    g_mp_extra_set_link_count[n] = set->count;
    n++;
  }
  ppu->numExtraSpriteSets = n;
  // Recolor the leader, who is drawn through the regular OAM.
  Mp_MakePlayerPalette(&ppu->cgram[0xf0], g_mp_saved_cgram, g_mp.leader, g_mp.p[g_mp.leader].downed);
}

void Mp_FinishRender(Ppu *ppu) {
  if (g_mp_hud_swapped) {
    memcpy(&g_zenv.vram[g_mp_saved_hud_addr], g_mp_saved_hud, sizeof(g_mp_saved_hud));
    g_mp_hud_swapped = false;
  }
  if (ppu->numExtraSpriteSets || Mp_Active() || g_mp_coop)
    memcpy(&ppu->cgram[0xf0], g_mp_saved_cgram, sizeof(g_mp_saved_cgram));
}

// ---------------------------------------------------------------------------
// Group exits: a room/screen change caused by walking only happens when every
// standing player holds that same direction. The frame is simulated, and if a
// player walked into an exit alone, it is simulated again from a RAM snapshot
// with that player standing still, which makes the exit act like a wall.

// Did the world just start to go somewhere else (a warp, a cutscene that
// ends elsewhere, the flute...), as opposed to something that happens in
// this very room: a door unlocking, shutters, a crystal switch, a pressure
// plate, the swamp draining, the item menu?
static bool Mp_LeavesRoom(uint8 m0, uint8 m1, uint8 s1) {
  if (m1 != m0)
    return m1 != 14 || s1 == 10;  // (module 14: menus and maps, but the flute's map flies off)
  if (m1 == 7)
    return !(s1 == 3 || s1 == 4 || s1 == 5 || (s1 >= 9 && s1 <= 0x0d) || s1 == 0x16 || s1 == 0x17);
  if (Mp_IsOverworldModule(m1))
    return !((s1 >= 9 && s1 <= 0x0c) || s1 == 0x2a);
  return true;
}

static bool Mp_IsTravel(uint8 m0, uint8 m1, uint8 s1) {
  if (m1 != m0) {
    // dungeon/overworld loads, spotlight exits, falling into a dungeon
    return m1 == 6 || m1 == 8 || m1 == 10 || m1 == 11 || m1 == 15 || m1 == 17;
  }
  if (m1 == 7) {
    // quadrant scroll, room change, stairs, falling, warp tiles
    return s1 == 1 || s1 == 2 || s1 == 6 || s1 == 7 || s1 == 8 || s1 == 0x0e ||
           (s1 >= 0x10 && s1 <= 0x13) || s1 == 0x15;
  }
  if (Mp_IsOverworldModule(m1)) {
    // anything but opening doors, mirror warps, drowning, weathervane
    return s1 != 0 && !(s1 >= 0x09 && s1 <= 0x0c) && s1 != 0x23 && s1 != 0x2a && s1 != 0x2c && s1 != 0x2d;
  }
  return false;
}

static uint8 Mp_HeldDirections(int p) {
  return *Mp_PlayerRam(p, 0xF0) & 0x0f;  // joypad1H_last: up, down, left, right
}

static void Mp_ClearDirections(int p) {
  *Mp_PlayerRam(p, 0xF0) &= 0xf0;  // joypad1H_last
  *Mp_PlayerRam(p, 0xF4) &= 0xf0;  // filtered_joypad_H
}

void Mp_RunRouting(void (*routing)(void)) {
  uint8 m0 = main_module_index, s0 = submodule_index;
  if (g_mp.exit_blocked_timer)
    g_mp.exit_blocked_timer--;
  // (Alone in this world in a co-op game too: the menu then still doesn't stop the world.)
  if (g_mp.independent && (Mp_Active() || g_mp_coop) && Mp_IsPlayModule(m0) && s0 == 0) {
    routing();
    // Someone went through an exit: they continue in a world of their own.
    // So does a leader starting anything else that might take the world
    // elsewhere (a mirror warp, the flute...) while a ghost lies here: the
    // ghost stays where it fell.
    bool ghost = false;
    for (int p = 0; p < kMpMaxPlayers; p++)
      ghost |= p != g_mp.leader && g_mp.p[p].joined && g_mp.p[p].downed;
    bool moved_on = main_module_index != m0 || submodule_index != s0;
    if (Mp_IsTravel(m0, main_module_index, submodule_index) ||
        (ghost && moved_on && Mp_IsAlive(g_mp.leader) && Mp_LeavesRoom(m0, main_module_index, submodule_index))) {
      g_mp.split_request = g_mp.leader;
      g_mp.split_travel = 1;
    }
    // Someone opened the item menu or a map: they look at it in a world of
    // their own, so the others play on (and each player equips their own
    // items). When it is closed, the worlds merge again.
    if (main_module_index == 14 && (submodule_index == 1 || submodule_index == 3 || submodule_index == 7)) {
      g_mp.split_request = g_mp.leader;
      g_mp.split_travel = 2;
    }
    return;
  }
  if (!g_mp.group_exits || !Mp_Active() || !(Mp_IsPlayModule(m0) && s0 == 0)) {
    routing();
    return;
  }
  // Directions everyone agrees on. (A Link held over a teammate's head, or
  // one in the item menu, goes where the others go.)
  uint8 common = 0x0f;
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (Mp_IsAlive(p) && !g_mp.carried_by[p] && !g_mp.away[p])
      common &= Mp_HeldDirections(p);

  static uint8 before[0x20000], after[0x20000];
  static MpState mp_before, mp_after;
  memcpy(before, g_ram, sizeof(before));
  mp_before = g_mp;
  routing();
  if (common || !Mp_IsTravel(m0, main_module_index, submodule_index))
    return;

  memcpy(after, g_ram, sizeof(after));
  mp_after = g_mp;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p) || mp_before.carried_by[p] ||
        !(*(p == mp_before.resident ? &before[0xF0] : &mp_before.p[p].ctx[Mp_CtxOffset(0xF0)]) & 0x0f))
      continue;
    memcpy(g_ram, before, sizeof(before));
    g_mp = mp_before;
    Mp_ClearDirections(p);
    routing();
    if (!Mp_IsTravel(m0, main_module_index, submodule_index)) {
      g_mp.exit_blocked_timer = 90;
      return;
    }
  }
  // Not caused by walking (pits, warps, scripts): let it happen.
  memcpy(g_ram, after, sizeof(after));
  g_mp = mp_after;
}

// ---------------------------------------------------------------------------
// World instance support

int Mp_CtxSize() {
  int n = 0;
  for (size_t i = 0; i < countof(kMpCtxRanges); i++)
    n += kMpCtxRanges[i].len;
  return n;
}

void Mp_ExportPlayer(int p, uint8 *ctx_out) {
  if (p == g_mp.resident)
    Mp_SaveCtx(ctx_out);
  else
    memcpy(ctx_out, g_mp.p[p].ctx, Mp_CtxSize());
}

void Mp_ImportPlayer(int p, const uint8 *ctx, uint8 downed, uint8 revive_ctr) {
  if (g_mp.p[p].joined)
    return;
  memcpy(g_mp.p[p].ctx, ctx, Mp_CtxSize());
  g_mp.p[p].joined = 1;
  g_mp.carried_by[p] = 0;
  g_mp.p[p].downed = downed;
  g_mp.p[p].revive_ctr = revive_ctr;
  if (p == g_mp.resident) {
    // Can only happen for an empty slot that was resident; load it.
    Mp_LoadCtx(ctx);
  }
}

// Removes a player from this world instance (they continue elsewhere).
void Mp_RemovePlayer(int p) {
  if (!g_mp.p[p].joined)
    return;
  Mp_ClearCoopState(p);
  if (p == g_mp.leader) {
    int nl = Mp_FindNewLeader(p);
    if (nl < 0)
      return;
    Mp_SwapTo(nl);
    g_mp.leader = nl;
  } else if (p == g_mp.resident) {
    Mp_SwapTo(g_mp.leader);
  }
  bool was_bunny = *Mp_PlayerRam(p, 0x56) != 0;  // link_is_bunny
  g_mp.p[p].joined = 0;
  Mp_HandOverObjects(p);
  Mp_BunnyColorsLeave(was_bunny);
  g_mp_render.p[p].count = 0;
}

void Mp_SetLeader(int p) {
  if (!g_mp.p[p].joined)
    return;
  Mp_SwapTo(p);
  g_mp.leader = p;
}

uint32 Mp_AreaKeyPublic() {
  return player_is_indoors << 16 | (player_is_indoors ? dungeon_room_index : overworld_area_index);
}

bool Mp_InStableGameplay() {
  return Mp_IsPlayModule(main_module_index) && submodule_index == 0 &&
         g_mp.area_key != 0xffffffff;
}
