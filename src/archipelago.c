#include "mods.h"  // (mods/SPEC.md: this file is the archipelago mod)
#if MOD_ARCHIPELAGO
#include "archipelago.h"
#include "ancilla.h"
#include "ap_client.h"
#include "instance.h"
#include "mp.h"
#include "player.h"
#include "randomizer.h"
#include "session.h"
#include "variables.h"
#include <stdio.h>
#include <string.h>

#define SAVE(off) g_ram[0xF000 + (off)]

const char *Ap_WorldGameName(ApWorld w) {
  return w == kApWorld_Alttp ? AP_ALTTP_GAME_NAME : AP_GAME_NAME;
}

static uint32 g_new_game_bind;
static ApWorld g_new_game_world;

void Ap_SetNewGameBind(uint32 bind) {
  g_new_game_bind = bind;
}

void Ap_SetNewGameWorld(ApWorld w) {
  g_new_game_world = w;
}

uint32 Ap_GetNewGameBind() {
  return g_new_game_bind;
}

void Ap_OnNewFile(uint8 *sram_slot) {
  memset(sram_slot + kApSave_Marker, 0, kApSave_End - kApSave_Marker);
  if (!g_new_game_bind)
    return;
  sram_slot[kApSave_Marker] = kApMarker;
  memcpy(sram_slot + kApSave_Bind, &g_new_game_bind, 4);
  sram_slot[kApSave_World] = g_new_game_world == kApWorld_Alttp ? kApWorld_Alttp : kApWorld_Fsotp;
  if (sram_slot[kApSave_World] == kApWorld_Alttp) {
    // What the room says about itself comes with the kSessEv_ApRoom events.
    sram_slot[kApSave_Medallions] = 0xff;
    sram_slot[kApSave_Goal] = 0xff;
    sram_slot[kApSave_CrystalsGt] = sram_slot[kApSave_CrystalsGanon] = 7;
  }
}

void Ap_OnFileLoaded() {
  // A DeathLink from the last time the file was played is over, and so are
  // its game overs: the count starts again (only a new one is sent).
  if (Ap_IsApFile()) {
    SAVE(kApSave_State) &= ~kApState_DeathPending;
    SAVE(kApSave_Deaths) = 0;
  }
}

bool Ap_IsApFile() {
  return SAVE(kApSave_Marker) == kApMarker;
}

bool Ap_IsAlttpFile() {
  return Ap_IsApFile() && SAVE(kApSave_World) == kApWorld_Alttp;
}

static uint32 SaveBind(const uint8 *save) {
  uint32 bind;
  memcpy(&bind, save + kApSave_Bind, 4);
  return save[kApSave_Marker] == kApMarker ? bind : 0;
}

uint32 Ap_FileBind() {
  return SaveBind(&g_ram[0xF000]);
}

static void CheckGoal();

static void MarkChecked(int bit) {
  SAVE(kApSave_Checked + (bit >> 3)) |= 1 << (bit & 7);
}

void Ap_OnChestOpened(int chest) {
  if (Ap_IsApFile() && Rando_IsApLocation(chest))
    MarkChecked(chest);
}

void Ap_OnSpecialPlace(int which) {
  if (Ap_IsApFile() && which >= 0 && which < kRando_NumSpecial)
    MarkChecked(kApSpecialBit + which);
}

static bool IsChecked(int bit) {
  return SAVE(kApSave_Checked + (bit >> 3)) >> (bit & 7) & 1;
}

// --- The places of the "A Link to the Past" world ---

static bool AlttpPlace(int loc) {
  return Ap_IsAlttpFile() && loc >= 0 && loc < kApNumSpecials && kAlttpSpecials[loc].id != 0;
}

uint8 Ap_PlaceItem(int loc, uint8 vanilla_item) {
  if (!AlttpPlace(loc))
    return vanilla_item;
  MarkChecked(kApSpecialBit + loc);
  return kApPlaceholderItem;
}

static int FindPlace(int kind, int a, int b) {
  if (!Ap_IsAlttpFile())
    return -1;
  for (int i = 0; i < kApNumSpecials; i++)
    if (kAlttpSpecials[i].kind == kind && kAlttpSpecials[i].a == a && kAlttpSpecials[i].b == b)
      return i;
  return -1;
}

int Ap_HeartPiecePlace(bool indoors, int where, int mask) {
  return indoors ? FindPlace(kApLocKind_UwFlag, where, mask) : FindPlace(kApLocKind_OwHeart, where, 0x40);
}

int Ap_RoomFlagPlace(int room, int mask) {
  return FindPlace(kApLocKind_UwFlag, room, mask);
}

int Ap_BossPlace(int dungeon) {
  if (!Ap_IsAlttpFile())
    return -1;
  for (int i = 0; i < kApNumSpecials; i++)
    if (kAlttpSpecials[i].kind == kApLocKind_Boss && kAlttpSpecials[i].a == dungeon)
      return i;
  return -1;
}

