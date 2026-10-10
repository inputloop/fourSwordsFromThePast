#include "randomizer.h"
#include "variables.h"
#include "assets.h"
#include "player.h"
#include <stdio.h>
#include <string.h>

// Save file bytes (offsets in the 7EF000 save area) that hold the setting.
enum {
  kRandoMarkerOffs = 0x4F0,  // 'R' when randomized
  kRandoSeedOffs = 0x4F1,    // 4 bytes
  kRandoMarker = 0x52,
  kRandoSmithSwordOffs = 0x4F5,  // sword level while the smiths have the sword
  kRandoPendingOffs = 0x4F6,     // start items still to hand out (bit 0, bit 1)
  kRandoDoneOffs = 0x4F7,        // special locations already used (bit per location)
  kMaxChests = 256,
};

// Item ids as used by the chest table and Link_ReceiveItem.
enum {
  kI_MirrorShield = 0x06, kI_FireRod = 0x07, kI_IceRod = 0x08, kI_Hammer = 0x09,
  kI_Hookshot = 0x0A, kI_Bow = 0x0B, kI_Lamp = 0x12, kI_Somaria = 0x15,
  kI_Cape = 0x19, kI_Glove = 0x1B, kI_Mitt = 0x1C, kI_Pearl = 0x1F,
  kI_BlueMail = 0x22, kI_RedMail = 0x23, kI_SmallKey = 0x24, kI_Compass = 0x25,
  kI_BigKey = 0x32, kI_Map = 0x33,
  kI_FighterSwordShield = 0x00, kI_MasterSword = 0x01, kI_TemperedSword = 0x02, kI_GoldenSword = 0x03,
  kI_FighterShield = 0x04, kI_RedShield = 0x05, kI_FighterSword = 0x49,
};

// Places outside chests that give swords and shields. They take part in the
// shuffle as extra locations (with made-up room numbers 0x1000+).
static const struct {
  uint16 room;
  uint8 vanilla;
} kSpecialLocations[kRando_NumSpecial] = {
  {0x1000, kI_FighterSword},   // start: the uncle's sword
  {0x1001, kI_FighterShield},  // start: the uncle's shield
  {0x1002, kI_MasterSword},    // Master Sword pedestal
  {0x1003, kI_TemperedSword},  // smiths
  {0x1004, kI_GoldenSword},    // Pyramid fairy
  {0x1005, kI_RedShield},      // Waterfall of Wishing fairy
};

static bool IsSword(uint8 item) {
  return item == kI_FighterSwordShield || item == kI_FighterSword || item == kI_MasterSword ||
         item == kI_TemperedSword || item == kI_GoldenSword;
}

static bool IsShield(uint8 item) {
  return item == kI_FighterShield || item == kI_RedShield || item == kI_MirrorShield;
}

static uint32 g_new_game_setting;

void Rando_SetNewGameSetting(uint32 setting) { g_new_game_setting = setting; }
uint32 Rando_GetNewGameSetting() { return g_new_game_setting; }

// Open start: no intro and no escape from Hyrule Castle (no rain, no guards
// blocking the way, no need to rescue Zelda first, so no Lamp needed). The
// game is as if Zelda was just brought to the sanctuary: Link may start at
// his house or the sanctuary and can go anywhere right away. save is a save
// file or the save area of RAM.
static void ApplyOpenStart(uint8 *save) {
  save[0x36E] = 0x80;     // full magic
  save[0x3C5] = 2;        // progress: Zelda is safe in the sanctuary
  // Progress flags as after the escape, except that the uncle still waits in
  // the secret passage (bit 0): he hands over two random items there.
  save[0x3C6] |= 0x14;
  save[0x3C7] = 1;        // map marker
  save[0x3C8] = 0;        // starting point: Link's house
  save[0x3CC] = 0;        // no follower (Zelda stays in the sanctuary)
}

void Rando_OnFileLoaded() {
  if (Rando_CurrentSeed() == 0)
    return;
  // Randomized files made before the open start existed get it now.
  if (g_ram[0xF3C5] < 2)
    ApplyOpenStart(&g_ram[0xF000]);
  // Files from the build that handed out the uncle's items at the start:
  // if they weren't handed out yet, the uncle gives them after all.
  if (g_ram[0xF000 + kRandoPendingOffs] == 3 && !Rando_SpecialDone(kRando_StartSword)) {
    g_ram[0xF000 + kRandoPendingOffs] = 0;
    g_ram[0xF3C6] &= ~1;
  }
}

