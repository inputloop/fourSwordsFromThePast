#include "instance.h"
#include "mp.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "audio.h"
#include "util.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "snes/saveload.h"

enum {
  kRamSize = 0x20000,
  // The save file area of RAM (7EF000-7EF4FF): items, progress and world flags.
  kSharedStart = 0xF000,
  kSharedEnd = 0xF500,
  kFlagsEnd = 0xF340,     // room and overworld flags: bits are only ever set
  kKeySlots = 0xF37C,     // keys per dungeon (16 bytes)
};

typedef struct Instance {
  uint8 active;
  uint8 born;  // created during the current frame, which it already ran
  uint8 *ram;
  Ppu *ppu;
  Dma *dma;
  MpState mp;
  MpRenderState render;
  uint8 apu_ports[4];
} Instance;

static Instance g_inst[kInstMax];
enum { kMergeGrace = 60 };

// How each player last looked in normal gameplay (display only), so a player
// who opened the item menu or a map in a world of their own is still drawn
// for the others, standing where they paused.
typedef struct LastSeen {
  bool valid;
  uint32 key;
  uint16 cam_x, cam_y;
  bool downed;
  MpRenderPlayer rp;  // with the Link tiles in rp.tiles
} LastSeen;
static LastSeen g_last_seen[kMpMaxPlayers];
static void RecordLastSeen();
static int g_cur;
static bool g_ff;
static bool g_independent = true;
static int g_audio_player;
static int g_audio_inst = -1;
static uint8 g_shared[kSharedEnd - kSharedStart];  // progress as of the last sync

// Bits the game clears again on every overworld transition (the dam switch
// pulled, the swamp drained): they belong to one world, like the water
// itself, instead of being OR-ed together with the flags that only ever get
// set.
static uint8 TransientBits(int addr) {
  return addr == 0xF2BB || addr == 0xF2FB ? 0x20 : addr == 0xF216 ? 0x80 : addr == 0xF051 ? 0x01 : 0;
}
static const uint16 kTransientAddrs[] = { 0xF2BB, 0xF2FB, 0xF216, 0xF051 };


// Pre-frame copy of an instance, so a frame can be replayed without a player.
static uint8 g_snap_ram[kRamSize];
static Ppu g_snap_ppu;
static Dma g_snap_dma;
static MpState g_snap_mp;
static MpRenderState g_snap_render;
static uint8 g_snap_ports[4];

static bool g_audio_output = true;

bool g_inst_trace;
#define TRACE(...) do { if (g_inst_trace) printf(__VA_ARGS__); } while (0)
static bool Inst_OtherInstanceAlive();
static void Inst_CollapseIntoCurrent();
static void Inst_Sync();
static bool Inst_NeighbourCamera(uint16 *cx, uint16 *cy);

static MpState *MpOf(int i) {
  return i == g_cur ? &g_mp : &g_inst[i].mp;
}

static void Bind(int i) {
  Instance *n = &g_inst[i];
  g_ram = n->ram;
  g_zenv.ram = n->ram;
  g_zenv.ppu = n->ppu;
  g_zenv.vram = n->ppu->vram;
  g_zenv.dma = n->dma;
  g_mp = n->mp;
  g_mp_render = n->render;
  ZeldaSetApuPorts(n->apu_ports);
  g_cur = i;
}

static void Unbind() {
  Instance *o = &g_inst[g_cur];
  o->mp = g_mp;
  o->render = g_mp_render;
  ZeldaGetApuPorts(o->apu_ports);
}

void Inst_Switch(int i) {
  if (i == g_cur || i < 0 || i >= kInstMax || !g_inst[i].active)
    return;
  Unbind();
  Bind(i);
}

// Allocates the memory of instance i. The PPU and DMA start as copies of the
// current ones, so tables set up by ppu_init are valid.
static void Alloc(int i) {
  Instance *n = &g_inst[i];
  if (!n->ram) {
    n->ram = (uint8 *)calloc(kRamSize, 1);
    n->ppu = (Ppu *)malloc(sizeof(Ppu));
    n->dma = (Dma *)malloc(sizeof(Dma));
    *n->ppu = *g_zenv.ppu;
    *n->dma = *g_zenv.dma;
  }
}

void Inst_Init() {
  memset(g_inst, 0, sizeof(g_inst));
  g_inst[0].active = 1;
  g_inst[0].ram = g_ram;
  g_inst[0].ppu = g_zenv.ppu;
  g_inst[0].dma = g_zenv.dma;
  g_cur = 0;
  g_audio_inst = 0;
  g_mp_other_instance_alive = &Inst_OtherInstanceAlive;
  g_mp_neighbour_camera = &Inst_NeighbourCamera;
  g_mp_state_loaded_hook = &Inst_CollapseIntoCurrent;
  memcpy(g_shared, g_ram + kSharedStart, sizeof(g_shared));
}

bool Inst_IsActive(int i) {
  return i >= 0 && i < kInstMax && g_inst[i].active;
}

int Inst_Count() {
  int n = 0;
  for (int i = 0; i < kInstMax; i++)
    n += g_inst[i].active;
  return n;
}

int Inst_Current() {
  return g_cur;
}

// A player in the item menu or a map is in two worlds: its Link stands in
// the world it came from ("away" there), and the menu runs in a world of its
// own. Inst_OfPlayer is where the Link is; Inst_ViewOfPlayer what the player
// sees and controls.
static int FindPlayer(int p, bool away) {
  if (p < 0 || p >= kMpMaxPlayers)
    return -1;
  for (int i = 0; i < kInstMax; i++)
    if (g_inst[i].active && MpOf(i)->p[p].joined && MpOf(i)->away[p] == away)
      return i;
  return -1;
}

int Inst_OfPlayer(int p) {
  int i = FindPlayer(p, true);
  return i >= 0 ? i : FindPlayer(p, false);
}

int Inst_ViewOfPlayer(int p) {
  int i = FindPlayer(p, false);
  return i >= 0 ? i : FindPlayer(p, true);
}

bool Inst_PlayerInMenu(int p) {
  return FindPlayer(p, true) >= 0;
}

bool Inst_IsJoined(int p) {
  return Inst_OfPlayer(p) >= 0;
}

int Inst_NumJoined() {
  int n = 0;
  for (int p = 0; p < kMpMaxPlayers; p++)
    n += Inst_IsJoined(p);
  return n;
}

static bool HasAlive(int i) {
  MpState *m = MpOf(i);
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (m->p[p].joined && !m->p[p].downed)
      return true;
  return false;
}