int Ap_FairyPlace(bool dark_world) {
  int first = dark_world ? kApLoc_PyramidFairyLeft : kApLoc_WaterfallFairyLeft;
  for (int loc = first; loc < first + 2; loc++)
    if (AlttpPlace(loc) && !IsChecked(kApSpecialBit + loc))
      return loc;
  return -1;
}

// Beaten dungeons: the pendants and crystals held (each dungeon gives one).
static int PrizeCount() {
  int n = 0;
  for (int b = 0; b < 3; b++)
    n += link_which_pendants >> b & 1;
  for (int b = 0; b < 7; b++)
    n += link_has_crystals >> b & 1;
  return n;
}

bool Ap_PrizeGate(int gate, bool vanilla) {
  if (!Ap_IsAlttpFile())
    return vanilla;
  int n = PrizeCount();
  switch (gate) {
  case kApGate_Sahasrahla: return n >= 1;    // the green pendant
  case kApGate_PyramidFairy: return n >= 2;  // crystals 5 and 6
  case kApGate_Pedestal: return n >= 3;      // the three pendants
  case kApGate_GanonsTower: return n >= SAVE(kApSave_CrystalsGt);
  case kApGate_Pyramid: return n >= SAVE(kApSave_CrystalsGanon);
  }
  return vanilla;
}

bool Ap_MedallionOpens(int medallion, int screen) {
  int vanilla = screen == 0x70 ? 1 : 2;  // Ether for Misery Mire, Quake for Turtle Rock
  if (!Ap_IsAlttpFile())
    return medallion == vanilla;
  int m = SAVE(kApSave_Medallions);
  int needed = screen == 0x70 ? m & 0xf : m >> 4;
  return needed == 0xf ? true : medallion == needed;  // (not told: any medallion does)
}

// Swords, shields, gloves, mails and boomerangs by name (that world with
// progressive items off): a lower one than Link has changes nothing, so the
// code of the best level comes back.
uint8 Ap_AlttpBestLevel(uint8 item) {
  static const uint8 kSwords[4] = { 0x49, 0x01, 0x02, 0x03 }, kShields[3] = { 0x04, 0x05, 0x06 };
  static const uint8 kGloves[2] = { 0x1B, 0x1C }, kMails[2] = { 0x22, 0x23 }, kBoomerangs[2] = { 0x0C, 0x2A };
  int level = -1, have = 0;
  const uint8 *table = NULL;
  for (int i = 0; i < 4; i++) {
    if (item == kSwords[i])
      table = kSwords, level = i, have = Rando_SwordLevel();
    if (i < 3 && item == kShields[i])
      table = kShields, level = i, have = link_shield_type;
    if (i < 2 && item == kGloves[i])
      table = kGloves, level = i, have = link_item_gloves;
    if (i < 2 && item == kMails[i])
      table = kMails, level = i, have = link_armor;
    if (i < 2 && item == kBoomerangs[i])
      table = kBoomerangs, level = i, have = link_item_boomerang;
  }
  if (!table)
    return item;
  return have > level + 1 ? table[have - 1] : table[level];
}

void Ap_OnGameOver() {
  // A fairy in a bottle brings the Link back (GameOver_SplatAndFade): the
  // game isn't over, and nobody in the multiworld should die for it.
  for (int i = 0; i < 4; i++)
    if (link_bottle_info[i] == 6)
      return;
  if (Ap_IsApFile())
    SAVE(kApSave_Deaths)++;
}

void Ap_OnGoal() {
  if (!Ap_IsApFile())
    return;
  if (!Ap_IsAlttpFile()) {
    SAVE(kApSave_State) |= kApState_Goal;
    return;
  }
  SAVE(kApSave_State) |= kApState_GanonBeaten;
  CheckGoal();
}

static uint16 ItemCount() {
  return SAVE(kApSave_ItemCount) | SAVE(kApSave_ItemCount + 1) << 8;
}

static void SetItemCount(uint16 n) {
  SAVE(kApSave_ItemCount) = (uint8)n;
  SAVE(kApSave_ItemCount + 1) = n >> 8;
}

// The item codes the world puts into its pool (ITEMS in
// archipelago/fsotp/items.py). Any other code from the server is skipped:
// a crystal, a pendant or a dungeon item would start a sequence that only
// works in its own place (a crystal ends the game outside a boss room).
static const uint8 kPoolItems[] = {
  0x49, 0x04, 0x1B, 0x22, 0x0C, 0x0B, 0x0A, 0x09, 0x07, 0x08, 0x12, 0x15, 0x19, 0x1F,
  0x16, 0x17, 0x18, 0x3F, 0x28, 0x34, 0x35, 0x36, 0x41, 0x40, 0x46, 0x43, 0x44,
};

static bool IsPoolItem(int64 code) {
  for (size_t i = 0; i < countof(kPoolItems); i++)
    if (code == kPoolItems[i])
      return true;
  return false;
}

// The dungeon of a dungeon item of the "A Link to the Past" world (its codes
// 0x72-0xad: maps, compasses, big keys, small keys), as the game counts them.
static int AlttpItemDungeon(uint8 code) {
  return code >= 0xA0 ? code & 0xf : 15 - (code & 0xf);  // (small keys count the other way)
}