void Rando_UncleGivesItems() {
  // First item now, the second one as soon as Link has put the first away.
  g_ram[0xF000 + kRandoPendingOffs] |= 2;
  sram_progress_flags |= 1;  // the uncle is gone next time
  Link_ReceiveItem(Rando_SpecialItem(kRando_StartSword, kSpecialLocations[kRando_StartSword].vanilla), 0);
}

void Rando_OnNewFile(uint8 *sram_slot) {
  if (g_new_game_setting == kRando_Off) {
    memset(sram_slot + kRandoMarkerOffs, 0, 5);
    return;
  }
  uint32 seed = g_new_game_setting;
  if (seed == kRando_RandomSeed) {
    // Random, but the same on every netplay machine: a hash of the game's
    // timers and work RAM at the moment the file is created.
    seed = 2166136261u;
    for (int i = 0; i < 0x2000; i++)
      seed = (seed ^ g_ram[i]) * 16777619u;
    for (int i = 0; i < 0x500; i++)
      seed = (seed ^ sram_slot[i]) * 16777619u;
    if (seed <= kRando_RandomSeed)
      seed += 2;
  }
  sram_slot[kRandoMarkerOffs] = kRandoMarker;
  memcpy(sram_slot + kRandoSeedOffs, &seed, 4);
  ApplyOpenStart(sram_slot);
}

uint32 Rando_CurrentSeed() {
  if (g_ram[0xF000 + kRandoMarkerOffs] != kRandoMarker)
    return 0;
  uint32 seed;
  memcpy(&seed, &g_ram[0xF000 + kRandoSeedOffs], 4);
  return seed;
}

// ---------------------------------------------------------------------------
// Logic: what is needed to reach each chest without glitches. It is on the
// safe side: a chest in a dungeon counts as reachable only with everything
// needed to finish that dungeon. Items that are never in chests (boots,
// book, flippers, mirror, master sword, medallions, flute) stay where they
// are in the original game, so their requirements are derived here.

typedef struct State {
  uint8 n[256];  // how many of each item
} State;

#define HAS(x) (s->n[x] != 0)

typedef struct Ev {
  bool lift1, lift2, lamp, bow, hook, hammer, fire, ice, pearl, somaria, cape;
  bool sword1, sword2, shield1, pedestal;
  bool ep, boots, book, dp, dm, mirror, dm_east, hera, ms, aga, dw_s, dw_n;
  bool flippers, flute, ether, quake, dwdm;
  bool pod, sp, sw, tt, ip, mm, tr, gt, win;
} Ev;