// Another world in this same area or room whose camera has this world's
// leader in view: its camera position, for a lone player's camera to close
// in on, so the worlds can merge without the view jumping. (The other
// world's memory is read directly; every machine holds all the worlds, so
// this is the same everywhere.)
static bool Inst_NeighbourCamera(uint16 *cx, uint16 *cy) {
  if (!Mp_InStableGameplay() || flag_is_link_immobilized)
    return false;
  uint32 key = Mp_AreaKeyPublic();
  int lx = Mp_PlayerX(g_mp.leader), ly = Mp_PlayerY(g_mp.leader);
  for (int i = 0; i < kInstMax; i++) {
    if (i == g_cur || !g_inst[i].active)
      continue;
    const uint8 *r = g_inst[i].ram;
    const MpState *m = &g_inst[i].mp;
    bool indoors = r[0x1B] != 0;  // player_is_indoors
    uint32 k = indoors << 16 | (indoors ? *(uint16 *)&r[0xA0] : *(uint16 *)&r[0x40A]);  // dungeon_room_index / overworld_area_index
    // (A world of ghosts waiting in plain gameplay counts too: the player
    // coming to revive them closes in on it the same way.)
    if (k != key || !((r[0x10] == 7 || r[0x10] == 9) && r[0x11] == 0) || m->area_key == 0xffffffff ||
        r[0x2E4])  // flag_is_link_immobilized
      continue;
    int cam_x = *(uint16 *)&r[0xE2], cam_y = *(uint16 *)&r[0xE8];  // BG2HOFS_copy2, BG2VOFS_copy2
    int sx = (int16)(lx - cam_x), sy = (int16)(ly - cam_y);
    if (sx < 8 || sx > 256 - 24 || sy < 8 || sy > 224 - 24)
      continue;
    *cx = cam_x, *cy = cam_y;
    return true;
  }
  return false;
}

// Is someone standing in another world? (A player in the item menu counts
// where its Link is, not in the menu's world: if that Link goes down, the
// menu doesn't keep the game going.)
static bool Inst_OtherInstanceAlive() {
  for (int i = 0; i < kInstMax; i++) {
    if (i == g_cur || !g_inst[i].active)
      continue;
    MpState *m = MpOf(i);
    for (int p = 0; p < kMpMaxPlayers; p++)
      if (m->p[p].joined && !m->p[p].downed && (m->away[p] || FindPlayer(p, true) < 0))
        return true;
  }
  return false;
}

static void UpdateCoopFlag() {
  g_mp_coop = Inst_NumJoined() > 1;
}

void Inst_Join(int p) {
  if (p < 0 || p >= kMpMaxPlayers || Inst_IsJoined(p))
    return;
  // New players appear next to the lowest numbered player who is standing.
  int target = -1;
  for (int q = 0; q < kMpMaxPlayers && target < 0; q++)
    if (Inst_IsJoined(q) && !Inst_PlayerDowned(q))
      target = Inst_OfPlayer(q);
  for (int q = 0; q < kMpMaxPlayers && target < 0; q++)
    target = Inst_OfPlayer(q);
  int r = g_cur;
  Inst_Switch(target < 0 ? g_cur : target);
  Mp_Join(p);
  Inst_Switch(r);
  UpdateCoopFlag();
}

static void Deactivate(int i) {
  assert(i != g_cur);
  g_inst[i].active = 0;
  g_inst[i].born = 0;
}

// The fields of a player that the item menu changes (and so go back with it
// to where its Link stands).
// (0x303, current_item_y, is what the Y button uses: hud_cur_item alone only
// changes what the box shows.)
static const uint16 kMenuChoices[] = { 0x202, 0x303, 0x656, 0x657, 0x658, 0xF34F };

// Ends a player's item menu or map world: with keep_choices, the items it
// picked there are carried back to its Link.
static void CloseMenu(int p, bool keep_choices) {
  int body = FindPlayer(p, true), menu = FindPlayer(p, false);
  if (body < 0)
    return;
  int r = g_cur;
  if (menu >= 0 && keep_choices) {
    uint8 v[countof(kMenuChoices)];
    Inst_Switch(menu);
    for (size_t k = 0; k < countof(kMenuChoices); k++)
      v[k] = *Mp_PlayerRam(p, kMenuChoices[k]);
    Inst_Switch(body);
    for (size_t k = 0; k < countof(kMenuChoices); k++)
      *Mp_PlayerRam(p, kMenuChoices[k]) = v[k];
  }
  MpOf(body)->away[p] = 0;
  if (menu >= 0) {
    Inst_Switch(body);
    Deactivate(menu);
    if (r == menu)
      r = body;
  }
  Inst_Switch(r);
}

// Menus that were closed this frame.
static void CloseFinishedMenus() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    int menu = FindPlayer(p, false), body = FindPlayer(p, true);
    // (Also when its Link went down meanwhile: the player sees the ghost.)
    if (menu < 0 || body < 0 || (g_inst[menu].ram[0x10] == 14 && !MpOf(body)->p[p].downed))  // main_module_index: still in it
      continue;
    TRACE("  P%d closed its menu in world %d\n", p + 1, menu);
    CloseMenu(p, true);
  }
}

void Inst_Leave(int p) {
  CloseMenu(p, false);
  int i = Inst_OfPlayer(p);
  if (i < 0 || Inst_NumJoined() <= 1)
    return;
  int r = g_cur;
  Inst_Switch(i);
  Mp_RecordLeaving(p);
  if (Mp_NumJoined() > 1) {
    Mp_Leave(p);
  } else {
    // Last one in this world: the world goes away with them.
    MpOf(i)->p[p].joined = 0;
    int other = -1;
    for (int k = 0; k < kInstMax && other < 0; k++)
      if (k != i && g_inst[k].active)
        other = k;
    Inst_Switch(other);
    Deactivate(i);
    if (r == i)
      r = other;
  }
  // The last one on its feet left: nobody could get the ghosts up again, so
  // they get up by themselves, with three hearts, as after a game over.
  bool standing = false;
  for (int q = 0; q < kMpMaxPlayers; q++)
    standing |= Inst_IsJoined(q) && !Inst_PlayerDowned(q);
  if (!standing) {
    for (int k = 0; k < kInstMax; k++) {
      if (!g_inst[k].active)
        continue;
      Inst_Switch(k);
      for (int q = 0; q < kMpMaxPlayers; q++)
        if (g_mp.p[q].joined && g_mp.p[q].downed)
          Mp_Revive(q, 0x18);
    }
  }
  Inst_Switch(r);
  UpdateCoopFlag();
}

void Inst_SetFriendlyFire(bool on) { g_ff = on; }
bool Inst_GetFriendlyFire() { return g_ff; }
void Inst_SetIndependent(bool on) { g_independent = on; }
bool Inst_GetIndependent() { return g_independent; }