// Does the "A Link to the Past" world's item code give something here?
static bool AlttpGivesSomething(uint8 code) {
  if (code == 0x5A || (code >= 0x5B && code <= 0x5D) || code == 0x62 || code == 0x63 || code == 0x0E ||
      code == 0x6B || code == 0xAF || code == 0xB0 || code == 0xB2)
    return false;  // Nothing, the clocks, the random items, bees, a Power Star, universal keys
  return code >= 0x02 && code <= 0xB4;
}

// The code to hand to Link_ReceiveItem for an item of that world, with the
// effects the game's receipt has no code for done here. 0xff: nothing to
// hold up.
static uint8 AlttpReceive(uint8 code) {
  switch (code) {
  case 0x5E: {  // a progressive sword: the next level
    static const uint8 kSwords[4] = { 0x49, 0x01, 0x02, 0x03 };
    int level = Rando_SwordLevel();
    return kSwords[level < 4 ? level : 3];
  }
  case 0x5F: return link_shield_type >= 2 ? 0x06 : link_shield_type == 1 ? 0x05 : 0x04;
  case 0x60: return link_armor ? 0x23 : 0x22;
  case 0x61: return link_item_gloves ? 0x1C : 0x1B;
  case 0x50: return Ap_AlttpBestLevel(0x01);  // the Master Sword by name
  case 0x64: case 0x65: return link_item_bow ? 0x3B : 0x0B;  // a progressive bow: then the silver one
  case 0x58: return 0x3B;  // silver arrows (with the bow if it isn't here yet)
  case 0x4A:  // the flute, activated
    link_item_flute |= 2;
    return 0x14;
  case 0x42: case 0xB4:  // a small heart, an apple
    link_hearts_filler += 8;
    return 0xff;
  case 0xB1:  // a fairy
    link_hearts_filler += 56;
    return 0xff;
  case 0xB3:  // a magic jar
    link_magic_filler = 0x80;
    return 0xff;
  case 0x4E: case 0x4F:  // half, quarter magic
    link_magic_consumption = code == 0x4F ? 2 : link_magic_consumption < 1 ? 1 : link_magic_consumption;
    return kApPlaceholderItem;
  case 0x51: case 0x52: case 0x4C:  // bomb capacity: +5, +10, to 50
    link_bomb_upgrades = code == 0x4C ? 8 : link_bomb_upgrades + (code == 0x52 ? 2 : 1);
    if (link_bomb_upgrades > 8)
      link_bomb_upgrades = 8;
    return kApPlaceholderItem;
  case 0x53: case 0x54: case 0x4D:  // arrow capacity: +5, +10, to 70
    link_arrow_upgrades = code == 0x4D ? 8 : link_arrow_upgrades + (code == 0x54 ? 2 : 1);
    if (link_arrow_upgrades > 8)
      link_arrow_upgrades = 8;
    return kApPlaceholderItem;
  case 0x59:  // a rupoor
    link_rupees_goal = link_rupees_goal >= 10 ? link_rupees_goal - 10 : 0;
    return kApPlaceholderItem;
  case 0x6A:  // the Triforce
    SAVE(kApSave_State) |= kApState_Triforce;
    return kApPlaceholderItem;
  case 0x6C:  // a Triforce piece
    if (SAVE(kApSave_Pieces) < 255)
      SAVE(kApSave_Pieces)++;
    return kApPlaceholderItem;
  }
  if (code >= 0x72 && code <= 0xAD) {
    int d = AlttpItemDungeon(code);
    if (code >= 0xA0) {  // a small key: of its dungeon (counted in the dungeon itself while Link is there)
      bool inside = player_is_indoors && (BYTE(cur_palace_index_x2) == 2 ? 0 : BYTE(cur_palace_index_x2) >> 1) == d &&
                    link_num_keys != 0xff;
      if (inside)
        link_num_keys++;
      else if (link_keys_earned_per_dungeon[d] < 99)
        link_keys_earned_per_dungeon[d]++;
      return 0xfe;  // (held up as a key, see GiveItem)
    }
    uint16 bit = 0x8000 >> d;
    if (code >= 0x92)
      link_bigkey |= bit;
    else if (code >= 0x82)
      link_compass |= bit;
    else
      link_dungeon_map |= bit;
    return code >= 0x92 ? 0xfd : code >= 0x82 ? 0xfc : 0xfb;  // (held up, see GiveItem)
  }
  return Ap_AlttpBestLevel(code);
}

// Can player p's Link hold up an item now? Standing on the ground and free,
// like after opening a chest. (A bunny or a swimming Link waits: holding up
// the item would make it a normal Link on land.) Read from p's own fields:
// making p the resident Link would change the game (who owns new objects),
// and the host asks this between frames too.
static bool LinkIsFree(int p) {
  return *Mp_PlayerRam(p, 0x5D) == kPlayerState_Ground &&  // link_player_handler_state
         !*Mp_PlayerRam(p, 0x4D) &&    // link_auxiliary_state
         !*Mp_PlayerRam(p, 0x2E9) &&   // item_receipt_method
         !*Mp_PlayerRam(p, 0x2E4) &&   // flag_is_link_immobilized
         !*Mp_PlayerRam(p, 0x308) &&   // link_state_bits
         !*Mp_PlayerRam(p, 0x309) &&   // link_picking_throw_state
         !*Mp_PlayerRam(p, 0x46);      // link_incapacitated_timer
}

