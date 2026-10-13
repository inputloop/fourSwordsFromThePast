#include "archipelago.h"
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
  // A DeathLink from the last time the file was played is over.
  if (Ap_IsApFile())
    SAVE(kApSave_State) &= ~kApState_DeathPending;
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

// Can the resident Link hold up an item now? Standing on the ground and free,
// like after opening a chest. (A bunny or a swimming Link waits: holding up
// the item would make it a normal Link on land.)
static bool ResidentCanReceive() {
  return link_player_handler_state == kPlayerState_Ground && !link_auxiliary_state &&
         !item_receipt_method && !flag_is_link_immobilized && !link_state_bits &&
         !link_picking_throw_state && !link_incapacitated_timer;
}

// Is a Link of the current world holding up an item? (The next one waits:
// one at a time, like chests.)
static bool WorldIsReceiving() {
  for (int i = 0; i < 10; i++)
    if (ancilla_type[i] == 0x22)
      return true;
  return false;
}

// Item number idx of the server's list. It goes to the first player (by
// number) who can take it now; if nobody can, the host proposes it again with
// the next frame.
static void GiveItem(uint8 code, uint16 idx) {
  if (idx != ItemCount())
    return;  // already received, or an older proposal
  if (code >= kApPlaceholderItem) {
    SetItemCount(idx + 1);  // nothing this game knows: just skip it
    return;
  }
  int prev = Inst_Current();
  for (int p = 0; p < kMpMaxPlayers; p++) {
    int w = Inst_OfPlayer(p);
    if (w < 0 || Inst_PlayerInMenu(p))
      continue;
    Inst_Switch(w);
    if (!Mp_InStableGameplay() || !Mp_IsAlive(p) || WorldIsReceiving())
      continue;
    int r = g_mp.resident;
    Mp_SwapTo(p);
    bool ok = ResidentCanReceive();
    if (ok) {
      item_receipt_method = 0;
      Link_ReceiveItem(code, 0);
      SetItemCount(idx + 1);
    }
    Mp_SwapTo(r);
    if (ok)
      break;
  }
  Inst_Switch(prev);
}

void Ap_ApplyEvent(const uint8 *ev) {
  if (!Ap_IsApFile())
    return;
  if (ev[0] == kSessEv_ApItem)
    GiveItem(ev[1], ev[2] | ev[3] << 8);
  else if (ev[0] == kSessEv_ApDeath)
    SAVE(kApSave_State) |= kApState_DeathPending;
}

void Ap_BeforeFrame() {
  if (!Ap_IsApFile() || !(SAVE(kApSave_State) & kApState_DeathPending))
    return;
  // Every standing Link falls; one in a menu or in a world that is busy (a
  // transition, a text box) falls as soon as it is back in normal play.
  int prev = Inst_Current();
  bool over = false, waiting = false;
  for (int p = 0; p < kMpMaxPlayers && !over; p++) {
    int w = Inst_OfPlayer(p);
    if (w < 0)
      continue;
    Inst_Switch(w);
    if (!Mp_IsAlive(p))
      continue;
    if (Inst_PlayerInMenu(p) || !Mp_InStableGameplay()) {
      waiting = true;
      continue;
    }
    int r = g_mp.resident;
    Mp_SwapTo(p);
    over = Mp_DeathLinkFall();  // the last one: game over (and p leads its world)
    if (!over)
      Mp_SwapTo(r);
  }
  if (over || !waiting)
    SAVE(kApSave_State) &= ~kApState_DeathPending;
  Inst_Switch(prev);
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
  ApClient_Start(&c);
  g_ap.started = true;
  g_ap.deathlink_on = false;
  g_ap.death_to_give = false;
  g_ap.refused = false;
  // A new connection knows nothing yet: everything is sent again.
  g_ap.file = 0;
  g_ap.goal_sent = false;
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
  if (!RoomInfo(&room))
    return;
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
  // Deaths from before this file was seen aren't news.
  if (g_ap.deaths_file != bind) {
    g_ap.deaths_file = bind;
    g_ap.deaths_seen = save[kApSave_Deaths];
  }
  if (save[kApSave_Deaths] != g_ap.deaths_seen) {
    g_ap.deaths_seen = save[kApSave_Deaths];
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
  ev[0] = kSessEv_ApItem;
  ev[1] = code >= 0 && code < kApPlaceholderItem ? (uint8)code : 0xff;
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

void Ap_TestRoom(uint32 bind, bool death_link) {
  memset(&g_test, 0, sizeof(g_test));
  g_test.bind = bind;
  g_test.death_link = death_link;
  g_ap.deathlink_on = false;
  g_ap.file = 0;
  g_ap.goal_sent = false;
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