void Inst_SetAudioPlayer(int p) {
  if (p >= 0 && p < kMpMaxPlayers)
    g_audio_player = p;
}

static int FreeSlot() {
  for (int i = 0; i < kInstMax; i++)
    if (!g_inst[i].active)
      return i;
  return -1;
}

static void TakeSnapshot() {
  memcpy(g_snap_ram, g_ram, kRamSize);
  g_snap_ppu = *g_zenv.ppu;
  g_snap_dma = *g_zenv.dma;
  g_snap_mp = g_mp;
  g_snap_render = g_mp_render;
  ZeldaGetApuPorts(g_snap_ports);
}

static void RestoreSnapshot() {
  memcpy(g_ram, g_snap_ram, kRamSize);
  *g_zenv.ppu = g_snap_ppu;
  *g_zenv.dma = g_snap_dma;
  g_mp = g_snap_mp;
  g_mp_render = g_snap_render;
  ZeldaSetApuPorts(g_snap_ports);
}

// Copies the current instance into slot j, keeping only player p there.
// A follower (Zelda, the old man...) stays in only one of the two worlds.
// Copies this world into slot j for player p (and keep, the teammate p
// carries, or -1); everybody else is removed there.
static void SplitOff(int j, int p, bool follower_goes_along, int keep) {
  int i = g_cur;
  Alloc(j);
  Instance *d = &g_inst[j];
  memcpy(d->ram, g_ram, kRamSize);
  *d->ppu = *g_zenv.ppu;
  *d->dma = *g_zenv.dma;
  d->mp = g_mp;
  d->render = g_mp_render;
  ZeldaGetApuPorts(d->apu_ports);
  d->active = 1;
  d->born = 1;
  Inst_Switch(j);
  Mp_SetLeader(p);
  for (int q = 0; q < kMpMaxPlayers; q++)
    if (q != p && q != keep)
      Mp_RemovePlayer(q);
  g_mp.split_request = -1;
  if (!follower_goes_along)
    follower_indicator = 0;
  Inst_Switch(i);
}

static void ApplySettings() {
  g_mp.friendly_fire = g_ff;
  g_mp.independent = g_independent;
  g_mp.group_exits = !g_independent;
}

static bool RunInstance(int i, const uint16 *inputs) {
  Inst_Switch(i);
  ApplySettings();
  Mp_SetInputs(inputs);
  bool can_split = g_mp.independent && Mp_NumJoined() > 1;
  // In co-op the item menu and the maps don't stop the world, even for a
  // player alone in it: the menu gets a world of its own.
  bool menus_live = g_mp.independent && g_mp_coop;
  if (can_split || menus_live)
    TakeSnapshot();
  bool replay = false;
  for (;;) {
    g_mp.split_request = -1;
    replay |= ZeldaRunFrame(g_mp.away[g_mp.leader] ? 0 : inputs[g_mp.leader]);
    int p = g_mp.split_request, j;
    if (p < 0 || (j = FreeSlot()) < 0)
      break;
    uint8 travel = g_mp.split_travel;
    if (travel == 2) {
      if (!menus_live)
        break;
      // The menu continues in the new world; here the frame is played again
      // without it, with this Link standing still until the menu closes.
      TRACE("  P%d opens a menu in world %d (from %d)\n", p + 1, j, i);
      SplitOff(j, p, false, -1);
      MpOf(j)->away[p] = 0;
      RestoreSnapshot();
      g_mp.away[p] = 1;
      // (The buttons were read during the last frame's vblank: forget them.)
      for (int a = 0xF0; a < 0xFB; a++)
        *Mp_PlayerRam(p, a) = 0;
      // The camera and doors follow someone who is playing.
      for (int q = 0; q < kMpMaxPlayers && g_mp.leader == p; q++)
        if (q != p && Mp_IsAlive(q) && !g_mp.away[q])
          Mp_SetLeader(q);
      TakeSnapshot();
      continue;
    }
    // The teammate p held over its head comes along. (As the frame started:
    // an exit's transition has already set it down. With nobody else left
    // behind there is nothing to split: the whole world travels.)
    int q = -1;
    for (int k = 0; k < kMpMaxPlayers; k++)
      if (g_snap_mp.carried_by[k] == p + 1 && g_mp.p[k].joined && k != p)
        q = k;
    if (!can_split || Mp_NumJoined() - (q >= 0) < 2)
      break;
    TRACE("  split P%d from world %d into %d travel=%d module=%d/%d\n", p + 1, i, j, travel, main_module_index, submodule_index);
    // The follower follows whoever led the group before this frame.
    // (Not into a menu: split_travel 2.)
    bool follower_goes_along = travel == 1 && p == g_snap_mp.leader;
    SplitOff(j, p, follower_goes_along, q);
    if (!travel) {
      // The new world's camera still has to catch up with its Link; until
      // then everybody looks in view of both cameras, and the worlds would
      // merge right back (with the camera jumping from one leader to the
      // other), only to split again.
      g_mp.merge_grace = g_inst[j].mp.merge_grace = kMergeGrace;
      Mp_RemovePlayer(p);
      if (q >= 0)
        Mp_RemovePlayer(q);
      break;
    }
    // The traveler continues in the new world. Everybody else replays the
    // frame as if they had never been here.
    RestoreSnapshot();
    Mp_RemovePlayer(p);
    if (q >= 0)
      Mp_RemovePlayer(q);
    if (follower_goes_along)
      follower_indicator = 0;
    can_split = Mp_NumJoined() > 1;
    TakeSnapshot();
  }
  g_mp.split_request = -1;
  return replay;
}

// Whatever died in world |from| (another copy of the same area or room) dies
// here too, so nobody meets an enemy a teammate already killed. Sprites far
// off-screen that the game merely put away (Sprite_KillSelf) are not kills;
// what's left of a kill here, a prize it dropped, stays. Dynamic spawns
// (no place in the area's sprite list) are not matched: they are in neither
// record. Call in world |to|, with the kills gathered by GatherSpriteKills.
typedef struct SpriteKills {
  uint32 key;               // the area or room the kills are of (Mp_AreaKeyPublic)
  bool indoors;
  uint16 room;
  uint16 room_bits[0x800];  // sprite_where_in_room: every room of this dungeon visit (indoors)
  uint8 area_bits[0x200];   // overworld_sprite_killed (outdoors)
} SpriteKills;

static void GatherSpriteKills(SpriteKills *sk) {
  sk->key = Mp_AreaKeyPublic();
  sk->indoors = player_is_indoors != 0;
  sk->room = dungeon_room_index2;
  memcpy(sk->room_bits, sprite_where_in_room, sizeof(sk->room_bits));
  memcpy(sk->area_bits, overworld_sprite_killed, sizeof(sk->area_bits));
}