// Is a Link of the current world holding up an item? (The next one waits:
// one at a time, like chests.)
static bool WorldIsReceiving() {
  for (int i = 0; i < 10; i++)
    if (ancilla_type[i] == 0x22)
      return true;
  return false;
}

// The first player (by number) who can take an item now, or -1. Leaves the
// world of the last player looked at current.
static int FindReceiver() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    int w = Inst_OfPlayer(p);
    if (w < 0 || Inst_PlayerInMenu(p))
      continue;
    Inst_Switch(w);
    // (The item needs a free object slot, or Link would hold up nothing,
    // frozen, for good: see Ancilla_AllocInit.)
    if (Mp_InStableGameplay() && Mp_IsAlive(p) && !WorldIsReceiving() && Ancilla_CanAlloc(0x22, 4) &&
        LinkIsFree(p))
      return p;
  }
  return -1;
}

// Item number idx of the server's list. It goes to the first player (by
// number) who can take it now; if nobody can, the host proposes it again
// once someone can.
static bool GivesSomething(uint8 code) {
  return Ap_IsAlttpFile() ? AlttpGivesSomething(code) : IsPoolItem(code);
}

static void GiveItem(uint8 code, uint16 idx) {
  if (idx != ItemCount())
    return;  // already received, or an older proposal
  if (!GivesSomething(code)) {
    SetItemCount(idx + 1);  // nothing our world sends: just skip it
    return;
  }
  int prev = Inst_Current();
  int p = FindReceiver();
  if (p >= 0) {
    int r = g_mp.resident;
    Mp_SwapTo(p);
    item_receipt_method = 0;
    if (Ap_IsAlttpFile()) {
      uint8 give = AlttpReceive(code);
      if (give >= 0xfb && give <= 0xfe) {
        // A dungeon item of another dungeon than the one Link is in: held up
        // as such, but the receipt's own bookkeeping (for the dungeon Link is
        // in) is undone around it. The item was given above.
        static const uint8 kHeld[4] = { 0x33, 0x25, 0x32, 0x24 };
        uint8 keys = link_num_keys;
        uint16 maps = link_dungeon_map, compasses = link_compass, bigkeys = link_bigkey;
        Link_ReceiveItem(kHeld[give - 0xfb], 0);
        link_num_keys = keys;
        link_dungeon_map = maps, link_compass = compasses, link_bigkey = bigkeys;
      } else if (give != 0xff) {
        Link_ReceiveItem(give, 0);
      }
    } else {
      Link_ReceiveItem(code, 0);
    }
    SetItemCount(idx + 1);
    Mp_SwapTo(r);
  }
  Inst_Switch(prev);
}

// The room told what it is (kSessEv_ApRoom, kSessEv_ApRoom2): kept in the
// save of every world alike.
static void SetRoomBytes(const uint8 *ev) {
  int prev = Inst_Current();
  for (int i = 0; i < kInstMax; i++) {
    if (!Inst_IsActive(i))
      continue;
    Inst_Switch(i);
    if (!Ap_IsAlttpFile())
      continue;
    if (ev[0] == kSessEv_ApRoom) {
      SAVE(kApSave_Medallions) = ev[1];
      SAVE(kApSave_CrystalsGt) = ev[2];
      SAVE(kApSave_CrystalsGanon) = ev[3];
    } else {
      SAVE(kApSave_Goal) = ev[1];
      SAVE(kApSave_PiecesNeeded) = ev[2];
      SAVE(kApSave_PiecesNeeded + 1) = ev[3];
      if (ev[1] & kApGoalFlag_OpenPyramid)
        SAVE(0x280 + 0x5b) |= 0x20;  // the pyramid's hole (save_ow_event_info)
    }
  }
  Inst_Switch(prev);
}

// Is the goal of a file of the "A Link to the Past" world reached?
static bool AlttpGoalReached() {
  uint8 st = SAVE(kApSave_State);
  int goal = SAVE(kApSave_Goal) == 0xff ? kApGoal_Ganon : SAVE(kApSave_Goal) & 0xf;
  if (st & kApState_Triforce)
    return true;  // (the pedestal goal's Triforce, wherever it came from)
  int pieces_needed = SAVE(kApSave_PiecesNeeded) | SAVE(kApSave_PiecesNeeded + 1) << 8;
  switch (goal) {
  case kApGoal_Bosses:  // every dungeon's prize, Agahnim twice
    return PrizeCount() == 10 && sram_progress_indicator >= 3 && (SAVE(0x280 + 0x5b) & 0x20);
  case kApGoal_Pedestal:
    return false;  // (the Triforce comes from the pedestal)
  case kApGoal_GanonPedestal:
    return (st & kApState_GanonBeaten) && IsChecked(kApSpecialBit + kApLoc_MasterSwordPedestal);
  case kApGoal_TriforceHunt: case kApGoal_LocalTriforceHunt:
    return SAVE(kApSave_Goal) != 0xff && SAVE(kApSave_Pieces) >= pieces_needed;
  default:  // Ganon (the Triforce hunts that end with him too: the pieces aren't held against him)
    return (st & kApState_GanonBeaten) != 0;
  }
}

