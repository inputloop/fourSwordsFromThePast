#include "mp.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "player.h"
#include "player_oam.h"
#include "sprite.h"
#include "ancilla.h"
#include "misc.h"
#include "hud.h"
#include "features.h"
#include "assets.h"

MpState g_mp;
MpRenderState g_mp_render;

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
  {0xF36D, 1},     // link_health_current
  {0xF372, 1},     // link_hearts_filler
};

// Fields of the context that stay personal when a player is moved to the leader.
static const MpRange kMpPersonalRanges[] = {
  {0xF0, 0x0B},
  {0x202, 1},
  {0x656, 3},
  {0x303, 1},
  {0x307, 1},
  {0xF34F, 1},
  {0xF36D, 1},
  {0xF372, 1},
};

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

bool Mp_IsAlive(int p) {
  return g_mp.p[p].joined && !g_mp.p[p].downed;
}

// Assigns ownership of newly spawned ancillae and newly lifted sprites to
// the player whose context is currently resident.
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
  // Don't inherit whatever the leader was holding or doing.
  static const uint16 kClear[] = { 0x301, 0x308, 0x309, 0x37A, 0x314, 0x2F4, 0x376, 0x373, 0x3C, 0x3A, 0x5D };
  for (size_t i = 0; i < countof(kClear); i++)
    *Mp_PlayerRam(p, kClear[i]) = 0;
  *Mp_PlayerRam(p, 0x31F) = 0x20;  // countdown_for_blink
  Mp_SwapTo(r);
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
  Mp_RegroupPlayer(p);
  *Mp_PlayerRam(p, 0xF36D) = link_health_capacity ? link_health_capacity : 0x18;
  *Mp_PlayerRam(p, 0x202) = hud_cur_item;
  *Mp_PlayerRam(p, 0x303) = current_item_y;
  Mp_SwapTo(r);
}

static int Mp_FindNewLeader(int except) {
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
  if (p == g_mp.leader) {
    int nl = Mp_FindNewLeader(p);
    Mp_SwapTo(nl);
    g_mp.leader = nl;
  } else if (p == g_mp.resident) {
    Mp_SwapTo(g_mp.leader);
  }
  g_mp.p[p].joined = 0;
  for (int i = 0; i < 10; i++)
    if (g_mp.ancilla_owner[i] == p)
      g_mp.ancilla_owner[i] = g_mp.leader;
  for (int i = 0; i < 16; i++)
    if (g_mp.sprite_owner[i] == p)
      g_mp.sprite_owner[i] = g_mp.leader;
  g_mp_render.p[p].count = 0;
}

// After loading a regular save state, RAM holds a single Link. Treat it as the
// leader and pull everybody else over to it.
void Mp_OnStateLoaded() {
  g_mp.resident = g_mp.leader;
  g_mp.area_key = 0xffffffff;
  memcpy(g_mp.ancilla_seen, ancilla_type, 10);
  memcpy(g_mp.sprite_seen_state, sprite_state, 16);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (g_mp.p[p].joined && p != g_mp.leader) {
      if (!*Mp_PlayerRam(p, 0xF36D))
        *Mp_PlayerRam(p, 0xF36D) = link_health_current;
      g_mp.p[p].downed = 0;
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

void Mp_ReadJoypads() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    Mp_ReadJoypadInto(g_mp.inputs[p],
                      Mp_PlayerRam(p, 0xF0), Mp_PlayerRam(p, 0xF2), Mp_PlayerRam(p, 0xF4),
                      Mp_PlayerRam(p, 0xF6), Mp_PlayerRam(p, 0xF8), Mp_PlayerRam(p, 0xFA));
  }
}

void Mp_BeginFrame() {
  if (g_mp.resident != g_mp.leader)
    Mp_SwapToLeader();
  for (int p = 0; p < kMpMaxPlayers; p++)
    g_mp_render.p[p].visible = 0;
}

static uint32 Mp_AreaKey() {
  return player_is_indoors << 16 | (player_is_indoors ? dungeon_room_index : overworld_screen_index);
}

void Mp_EndFrame() {
  if (!Mp_Active())
    return;
  Mp_SwapToLeader();
  Mp_AttributeObjects();
  if (main_module_index == 18 || main_module_index == 23)  // death / game over
    g_mp.after_death = 1;
  // A downed leader hands over the camera to someone who is still standing.
  if (g_mp.p[g_mp.leader].downed) {
    int nl = Mp_FindNewLeader(g_mp.leader);
    if (nl >= 0 && Mp_IsAlive(nl)) {
      Mp_SwapTo(nl);
      g_mp.leader = nl;
    }
  }
}

void Mp_BeginObject() {
  g_mp.module_snapshot[0] = main_module_index;
  g_mp.module_snapshot[1] = submodule_index;
}

// Called after running a piece of game code under some player's context. If
// that code switched the game module, that player now leads.
void Mp_EndObject() {
  if (g_mp.resident != g_mp.leader &&
      (g_mp.module_snapshot[0] != main_module_index || g_mp.module_snapshot[1] != submodule_index)) {
    g_mp.leader = g_mp.resident;
  }
  Mp_SwapToLeader();
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

void Mp_PreAncilla(int k) {
  if (!Mp_Active())
    return;
  Mp_BeginObject();
  int owner = g_mp.ancilla_owner[k];
  if (!g_mp.p[owner].joined)
    owner = g_mp.leader;
  Mp_SwapTo(owner);
}

bool Mp_SuppressCamera() {
  return g_mp.in_other_update != 0;
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
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_mp.p[p].joined && p != g_mp.resident)
      *Mp_PlayerRam(p, 0x314) = 0;  // flag_is_sprite_to_pick_up
}