static bool IsPrizeSprite(int k) {
  return sprite_type[k] >= 0xd8 && sprite_type[k] <= 0xe6;
}

static void ApplySpriteKills(const SpriteKills *sk) {
  // (Only another copy of the same place: everybody also comes together at
  // the title screen or a game over, from anywhere.)
  if (sk->key != Mp_AreaKeyPublic() || sk->indoors != (player_is_indoors != 0) ||
      (sk->indoors && sk->room != dungeon_room_index2))
    return;
  int killed = 0;
  if (sk->indoors) {
    // Same room means same dungeon: the record is cleared on entering one, so
    // what |from| killed in the dungeon's other rooms is a kill here too.
    for (int i = 0; i < 0x800; i++)
      sprite_where_in_room[i] |= sk->room_bits[i];
    uint16 bits = sk->room_bits[sk->room];
    for (int k = 0; k < 16; k++) {
      if (!sprite_state[k] || sprite_state[k] >= 10 || sign8(sprite_N[k]) || (sprite_defl_bits[k] & 1) || IsPrizeSprite(k))
        continue;
      if (bits & (1 << sprite_N[k]))
        sprite_state[k] = 0, killed++;
    }
  } else {
    for (int i = 0; i < 0x200; i++) {
      overworld_sprite_killed[i] |= sk->area_bits[i];
      // (Not loaded here yet: don't let the camera find it later either.)
      overworld_sprite_was_loaded[i] |= sk->area_bits[i];
    }
    for (int k = 0; k < 16; k++) {
      uint16 blk = sprite_N_word[k];
      if (!sprite_state[k] || sprite_state[k] >= 10 || blk >= 0x1000 || IsPrizeSprite(k))
        continue;
      if (sk->area_bits[blk >> 3] & (0x80 >> (blk & 7)))
        sprite_state[k] = 0, killed++;
    }
  }
  if (killed)
    TRACE("  %d sprites killed in the other world die here\n", killed);
}

static void MovePlayers(int from, int to) {
  TRACE("  move players of world %d into %d\n", from, to);
  uint8 ctx[kMpMaxPlayers][kMpCtxMaxBytes];
  uint8 joined[kMpMaxPlayers] = { 0 }, downed[kMpMaxPlayers], ctr[kMpMaxPlayers], away[kMpMaxPlayers];
  uint8 transient[countof(kTransientAddrs)];
  Inst_Switch(from);
  // Whoever drained the swamp keeps it drained when met by a teammate.
  for (size_t k = 0; k < countof(kTransientAddrs); k++)
    transient[k] = g_ram[kTransientAddrs[k]] & TransientBits(kTransientAddrs[k]);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    joined[p] = 1;
    downed[p] = g_mp.p[p].downed;
    ctr[p] = g_mp.p[p].revive_ctr;
    away[p] = g_mp.away[p];
    Mp_ExportPlayer(p, ctx[p]);
  }
  SpriteKills kills;
  GatherSpriteKills(&kills);
  Inst_Switch(to);
  ApplySpriteKills(&kills);
  Deactivate(from);
  for (size_t k = 0; k < countof(kTransientAddrs); k++)
    g_ram[kTransientAddrs[k]] |= transient[k];
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!joined[p])
      continue;
    // (A player both here and there was in a menu: the menu ends.)
    bool both = g_mp.p[p].joined;
    Mp_ImportPlayer(p, ctx[p], downed[p], ctr[p]);
    g_mp.away[p] = both ? 0 : away[p];
  }
}

typedef struct InstView {
  bool stable;
  bool playing;  // plain gameplay: a lone player's camera can close in on this world's
  bool has_follower;
  uint32 key;
  uint16 cam_x, cam_y;
} InstView;

static void GetView(int i, InstView *v) {
  Inst_Switch(i);
  v->stable = Mp_InStableGameplay() && !flag_is_link_immobilized;
  // A world of ghosts can't press A: one stuck on a text box (Zelda calling
  // out, say) is still ready to be joined.
  if (!v->stable && !HasAlive(i) && main_module_index == 14)
    v->stable = true;
  v->playing = Mp_InStableGameplay() && !flag_is_link_immobilized;
  v->has_follower = follower_indicator != 0;
  v->key = Mp_AreaKeyPublic();
  v->cam_x = BG2HOFS_copy2;
  v->cam_y = BG2VOFS_copy2;
}

// Can everyone in instance b join instance a? They must be standing in a's
// room, in view of a's camera, and not in the middle of something.
static bool CanMerge(int a, const InstView *va, int b, const InstView *vb) {
  if (!va->stable || !vb->stable || va->key != vb->key)
    return false;
  // A world of ghosts only takes in the living to keep its follower.
  if (!HasAlive(a) && !(va->has_follower && HasAlive(b)))
    return false;
  // Ghosts lying in this room show up as soon as someone standing is here.
  if (!HasAlive(b))
    return true;
  // (A world of ghosts is paused, so its grace would never run out.)
  if ((MpOf(a)->merge_grace && HasAlive(a)) || (MpOf(b)->merge_grace && HasAlive(b)))
    return false;
  // Only once the cameras are nearly in the same place, so nobody's view
  // jumps. (A lone player's camera closes in on the other world's by itself
  // while both play on, see Inst_NeighbourCamera, so with one of them alone
  // the jump can be kept really small, whichever side it is on.)
  int na = 0, nb = 0;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    na += MpOf(a)->p[p].joined && !MpOf(a)->p[p].downed && !MpOf(a)->away[p];
    nb += MpOf(b)->p[p].joined && !MpOf(b)->p[p].downed && !MpOf(b)->away[p];
  }
  int tol = (na == 1 || nb == 1) && va->playing && vb->playing ? 12 : 48;
  if (abs((int16)(va->cam_x - vb->cam_x)) > tol || abs((int16)(va->cam_y - vb->cam_y)) > tol)
    return false;
  Inst_Switch(b);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    int sx = (int16)(Mp_PlayerX(p) - va->cam_x), sy = (int16)(Mp_PlayerY(p) - va->cam_y);
    // (Well inside the edges where a Link gets a world of its own, so the
    // worlds don't split and merge back and forth.)
    if (sx < 8 || sx > 256 - 24 || sy < 8 || sy > 224 - 24)
      return false;
    // Not while held over a teammate's head or flying from a throw: the
    // other world knows nothing about it and would set the Link down.
    if (g_mp.carried_by[p] || g_mp.thrown[p])
      return false;
    if (g_mp.p[p].downed)
      continue;
    if (*Mp_PlayerRam(p, 0x5D) != 0 ||   // link_player_handler_state: ground
        *Mp_PlayerRam(p, 0x4D) != 0 ||   // link_auxiliary_state: recoil, falling...
        *Mp_PlayerRam(p, 0x308) != 0 ||  // carrying something
        *Mp_PlayerRam(p, 0x309) != 0 ||
        *Mp_PlayerRam(p, 0x301) != 0 ||  // item in hand
        *Mp_PlayerRam(p, 0x37A) != 0)
      return false;
  }
  return true;
}