static void CheckGoal() {
  if (Ap_IsAlttpFile() && !(SAVE(kApSave_State) & kApState_Goal) && AlttpGoalReached())
    SAVE(kApSave_State) |= kApState_Goal;
}

// The DeathLink still to hand out is a save byte, so it is set and cleared
// in every world alike: the world that is current between frames is the
// local player's, different on each computer, and the save bytes of all
// worlds are merged after the frame.
static void SetDeathPending(bool on) {
  int prev = Inst_Current();
  for (int i = 0; i < kInstMax; i++) {
    if (!Inst_IsActive(i))
      continue;
    Inst_Switch(i);
    if (on)
      SAVE(kApSave_State) |= kApState_DeathPending;
    else
      SAVE(kApSave_State) &= ~kApState_DeathPending;
  }
  Inst_Switch(prev);
}

void Ap_ApplyEvent(const uint8 *ev) {
  if (!Ap_IsApFile())
    return;
  if (ev[0] == kSessEv_ApItem)
    GiveItem(ev[1], ev[2] | ev[3] << 8);
  else if (ev[0] == kSessEv_ApDeath)
    SetDeathPending(true);
  else if (ev[0] == kSessEv_ApRoom || ev[0] == kSessEv_ApRoom2)
    SetRoomBytes(ev);
}

// Is the team in a game over (the death sequence, up to Continue)?
static bool InGameOver() {
  int prev = Inst_Current();
  bool over = false;
  for (int i = 0; i < kInstMax && !over; i++) {
    if (Inst_IsActive(i)) {
      Inst_Switch(i);
      over = main_module_index == 18;
    }
  }
  Inst_Switch(prev);
  return over;
}

void Ap_BeforeFrame() {
  if (!Ap_IsApFile())
    return;
  CheckGoal();
  if (!(SAVE(kApSave_State) & kApState_DeathPending))
    return;
  // A DeathLink that comes during a game over of ours is over with it
  // (otherwise it would take the team down again right after Continue).
  if (InGameOver()) {
    SetDeathPending(false);
    return;
  }
  // Every standing Link falls; one in a menu or in a world that is busy (a
  // transition, a text box, a Link holding up an item that isn't added yet)
  // falls as soon as it is back in normal play.
  int prev = Inst_Current();
  bool over = false, waiting = false;
  for (int p = 0; p < kMpMaxPlayers && !over; p++) {
    int w = Inst_OfPlayer(p);
    if (w < 0)
      continue;
    Inst_Switch(w);
    if (!Mp_IsAlive(p))
      continue;
    if (Inst_PlayerInMenu(p) || !Mp_InStableGameplay() || WorldIsReceiving()) {
      waiting = true;
      continue;
    }
    int r = g_mp.resident;
    Mp_SwapTo(p);
    over = Mp_DeathLinkFall();  // the last one: game over (and p leads its world)
    if (!over)
      Mp_SwapTo(r);
  }
  Inst_Switch(prev);
  if (over || !waiting)
    SetDeathPending(false);
}

int Ap_CheckedLocations(const uint8 *save, int64 *ids, int max) {
  int n = 0;
  bool alttp = save[kApSave_World] == kApWorld_Alttp;
  for (int bit = 0; bit < kApSave_CheckedSize * 8 && n < max; bit++) {
    if (!(save[kApSave_Checked + (bit >> 3)] >> (bit & 7) & 1))
      continue;
    if (alttp) {
      if (bit < kAlttpNumChests && kAlttpChestIds[bit])
        ids[n++] = kAlttpChestIds[bit];
      else if (bit >= kApSpecialBit && bit < kApSpecialBit + kApNumSpecials && kAlttpSpecials[bit - kApSpecialBit].id)
        ids[n++] = kAlttpSpecials[bit - kApSpecialBit].id;
    } else if (bit < kApSpecialBit) {
      ids[n++] = kApIdBase + bit;
    } else if (bit < kApSpecialBit + kRando_NumSpecial) {
      ids[n++] = kApSpecialLocationBase + bit - kApSpecialBit;
    }
  }
  return n;
}

// ---------------------------------------------------------------------------
// The host

ApSettings g_ap_settings;

// Tests: a pretend room.
static struct {
  uint32 bind;
  bool death_link;
  ApWorld world;
  int alttp[kApAlttp_Count];
  int64 items[512];
  int nitems;
  int deaths_to_give;
  ApTestSent sent;
} g_test;