static void Evaluate(const State *s, Ev *e) {
  int gloves = s->n[kI_Glove] + s->n[kI_Mitt];
  e->lift1 = gloves >= 1;
  e->lift2 = gloves >= 2;
  e->lamp = HAS(kI_Lamp);
  e->bow = HAS(kI_Bow);
  e->hook = HAS(kI_Hookshot);
  e->hammer = HAS(kI_Hammer);
  e->fire = HAS(kI_FireRod);
  e->ice = HAS(kI_IceRod);
  e->pearl = HAS(kI_Pearl);
  e->somaria = HAS(kI_Somaria);
  e->cape = HAS(kI_Cape);
  int swords = s->n[kI_FighterSwordShield] + s->n[kI_FighterSword] + s->n[kI_MasterSword] +
               s->n[kI_TemperedSword] + s->n[kI_GoldenSword];
  e->sword1 = swords >= 1;
  e->sword2 = swords >= 2;                         // the Master Sword level
  e->shield1 = s->n[kI_FighterShield] + s->n[kI_RedShield] + s->n[kI_MirrorShield] >= 1;
  // Light World. Every dungeon needs a sword for its boss.
  e->ep = e->lamp && e->bow && e->sword1;          // Eastern Palace
  e->boots = e->ep;                                // Sahasrahla, after the green pendant
  e->book = e->boots;                              // library: dash into the shelf
  e->dp = e->book && e->lift1 && (e->lamp || e->fire) && e->sword1;  // Desert Palace
  e->dm = e->lift1 && e->lamp;                     // Death Mountain via the old man
  e->mirror = e->dm;
  e->dm_east = e->dm && (e->hook || (e->mirror && e->hammer));
  e->hera = e->dm && (e->mirror || (e->hook && e->hammer)) && (e->lamp || e->fire) && e->sword1;
  e->pedestal = e->ep && e->dp && e->hera;         // three pendants
  e->ms = e->sword2;
  // The barrier to the tower needs the Master Sword level, or the cape.
  e->aga = (e->sword2 || e->cape) && e->sword1 && e->lamp;
  e->flippers = e->lift1;                          // Zora
  // Dark World
  e->dw_s = e->pearl && (e->aga || e->lift2 || (e->lift1 && e->hammer));
  e->dw_n = e->pearl && (e->lift2 || (e->lift1 && e->hammer) || (e->aga && e->hook));
  e->flute = e->dw_s;                              // shovel from the flute boy, then dig
  e->ether = e->dm && e->book && e->sword2;        // tablets need the Master Sword
  e->quake = e->dw_n && e->lift1;                  // catfish
  e->dwdm = e->dm_east && e->lift2 && e->pearl;
  e->pod = e->dw_s && e->hammer && e->bow && e->lamp && e->sword1;
  e->sp = e->dw_s && e->mirror && e->flippers && e->hook && e->hammer && e->sword1;
  e->sw = e->dw_n && e->fire && e->sword1;
  e->tt = e->dw_n && e->hammer && e->sword1;
  e->ip = e->lift2 && e->flippers && e->pearl && e->fire && e->hammer && e->hook && e->sword1;
  e->mm = e->lift2 && e->flute && e->pearl && e->ether && e->somaria && e->lamp && e->fire &&
          e->boots && e->hook;
  e->tr = e->dm_east && e->lift2 && e->hammer && e->pearl && e->somaria && e->quake &&
          e->fire && e->ice && e->lamp && e->hook;
  bool crystals = e->pod && e->sp && e->sw && e->tt && e->ip && e->mm && e->tr;
  e->gt = crystals && e->dwdm && e->hook && e->hammer && e->bow && e->fire && e->somaria &&
          e->lamp && e->boots;
  e->win = e->gt && e->aga;
}

static bool IsEarlyRoom(int room) {
  // Link's house and the sanctuary: the two places a game starts from.
  return room == 0x104 || room == 0x12;
}

// Returns 1 if the chest in room can be reached, 0 if not, -1 if the room
// isn't known (then only filler goes there).
static int RoomAccess(int room, const Ev *e) {
  switch (room) {
  // Places open from the start (bombs can be bought). The game starts after
  // the escape, so the castle basement and sewers (rooms 0x11, 0x32, 0x71,
  // 0x72, 0x80), whose keys come from the escape's guards, only get filler.
  case 0x104: case 0x55: case 0x12:
  case 0x2F: case 0x11D: case 0x105: case 0x10A: case 0x123: case 0x120: case 0x10B:
    return true;
  case 0xE0: case 0xD0: return e->aga;              // Agahnim's tower
  case 0xA8: case 0xA9: case 0xAA: case 0xB8: case 0xB9: return e->ep;
  case 0x73: case 0x74: case 0x75: case 0x85: return e->dp;
  case 0x27: case 0x77: case 0x87: return e->hera;
  case 0x09: case 0x0A: case 0x19: case 0x1A: case 0x2A: case 0x2B: case 0x3A: case 0x6A:
    return e->pod;
  case 0x16: case 0x28: case 0x34: case 0x35: case 0x36: case 0x37: case 0x46: case 0x66: case 0x76:
    return e->sp;
  case 0x57: case 0x58: case 0x59: case 0x67: case 0x68: return e->sw;
  case 0x44: case 0x45: case 0x65: case 0xCB: case 0xDB: case 0xDC: return e->tt;
  case 0x1F: case 0x2E: case 0x3F: case 0x5F: case 0x7E: case 0x9E: case 0xAE: return e->ip;
  case 0xA2: case 0xB3: case 0xC1: case 0xC2: case 0xC3: case 0xD1: return e->mm;
  case 0x04: case 0x14: case 0x24: case 0xB6: case 0xB7: case 0xD5: case 0xD6: return e->tr;
  case 0x1C: case 0x3D: case 0x4D: case 0x5B: case 0x7B: case 0x7C: case 0x7D: case 0x8B:
  case 0x8C: case 0x8D: case 0x9D: return e->gt;
  case 0x11C: return e->dw_n;                       // C-shaped house
  case 0x11E: return e->dw_s;                       // hype cave
  case 0x113: return e->boots && e->lift2;          // King's tomb
  case 0x117: return e->dwdm && e->hammer && e->cape;  // spike cave
  case 0x10D: return e->lift2 && e->flute && e->pearl; // Mire shed
  case 0xEF: case 0xFF: case 0xFE: return e->dm_east;  // paradox cave, spiral cave
  case 0x10C: return e->tr;                         // mimic cave
  case 0xF8: return e->dwdm;                        // superbunny cave
  case 0x3C: return e->dwdm && e->hook && e->lift1; // hookshot cave
  // Swords and shields outside chests
  case 0x1000: case 0x1001: return true;            // handed out at the start
  case 0x1002: return e->pedestal;
  case 0x1003: return e->dw_n && e->lift2 && e->sword1;  // frog smith, then hand over a sword
  case 0x1004: return e->dw_s && e->ip && e->mm && e->sword1;  // crystals 5 and 6, throw a sword
  case 0x1005: return e->flippers && e->shield1;    // throw a shield into the waterfall
  default: return -1;
  }
}