// Is a standing player of world o within reviving distance of a ghost of world f?
static bool ReviverNearby(int o, int f) {
  uint16 gx[kMpMaxPlayers], gy[kMpMaxPlayers];
  bool ghost[kMpMaxPlayers] = { 0 };
  Inst_Switch(f);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (g_mp.p[p].joined && g_mp.p[p].downed)
      ghost[p] = true, gx[p] = Mp_PlayerX(p), gy[p] = Mp_PlayerY(p);
  }
  Inst_Switch(o);
  for (int q = 0; q < kMpMaxPlayers; q++) {
    if (!Mp_IsAlive(q))
      continue;
    for (int p = 0; p < kMpMaxPlayers; p++)
      if (ghost[p] && abs((int16)(Mp_PlayerX(q) - gx[p])) < 24 && abs((int16)(Mp_PlayerY(q) - gy[p])) < 24)
        return true;
  }
  return false;
}

static void MergeAndCleanUp() {
  // Somebody went back to the title screen or reached the ending: everybody comes along.
  for (int i = 0; i < kInstMax; i++) {
    if (!g_inst[i].active || Inst_Count() < 2)
      continue;
    Inst_Switch(i);
    uint8 m = main_module_index;
    bool fairy = false;
    for (int k = 0; k < 4; k++)
      fairy |= link_bottle_info[k] == 6;
    // Game over (the last standing Link died without a fairy) also brings
    // everybody together, so they all continue from the same place.
    if (m < 6 || m == 0x14 || m == 0x19 || m == 0x1a || (m == 18 && !fairy)) {
      TRACE("  collapse into world %d (module %d)\n", i, m);
      Inst_CollapseIntoCurrent();
      return;
    }
  }
  // Groups that meet again play in one world.
  for (int a = 0; a < kInstMax; a++) {
    for (int b = a + 1; b < kInstMax; b++) {
      if (!g_inst[a].active || !g_inst[b].active)
        continue;
      InstView va, vb;
      GetView(a, &va);
      GetView(b, &vb);
      // The world with a follower (Zelda, the old man...) is the one that
      // stays, even when only a ghost is left there. Otherwise a world of
      // standing players always outlasts one of ghosts.
      if (va.has_follower != vb.has_follower) {
        int f = va.has_follower ? a : b, o = f == a ? b : a;
        const InstView *vf = f == a ? &va : &vb, *vo = f == a ? &vb : &va;
        if (CanMerge(f, vf, o, vo)) {
          TRACE("  merge %d into %d key=%x (follower)\n", o, f, vf->key);
          MovePlayers(o, f);
        } else if (HasAlive(f) || !ReviverNearby(o, f)) {
          // Wait until the living get close enough to the follower's world.
        } else if (CanMerge(o, vo, f, vf)) {
          // Someone is right next to the ghost but the views won't line up:
          // reviving matters more than the follower.
          TRACE("  merge %d into %d key=%x (follower lost)\n", f, o, vf->key);
          MovePlayers(f, o);
        }
        continue;
      }
      bool b_first = !HasAlive(a);
      if (!b_first && CanMerge(a, &va, b, &vb)) {
        TRACE("  merge %d into %d key=%x\n", b, a, va.key);
        MovePlayers(b, a);
      } else if (CanMerge(b, &vb, a, &va)) {
        TRACE("  merge %d into %d key=%x\n", a, b, va.key);
        MovePlayers(a, b);
      }
    }
  }
}

static bool IsCounter(int addr) {
  return addr == 0xF343 || addr == 0xF377 ||
         (addr >= kKeySlots && addr < kKeySlots + 16);
}

static bool IsPerWorld(int addr) {
  return addr == 0xF34F || addr == 0xF36D || addr == 0xF36E ||  // personal
         addr == 0xF372 ||
         addr == 0xF36F ||                                       // keys of the current dungeon
         addr == 0xF373 || addr == 0xF375 || addr == 0xF376 ||   // refills in progress
         addr == 0xF3CA ||                                       // light/dark world
         (addr >= 0xF3CC && addr <= 0xF3D3) ||                   // follower
         addr == 0xF360 || addr == 0xF361;                       // rupees: handled separately
}

static int KeySlot() {
  uint8 d = BYTE(cur_palace_index_x2);
  if (d == 0xff || link_num_keys == 0xff)
    return -1;
  return kKeySlots + ((d == 2 ? 0 : d) >> 1);
}