static struct {
  bool started;
  bool deathlink_on;
  bool death_to_give;   // a DeathLink to put into the next frame
  bool refused;         // the room is for another version of the world, or plays options this game can't
  uint32 file;          // the file (bind hash) whose checks were sent
  uint8 sent_checked[kApSave_CheckedSize];
  bool goal_sent;
  uint32 deaths_file;   // deaths_seen is for this file
  uint8 deaths_seen;
  uint32 warned_file;   // said that this file is for another room
  // Our own lines for the screen.
  char notice[8][96];
  uint8 notice_kind[8];
  int notice_rd, notice_wr;
} g_ap;

static void Notice(int kind, const char *fmt, const char *arg) {
  int i = g_ap.notice_wr % 8;
  snprintf(g_ap.notice[i], sizeof(g_ap.notice[i]), fmt, arg);
  g_ap.notice_kind[i] = kind;
  g_ap.notice_wr++;
  if (g_ap.notice_wr - g_ap.notice_rd > 8)
    g_ap.notice_rd = g_ap.notice_wr - 8;
}

void Ap_Connect() {
  ApConfig c = { 0 };
  c.server = g_ap_settings.server;
  c.slot = g_ap_settings.slot;
  c.password = g_ap_settings.password;
  c.game = Ap_WorldGameName(g_ap_settings.world == kApWorld_Fsotp ? kApWorld_Fsotp : kApWorld_Alttp);
  // DeathLink starts off in the client too (an earlier room may have turned
  // it on), until this room's slot_data says otherwise: else the tag would
  // go out with Connect and Ap_Tick, thinking it off, would never take it back.
  ApClient_SetDeathLink(false);
  ApClient_Start(&c);
  g_ap.started = true;
  g_ap.deathlink_on = false;
  g_ap.death_to_give = false;
  g_ap.refused = false;
  // A new connection knows nothing yet: everything is sent again. (Not
  // game overs: one from before is old news.)
  g_ap.file = 0;
  g_ap.goal_sent = false;
  g_ap.deaths_file = 0;
  g_ap.warned_file = 0;
}

void Ap_Disconnect() {
  ApClient_Stop();
  g_ap.started = false;
}

// (A refused connection is over: the menu offers to connect again.)
bool Ap_IsConnecting() {
  return g_ap.started && ApClient_GetStatus(NULL, 0) != kApStatus_Failed;
}

// The room we are connected to (or the tests' one).
static bool RoomInfo(ApRoom *room) {
  if (g_test.bind) {
    memset(room, 0, sizeof(*room));
    room->bind_hash = g_test.bind;
    room->world = g_test.world;
    room->slot_data_version = kApSlotDataVersion;
    room->slot_data_death_link = g_test.death_link;
    for (int i = 0; i < kApAlttp_Count; i++)
      room->alttp[i] = g_test.alttp[i];
    snprintf(room->slot_name, sizeof(room->slot_name), "Test");
    return true;
  }
  return Ap_IsConnecting() && ApClient_GetRoom(room);
}

// Why a room of the "A Link to the Past" world can't be played here, or NULL.
static const char *AlttpUnsupported(const ApRoom *room) {
  const int *o = room->alttp;
  if (o[kApAlttp_Mode] == 2)
    return "This room plays the inverted world, which this game can't";
  if (o[kApAlttp_EntranceShuffle] > 0)
    return "This room shuffles entrances, which this game can't";
  if (o[kApAlttp_Swordless] > 0)
    return "This room is swordless, which this game can't play";
  if (o[kApAlttp_RetroBow] > 0 || o[kApAlttp_RetroCaves] > 0)
    return "This room's retro options aren't in this game";
  if (o[kApAlttp_ShopItemSlots] > 0 || o[kApAlttp_RandomizeShops] > 0 || o[kApAlttp_ShuffleShops] > 0)
    return "This room shuffles shops, which this game can't";
  if (o[kApAlttp_BossShuffle] > 0 || o[kApAlttp_EnemyShuffle] > 0 || o[kApAlttp_PotShuffle] > 0)
    return "This room shuffles bosses, enemies or pots, which this game can't";
  if (o[kApAlttp_SmallKeyShuffle] == 5)
    return "This room uses universal keys, which this game can't";
  return NULL;
}

static int RoomItemCount() {
  return g_test.bind ? g_test.nitems : ApClient_ItemCount();
}

static bool RoomItem(int i, int64 *item) {
  if (!g_test.bind) {
    int64 location;
    int player;
    return ApClient_GetItem(i, item, &location, &player);
  }
  if (i < 0 || i >= g_test.nitems)
    return false;
  *item = g_test.items[i];
  return true;
}

static void RoomCheck(const int64 *ids, int n) {
  if (!g_test.bind) {
    ApClient_CheckLocations(ids, n);
    return;
  }
  g_test.sent.checks = n < 256 ? n : 256;
  memcpy(g_test.sent.check, ids, g_test.sent.checks * sizeof(int64));
}

static void RoomGoal() {
  if (g_test.bind)
    g_test.sent.goal = true;
  else
    ApClient_SetGoalReached();
}

static void RoomSetDeathLink(bool on) {
  if (!g_test.bind)
    ApClient_SetDeathLink(on);
}

