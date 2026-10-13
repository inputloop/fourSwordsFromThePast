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

static uint32 g_new_game_bind;

void Ap_SetNewGameBind(uint32 bind) {
  g_new_game_bind = bind;
}

uint32 Ap_GetNewGameBind() {
  return g_new_game_bind;
}

void Ap_OnNewFile(uint8 *sram_slot) {
  memset(sram_slot + kApSave_Marker, 0, kApSave_Checked + kApSave_CheckedSize - kApSave_Marker);
  if (!g_new_game_bind)
    return;
  sram_slot[kApSave_Marker] = kApMarker;
  memcpy(sram_slot + kApSave_Bind, &g_new_game_bind, 4);
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

static uint32 SaveBind(const uint8 *save) {
  uint32 bind;
  memcpy(&bind, save + kApSave_Bind, 4);
  return save[kApSave_Marker] == kApMarker ? bind : 0;
}

uint32 Ap_FileBind() {
  return SaveBind(&g_ram[0xF000]);
}

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
  if (Ap_IsApFile())
    SAVE(kApSave_State) |= kApState_Goal;
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
static void GiveItem(uint8 code, uint16 idx) {
  if (idx != ItemCount())
    return;  // already received, or an older proposal
  if (!IsPoolItem(code)) {
    SetItemCount(idx + 1);  // nothing our world sends: just skip it
    return;
  }
  int prev = Inst_Current();
  int p = FindReceiver();
  if (p >= 0) {
    int r = g_mp.resident;
    Mp_SwapTo(p);
    item_receipt_method = 0;
    Link_ReceiveItem(code, 0);
    SetItemCount(idx + 1);
    Mp_SwapTo(r);
  }
  Inst_Switch(prev);
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
  if (!Ap_IsApFile() || !(SAVE(kApSave_State) & kApState_DeathPending))
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
  for (int bit = 0; bit < kApSave_CheckedSize * 8 && n < max; bit++) {
    if (!(save[kApSave_Checked + (bit >> 3)] >> (bit & 7) & 1))
      continue;
    if (bit < kApSpecialBit)
      ids[n++] = kApIdBase + bit;
    else if (bit < kApSpecialBit + kRando_NumSpecial)
      ids[n++] = kApSpecialLocationBase + bit - kApSpecialBit;
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
  int64 items[512];
  int nitems;
  int deaths_to_give;
  ApTestSent sent;
} g_test;

static struct {
  bool started;
  bool deathlink_on;
  bool death_to_give;   // a DeathLink to put into the next frame
  bool refused;         // the room is for another version of the world
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
    room->slot_data_version = kApSlotDataVersion;
    room->slot_data_death_link = g_test.death_link;
    snprintf(room->slot_name, sizeof(room->slot_name), "Test");
    return true;
  }
  return Ap_IsConnecting() && ApClient_GetRoom(room);
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
  return !g_ap.refused && RoomInfo(room) && room->slot_data_version == kApSlotDataVersion;
}

uint32 Ap_HostBind() {
  ApRoom room;
  return GetRoom(&room) ? room.bind_hash : 0;
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
  if (room.slot_data_version != kApSlotDataVersion) {
    if (!g_ap.refused) {
      g_ap.refused = true;
      Notice(kApText_Error, "%s", room.slot_data_version > kApSlotDataVersion ?
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
  int have = save[kApSave_ItemCount] | save[kApSave_ItemCount + 1] << 8;
  int64 item;
  if (have >= RoomItemCount() || !RoomItem(have, &item))
    return false;
  int64 code = item - kApIdBase;
  // Only once a Link can take it (in the game as it is now; GiveItem looks
  // again in the frame itself). Netplay clients take a frame with an event
  // for a wrong guess and play it again: an item offered in every frame
  // while a text box is open would make them do that every frame.
  if (IsPoolItem(code)) {
    int prev = Inst_Current();
    bool ready = FindReceiver() >= 0;
    Inst_Switch(prev);
    if (!ready)
      return false;
  }
  ev[0] = kSessEv_ApItem;
  ev[1] = IsPoolItem(code) ? (uint8)code : 0xff;
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

void Ap_TestRoom(uint32 bind, bool death_link) {
  memset(&g_test, 0, sizeof(g_test));
  g_test.bind = bind;
  g_test.death_link = death_link;
  g_ap.deathlink_on = false;
  g_ap.file = 0;
  g_ap.goal_sent = false;
  g_ap.deaths_file = 0;
}

void Ap_TestGiveItem(int64 item) {
  if (g_test.nitems < (int)countof(g_test.items))
    g_test.items[g_test.nitems++] = item;
}

void Ap_TestDeathLink() {
  g_test.deaths_to_give++;
}

const ApTestSent *Ap_TestSent() {
  return &g_test.sent;
}