// Shares progress between worlds: whatever changed in one world since the
// last sync is applied to all of them.
static void Inst_Sync() {
  int n = Inst_Count();
  uint8 *base = g_shared;
  uint8 merged[kSharedEnd - kSharedStart];
  memcpy(merged, base, sizeof(merged));
  int delta[kSharedEnd - kSharedStart] = { 0 };
  int rupee_delta = 0;
  for (int i = 0; i < kInstMax; i++) {
    if (!g_inst[i].active)
      continue;
    Inst_Switch(i);
    int ks = KeySlot();
    if (ks >= 0)
      g_ram[ks] = link_num_keys;
    // A world split off this frame is mostly a copy of one that reports the
    // same changes, so it doesn't report counters (they would count twice).
    // Its flag bits are still welcome (OR-ing them twice changes nothing):
    // after an exit the traveler's frame was replayed without it in the old
    // world, so what that frame flushed (the room's keys taken, doors
    // opened) only exists here.
    bool born = g_inst[i].born;
    const uint8 *r = g_ram + kSharedStart;
    if (!born)
      rupee_delta += (int16)(link_rupees_goal - WORD(base[0xF360 - kSharedStart]));
    for (int j = 0; j < kSharedEnd - kSharedStart; j++) {
      int addr = kSharedStart + j;
      if (r[j] == base[j] || IsPerWorld(addr))
        continue;
      if (n > 1 && IsCounter(addr)) {
        if (!born)
          delta[j] += r[j] - base[j];
      } else if (n > 1 && addr < kFlagsEnd) {
        merged[j] |= r[j] & ~TransientBits(addr);
      } else if (!born) {
        merged[j] = r[j];
      }
    }
  }
  for (int j = 0; j < kSharedEnd - kSharedStart; j++) {
    if (delta[j]) {
      int v = base[j] + delta[j];
      merged[j] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
  }
  int rupees = WORD(base[0xF360 - kSharedStart]) + rupee_delta;
  WORD(merged[0xF360 - kSharedStart]) = rupees < 0 ? 0 : rupees > 999 ? 999 : rupees;
  for (int i = 0; i < kInstMax; i++) {
    if (!g_inst[i].active)
      continue;
    Inst_Switch(i);
    uint8 *r = g_ram + kSharedStart;
    for (int j = 0; j < kSharedEnd - kSharedStart; j++) {
      if (!IsPerWorld(kSharedStart + j) || j == 0xF360 - kSharedStart || j == 0xF361 - kSharedStart) {
        uint8 m = TransientBits(kSharedStart + j);
        r[j] = (merged[j] & ~m) | (r[j] & m);
      }
    }
    int ks = KeySlot();
    if (ks >= 0)
      link_num_keys = g_ram[ks];
  }
  memcpy(g_shared, merged, sizeof(merged));
}

bool Inst_RunFrame(const uint16 *inputs) {
  UpdateCoopFlag();
  for (int i = 0; i < kInstMax; i++)
    g_inst[i].born = 0;
  bool replay = false;
  g_apu_push_enabled = false;
  static const uint16 kNoInputs[kMpMaxPlayers];
  for (int i = 0; i < kInstMax; i++) {
    if (!g_inst[i].active || g_inst[i].born)
      continue;
    if (HasAlive(i)) {
      replay |= RunInstance(i, inputs);
    } else {
      // Only ghosts here: the world waits, frozen, for someone to come by.
      // It first settles into normal gameplay so it can be joined.
      Inst_Switch(i);
      if (!Mp_InStableGameplay())
        replay |= RunInstance(i, kNoInputs);
    }
  }
  g_apu_push_enabled = true;
  CloseFinishedMenus();
  if (Inst_Count() > 1) {
    if (g_independent) {
      MergeAndCleanUp();
    } else {
      // Shared screen mode: everybody plays in the world of the lowest player.
      int first = -1;
      for (int p = 0; p < kMpMaxPlayers && first < 0; p++)
        first = Inst_OfPlayer(p);
      Inst_Switch(first);
      Inst_CollapseIntoCurrent();
    }
  }
  Inst_Sync();
  RecordLastSeen();
  // Hear (and by default show) the world of the local player.
  int ai = Inst_ViewOfPlayer(g_audio_player);
  for (int p = 0; p < kMpMaxPlayers && ai < 0; p++)
    ai = Inst_ViewOfPlayer(p);
  if (ai < 0)
    ai = g_cur;
  if (ai != g_audio_inst && g_audio_inst >= 0 && g_inst[ai].active) {
    // Ambient and sound effect ports were last set by the old world; don't
    // replay the new world's last sounds.
    uint8 old_ports[4];
    Inst_Switch(g_audio_inst >= 0 && g_inst[g_audio_inst].active ? g_audio_inst : ai);
    ZeldaGetApuPorts(old_ports);
    Inst_Switch(ai);
    uint8 ports[4];
    ZeldaGetApuPorts(ports);
    memcpy(ports + 1, old_ports + 1, 3);
    ZeldaSetApuPorts(ports);
  }
  g_audio_inst = ai;
  Inst_Switch(ai);
  if (g_audio_output)
    ZeldaPushApuState();
  return replay;
}

static void Inst_CollapseIntoCurrent() {
  int c = g_cur;
  for (int i = 0; i < kInstMax; i++)
    if (i != c && g_inst[i].active)
      MovePlayers(i, c);
  Inst_Switch(c);
  g_mp.area_key = 0xffffffff;  // regroup everybody
  memcpy(g_shared, g_ram + kSharedStart, sizeof(g_shared));
}

// ---------------------------------------------------------------------------
// Snapshots and hashing

typedef struct Reader {
  const uint8 *p, *end;
  bool fail;
} Reader;

static void ReadFunc(void *ctx, void *data, size_t size) {
  Reader *r = (Reader *)ctx;
  if ((size_t)(r->end - r->p) < size) {
    r->fail = true;
    memset(data, 0, size);
    return;
  }
  memcpy(data, r->p, size);
  r->p += size;
}

static void WriteFunc(void *ctx, void *data, size_t size) {
  ByteArray_AppendData((ByteArray *)ctx, (const uint8 *)data, size);
}

static void SaveLoadExtra(SaveLoadFunc *func, void *ctx) {
  func(ctx, g_ram, kRamSize);
  dma_saveload(g_zenv.dma, func, ctx);
  ppu_saveload(g_zenv.ppu, func, ctx);
  func(ctx, &g_mp, sizeof(g_mp));
  uint8 ports[4];
  ZeldaGetApuPorts(ports);
  func(ctx, ports, 4);
  ZeldaSetApuPorts(ports);
}

void Inst_SaveState(ByteArray *arr) {
  int r = g_cur;
  uint8 mask = 0;
  int first = -1;
  for (int i = 0; i < kInstMax; i++) {
    if (g_inst[i].active) {
      mask |= 1 << i;
      if (first < 0)
        first = i;
    }
  }
  ByteArray_AppendByte(arr, mask);
  ByteArray_AppendByte(arr, g_ff | g_independent << 1);
  size_t len_pos = arr->size;
  ByteArray_AppendData(arr, (const uint8 *)"\0\0\0\0", 4);
  Inst_Switch(first);
  ZeldaSaveStateToBuffer(arr);
  uint32 len = (uint32)(arr->size - len_pos - 4);
  memcpy(arr->data + len_pos, &len, 4);
  for (int i = first + 1; i < kInstMax; i++) {
    if (g_inst[i].active) {
      Inst_Switch(i);
      SaveLoadExtra(&WriteFunc, arr);
    }
  }
  Inst_Switch(r);
}

bool Inst_LoadState(const uint8 *data, size_t size) {
  if (size < 6)
    return false;
  uint8 mask = data[0];
  if (!(mask & ((1 << kInstMax) - 1)))
    return false;
  g_ff = data[1] & 1;
  g_independent = (data[1] >> 1) & 1;
  uint32 len;
  memcpy(&len, data + 2, 4);
  if (len > size - 6)
    return false;
  // Forget the current worlds.
  int first = -1;
  for (int i = 0; i < kInstMax; i++)
    if ((mask >> i) & 1 && first < 0)
      first = i;
  Unbind();
  Alloc(first);
  for (int i = 0; i < kInstMax; i++)
    g_inst[i].active = 0;
  g_inst[first].active = 1;
  Bind(first);
  bool ok = ZeldaLoadStateFromBuffer(data + 6, len);
  Reader rd = { data + 6 + len, data + size, false };
  for (int i = first + 1; i < kInstMax; i++) {
    if ((mask >> i) & 1) {
      Alloc(i);
      Unbind();
      g_inst[i].active = 1;
      Bind(i);
      SaveLoadExtra(&ReadFunc, &rd);
      memset(&g_mp_render, 0, sizeof(g_mp_render));
    }
  }
  Inst_Switch(first);
  memcpy(g_shared, g_ram + kSharedStart, sizeof(g_shared));
  // (Nothing of these worlds has been seen yet.)
  memset(g_last_seen, 0, sizeof(g_last_seen));
  g_audio_inst = -1;
  UpdateCoopFlag();
  return ok && !rd.fail && rd.p == rd.end;
}

uint32 Inst_HashState() {
  int r = g_cur;
  uint32 h = 2166136261u;
  for (int i = 0; i < kInstMax; i++) {
    if (!g_inst[i].active)
      continue;
    Inst_Switch(i);
    h = (h ^ (i + 1)) * 16777619u;
    h = (h ^ ZeldaHashState()) * 16777619u;
  }
  Inst_Switch(r);
  return h;
}

// ---------------------------------------------------------------------------
// Rollback snapshots

struct InstSnapshot {
  uint8 active[kInstMax];
  uint8 *ram[kInstMax];
  Ppu *ppu[kInstMax];
  Dma dma[kInstMax];
  MpState mp[kInstMax];
  MpRenderState render[kInstMax];
  uint8 ports[kInstMax][4];
  int cur, audio_inst;
  uint8 shared[kSharedEnd - kSharedStart];
  uint8 sram[0x2000];
  LastSeen last_seen[kMpMaxPlayers];  // display state, rolled back with the rest
};

InstSnapshot *Inst_SnapshotNew() {
  return (InstSnapshot *)calloc(1, sizeof(InstSnapshot));
}

void Inst_SnapshotFree(InstSnapshot *s) {
  if (!s)
    return;
  for (int i = 0; i < kInstMax; i++) {
    free(s->ram[i]);
    free(s->ppu[i]);
  }
  free(s);
}

void Inst_SnapshotSave(InstSnapshot *s) {
  Unbind();  // the current world's co-op state lives in g_mp
  for (int i = 0; i < kInstMax; i++) {
    Instance *n = &g_inst[i];
    s->active[i] = n->active;
    if (!n->active)
      continue;
    if (!s->ram[i]) {
      s->ram[i] = (uint8 *)malloc(kRamSize);
      s->ppu[i] = (Ppu *)malloc(sizeof(Ppu));
    }
    memcpy(s->ram[i], n->ram, kRamSize);
    *s->ppu[i] = *n->ppu;
    s->dma[i] = *n->dma;
    s->mp[i] = n->mp;
    s->render[i] = n->render;
    memcpy(s->ports[i], n->apu_ports, 4);
  }
  s->cur = g_cur;
  s->audio_inst = g_audio_inst;
  memcpy(s->shared, g_shared, sizeof(g_shared));
  memcpy(s->sram, g_zenv.sram, sizeof(s->sram));
  memcpy(s->last_seen, g_last_seen, sizeof(g_last_seen));
}

void Inst_SnapshotLoad(const InstSnapshot *s) {
  for (int i = 0; i < kInstMax; i++) {
    Instance *n = &g_inst[i];
    n->active = s->active[i];
    n->born = 0;
    if (!n->active)
      continue;
    Alloc(i);
    memcpy(n->ram, s->ram[i], kRamSize);
    *n->ppu = *s->ppu[i];
    *n->dma = s->dma[i];
    n->mp = s->mp[i];
    n->render = s->render[i];
    memcpy(n->apu_ports, s->ports[i], 4);
  }
  Bind(s->cur);
  g_audio_inst = s->audio_inst;
  memcpy(g_shared, s->shared, sizeof(g_shared));
  memcpy(g_zenv.sram, s->sram, sizeof(s->sram));
  memcpy(g_last_seen, s->last_seen, sizeof(g_last_seen));
  UpdateCoopFlag();
}

void Inst_SnapshotPrintSprites(const InstSnapshot *s) {
  for (int i = 0; i < kInstMax; i++) {
    if (!s->active[i])
      continue;
    const uint8 *r = s->ram[i];
    printf(" w%d[m%d/%d L%d]", i, r[0x10], r[0x11], s->mp[i].leader);
    for (int k = 0; k < 16; k++)
      if (r[0xDD0 + k])
        printf(" %d:%02x/%d/%d/%d,%d", k, r[0xE20 + k], r[0xDD0 + k], r[0xE50 + k], r[0xD10 + k] | r[0xD30 + k] << 8, r[0xD00 + k] | r[0xD20 + k] << 8);
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (!s->mp[i].p[p].joined)
        continue;
      const uint8 *c = p == s->mp[i].resident ? r + 0x20 : s->mp[i].p[p].ctx;  // the resident's Link is in RAM
      printf(" P%d:%d,%d%s", p + 1, c[2] | c[3] << 8, c[0] | c[1] << 8, s->mp[i].p[p].downed ? "x" : "");
    }
  }
}