static bool CanReach(int room, const Ev *e) {
  int a = RoomAccess(room, e);
  return a < 0 ? e->win : a != 0;
}

static bool IsKnownRoom(int room) {
  Ev e = { 0 };
  return RoomAccess(room, &e) >= 0;
}

// Not needed to finish the game, but nobody wants to miss them.
static bool IsUsefulItem(uint8 item) {
  switch (item) {
  case 0x0C: case 0x16: case 0x17: case 0x18: case 0x22: case 0x23: case 0x2A: case 0x3F:
    return true;
  }
  return false;
}

static bool IsDungeonItem(uint8 item) {
  return item == kI_SmallKey || item == kI_BigKey || item == kI_Map || item == kI_Compass;
}

static bool IsProgression(uint8 item) {
  switch (item) {
  case kI_Bow: case kI_Hookshot: case kI_Hammer: case kI_FireRod: case kI_IceRod:
  case kI_Lamp: case kI_Glove: case kI_Mitt: case kI_Pearl: case kI_Somaria: case kI_Cape:
    return true;
  }
  return IsSword(item) || IsShield(item);
}

typedef struct Rng {
  uint64 s;
} Rng;

static uint32 RngNext(Rng *r) {
  // splitmix64
  uint64 z = (r->s += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return (uint32)((z ^ (z >> 31)) >> 32);
}

static int RngBelow(Rng *r, int n) {
  return (int)(RngNext(r) % (uint32)n);
}

static void Shuffle(Rng *r, uint8 *a, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = RngBelow(r, i + 1);
    uint8 t = a[i]; a[i] = a[j]; a[j] = t;
  }
}

typedef struct Placement {
  int nchests;  // chests and special locations
  int nreal;    // chests only (they come first)
  uint16 room[kMaxChests];
  uint8 vanilla[kMaxChests];
  uint8 item[kMaxChests];
  bool shuffled[kMaxChests];
} Placement;

static void LoadChests(Placement *pl) {
  pl->nchests = 0;
  for (size_t i = 0; i + 2 < kDungeonRoomChests_SIZE && pl->nchests < kMaxChests - kRando_NumSpecial; i += 3) {
    int k = pl->nchests++;
    pl->room[k] = WORD(kDungeonRoomChests[i]) & 0x7fff;
    pl->vanilla[k] = pl->item[k] = kDungeonRoomChests[i + 2];
    pl->shuffled[k] = !IsDungeonItem(pl->vanilla[k]);
  }
  pl->nreal = pl->nchests;
  for (int i = 0; i < kRando_NumSpecial; i++) {
    int k = pl->nchests++;
    pl->room[k] = kSpecialLocations[i].room;
    pl->vanilla[k] = pl->item[k] = kSpecialLocations[i].vanilla;
    pl->shuffled[k] = true;
  }
}

