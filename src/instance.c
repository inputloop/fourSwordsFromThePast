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
static int g_cur;
static bool g_ff;
static bool g_independent = true;
static int g_audio_player;
static int g_audio_inst = -1;
static uint8 g_shared[kSharedEnd - kSharedStart];  // progress as of the last sync

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

int Inst_OfPlayer(int p) {
  if (p < 0 || p >= kMpMaxPlayers)
    return -1;
  for (int i = 0; i < kInstMax; i++)
    if (g_inst[i].active && MpOf(i)->p[p].joined)
      return i;
  return -1;
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

static int NumJoinedIn(int i) {
  int n = 0;
  for (int p = 0; p < kMpMaxPlayers; p++)
    n += MpOf(i)->p[p].joined;
  return n;
}

static bool HasAlive(int i) {
  MpState *m = MpOf(i);
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (m->p[p].joined && !m->p[p].downed)
      return true;
  return false;
}

static bool Inst_OtherInstanceAlive() {
  for (int i = 0; i < kInstMax; i++)
    if (i != g_cur && g_inst[i].active && HasAlive(i))
      return true;
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

void Inst_Leave(int p) {
  int i = Inst_OfPlayer(p);
  if (i < 0 || Inst_NumJoined() <= 1)
    return;
  int r = g_cur;
  Inst_Switch(i);
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
static void SplitOff(int j, int p, bool follower_goes_along) {
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
    if (q != p)
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
  if (can_split)
    TakeSnapshot();
  bool replay = false;
  for (;;) {
    g_mp.split_request = -1;
    replay |= ZeldaRunFrame(inputs[g_mp.leader]);
    int p = g_mp.split_request, j;
    if (p < 0 || !can_split || Mp_NumJoined() < 2 || (j = FreeSlot()) < 0)
      break;
    bool travel = g_mp.split_travel;
    TRACE("  split P%d from world %d into %d travel=%d module=%d/%d\n", p + 1, i, j, travel, main_module_index, submodule_index);
    // The follower follows whoever led the group before this frame.
    bool follower_goes_along = travel && p == g_snap_mp.leader;
    SplitOff(j, p, follower_goes_along);
    if (!travel) {
      Mp_RemovePlayer(p);
      break;
    }
    // The traveler continues in the new world. Everybody else replays the
    // frame as if they had never been here.
    RestoreSnapshot();
    Mp_RemovePlayer(p);
    if (follower_goes_along)
      follower_indicator = 0;
    can_split = Mp_NumJoined() > 1;
    TakeSnapshot();
  }
  g_mp.split_request = -1;
  return replay;
}

static void MovePlayers(int from, int to) {
  TRACE("  move players of world %d into %d\n", from, to);
  uint8 ctx[kMpMaxPlayers][kMpCtxMaxBytes];
  uint8 joined[kMpMaxPlayers] = { 0 }, downed[kMpMaxPlayers], ctr[kMpMaxPlayers];
  Inst_Switch(from);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    joined[p] = 1;
    downed[p] = g_mp.p[p].downed;
    ctr[p] = g_mp.p[p].revive_ctr;
    Mp_ExportPlayer(p, ctx[p]);
  }
  Inst_Switch(to);
  Deactivate(from);
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (joined[p])
      Mp_ImportPlayer(p, ctx[p], downed[p], ctr[p]);
}

typedef struct InstView {
  bool stable;
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
  // Only once the cameras are nearly in the same place, so nobody's view jumps.
  if (abs((int16)(va->cam_x - vb->cam_x)) > 48 || abs((int16)(va->cam_y - vb->cam_y)) > 48)
    return false;
  Inst_Switch(b);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    int sx = (int16)(Mp_PlayerX(p) - va->cam_x), sy = (int16)(Mp_PlayerY(p) - va->cam_y);
    if (sx < 32 || sx > 256 - 48 || sy < 32 || sy > 224 - 64)
      return false;
    if (g_mp.p[p].downed)
      continue;
    if (*Mp_PlayerRam(p, 0x5D) != 0 ||   // link_player_handler_state: ground
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
  return addr == 0xF343 || addr == 0xF36E || addr == 0xF377 ||
         (addr >= kKeySlots && addr < kKeySlots + 16);
}

static bool IsPerWorld(int addr) {
  return addr == 0xF34F || addr == 0xF36D || addr == 0xF372 ||  // personal
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
    // A world split off this frame is a copy of one that reports the same changes.
    if (g_inst[i].born)
      continue;
    const uint8 *r = g_ram + kSharedStart;
    rupee_delta += (int16)(link_rupees_goal - WORD(base[0xF360 - kSharedStart]));
    for (int j = 0; j < kSharedEnd - kSharedStart; j++) {
      int addr = kSharedStart + j;
      if (r[j] == base[j] || IsPerWorld(addr))
        continue;
      if (n > 1 && IsCounter(addr))
        delta[j] += r[j] - base[j];
      else if (n > 1 && addr < kFlagsEnd)
        merged[j] |= r[j];
      else
        merged[j] = r[j];
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
    for (int j = 0; j < kSharedEnd - kSharedStart; j++)
      if (!IsPerWorld(kSharedStart + j) || j == 0xF360 - kSharedStart || j == 0xF361 - kSharedStart)
        r[j] = merged[j];
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
  // Hear (and by default show) the world of the local player.
  int ai = Inst_OfPlayer(g_audio_player);
  for (int p = 0; p < kMpMaxPlayers && ai < 0; p++)
    ai = Inst_OfPlayer(p);
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
  UpdateCoopFlag();
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

int Inst_PlayerPos(int p, uint16 *x, uint16 *y) {
  int i = Inst_OfPlayer(p), r = g_cur;
  if (i < 0)
    return -1;
  Inst_Switch(i);
  *x = Mp_PlayerX(p), *y = Mp_PlayerY(p);
  Inst_Switch(r);
  return i;
}

bool Inst_PlayerDowned(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 && MpOf(i)->p[p].downed;
}

uint8 Inst_PlayerReviveCtr(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? MpOf(i)->p[p].revive_ctr : 0;
}

uint8 Inst_PlayerModule(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? g_inst[i].ram[0x10] : 0;
}

uint8 Inst_ExitBlockedTimer(int p) {
  int i = Inst_OfPlayer(p);
  return i >= 0 ? MpOf(i)->exit_blocked_timer : 0;
}