bool Mp_OnFatalDamage() {
  if (!Mp_Active())
    return false;
  int me = g_mp.resident;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p != me && Mp_IsAlive(p)) {
      g_mp.p[me].downed = 1;
      g_mp.p[me].revive_ctr = 0;
      link_health_current = 0;
      countdown_for_blink = 0;
      link_hearts_filler = 0;
      return true;
    }
  }
  return false;
}

static void Mp_Revive(int p, uint8 health) {
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
      Mp_Revive(p, 0x18);
      sound_effect_2 = 0x2f;
    }
  }
}

static void Mp_ClampToScreen() {
  int extra = (enhanced_features0 & kFeatures0_ExtendScreen64) ? 40 : 0;
  int x0 = BG2HOFS_copy2 - extra, x1 = BG2HOFS_copy2 + 256 - 16 + extra;
  int y0 = BG2VOFS_copy2 - 8, y1 = BG2VOFS_copy2 + 224 - 32;
  int x = (int16)(link_x_coord - BG2HOFS_copy2) + BG2HOFS_copy2;
  int y = (int16)(link_y_coord - BG2VOFS_copy2) + BG2VOFS_copy2;
  if (x < x0) link_x_coord = x0;
  if (x > x1) link_x_coord = x1;
  if (y < y0) link_y_coord = y0;
  if (y > y1) link_y_coord = y1;
}

// Called by the gameplay modules after the leader's player control ran.
void Mp_RunOtherPlayers(void (*control)(void)) {
  if (!Mp_Active() || submodule_index != 0 || (main_module_index != 7 && main_module_index != 9))
    return;
  assert(g_mp.resident == g_mp.leader);

  // While the leader sleeps through the intro the others aren't around yet.
  // They join the leader once it is standing on the floor.
  if (link_player_handler_state == kPlayerState_AsleepInBed) {
    g_mp.area_key = 0xffffffff;
    return;
  }
  if (g_mp.area_key == 0xffffffff && (link_player_handler_state != kPlayerState_Ground || link_auxiliary_state))
    return;

  uint32 key = Mp_AreaKey();
  if (key != g_mp.area_key || g_mp.after_death) {
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

  uint8 m = main_module_index, s = submodule_index;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (p == g_mp.leader || !Mp_IsAlive(p))
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
    g_mp.in_other_update = 1;
    control();
    g_mp.in_other_update = 0;
    if (main_module_index == m && submodule_index == s)
      Mp_ClampToScreen();
    // Hud_RefillLogic only runs for the leader, so apply pending hearts directly.
    if (link_hearts_filler) {
      int h = link_health_current + link_hearts_filler;
      link_health_current = h > link_health_capacity ? link_health_capacity : h;
      link_hearts_filler = 0;
    }
    Mp_EndObject();
  }
  Mp_SwapToLeader();
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
    case 0x05: case 0x1F: dmg = 2; break;
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
    LinkOam_MainOne();
    g_mp.in_other_update = 0;
    bool flicker = g_mp.p[p].downed && (frame_counter & 4);
    for (int i = 0; i < 128 && rp->count < kMpMaxExtraOam; i++) {
      OamEnt *e = &oam_buf[i];
      if (e->y == 0xf0)
        continue;
      if (flicker)
        continue;
      rp->oam[rp->count * 2 + 0] = e->x | e->y << 8;
      rp->oam[rp->count * 2 + 1] = e->charnum | e->flags << 8;
      rp->high[rp->count] = bytewise_extended_oam[i];
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

// Tunic colors for P1..P4: green, red, blue, purple (5-bit RGB).
static const uint8 kMpPlayerColors[kMpMaxPlayers][3] = {
  {6, 26, 8}, {31, 6, 6}, {7, 13, 31}, {24, 8, 31},
};
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
  const uint8 *col = kMpPlayerColors[player];
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

void Mp_PrepareRender(Ppu *ppu) {
  ppu->numExtraSpriteSets = 0;
  if (!Mp_Active())
    return;
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
    memcpy(set->tiles, rp->tiles, sizeof(set->tiles));
    Mp_MakePalette(ppu->extraPalettes[n + 1], g_mp_saved_cgram, p);
    n++;
  }
  ppu->numExtraSpriteSets = n;
  // Recolor the leader, who is drawn through the regular OAM.
  Mp_MakePalette(&ppu->cgram[0xf0], g_mp_saved_cgram, g_mp.leader);
}

void Mp_FinishRender(Ppu *ppu) {
  if (ppu->numExtraSpriteSets || Mp_Active())
    memcpy(&ppu->cgram[0xf0], g_mp_saved_cgram, sizeof(g_mp_saved_cgram));
}

// ---------------------------------------------------------------------------
// Group exits: a room/screen change caused by walking only happens when every
// standing player holds that same direction. The frame is simulated, and if a
// player walked into an exit alone, it is simulated again from a RAM snapshot
// with that player standing still, which makes the exit act like a wall.

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
  if (m1 == 9) {
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
  if (!g_mp.group_exits || !Mp_Active() || !((m0 == 7 || m0 == 9) && s0 == 0)) {
    routing();
    return;
  }
  // Directions everyone agrees on.
  uint8 common = 0x0f;
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (Mp_IsAlive(p))
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
    if (!Mp_IsAlive(p) || !(*(p == mp_before.resident ? &before[0xF0] : &mp_before.p[p].ctx[Mp_CtxOffset(0xF0)]) & 0x0f))
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