// Collects every item reachable from what the player has in s. Chests not
// filled yet (item 0xff) are skipped.
static void Sweep(const Placement *pl, State *s, bool *taken) {
  for (bool changed = true; changed;) {
    changed = false;
    Ev e;
    Evaluate(s, &e);
    for (int k = 0; k < pl->nchests; k++) {
      if (taken[k] || pl->item[k] == 0xff || !CanReach(pl->room[k], &e))
        continue;
      taken[k] = true;
      s->n[pl->item[k]]++;
      changed = true;
    }
  }
}

static bool TryFill(Placement *pl, Rng *r) {
  uint8 prog[kMaxChests], junk[kMaxChests];
  int nprog = 0, njunk = 0;
  for (int k = 0; k < pl->nchests; k++) {
    if (!pl->shuffled[k])
      continue;
    uint8 it = pl->vanilla[k];
    if (IsProgression(it))
      prog[nprog++] = it;
    else
      junk[njunk++] = it;
    pl->item[k] = 0xff;
  }
  Shuffle(r, prog, nprog);
  Shuffle(r, junk, njunk);

  // One lamp is in Link's house or the sanctuary, where the game starts.
  int escape[8], nescape = 0;
  for (int k = 0; k < pl->nchests; k++)
    if (pl->shuffled[k] && IsEarlyRoom(pl->room[k]) && nescape < 8)
      escape[nescape++] = k;
  for (int i = 0; i < nprog && nescape; i++) {
    if (prog[i] == kI_Lamp) {
      pl->item[escape[RngBelow(r, nescape)]] = kI_Lamp;
      prog[i] = prog[--nprog];
      break;
    }
  }

  // Assumed fill: each progression item goes to a chest that can be reached
  // with all the items that are still to be placed.
  Shuffle(r, prog, nprog);
  for (int i = 0; i < nprog; i++) {
    State s = { { 0 } };
    for (int j = i + 1; j < nprog; j++)
      s.n[prog[j]]++;
    bool taken[kMaxChests] = { 0 };
    Sweep(pl, &s, taken);
    Ev e;
    Evaluate(&s, &e);
    int cand[kMaxChests], ncand = 0;
    for (int k = 0; k < pl->nchests; k++)
      if (pl->shuffled[k] && pl->item[k] == 0xff && CanReach(pl->room[k], &e))
        cand[ncand++] = k;
    if (ncand == 0)
      return false;
    pl->item[cand[RngBelow(r, ncand)]] = prog[i];
  }
  // Useful items go to chests in known places, then the rest anywhere.
  int free_known[kMaxChests], nfree_known = 0;
  for (int k = 0; k < pl->nchests; k++)
    if (pl->shuffled[k] && pl->item[k] == 0xff && IsKnownRoom(pl->room[k]))
      free_known[nfree_known++] = k;
  for (int i = 0; i < njunk; i++) {
    if (IsUsefulItem(junk[i]) && nfree_known) {
      int c = RngBelow(r, nfree_known);
      pl->item[free_known[c]] = junk[i];
      free_known[c] = free_known[--nfree_known];
      junk[i] = 0xff;
    }
  }
  int j = 0;
  for (int k = 0; k < pl->nchests; k++) {
    if (!pl->shuffled[k] || pl->item[k] != 0xff)
      continue;
    while (j < njunk && junk[j] == 0xff)
      j++;
    pl->item[k] = j < njunk ? junk[j++] : 0x34;  // 1 rupee (never happens)
  }

  // Final check: starting with nothing, can the game be finished?
  State s = { { 0 } };
  bool taken[kMaxChests] = { 0 };
  Sweep(pl, &s, taken);
  Ev e;
  Evaluate(&s, &e);
  return e.win;
}

static bool Generate(uint32 seed, Placement *pl) {
  LoadChests(pl);
  Rng r = { seed * 0x2545F4914F6CDD1Dull + 1 };
  for (int attempt = 0; attempt < 64; attempt++) {
    if (TryFill(pl, &r))
      return true;
  }
  LoadChests(pl);  // give up: the original placement always works
  return false;
}