static void RoomSendDeath(const char *cause) {
  if (g_test.bind)
    g_test.sent.deaths++;
  else
    ApClient_SendDeath(cause);
}

static bool RoomPollDeath(char *who, size_t size) {
  if (!g_test.bind)
    return ApClient_PollDeath(who, size);
  if (!g_test.deaths_to_give)
    return false;
  g_test.deaths_to_give--;
  snprintf(who, size, "Tester");
  return true;
}

// The room, if the game can play it.
static bool GetRoom(ApRoom *room) {
  if (g_ap.refused || !RoomInfo(room))
    return false;
  return room->world == kApWorld_Alttp ? !AlttpUnsupported(room) : room->slot_data_version == kApSlotDataVersion;
}

uint32 Ap_HostBind() {
  ApRoom room;
  return GetRoom(&room) ? room.bind_hash : 0;
}

ApWorld Ap_HostWorld() {
  ApRoom room;
  return GetRoom(&room) && room.world == kApWorld_Alttp ? kApWorld_Alttp : kApWorld_Fsotp;
}

void Ap_Tick() {
  // In someone else's online game the host talks to the room (both doing it
  // would send every death twice).
  if (g_ap.started && Session_GetStatus()->mode == kSessionClient) {
    Ap_Disconnect();
    Notice(kApText_Error, "%s", "Archipelago: in an online game, the host connects");
  }
  ApRoom room;
  if (!RoomInfo(&room)) {
    g_ap.deaths_file = 0;  // (game overs while not connected aren't sent later)
    return;
  }
  if (room.world == kApWorld_Alttp ? AlttpUnsupported(&room) != NULL : room.slot_data_version != kApSlotDataVersion) {
    if (!g_ap.refused) {
      g_ap.refused = true;
      Notice(kApText_Error, "%s", room.world == kApWorld_Alttp ? AlttpUnsupported(&room) :
             room.slot_data_version > kApSlotDataVersion ?
             "This Archipelago room needs a newer version of the game" :
             "This Archipelago room was made with an older fsotp.apworld");
    }
    return;
  }
  bool dl = g_ap_settings.death_link == 1 || (g_ap_settings.death_link == 0 && room.slot_data_death_link == 1);
  if (dl != g_ap.deathlink_on) {
    RoomSetDeathLink(dl);
    g_ap.deathlink_on = dl;
  }
  char who[64];
  while (RoomPollDeath(who, sizeof(who))) {
    if (dl) {
      g_ap.death_to_give = true;
      Notice(kApText_Trap, "DeathLink: %s died", who);
    }
  }
  // What happened in the game, as of the newest frame that can't change any more.
  const uint8 *save = Session_ConfirmedSaveArea();
  uint32 bind = SaveBind(save);
  if (bind != room.bind_hash) {
    g_ap.death_to_give = false;
    g_ap.deaths_file = 0;
    if (bind && bind != g_ap.warned_file) {
      g_ap.warned_file = bind;
      Notice(kApText_Error, "%s", "This save file belongs to another Archipelago room");
    }
    return;
  }
  if (g_ap.file != bind) {
    g_ap.file = bind;
    memset(g_ap.sent_checked, 0xff, sizeof(g_ap.sent_checked));  // (forces a send)
  }
  if (memcmp(g_ap.sent_checked, save + kApSave_Checked, kApSave_CheckedSize)) {
    int64 ids[kApSave_CheckedSize * 8];
    int n = Ap_CheckedLocations(save, ids, kApSave_CheckedSize * 8);
    RoomCheck(ids, n);
    memcpy(g_ap.sent_checked, save + kApSave_Checked, kApSave_CheckedSize);
  }
  if ((save[kApSave_State] & kApState_Goal) && !g_ap.goal_sent) {
    RoomGoal();
    g_ap.goal_sent = true;
  }
  // Game overs since the file was loaded. Only a new one is news: ones from
  // before this file was seen (or connected), and a count that went down (a
  // file was loaded, or a state) are taken as they are.
  uint8 deaths = save[kApSave_Deaths];
  if (g_ap.deaths_file != bind || deaths < g_ap.deaths_seen) {
    g_ap.deaths_file = bind;
    g_ap.deaths_seen = deaths;
  }
  if (deaths != g_ap.deaths_seen) {
    g_ap.deaths_seen = deaths;
    if (dl) {
      char cause[96];
      snprintf(cause, sizeof(cause), "%s fell in Hyrule.", room.slot_name);
      RoomSendDeath(cause);
    }
  }
}