void Inst_SetAudioOutput(bool on) {
  g_audio_output = on;
}

// ---------------------------------------------------------------------------
// Queries

uint8 Inst_PlayerHealth(int p) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return 0;
  Inst_Switch(i);
  uint8 v = Mp_PlayerHealth(p);
  Inst_Switch(r);
  return v;
}

uint8 Inst_PlayerMagic(int p) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return 0;
  Inst_Switch(i);
  uint8 v = Mp_PlayerMagic(p);
  Inst_Switch(r);
  return v;
}

int Inst_PlayerPos(int p, uint16 *x, uint16 *y) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return -1;
  Inst_Switch(i);
  *x = Mp_PlayerX(p), *y = Mp_PlayerY(p);
  Inst_Switch(r);
  return i;
}

// Which coordinates the current world's positions are in: the light or
// dark world, a dungeon (its rooms share coordinates) or one house or cave.
static uint32 CurrentSpace() {
  uint8 palace = BYTE(cur_palace_index_x2);
  return !player_is_indoors ? (overworld_area_index & 0x40) :
         palace == 0xff ? 0x20000 | dungeon_room_index : 0x10000 | palace;
}

bool Inst_PlayerPing(int p, InstPing *out) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0 || !MpOf(i)->ping[p].ttl)
    return false;
  Inst_Switch(i);
  const MpPing *pg = &g_mp.ping[p];
  bool ok = Mp_AreaKeyPublic() == pg->key;
  if (ok) {
    out->world = i;
    out->x = pg->x, out->y = pg->y;
    out->space = CurrentSpace();
    out->ttl = pg->ttl, out->kind = pg->kind;
  }
  Inst_Switch(r);
  return ok;
}