static Placement g_cache;
static uint32 g_cache_seed;
static bool g_cache_valid;

uint8 Rando_ChestItem(int idx, uint8 vanilla_item) {
  uint32 seed = Rando_CurrentSeed();
  if (seed == 0)
    return vanilla_item;
  if (!g_cache_valid || g_cache_seed != seed) {
    Generate(seed, &g_cache);
    g_cache_seed = seed;
    g_cache_valid = true;
  }
  return idx >= 0 && idx < g_cache.nreal ? g_cache.item[idx] : vanilla_item;
}

uint8 Rando_SpecialItem(int which, uint8 vanilla_item) {
  if (Rando_CurrentSeed() == 0)
    return vanilla_item;
  Rando_ChestItem(-1, 0);  // make sure the placement is there
  g_ram[0xF000 + kRandoDoneOffs] |= 1 << which;
  return g_cache.item[g_cache.nreal + which];
}

bool Rando_SpecialDone(int which) {
  return (g_ram[0xF000 + kRandoDoneOffs] >> which) & 1;
}

static uint8 SwordLevel() {
  return link_sword_type == 0xff ? g_ram[0xF000 + kRandoSmithSwordOffs] : link_sword_type;
}

void Rando_OnSmithsTakeSword() {
  if (Rando_CurrentSeed() != 0)
    g_ram[0xF000 + kRandoSmithSwordOffs] = link_sword_type;
}

void Rando_OnSmithsReturnSword() {
  if (Rando_CurrentSeed() != 0 && link_sword_type == 0xff)
    link_sword_type = g_ram[0xF000 + kRandoSmithSwordOffs];
}

void Rando_GiveStartItems() {
  uint8 *pending = &g_ram[0xF000 + kRandoPendingOffs];
  if (!*pending || Rando_CurrentSeed() == 0 || submodule_index != 0 ||
      (main_module_index != 7 && main_module_index != 9) ||
      link_player_handler_state != 0 || link_auxiliary_state || item_receipt_method ||
      flag_is_link_immobilized)
    return;
  int which = (*pending & 1) ? kRando_StartSword : kRando_StartShield;
  *pending &= (*pending & 1) ? ~1 : ~2;
  if (Rando_SpecialDone(which))
    return;
  Link_ReceiveItem(Rando_SpecialItem(which, kSpecialLocations[which].vanilla), 0);
}

uint8 Rando_AdjustReceivedItem(uint8 item) {
  if (Rando_CurrentSeed() == 0)
    return item;
  // Swords and shields are progressive: each one found is the next level.
  if (IsSword(item)) {
    static const uint8 kSwords[4] = { kI_FighterSword, kI_MasterSword, kI_TemperedSword, kI_GoldenSword };
    uint8 level = SwordLevel();
    return kSwords[level < 4 ? level : 3];
  }
  if (IsShield(item)) {
    static const uint8 kShields[3] = { kI_FighterShield, kI_RedShield, kI_MirrorShield };
    return kShields[link_shield_type < 3 ? link_shield_type : 2];
  }
  // The first glove found is the Power Glove, the next the Titan's Mitt.
  if (item == kI_Glove || item == kI_Mitt)
    return link_item_gloves ? kI_Mitt : kI_Glove;
  if (item == kI_BlueMail || item == kI_RedMail)
    return link_armor ? kI_RedMail : kI_BlueMail;
  return item;
}

bool Rando_Check(uint32 seed, bool verbose) {
  Placement pl;
  bool ok = Generate(seed, &pl);
  // Same items as the original game, and dungeon items where they were.
  int count[256] = { 0 };
  for (int k = 0; k < pl.nchests; k++) {
    count[pl.vanilla[k]]++;
    count[pl.item[k]]--;
    if (!pl.shuffled[k] && pl.item[k] != pl.vanilla[k])
      ok = false;
  }
  for (int i = 0; i < 256; i++)
    if (count[i])
      ok = false;
  if (verbose) {
    for (int k = 0; k < pl.nchests; k++)
      if (pl.item[k] != pl.vanilla[k])
        printf("  %s %3d room %03x: %02x (was %02x)\n", k < pl.nreal ? "chest" : "place", k,
               pl.room[k], pl.item[k], pl.vanilla[k]);
  }
  return ok;
}