bool Ap_NextEvent(uint8 ev[4]) {
  ApRoom room;
  if (!GetRoom(&room))
    return false;
  // The game as it is now (not the confirmed state): an item given in a frame
  // that a rollback replays differently is simply proposed again.
  const uint8 *save = &g_ram[0xF000];
  if (SaveBind(save) != room.bind_hash)
    return false;
  if (g_ap.death_to_give) {
    g_ap.death_to_give = false;
    ev[0] = kSessEv_ApDeath, ev[1] = ev[2] = ev[3] = 0;
    return true;
  }
  // A file of the "A Link to the Past" world that doesn't know its room yet.
  if (save[kApSave_World] == kApWorld_Alttp && save[kApSave_Medallions] == 0xff) {
    const int *o = room.alttp;
    int mm = o[kApAlttp_MmMedallion], tr = o[kApAlttp_TrMedallion];
    ev[0] = kSessEv_ApRoom;
    ev[1] = (mm < 0 ? 0xf : mm & 0xf) | (tr < 0 ? 0xf : tr & 0xf) << 4;
    ev[2] = o[kApAlttp_CrystalsGt] < 0 ? 7 : o[kApAlttp_CrystalsGt];
    ev[3] = o[kApAlttp_CrystalsGanon] < 0 ? 7 : o[kApAlttp_CrystalsGanon];
    return true;
  }
  if (save[kApSave_World] == kApWorld_Alttp && save[kApSave_Goal] == 0xff) {
    const int *o = room.alttp;
    int goal = o[kApAlttp_Goal] < 0 ? kApGoal_Ganon : o[kApAlttp_Goal] & 0xf;
    int op = o[kApAlttp_OpenPyramid];
    bool open = op == 1 || (op >= 2 && (goal == kApGoal_Crystals || goal == kApGoal_GanonPedestal ||
                                        goal == kApGoal_GanonTriforceHunt || goal == kApGoal_LocalGanonTriforceHunt));
    int pieces = o[kApAlttp_PiecesRequired] < 0 ? 0 : o[kApAlttp_PiecesRequired];
    ev[0] = kSessEv_ApRoom2;
    ev[1] = goal | (open ? kApGoalFlag_OpenPyramid : 0);
    ev[2] = (uint8)pieces;
    ev[3] = (uint8)(pieces >> 8);
    return true;
  }
  int have = save[kApSave_ItemCount] | save[kApSave_ItemCount + 1] << 8;
  int64 item;
  if (have >= RoomItemCount() || !RoomItem(have, &item))
    return false;
  bool alttp = save[kApSave_World] == kApWorld_Alttp;
  int64 code = alttp ? item : item - kApIdBase;  // (that world's ids are the codes themselves)
  bool gives = code >= 0 && code < 256 && (alttp ? AlttpGivesSomething((uint8)code) : IsPoolItem(code));
  // Only once a Link can take it (in the game as it is now; GiveItem looks
  // again in the frame itself). Netplay clients take a frame with an event
  // for a wrong guess and play it again: an item offered in every frame
  // while a text box is open would make them do that every frame.
  if (gives) {
    int prev = Inst_Current();
    bool ready = FindReceiver() >= 0;
    Inst_Switch(prev);
    if (!ready)
      return false;
  }
  ev[0] = kSessEv_ApItem;
  ev[1] = gives ? (uint8)code : 0xff;
  ev[2] = (uint8)have;
  ev[3] = (uint8)(have >> 8);
  return true;
}

void Ap_StatusLine(char *buf, size_t size) {
  if (!g_ap.started) {
    snprintf(buf, size, "Not connected");
    return;
  }
  ApClient_GetStatus(buf, size);
}

bool Ap_PollNotice(char *buf, size_t size, int *kind) {
  bool got = false;
  if (Session_GetStatus()->mode == kSessionClient) {
    got = Session_PollSharedNotice(buf, size, kind);
  } else if (g_ap.notice_rd != g_ap.notice_wr) {
    int i = g_ap.notice_rd++ % 8;
    snprintf(buf, size, "%s", g_ap.notice[i]);
    *kind = g_ap.notice_kind[i];
    got = true;
  } else if (g_ap.started) {
    ApTextKind k;
    got = ApClient_PollText(buf, size, &k);
    *kind = k;
  }
  // The players on the other computers see the same lines.
  if (got && Session_GetStatus()->mode == kSessionHost)
    Session_ShareNotice(buf, *kind);
  return got;
}

void Ap_OnStateLoaded() {
  g_ap.deaths_file = 0;  // (its game overs aren't new ones)
}

void Ap_TestRoom(uint32 bind, bool death_link, ApWorld world) {
  memset(&g_test, 0, sizeof(g_test));
  g_test.bind = bind;
  g_test.death_link = death_link;
  g_test.world = world;
  for (int i = 0; i < kApAlttp_Count; i++)
    g_test.alttp[i] = -1;
  g_ap.deathlink_on = false;
  g_ap.file = 0;
  g_ap.goal_sent = false;
  g_ap.deaths_file = 0;
}

void Ap_TestGiveItem(int64 item) {
  if (g_test.nitems < (int)countof(g_test.items))
    g_test.items[g_test.nitems++] = item;
}

void Ap_TestSetOption(int which, int value) {
  if (which >= 0 && which < kApAlttp_Count)
    g_test.alttp[which] = value;
}

void Ap_TestDeathLink() {
  g_test.deaths_to_give++;
}

const ApTestSent *Ap_TestSent() {
  return &g_test.sent;
}
#endif  // MOD_ARCHIPELAGO