bool Inst_PlayerPlace(int p, InstPlace *out) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return false;
  Inst_Switch(i);
  uint8 m = main_module_index;
  bool ok = (m == 7 || m == 9 || m == 14) && (submodule_index == 0 || m == 14);
  if (ok) {
    out->world = i;
    out->x = Mp_PlayerX(p) + 8, out->y = Mp_PlayerY(p) + 12;  // the middle of the Link
    out->cam_x = BG2HOFS_copy2, out->cam_y = BG2VOFS_copy2;
    out->space = CurrentSpace();
    out->downed = g_mp.p[p].downed;
  }
  Inst_Switch(r);
  return ok;
}

// Where world i is and where its camera looks, if it is in normal gameplay.
static bool WorldView(int i, uint32 *key, uint16 *cam_x, uint16 *cam_y) {
  const uint8 *ram = g_inst[i].ram;
  uint8 m = ram[0x10], s = ram[0x11];  // main_module_index, submodule_index
  if (!((m == 7 || m == 9) && s == 0))
    return false;
  // player_is_indoors, dungeon_room_index, overworld_area_index
  *key = ram[0x1B] << 16 | (ram[0x1B] ? WORD(ram[0xA0]) : WORD(ram[0x40A]));
  *cam_x = WORD(ram[0xE2]), *cam_y = WORD(ram[0xE8]);  // BG2HOFS/BG2VOFS_copy2
  return true;
}

static bool InMenu(int i) {
  return g_inst[i].ram[0x10] == 14;  // main_module_index: item menu, maps
}

// Remembers how every player in normal gameplay looks right now.
static void RecordLastSeen() {
  Unbind();
  for (int i = 0; i < kInstMax; i++) {
    uint32 key;
    uint16 x, y;
    if (!g_inst[i].active)
      continue;
    const MpState *m = &g_inst[i].mp;
    bool live = WorldView(i, &key, &x, &y);
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (!m->p[p].joined)
        continue;
      LastSeen *ls = &g_last_seen[p];
      if (!live) {
        ls->valid &= InMenu(i);  // keep showing it while in a menu
        continue;
      }
      bool leader = p == g_inst[i].render.self_player;
      const MpRenderPlayer *rp = leader ? &g_inst[i].render.self : &g_inst[i].render.p[p];
      ls->valid = rp->visible && rp->count;
      if (!ls->valid)
        continue;
      ls->key = key, ls->cam_x = x, ls->cam_y = y;
      ls->downed = m->p[p].downed;
      ls->rp = *rp;
      if (leader)
        memcpy(ls->rp.tiles, &g_inst[i].ppu->vram[0x4000], sizeof(ls->rp.tiles));
    }
  }
}

int Inst_LinksFromOtherWorlds(InstRemoteLink *out, int max) {
  uint32 key0, key;
  uint16 x0, y0, x, y;
  int n = 0;
  Unbind();  // the current world's render state is in g_mp_render
  if (!WorldView(g_cur, &key0, &x0, &y0))
    return 0;
  for (int i = 0; i < kInstMax; i++) {
    if (i == g_cur || !g_inst[i].active)
      continue;
    const MpState *m = &g_inst[i].mp;
    const MpRenderState *r = &g_inst[i].render;
    bool live = WorldView(i, &key, &x, &y);
    for (int p = 0; p < kMpMaxPlayers && n < max; p++) {
      // (A player in a menu: its Link stands in another world, drawn from there.)
      if (!m->p[p].joined || MpOf(g_cur)->p[p].joined || (!m->away[p] && FindPlayer(p, true) >= 0))
        continue;
      const LastSeen *ls = &g_last_seen[p];
      if (live && key == key0) {
        bool leader = p == g_inst[i].render.self_player;
        const MpRenderPlayer *rp = leader ? &r->self : &r->p[p];
        if (!rp->visible || !rp->count)
          continue;
        out[n].player = p;
        out[n].downed = m->p[p].downed;
        out[n].rp = rp;
        out[n].tiles = leader ? &g_inst[i].ppu->vram[0x4000] : rp->tiles;
        out[n].dx = (int16)(x - x0);
        out[n].dy = (int16)(y - y0);
        n++;
      } else if (!live && InMenu(i) && ls->valid && ls->key == key0) {
        // In the item menu or a map: drawn as last seen, where it paused.
        out[n].player = p;
        out[n].downed = ls->downed;
        out[n].rp = &ls->rp;
        out[n].tiles = ls->rp.tiles;
        out[n].dx = (int16)(ls->cam_x - x0);
        out[n].dy = (int16)(ls->cam_y - y0);
        n++;
      }
    }
  }
  return n;
}

int Inst_SpectateTarget(int p) {
  int i = Inst_OfPlayer(p);
  if (i < 0 || !MpOf(i)->p[p].downed || HasAlive(i))
    return p;
  for (int q = 0; q < kMpMaxPlayers; q++)
    if (q != p && Inst_IsJoined(q) && !Inst_PlayerDowned(q))
      return q;
  return p;
}

bool Inst_PlayerDowned(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 && MpOf(i)->p[p].downed;
}

bool Inst_PlayerItemIcon(int p, uint16 tiles[4]) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return false;
  Inst_Switch(i);
  bool ok = Mp_ItemIconTiles(p, tiles);
  Inst_Switch(r);
  return ok;
}

uint8 Inst_PlayerItem(int p) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return 0;
  Inst_Switch(i);
  uint8 v = *Mp_PlayerRam(p, 0x202);  // hud_cur_item
  Inst_Switch(r);
  return v;
}

int Inst_PlayerCarry(int p, bool *thrown, bool *carrying) {
  int i = Inst_OfPlayer(p);
  *thrown = *carrying = false;
  if (i < 0)
    return -1;
  const MpState *m = MpOf(i);
  *thrown = m->thrown[p] != 0;
  for (int q = 0; q < kMpMaxPlayers; q++)
    *carrying |= q != p && m->carried_by[q] == p + 1;
  return m->carried_by[p] - 1;
}

bool Inst_PlayerCanSendHeart(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 && MpOf(i)->p[p].downed && MpOf(i)->ghost_cooldown[p] == 0;
}

uint8 Inst_PlayerReviveCtr(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? MpOf(i)->p[p].revive_ctr : 0;
}

uint8 Inst_PlayerViewModule(int p) {
  int i = Inst_ViewOfPlayer(p);
  return i >= 0 ? g_inst[i].ram[0x10] : 0;
}

const uint8 *Inst_PlayerWorldRam(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? g_inst[i].ram : NULL;
}

uint8 Inst_PlayerModule(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? g_inst[i].ram[0x10] : 0;
}

uint8 Inst_ExitBlockedTimer(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? MpOf(i)->exit_blocked_timer : 0;
}
