// Archipelago client: the JSON protocol on top of ap_ws, on a thread of its
// own. The thread owns the connection; the game talks to it through g_ap,
// which only ever changes under g_ap.mutex, and never waits for the network.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include "ap_client.h"
#include "ap_ws.h"
#include "third_party/cJSON/cJSON.h"

enum {
  kMaxTexts = 32,
  kTextLen = 200,
  kMaxDeathsIn = 8,
  kMaxDeathsOut = 4,
  kConnectTimeoutMs = 10000,
  kHandshakeTimeoutMs = 60000,  // from the WebSocket opening to Connected
  kPingAfterMs = 30000,         // the server pings every 20 s; quiet for longer: ask
  kDeadAfterMs = 60000,         // and give up on it after this
  kFirstRetryMs = 2000,
  kMaxRetryMs = 30000,
  kItemsHandling = 7,           // every item comes from the server, our own finds too
  // Far more than a real room has. A server that sends more (games in a
  // room, names in a game) doesn't get them all read: lists that long would
  // keep the network thread busy for minutes.
  kMaxGames = 2048,
  kMaxNames = 1 << 18,
};

typedef struct ApItemEntry {
  int64 item, location;
  int player;
} ApItemEntry;

typedef struct ApText {
  char text[kTextLen];
  ApTextKind kind;
} ApText;

typedef struct ApAddr {
  char host[256], path[256], display[300];
  int port;
  int scheme;  // kScheme_*
} ApAddr;

enum { kScheme_Either, kScheme_Ws, kScheme_Wss };

// Shared between the game and the network thread (under mutex).
static struct {
  SDL_mutex *mutex;
  SDL_Thread *thread;
  SDL_atomic_t stop;
  bool debug;
  // Set by ApClient_Start before the thread starts; read-only while it runs.
  char slot[64], password[128], game[128], uuid[64];
  ApAddr addr;

  ApStatus status;
  char status_text[256];
  uint32 retry_at;  // SDL_GetTicks() of the next attempt while waiting to retry
  bool have_room;
  ApRoom room;
  ApItemEntry *items;
  int num_items, items_cap;
  int64 *checks;        // sorted
  int num_checks, checks_cap;
  int64 *new_checks;    // not sent yet
  int num_new, new_cap;
  bool goal;
  bool death_link, tags_dirty;
  char deaths_out[kMaxDeathsOut][128];
  int num_deaths_out;
  char deaths_in[kMaxDeathsIn][64];
  int num_deaths_in;
  ApText texts[kMaxTexts];
  int text_first, num_texts;
} g_ap;

// Names from the server's data package, per game.
typedef struct ApName {
  int64 id;
  const char *name;
} ApName;

typedef struct ApGameData {
  char *game, *checksum, *item_pool, *location_pool;
  ApName *items, *locations;
  int num_items, num_locations;
} ApGameData;

typedef struct ApSlotInfo {
  int slot;
  char name[64];
  char *game;
  int *members;  // for item link groups
  int num_members;
} ApSlotInfo;

typedef struct ApPlayer {
  int slot;
  char alias[64];
} ApPlayer;

enum { kPhase_RoomInfo, kPhase_DataPackage, kPhase_Connect, kPhase_Connected };

// One connection, network thread only.
typedef struct Conn {
  ApWs *ws;
  bool tls;
  int phase;
  int pending_packages;
  char **wanted;  // the games whose names we asked for
  int num_wanted, wanted_cap;
  char seed_name[64];
  int team, slot;
  ApItemEntry *items;  // what the server sent on this connection
  int num_items, items_cap;
  ApPlayer *players;
  int num_players;
  ApSlotInfo *slots;
  int num_slots;
  bool goal_sent, pinged;
} Conn;

// Network thread only; lives from ApClient_Start to ApClient_Stop.
static struct {
  ApGameData *games;
  int num_games, games_cap;
  int preferred_tls;      // -1: unknown, else what worked last time
  double sent_deaths[4];  // time values of our recent DeathLinks, to spot their echo
  int sent_death_next;
  bool had_connection, outage_announced;
  char seed_name[64];
  int team, slot;
} g_t;

static void Lock(void) {
  if (!g_ap.mutex)
    g_ap.mutex = SDL_CreateMutex();
  SDL_LockMutex(g_ap.mutex);
}

static void Unlock(void) {
  SDL_UnlockMutex(g_ap.mutex);
}

static void Dbg(const char *fmt, ...) {
  if (!g_ap.debug)
    return;
  va_list va;
  va_start(va, fmt);
  fprintf(stderr, "[ap] ");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
}

// ---- small helpers ----

static void *Grow(void *p, int *cap, int need, size_t elem) {
  if (need <= *cap)
    return p;
  int n = *cap ? *cap : 16;
  while (n < need)
    n *= 2;
  *cap = n;
  return realloc(p, n * elem);
}

static char *DupStr(const char *s) {
  size_t n = strlen(s) + 1;
  char *r = malloc(n);
  memcpy(r, s, n);
  return r;
}

// Printable ASCII for the game's font: every other character (UTF-8
// sequence) becomes one '?'.
static void Sanitize(char *dst, size_t size, const char *src) {
  size_t n = 0;
  const unsigned char *s = (const unsigned char *)src;
  while (*s && n + 1 < size) {
    unsigned char c = *s++;
    if (c >= 0x20 && c < 0x7f) {
      dst[n++] = c;
    } else if (c == '\n' || c == '\t' || c == '\r') {
      dst[n++] = ' ';
    } else {
      dst[n++] = '?';
      if (c >= 0xc0)
        while ((*s & 0xc0) == 0x80)
          s++;
    }
  }
  if (*s && size > 4)
    memcpy(dst + size - 4, "...", 3), n = size - 1;
  dst[n] = 0;
}

// int64 to decimal and back by hand: not every C library the game is built
// with (MSVCRT) knows the standard printf/strtoll spellings for 64 bits.
static const char *I64ToStr(int64 v, char buf[24]) {
  char *p = buf + 23;
  uint64 u = v < 0 ? 0 - (uint64)v : (uint64)v;
  *p = 0;
  do {
    *--p = (char)('0' + u % 10);
    u /= 10;
  } while (u);
  if (v < 0)
    *--p = '-';
  return p;
}

static int64 StrToI64(const char *s) {
  bool neg = *s == '-';
  uint64 u = 0;
  for (s += neg; *s >= '0' && *s <= '9'; s++)
    u = u * 10 + (*s - '0');
  return neg ? (int64)(0 - u) : (int64)u;
}

static uint32 Fnv1a(const char *s) {
  uint32 h = 0x811c9dc5;
  for (; *s; s++)
    h = (h ^ (uint8)*s) * 0x01000193;
  return h;
}

// ---- shared state (callers hold no lock) ----

static void AddText(ApTextKind kind, const char *fmt, ...) {
  char raw[512];
  va_list va;
  va_start(va, fmt);
  vsnprintf(raw, sizeof(raw), fmt, va);
  va_end(va);
  Dbg("text: %s", raw);
  Lock();
  if (g_ap.num_texts == kMaxTexts) {  // full: the oldest line goes
    g_ap.text_first = (g_ap.text_first + 1) % kMaxTexts;
    g_ap.num_texts--;
  }
  ApText *t = &g_ap.texts[(g_ap.text_first + g_ap.num_texts++) % kMaxTexts];
  Sanitize(t->text, sizeof(t->text), raw);
  t->kind = kind;
  Unlock();
}

static void SetStatus(ApStatus status, uint32 retry_at, const char *fmt, ...) {
  char text[256];
  va_list va;
  va_start(va, fmt);
  vsnprintf(text, sizeof(text), fmt, va);
  va_end(va);
  Dbg("status %d: %s", status, text);
  Lock();
  g_ap.status = status;
  g_ap.retry_at = retry_at;
  Sanitize(g_ap.status_text, sizeof(g_ap.status_text), text);
  Unlock();
}

// ---- outgoing JSON ----

typedef struct Buf {
  char *p;
  size_t len, cap;
} Buf;

static void BufAdd(Buf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2 + 256;
    b->p = realloc(b->p, b->cap);
  }
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = 0;
}

#define BufLit(b, s) BufAdd(b, s, sizeof(s) - 1)

static void BufPrintf(Buf *b, const char *fmt, ...) {
  char tmp[256];
  va_list va;
  va_start(va, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, va);
  va_end(va);
  BufAdd(b, tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
}

// A JSON string; UTF-8 passes through.
static void BufStr(Buf *b, const char *s) {
  BufLit(b, "\"");
  for (; *s; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\')
      BufPrintf(b, "\\%c", c);
    else if (c < 0x20)
      BufPrintf(b, "\\u%04x", c);
    else
      BufAdd(b, s, 1);
  }
  BufLit(b, "\"");
}

static void BufIds(Buf *b, const int64 *ids, int n) {
  char num[24];
  BufLit(b, "[");
  for (int i = 0; i < n; i++)
    BufPrintf(b, i ? ",%s" : "%s", I64ToStr(ids[i], num));
  BufLit(b, "]");
}

static void BufTags(Buf *b, bool death_link) {
  if (death_link)
    BufLit(b, "[\"AP\",\"DeathLink\"]");
  else
    BufLit(b, "[\"AP\"]");
}

static bool SendBuf(Conn *c, Buf *b) {
  if (!b->p)
    return true;
  Dbg(">> %.300s", b->p);
  bool ok = ApWs_SendText(c->ws, b->p, b->len);
  free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
  return ok;
}

// ---- incoming JSON ----

static const char *GetStr(const cJSON *o, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int64 GetNum(const cJSON *o, const char *key, int64 def) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsNumber(v))
    return (int64)v->valuedouble;
  if (cJSON_IsBool(v))
    return cJSON_IsTrue(v);
  if (cJSON_IsString(v) && v->valuestring[0])  // ids in JSON message parts are strings
    return StrToI64(v->valuestring);
  return def;
}

// ---- names ----

static int CompareNames(const void *a, const void *b) {
  int64 x = ((const ApName *)a)->id, y = ((const ApName *)b)->id;
  return x < y ? -1 : x > y;
}

static void FreeGame(ApGameData *g) {
  free(g->game);
  free(g->checksum);
  free(g->item_pool);
  free(g->location_pool);
  free(g->items);
  free(g->locations);
}

static void FreeGames(void) {
  for (int i = 0; i < g_t.num_games; i++)
    FreeGame(&g_t.games[i]);
  free(g_t.games);
  g_t.games = NULL;
  g_t.num_games = g_t.games_cap = 0;
}

static ApGameData *FindGame(const char *game) {
  for (int i = 0; game && i < g_t.num_games; i++)
    if (!strcmp(g_t.games[i].game, game))
      return &g_t.games[i];
  return NULL;
}

// name_to_id: {"name": id, ...} into a sorted table, the names in one block.
static ApName *ReadNames(const cJSON *name_to_id, int *count, char **pool) {
  size_t chars = 0;
  int n = 0;
  const cJSON *e;
  cJSON_ArrayForEach(e, name_to_id) {
    if (n == kMaxNames)
      break;
    if (cJSON_IsNumber(e) && e->string)
      chars += strlen(e->string) + 1, n++;
  }
  ApName *names = malloc(sizeof(ApName) * (n ? n : 1));
  char *p = *pool = malloc(chars ? chars : 1);
  n = 0;
  cJSON_ArrayForEach(e, name_to_id) {
    if (n == kMaxNames)
      break;
    if (cJSON_IsNumber(e) && e->string) {
      size_t len = strlen(e->string) + 1;
      memcpy(p, e->string, len);
      names[n].id = (int64)e->valuedouble;
      names[n++].name = p;
      p += len;
    }
  }
  qsort(names, n, sizeof(ApName), CompareNames);
  *count = n;
  return names;
}

static void StoreGame(const char *game, const cJSON *data) {
  ApGameData *g = FindGame(game);
  if (g) {
    FreeGame(g);
  } else {
    g_t.games = Grow(g_t.games, &g_t.games_cap, g_t.num_games + 1, sizeof(ApGameData));
    g = &g_t.games[g_t.num_games++];
  }
  memset(g, 0, sizeof(*g));
  g->game = DupStr(game);
  const char *checksum = GetStr(data, "checksum");
  g->checksum = checksum ? DupStr(checksum) : NULL;
  g->items = ReadNames(cJSON_GetObjectItemCaseSensitive(data, "item_name_to_id"), &g->num_items, &g->item_pool);
  g->locations = ReadNames(cJSON_GetObjectItemCaseSensitive(data, "location_name_to_id"), &g->num_locations,
                           &g->location_pool);
  Dbg("data package for %s: %d items, %d locations", game, g->num_items, g->num_locations);
}

static const char *LookUp(const ApName *names, int n, int64 id) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (names[mid].id == id)
      return names[mid].name;
    if (names[mid].id < id)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return NULL;
}

static const ApSlotInfo *FindSlot(Conn *c, int slot) {
  for (int i = 0; i < c->num_slots; i++)
    if (c->slots[i].slot == slot)
      return &c->slots[i];
  return NULL;
}

static const char *SlotGame(Conn *c, int slot) {
  const ApSlotInfo *s = FindSlot(c, slot);
  return s ? s->game : slot == 0 ? "Archipelago" : NULL;
}

static const char *PlayerName(Conn *c, int slot, char *tmp, size_t size) {
  for (int i = 0; i < c->num_players; i++)
    if (c->players[i].slot == slot)
      return c->players[i].alias;
  const ApSlotInfo *s = FindSlot(c, slot);
  if (s)
    return s->name;
  if (slot == 0)
    return "Archipelago";
  snprintf(tmp, size, "Player %d", slot);
  return tmp;
}

// Item and location ids only mean something within the game of a slot.
static const char *ItemName(Conn *c, int64 id, int slot, char *tmp, size_t size) {
  ApGameData *g = FindGame(SlotGame(c, slot));
  const char *name = g ? LookUp(g->items, g->num_items, id) : NULL;
  if (name)
    return name;
  char num[24];
  snprintf(tmp, size, "Item %s", I64ToStr(id, num));
  return tmp;
}

static const char *LocationName(Conn *c, int64 id, int slot, char *tmp, size_t size) {
  ApGameData *g = FindGame(SlotGame(c, slot));
  const char *name = g ? LookUp(g->locations, g->num_locations, id) : NULL;
  if (name)
    return name;
  char num[24];
  snprintf(tmp, size, "Location %s", I64ToStr(id, num));
  return tmp;
}

// Our slot, or an item link group we are in.
static bool ConcernsUs(Conn *c, int slot) {
  if (slot == c->slot)
    return true;
  const ApSlotInfo *s = FindSlot(c, slot);
  for (int i = 0; s && i < s->num_members; i++)
    if (s->members[i] == c->slot)
      return true;
  return false;
}

static ApTextKind KindFromFlags(int64 flags) {
  return flags & 1 ? kApText_Progression : flags & 2 ? kApText_Useful : flags & 4 ? kApText_Trap : kApText_Normal;
}

static void FreeConn(Conn *c) {
  for (int i = 0; i < c->num_slots; i++) {
    free(c->slots[i].game);
    free(c->slots[i].members);
  }
  free(c->slots);
  free(c->players);
  free(c->items);
  for (int i = 0; i < c->num_wanted; i++)
    free(c->wanted[i]);
  free(c->wanted);
  memset(c, 0, sizeof(*c));
}

// ---- sending ----

static void SendConnect(Conn *c) {
  Lock();
  bool death_link = g_ap.death_link;
  g_ap.tags_dirty = false;  // the tags go with this Connect
  Unlock();
  Buf b = { 0 };
  BufLit(&b, "[{\"cmd\":\"Connect\",\"password\":");
  if (g_ap.password[0])
    BufStr(&b, g_ap.password);
  else
    BufLit(&b, "null");
  BufLit(&b, ",\"game\":");
  BufStr(&b, g_ap.game);
  BufLit(&b, ",\"name\":");
  BufStr(&b, g_ap.slot);
  BufLit(&b, ",\"uuid\":");
  BufStr(&b, g_ap.uuid);
  BufPrintf(&b, ",\"version\":{\"major\":0,\"minor\":6,\"build\":9,\"class\":\"Version\"},"
                "\"items_handling\":%d,\"tags\":", kItemsHandling);
  BufTags(&b, death_link);
  BufLit(&b, ",\"slot_data\":true}]");
  SendBuf(c, &b);
  c->phase = kPhase_Connect;
}

// Everything checked so far (after a reconnect, or when the server lost track).
static bool SendAllChecks(Conn *c, bool with_sync) {
  Lock();
  int n = g_ap.num_checks;
  int64 *ids = malloc(sizeof(int64) * (n ? n : 1));
  if (n)
    memcpy(ids, g_ap.checks, sizeof(int64) * n);
  g_ap.num_new = 0;  // part of the whole set
  Unlock();
  Buf b = { 0 };
  BufLit(&b, "[");
  if (with_sync)
    BufLit(&b, "{\"cmd\":\"Sync\"}");
  if (n) {
    if (with_sync)
      BufLit(&b, ",");
    BufLit(&b, "{\"cmd\":\"LocationChecks\",\"locations\":");
    BufIds(&b, ids, n);
    BufLit(&b, "}");
  }
  BufLit(&b, "]");
  free(ids);
  if (!with_sync && !n) {
    free(b.p);
    return true;
  }
  return SendBuf(c, &b);
}

// What the game asked for since the last call.
static bool SendOutbound(Conn *c) {
  Lock();
  int n = g_ap.num_new;
  int64 *ids = n ? malloc(sizeof(int64) * n) : NULL;
  if (n)
    memcpy(ids, g_ap.new_checks, sizeof(int64) * n);
  g_ap.num_new = 0;
  bool goal = g_ap.goal && !c->goal_sent;
  bool tags = g_ap.tags_dirty, death_link = g_ap.death_link;
  g_ap.tags_dirty = false;
  int num_deaths = g_ap.num_deaths_out;
  char deaths[kMaxDeathsOut][128];
  memcpy(deaths, g_ap.deaths_out, sizeof(deaths));
  g_ap.num_deaths_out = 0;
  Unlock();

  Buf b = { 0 };
  if (n) {
    BufLit(&b, "[{\"cmd\":\"LocationChecks\",\"locations\":");
    BufIds(&b, ids, n);
    BufLit(&b, "}]");
    free(ids);
  }
  bool ok = SendBuf(c, &b);
  if (goal) {
    BufLit(&b, "[{\"cmd\":\"StatusUpdate\",\"status\":30}]");
    ok = SendBuf(c, &b) && ok;
    c->goal_sent = true;
  }
  if (tags) {
    BufLit(&b, "[{\"cmd\":\"ConnectUpdate\",\"tags\":");
    BufTags(&b, death_link);
    BufLit(&b, "}]");
    ok = SendBuf(c, &b) && ok;
  }
  for (int i = 0; i < num_deaths; i++) {
    // Seconds with 6 decimals (written by hand: no locale's decimal comma);
    // remember the value as the server will echo it back, to recognize our
    // own death.
    int64 micros = (int64)(ApWs_WallClock() * 1e6);
    char time_text[40], num[24];
    snprintf(time_text, sizeof(time_text), "%s.%06d", I64ToStr(micros / 1000000, num), (int)(micros % 1000000));
    g_t.sent_deaths[g_t.sent_death_next++ & 3] = micros / 1e6;
    BufPrintf(&b, "[{\"cmd\":\"Bounce\",\"tags\":[\"DeathLink\"],\"data\":{\"time\":%s,\"source\":", time_text);
    BufStr(&b, g_ap.slot);
    BufLit(&b, ",\"cause\":");
    BufStr(&b, deaths[i]);
    BufLit(&b, "}}]");
    ok = SendBuf(c, &b) && ok;
  }
  return ok;
}

// ---- handling server commands ----
// Each returns 0, or -1 when the session has to end for good (status set).

static bool HaveGame(const char *game, const char *checksum) {
  ApGameData *g = FindGame(game);
  return g && checksum && g->checksum && !strcmp(g->checksum, checksum);
}

static bool Wanted(Conn *c, const char *game) {
  for (int i = 0; i < c->num_wanted; i++)
    if (!strcmp(c->wanted[i], game))
      return true;
  return false;
}

// Asks for the names of a game, unless we have them, asked already, or it
// has no data package (no checksum; the list is read up to kMaxGames).
static void AskForNames(Conn *c, Buf *b, const char *game, const cJSON *checksums) {
  if (c->num_wanted == kMaxGames || Wanted(c, game))
    return;
  const cJSON *sum = NULL, *e;
  int seen = 0;
  cJSON_ArrayForEach(e, checksums) {
    if (++seen > kMaxGames)
      break;
    if (e->string && !strcmp(e->string, game)) {
      sum = e;
      break;
    }
  }
  if (!sum || HaveGame(game, cJSON_IsString(sum) ? sum->valuestring : NULL))
    return;
  BufAdd(b, c->num_wanted ? "," : "[", 1);
  BufLit(b, "{\"cmd\":\"GetDataPackage\",\"games\":[");
  BufStr(b, game);
  BufLit(b, "]}");
  c->wanted = Grow(c->wanted, &c->wanted_cap, c->num_wanted + 1, sizeof(char *));
  c->wanted[c->num_wanted++] = DupStr(game);
}

static int OnRoomInfo(Conn *c, const cJSON *cmd) {
  if (c->phase != kPhase_RoomInfo)
    return 0;
  const char *seed = GetStr(cmd, "seed_name");
  snprintf(c->seed_name, sizeof(c->seed_name), "%s", seed ? seed : "");
  const cJSON *games = cJSON_GetObjectItemCaseSensitive(cmd, "games");
  const cJSON *checksums = cJSON_GetObjectItemCaseSensitive(cmd, "datapackage_checksums");
  // Names are needed for every game in the room (and the server's own,
  // "Archipelago"). Ask for each game alone, so that one message never has
  // to hold the names of a huge multiworld.
  Buf b = { 0 };
  AskForNames(c, &b, "Archipelago", checksums);
  const cJSON *g;
  int seen = 0;
  cJSON_ArrayForEach(g, games) {
    if (++seen > kMaxGames)
      break;
    if (cJSON_IsString(g))
      AskForNames(c, &b, g->valuestring, checksums);
  }
  if (!c->num_wanted) {
    SendConnect(c);
    return 0;
  }
  BufLit(&b, "]");
  c->pending_packages = c->num_wanted;
  c->phase = kPhase_DataPackage;
  SendBuf(c, &b);
  return 0;
}

static int OnDataPackage(Conn *c, const cJSON *cmd) {
  const cJSON *data = cJSON_GetObjectItemCaseSensitive(cmd, "data");
  const cJSON *games = cJSON_GetObjectItemCaseSensitive(data, "games");
  const cJSON *g;
  int seen = 0;
  cJSON_ArrayForEach(g, games) {
    if (++seen > kMaxGames)  // more than we can have asked for
      break;
    if (g->string && cJSON_IsObject(g) && Wanted(c, g->string))
      StoreGame(g->string, g);
  }
  if (c->phase == kPhase_DataPackage && --c->pending_packages <= 0)
    SendConnect(c);
  return 0;
}

static int OnConnectionRefused(Conn *c, const cJSON *cmd) {
  const cJSON *errors = cJSON_GetObjectItemCaseSensitive(cmd, "errors");
  bool has[5] = { 0 };
  static const char *const kCodes[5] = {
    "InvalidSlot", "InvalidGame", "IncompatibleVersion", "InvalidItemsHandling", "InvalidPassword",
  };
  const cJSON *e;
  const char *other = NULL;
  cJSON_ArrayForEach(e, errors) {
    if (!cJSON_IsString(e))
      continue;
    bool known = false;
    for (int i = 0; i < 5; i++)
      if (!strcmp(e->valuestring, kCodes[i]))
        has[i] = known = true;
    if (!known)
      other = e->valuestring;
  }
  char text[256];
  if (has[0])
    snprintf(text, sizeof(text), "This room has no slot named \"%s\"", g_ap.slot);
  else if (has[1])
    snprintf(text, sizeof(text), "Slot \"%s\" doesn't play %s", g_ap.slot, g_ap.game);
  else if (has[2])
    snprintf(text, sizeof(text), "The server needs a newer client version");
  else if (has[3])
    snprintf(text, sizeof(text), "The server doesn't accept how this client receives items");
  else if (has[4])
    snprintf(text, sizeof(text), g_ap.password[0] ? "Wrong password" : "This room needs a password");
  else
    snprintf(text, sizeof(text), "The server refused the connection (%s)", other ? other : "no reason given");
  (void)c;
  SetStatus(kApStatus_Failed, 0, "%s", text);
  AddText(kApText_Error, "%s", text);
  return -1;
}

static void ReadPlayers(Conn *c, const cJSON *players) {
  if (!cJSON_IsArray(players))
    return;
  free(c->players);
  c->players = malloc(sizeof(ApPlayer) * (cJSON_GetArraySize(players) + 1));
  c->num_players = 0;
  const cJSON *p;
  cJSON_ArrayForEach(p, players) {
    const char *alias = GetStr(p, "alias");
    if (GetNum(p, "team", -1) != c->team || !alias)
      continue;
    ApPlayer *dst = &c->players[c->num_players++];
    dst->slot = (int)GetNum(p, "slot", -1);
    snprintf(dst->alias, sizeof(dst->alias), "%s", alias);
  }
}

static int OnConnected(Conn *c, const cJSON *cmd) {
  c->team = (int)GetNum(cmd, "team", 0);
  c->slot = (int)GetNum(cmd, "slot", 0);
  ReadPlayers(c, cJSON_GetObjectItemCaseSensitive(cmd, "players"));
  const cJSON *slot_info = cJSON_GetObjectItemCaseSensitive(cmd, "slot_info"), *s;
  for (int i = 0; i < c->num_slots; i++) {
    free(c->slots[i].game);
    free(c->slots[i].members);
  }
  free(c->slots);
  c->num_slots = 0;
  c->slots = malloc(sizeof(ApSlotInfo) * (cJSON_GetArraySize(slot_info) + 1));
  cJSON_ArrayForEach(s, slot_info) {
    ApSlotInfo *dst = &c->slots[c->num_slots++];
    memset(dst, 0, sizeof(*dst));
    dst->slot = s->string ? atoi(s->string) : -1;  // JSON object keys: the slot numbers as text
    const char *name = GetStr(s, "name"), *game = GetStr(s, "game");
    snprintf(dst->name, sizeof(dst->name), "%s", name ? name : "");
    dst->game = DupStr(game ? game : "");
    const cJSON *members = cJSON_GetObjectItemCaseSensitive(s, "group_members"), *m;
    int n = cJSON_GetArraySize(members);
    dst->members = n ? malloc(sizeof(int) * n) : NULL;
    cJSON_ArrayForEach(m, members) {
      if (cJSON_IsNumber(m))
        dst->members[dst->num_members++] = (int)m->valuedouble;
    }
  }
  const cJSON *slot_data = cJSON_GetObjectItemCaseSensitive(cmd, "slot_data");

  ApRoom room;
  memset(&room, 0, sizeof(room));
  snprintf(room.seed_name, sizeof(room.seed_name), "%s", c->seed_name);
  snprintf(room.slot_name, sizeof(room.slot_name), "%s", g_ap.slot);
  room.slot = c->slot;
  room.team = c->team;
  room.slot_data_version = (int)GetNum(slot_data, "version", -1);
  room.slot_data_death_link = (int)GetNum(slot_data, "death_link", -1);
  room.slot_data_goal = (int)GetNum(slot_data, "goal", -1);
  bool alttp = !strcmp(g_ap.game, AP_ALTTP_GAME_NAME);
  room.world = alttp ? 1 : 0;  // kApWorld_Alttp / kApWorld_Fsotp
  // "A Link to the Past": the options this game looks at (the order of kApAlttp_*).
  static const char *const kAlttpKeys[] = {
    "crystals_needed_for_gt", "crystals_needed_for_ganon", "open_pyramid", "small_key_shuffle",
    "progressive", "swordless", "retro_bow", "retro_caves", "shop_item_slots",
    "boss_shuffle", "pot_shuffle", "enemy_shuffle", "key_drop_shuffle",
    "randomize_shop_inventories", "shuffle_shop_inventories", "entrance_shuffle", "goal", "mode",
    "triforce_pieces_required",
  };
  for (int i = 0; i < kApAlttp_Count; i++)
    room.alttp[i] = -1;
  if (alttp) {
    for (int i = 0; i < (int)countof(kAlttpKeys); i++)
      room.alttp[i] = (int)GetNum(slot_data, kAlttpKeys[i], -1);
    static const char *const kMedallionKeys[2] = { "mm_medalion", "tr_medalion" };
    for (int i = 0; i < 2; i++) {
      const char *m = GetStr(slot_data, kMedallionKeys[i]);
      room.alttp[kApAlttp_MmMedallion + i] = !m ? -1 : !strcmp(m, "Bombos") ? 0 : !strcmp(m, "Ether") ? 1 :
                                            !strcmp(m, "Quake") ? 2 : -1;
    }
  }
  char key[128];
  snprintf(key, sizeof(key), "%s/%d/%d%s", c->seed_name, c->slot, kItemsHandling, alttp ? "/alttp" : "");
  room.bind_hash = Fnv1a(key);
  if (!room.bind_hash)
    room.bind_hash = 1;

  // The item list and the checks belong to one room. A server that now runs
  // another seed on the same address needs a fresh start.
  if (g_t.had_connection && (strcmp(g_t.seed_name, c->seed_name) || g_t.slot != c->slot || g_t.team != c->team)) {
    SetStatus(kApStatus_Failed, 0, "The server now hosts a different room; connect again");
    AddText(kApText_Error, "The server now hosts a different room; connect again");
    return -1;
  }
  bool again = g_t.had_connection;
  g_t.had_connection = true;
  snprintf(g_t.seed_name, sizeof(g_t.seed_name), "%s", c->seed_name);
  g_t.slot = c->slot, g_t.team = c->team;

  Lock();
  g_ap.room = room;
  g_ap.have_room = true;
  Unlock();
  c->phase = kPhase_Connected;
  // Every check (and the goal) again: whatever got lost with the last
  // connection, or was found while there was none, arrives now.
  SendAllChecks(c, false);
  Lock();
  bool goal = g_ap.goal;
  Unlock();
  if (goal) {
    Buf b = { 0 };
    BufLit(&b, "[{\"cmd\":\"StatusUpdate\",\"status\":30}]");
    SendBuf(c, &b);
    c->goal_sent = true;
  }
  const char *addr = g_ap.addr.display;
  SetStatus(kApStatus_Connected, 0, "Connected to %s (%s) as %s", addr, c->tls ? "wss" : "ws", g_ap.slot);
  if (g_t.outage_announced)  // the screen said something was wrong: say it's fine now
    AddText(kApText_Normal, again ? "Reconnected to the Archipelago server" : "Connected to the Archipelago server");
  g_t.outage_announced = false;
  return 0;
}

static int OnReceivedItems(Conn *c, const cJSON *cmd) {
  int index = (int)GetNum(cmd, "index", -1);
  const cJSON *items = cJSON_GetObjectItemCaseSensitive(cmd, "items"), *it;
  if (index == 0) {
    c->num_items = 0;  // the whole list (after every Connect, or a Sync)
  } else if (index != c->num_items) {
    Dbg("ReceivedItems index %d, have %d: asking for all items again", index, c->num_items);
    SendAllChecks(c, true);
    return 0;
  }
  cJSON_ArrayForEach(it, items) {
    c->items = Grow(c->items, &c->items_cap, c->num_items + 1, sizeof(ApItemEntry));
    ApItemEntry *e = &c->items[c->num_items++];
    e->item = GetNum(it, "item", 0);
    e->location = GetNum(it, "location", 0);
    e->player = (int)GetNum(it, "player", 0);
  }
  // The game's list only grows: entries it has seen never change.
  Lock();
  if (c->num_items > g_ap.num_items) {
    g_ap.items = Grow(g_ap.items, &g_ap.items_cap, c->num_items, sizeof(ApItemEntry));
    memcpy(g_ap.items + g_ap.num_items, c->items + g_ap.num_items,
           sizeof(ApItemEntry) * (c->num_items - g_ap.num_items));
    g_ap.num_items = c->num_items;
  }
  Unlock();
  return 0;
}

static int OnRoomUpdate(Conn *c, const cJSON *cmd) {
  const cJSON *players = cJSON_GetObjectItemCaseSensitive(cmd, "players");
  if (players)
    ReadPlayers(c, players);
  return 0;
}

// JSON message parts into one line, with names for ids.
static void RenderParts(Conn *c, const cJSON *parts, char *out, size_t size) {
  size_t n = 0;
  out[0] = 0;
  const cJSON *part;
  cJSON_ArrayForEach(part, parts) {
    const char *type = GetStr(part, "type"), *text = GetStr(part, "text");
    char tmp[64];
    if (type && !strcmp(type, "player_id"))
      text = PlayerName(c, (int)GetNum(part, "text", 0), tmp, sizeof(tmp));
    else if (type && !strcmp(type, "item_id"))
      text = ItemName(c, GetNum(part, "text", 0), (int)GetNum(part, "player", 0), tmp, sizeof(tmp));
    else if (type && !strcmp(type, "location_id"))
      text = LocationName(c, GetNum(part, "text", 0), (int)GetNum(part, "player", 0), tmp, sizeof(tmp));
    if (text && n + 1 < size)
      n += snprintf(out + n, size - n, "%s", text), n = n < size ? n : size - 1;
  }
}

static int OnPrintJson(Conn *c, const cJSON *cmd) {
  const char *type = GetStr(cmd, "type");
  if (!type)
    return 0;
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(cmd, "item");
  int receiving = (int)GetNum(cmd, "receiving", -1);
  int finder = (int)GetNum(item, "player", -1);
  int64 id = GetNum(item, "item", 0), location = GetNum(item, "location", 0);
  ApTextKind kind = KindFromFlags(GetNum(item, "flags", 0));
  char a[64], b[64], d[64];
  if (!strcmp(type, "ItemSend")) {
    bool to_us = ConcernsUs(c, receiving), from_us = ConcernsUs(c, finder);
    // The item's name is in the receiver's game, the location's in the finder's.
    if (to_us && from_us)
      AddText(kind, "Found your own %s", ItemName(c, id, receiving, a, sizeof(a)));
    else if (to_us)
      AddText(kind, "Received %s from %s (%s)", ItemName(c, id, receiving, a, sizeof(a)),
              PlayerName(c, finder, b, sizeof(b)), LocationName(c, location, finder, d, sizeof(d)));
    else if (from_us)
      AddText(kind, "Sent %s to %s", ItemName(c, id, receiving, a, sizeof(a)), PlayerName(c, receiving, b, sizeof(b)));
  } else if (!strcmp(type, "ItemCheat")) {
    // Told to every team; slot numbers only mean us within ours.
    if (GetNum(cmd, "team", c->team) == c->team && ConcernsUs(c, receiving))
      AddText(kind, "Received %s (cheat)", ItemName(c, id, receiving, a, sizeof(a)));
  } else if (!strcmp(type, "Hint")) {
    if (ConcernsUs(c, receiving) || ConcernsUs(c, finder)) {
      char line[512];
      RenderParts(c, cJSON_GetObjectItemCaseSensitive(cmd, "data"), line, sizeof(line));
      AddText(kind, "Hint: %s", !strncmp(line, "[Hint]: ", 8) ? line + 8 : line);
    }
  } else if (!strcmp(type, "Chat") || !strcmp(type, "ServerChat") || !strcmp(type, "Goal") ||
             !strcmp(type, "Release") || !strcmp(type, "Collect") || !strcmp(type, "Countdown")) {
    char line[512];
    RenderParts(c, cJSON_GetObjectItemCaseSensitive(cmd, "data"), line, sizeof(line));
    if (line[0])
      AddText(kApText_Normal, "%s", line);
  }
  return 0;
}

static int OnBounced(Conn *c, const cJSON *cmd) {
  const cJSON *tags = cJSON_GetObjectItemCaseSensitive(cmd, "tags"), *t;
  bool death = false;
  cJSON_ArrayForEach(t, tags) {
    if (cJSON_IsString(t) && !strcmp(t->valuestring, "DeathLink"))
      death = true;
  }
  const cJSON *data = cJSON_GetObjectItemCaseSensitive(cmd, "data");
  const cJSON *when = cJSON_GetObjectItemCaseSensitive(data, "time");
  if (!death || !cJSON_IsNumber(when))
    return 0;
  const char *source = GetStr(data, "source");
  // Our own death comes back to us too.
  for (int i = 0; i < 4; i++)
    if (fabs(when->valuedouble - g_t.sent_deaths[i]) < 0.001 && source && !strcmp(source, g_ap.slot))
      return 0;
  (void)c;
  Lock();
  if (g_ap.death_link) {
    if (g_ap.num_deaths_in == kMaxDeathsIn) {
      memmove(g_ap.deaths_in[0], g_ap.deaths_in[1], sizeof(g_ap.deaths_in[0]) * (kMaxDeathsIn - 1));
      g_ap.num_deaths_in--;
    }
    Sanitize(g_ap.deaths_in[g_ap.num_deaths_in++], sizeof(g_ap.deaths_in[0]), source ? source : "Someone");
  }
  Unlock();
  return 0;
}

// cJSON allocates every value of a message while reading it. Once the client
// is told to stop that fails, so even a message of millions of values is
// given up at once.
static void *CJSON_CDECL JsonAlloc(size_t size) {
  return SDL_AtomicGet(&g_ap.stop) ? NULL : malloc(size);
}

static int HandleMessage(Conn *c, const char *msg, size_t len) {
  Dbg("<< %.300s", msg);
  cJSON *root = cJSON_ParseWithLength(msg, len);
  if (!cJSON_IsArray(root)) {
    Dbg("not a JSON array");
    cJSON_Delete(root);
    return 0;
  }
  int r = 0;
  const cJSON *cmd;
  cJSON_ArrayForEach(cmd, root) {
    if (SDL_AtomicGet(&g_ap.stop))  // one message can hold any number of commands
      break;
    const char *name = GetStr(cmd, "cmd");
    if (!name)
      continue;
    if (!strcmp(name, "RoomInfo"))
      r = OnRoomInfo(c, cmd);
    else if (!strcmp(name, "DataPackage"))
      r = OnDataPackage(c, cmd);
    else if (!strcmp(name, "Connected"))
      r = OnConnected(c, cmd);
    else if (!strcmp(name, "ConnectionRefused"))
      r = OnConnectionRefused(c, cmd);
    else if (!strcmp(name, "ReceivedItems"))
      r = OnReceivedItems(c, cmd);
    else if (!strcmp(name, "RoomUpdate"))
      r = OnRoomUpdate(c, cmd);
    else if (!strcmp(name, "PrintJSON"))
      r = OnPrintJson(c, cmd);
    else if (!strcmp(name, "Bounced"))
      r = OnBounced(c, cmd);
    else if (!strcmp(name, "InvalidPacket"))
      Dbg("the server says we sent an invalid packet: %s", GetStr(cmd, "text") ? GetStr(cmd, "text") : "?");
    if (r < 0)
      break;
  }
  cJSON_Delete(root);
  return r;
}

// ---- the network thread ----

static bool IsArchipelagoGg(const char *host) {
  size_t n = strlen(host);
  return !SDL_strcasecmp(host, "archipelago.gg") || (n > 15 && !SDL_strcasecmp(host + n - 15, ".archipelago.gg"));
}

// Opens the WebSocket. Without a scheme in the address: ws:// and then
// wss:// (a server that answers with TLS); only wss:// for archipelago.gg,
// which serves nothing else, and once wss:// has worked. Never the other way
// round: falling back to plain ws:// when TLS fails (a certificate that isn't
// trusted, a broken handshake) is what someone in between would want.
// *fatal: trying again won't help.
static ApWs *OpenConnection(bool *used_tls, bool *fatal, char *err, size_t err_size) {
  const ApAddr *a = &g_ap.addr;
  bool order[2];
  int n = 1;
  if (a->scheme == kScheme_Ws) {
    order[0] = false;
  } else if (a->scheme == kScheme_Wss || IsArchipelagoGg(a->host) || g_t.preferred_tls == 1) {
    order[0] = true;
  } else {
    order[0] = false, order[1] = true;
    n = 2;
  }
  char errs[2][256];
  ApWsFail fails[2];
  int tried = 0;
  *fatal = false;
  for (int i = 0; i < n; i++) {
    Dbg("connecting to %s://%s:%d%s", order[i] ? "wss" : "ws", a->host, a->port, a->path);
    ApWs *ws = ApWs_Connect(a->host, a->port, a->path, order[i], kConnectTimeoutMs, &g_ap.stop,
                            &fails[i], errs[i], sizeof(errs[i]));
    tried++;
    if (ws) {
      Dbg("connected (%s)", ApWs_Compressed(ws) ? "compressed" : "not compressed");
      *used_tls = order[i];
      g_t.preferred_tls = order[i];
      return ws;
    }
    Dbg("failed: %s", errs[i]);
    // No TCP connection at all: the other scheme won't do better.
    if (fails[i] == kApWsFail_Resolve || fails[i] == kApWsFail_Connect || fails[i] == kApWsFail_Cancelled)
      break;
  }
  if (tried == 1) {
    snprintf(err, err_size, "%s", errs[0]);
    *fatal = fails[0] == kApWsFail_NoTls;
    return NULL;
  }
  // Both failed: a reason that isn't just "wrong scheme" is the one to show.
  int pick = -1;
  for (int i = 0; i < 2 && pick < 0; i++)
    if (fails[i] != kApWsFail_Protocol && fails[i] != kApWsFail_NoTls)
      pick = i;
  for (int i = 0; i < 2 && pick < 0; i++)
    if (fails[i] == kApWsFail_NoTls)
      pick = i;
  if (pick >= 0)
    snprintf(err, err_size, "%s", errs[pick]);
  else
    snprintf(err, err_size, "%s doesn't answer like an Archipelago server (tried ws:// and wss://)", a->display);
  return NULL;
}

enum { kSession_Lost, kSession_Stopped, kSession_Fatal };

static int RunSession(Conn *c, char *err, size_t err_size) {
  uint32 start = SDL_GetTicks();
  for (;;) {
    if (SDL_AtomicGet(&g_ap.stop))
      return kSession_Stopped;
    char *msg;
    size_t len;
    int r = ApWs_Poll(c->ws, 50, &msg, &len);
    while (r == 1) {
      int h = HandleMessage(c, msg, len);
      free(msg);
      if (h < 0)
        return kSession_Fatal;
      if (SDL_AtomicGet(&g_ap.stop))
        return kSession_Stopped;
      r = ApWs_Poll(c->ws, 0, &msg, &len);
    }
    if (r < 0) {
      snprintf(err, err_size, "%s", ApWs_Error(c->ws));
      return kSession_Lost;
    }
    uint32 now = SDL_GetTicks();
    if (c->phase == kPhase_Connected) {
      if (!SendOutbound(c)) {
        snprintf(err, err_size, "%s", ApWs_Error(c->ws));
        return kSession_Lost;
      }
    } else if (now - start > kHandshakeTimeoutMs) {
      snprintf(err, err_size, "The server didn't finish the handshake");
      return kSession_Lost;
    }
    // A connection that went quiet may be dead without anyone telling us.
    uint32 idle = now - ApWs_LastReceive(c->ws);
    if (idle > kDeadAfterMs) {
      snprintf(err, err_size, "The server stopped answering");
      return kSession_Lost;
    }
    if (idle > kPingAfterMs && !c->pinged)
      c->pinged = ApWs_SendPing(c->ws);
    else if (idle < kPingAfterMs)
      c->pinged = false;
  }
}

static int SDLCALL ApThread(void *arg) {
  (void)arg;
  int delay = kFirstRetryMs;
  bool first = true;
  while (!SDL_AtomicGet(&g_ap.stop)) {
    if (!first)  // the first time, ApClient_Start said so already
      SetStatus(kApStatus_Retrying, 0, "Reconnecting to %s...", g_ap.addr.display);
    char err[512] = "";
    bool fatal = false, was_connected = false;
    Conn c;
    memset(&c, 0, sizeof(c));
    c.ws = OpenConnection(&c.tls, &fatal, err, sizeof(err));
    if (c.ws) {
      int r = RunSession(&c, err, sizeof(err));
      was_connected = c.phase == kPhase_Connected;
      ApWs_Close(c.ws);
      FreeConn(&c);
      if (r != kSession_Lost)
        break;  // stopped, or refused (the status says why)
    }
    if (SDL_AtomicGet(&g_ap.stop))
      break;
    if (fatal) {
      SetStatus(kApStatus_Failed, 0, "%s", err);
      AddText(kApText_Error, "%s", err);
      break;
    }
    if (was_connected)
      delay = kFirstRetryMs;
    // One line per outage on the screen; the status keeps the details.
    if (!g_t.outage_announced) {
      if (was_connected)
        AddText(kApText_Error, "Lost connection to the Archipelago server: %s", err);
      else
        AddText(kApText_Error, "Can't connect to %s: %s", g_ap.addr.display, err);
      g_t.outage_announced = true;
    }
    first = false;
    uint32 retry_at = SDL_GetTicks() + delay;
    SetStatus(kApStatus_Retrying, retry_at, was_connected ? "Connection lost: %s" : "%s", err);
    while (!SDL_AtomicGet(&g_ap.stop) && (int32)(retry_at - SDL_GetTicks()) > 0)
      SDL_Delay(20);
    delay = delay * 2 > kMaxRetryMs ? kMaxRetryMs : delay * 2;
  }
  FreeGames();
  return 0;
}

// ---- address ----

static bool ParseAddress(const char *in, ApAddr *a, char *err, size_t err_size) {
  memset(a, 0, sizeof(*a));
  // What gets pasted: the room page's line "/connect host:port" (in quotes,
  // and the menu drops its space), with spaces or line breaks around it. An
  // address has none of these characters.
  char buf[512];
  size_t len = 0;
  for (; *in && len < sizeof(buf) - 1; in++)
    if (!strchr(" \t\r\n'\"`", *in))
      buf[len++] = *in;
  buf[len] = 0;
  const char *p = buf;
  if (!SDL_strncasecmp(p, "/connect", 8))
    p += 8;
  a->scheme = kScheme_Either;
  if (!SDL_strncasecmp(p, "wss://", 6))
    a->scheme = kScheme_Wss, p += 6;
  else if (!SDL_strncasecmp(p, "ws://", 5))
    a->scheme = kScheme_Ws, p += 5;
  else if (!SDL_strncasecmp(p, "archipelago://", 14))
    p += 14;
  else if (!SDL_strncasecmp(p, "http://", 7) || !SDL_strncasecmp(p, "https://", 8)) {
    snprintf(err, err_size, "Enter the address the room page shows, not its link");
    return false;
  } else if (strstr(p, "://")) {
    snprintf(err, err_size, "Unknown kind of address (use ws:// or wss://)");
    return false;
  }
  const char *slash = strchr(p, '/');
  size_t auth = slash ? (size_t)(slash - p) : strlen(p);
  snprintf(a->path, sizeof(a->path), "%s", slash ? slash : "/");
  const char *port = NULL;
  size_t port_len = 0;
  if (memchr(p, '@', auth)) {
    snprintf(err, err_size, "Put the slot name and password in their own fields");
    return false;
  }
  if (p[0] == '[') {  // [IPv6]:port
    const char *close = memchr(p, ']', auth);
    if (!close) {
      snprintf(err, err_size, "Bad server address");
      return false;
    }
    snprintf(a->host, sizeof(a->host), "%.*s", (int)(close - p - 1), p + 1);
    if (close + 1 < p + auth) {
      if (close[1] != ':') {
        snprintf(err, err_size, "Bad server address");
        return false;
      }
      port = close + 2, port_len = p + auth - port;
    }
  } else {
    const char *colon = memchr(p, ':', auth);
    if (colon && memchr(colon + 1, ':', p + auth - colon - 1))
      colon = NULL;  // a bare IPv6 address, no port
    size_t host_len = colon ? (size_t)(colon - p) : auth;
    snprintf(a->host, sizeof(a->host), "%.*s", (int)host_len, p);
    if (colon)
      port = colon + 1, port_len = p + auth - port;
  }
  if (!a->host[0]) {
    snprintf(err, err_size, "Enter the server's address");
    return false;
  }
  a->port = kApDefaultPort;
  if (port) {
    int value = 0;
    for (size_t i = 0; i < port_len; i++) {
      if (port[i] < '0' || port[i] > '9' || value > 65535) {
        value = -1;
        break;
      }
      value = value * 10 + port[i] - '0';
    }
    if (value < 1 || value > 65535) {
      snprintf(err, err_size, "Bad port number in the server address");
      return false;
    }
    a->port = value;
  }
  bool v6 = strchr(a->host, ':') != NULL;
  snprintf(a->display, sizeof(a->display), v6 ? "[%s]:%d" : "%s:%d", a->host, a->port);
  return true;
}

// ---- the game's side ----

static void MakeUuid(char *out, size_t size) {
  static char uuid[33];
  if (!uuid[0]) {
    int where;  // its address differs between runs too
    uint64 x = SDL_GetPerformanceCounter() ^ ((uint64)time(NULL) << 20) ^ (uint64)(uintptr_t)&where ^ SDL_GetTicks();
    for (int i = 0; i < 16; i++) {
      // splitmix64
      x += 0x9E3779B97F4A7C15ULL;
      uint64 z = x;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      z ^= z >> 31;
      snprintf(uuid + i * 2, 3, "%02x", (unsigned)(z & 0xff));
    }
  }
  snprintf(out, size, "%s", uuid);
}

void ApClient_Start(const ApConfig *cfg) {
  ApClient_Stop();
  static cJSON_Hooks hooks = { JsonAlloc, free };
  cJSON_InitHooks(&hooks);  // only this file uses cJSON
  const char *debug = getenv("ZELDA3_AP_DEBUG");
  Lock();
  g_ap.debug = debug && *debug && strcmp(debug, "0");
  free(g_ap.items);
  free(g_ap.checks);
  free(g_ap.new_checks);
  g_ap.items = NULL, g_ap.checks = g_ap.new_checks = NULL;
  g_ap.num_items = g_ap.items_cap = g_ap.num_checks = g_ap.checks_cap = g_ap.num_new = g_ap.new_cap = 0;
  g_ap.goal = false;
  g_ap.tags_dirty = false;
  g_ap.num_deaths_in = g_ap.num_deaths_out = 0;
  g_ap.num_texts = g_ap.text_first = 0;
  g_ap.have_room = false;
  memset(&g_ap.room, 0, sizeof(g_ap.room));
  snprintf(g_ap.slot, sizeof(g_ap.slot), "%s", cfg && cfg->slot ? cfg->slot : "");
  snprintf(g_ap.password, sizeof(g_ap.password), "%s", cfg && cfg->password ? cfg->password : "");
  snprintf(g_ap.game, sizeof(g_ap.game), "%s", cfg && cfg->game && cfg->game[0] ? cfg->game : AP_GAME_NAME);
  if (cfg && cfg->uuid && cfg->uuid[0])
    snprintf(g_ap.uuid, sizeof(g_ap.uuid), "%s", cfg->uuid);
  else
    MakeUuid(g_ap.uuid, sizeof(g_ap.uuid));
  Unlock();

  memset(&g_t, 0, sizeof(g_t));
  g_t.preferred_tls = -1;
  char err[256];
  if (!ParseAddress(cfg && cfg->server ? cfg->server : "", &g_ap.addr, err, sizeof(err))) {
    SetStatus(kApStatus_Failed, 0, "%s", err);
    AddText(kApText_Error, "%s", err);
    return;
  }
  // Leading or trailing spaces are typos: slot names can't have them.
  char *s = g_ap.slot;
  size_t n = strlen(s);
  while (n && s[n - 1] == ' ')
    s[--n] = 0;
  if (s[0] == ' ')
    memmove(s, s + strspn(s, " "), strlen(s + strspn(s, " ")) + 1);
  if (!s[0]) {
    SetStatus(kApStatus_Failed, 0, "Enter your slot name");
    AddText(kApText_Error, "Enter your slot name");
    return;
  }
  SetStatus(kApStatus_Connecting, 0, "Connecting to %s...", g_ap.addr.display);
  SDL_AtomicSet(&g_ap.stop, 0);
  g_ap.thread = SDL_CreateThread(ApThread, "archipelago", NULL);
  if (!g_ap.thread) {
    SetStatus(kApStatus_Failed, 0, "Couldn't start the network thread");
    AddText(kApText_Error, "Couldn't start the network thread");
  }
}

void ApClient_Stop(void) {
  if (g_ap.thread) {
    SDL_AtomicSet(&g_ap.stop, 1);
    SDL_WaitThread(g_ap.thread, NULL);
    g_ap.thread = NULL;
  }
  Lock();
  g_ap.status = kApStatus_Off;
  g_ap.retry_at = 0;
  snprintf(g_ap.status_text, sizeof(g_ap.status_text), "Not connected");
  Unlock();
}

ApStatus ApClient_GetStatus(char *text, size_t size) {
  Lock();
  ApStatus status = g_ap.status;
  if (text && size) {
    if (status == kApStatus_Off && !g_ap.status_text[0]) {
      snprintf(text, size, "Not connected");
    } else if (status == kApStatus_Retrying && g_ap.retry_at) {
      int left = (int32)(g_ap.retry_at - SDL_GetTicks());
      int secs = left > 0 ? (left + 999) / 1000 : 0;
      if (secs)
        snprintf(text, size, "%s; retrying in %d s", g_ap.status_text, secs);
      else
        snprintf(text, size, "%s; retrying now", g_ap.status_text);
    } else {
      snprintf(text, size, "%s", g_ap.status_text);
    }
  }
  Unlock();
  return status;
}

bool ApClient_GetRoom(ApRoom *room) {
  Lock();
  bool have = g_ap.have_room;
  if (have && room)
    *room = g_ap.room;
  Unlock();
  return have;
}

int ApClient_ItemCount(void) {
  Lock();
  int n = g_ap.num_items;
  Unlock();
  return n;
}

bool ApClient_GetItem(int index, int64 *item, int64 *location, int *player) {
  Lock();
  bool ok = index >= 0 && index < g_ap.num_items;
  if (ok) {
    ApItemEntry e = g_ap.items[index];
    if (item)
      *item = e.item;
    if (location)
      *location = e.location;
    if (player)
      *player = e.player;
  }
  Unlock();
  return ok;
}

void ApClient_CheckLocations(const int64 *ids, int n) {
  Lock();
  for (int i = 0; i < n; i++) {
    int lo = 0, hi = g_ap.num_checks;
    while (lo < hi) {
      int mid = (lo + hi) >> 1;
      if (g_ap.checks[mid] < ids[i])
        lo = mid + 1;
      else
        hi = mid;
    }
    if (lo < g_ap.num_checks && g_ap.checks[lo] == ids[i])
      continue;
    g_ap.checks = Grow(g_ap.checks, &g_ap.checks_cap, g_ap.num_checks + 1, sizeof(int64));
    memmove(g_ap.checks + lo + 1, g_ap.checks + lo, sizeof(int64) * (g_ap.num_checks - lo));
    g_ap.checks[lo] = ids[i];
    g_ap.num_checks++;
    g_ap.new_checks = Grow(g_ap.new_checks, &g_ap.new_cap, g_ap.num_new + 1, sizeof(int64));
    g_ap.new_checks[g_ap.num_new++] = ids[i];
  }
  Unlock();
}

void ApClient_SetGoalReached(void) {
  Lock();
  g_ap.goal = true;
  Unlock();
}

void ApClient_SetDeathLink(bool on) {
  Lock();
  if (g_ap.death_link != on) {
    g_ap.death_link = on;
    g_ap.tags_dirty = true;
    if (!on)
      g_ap.num_deaths_in = g_ap.num_deaths_out = 0;
  }
  Unlock();
}

void ApClient_SendDeath(const char *cause) {
  Lock();
  // Only while connected: a death from minutes ago would surprise everyone.
  if (g_ap.death_link && g_ap.status == kApStatus_Connected && g_ap.num_deaths_out < kMaxDeathsOut)
    snprintf(g_ap.deaths_out[g_ap.num_deaths_out++], sizeof(g_ap.deaths_out[0]), "%s", cause ? cause : "");
  Unlock();
}

bool ApClient_PollDeath(char *source, size_t size) {
  Lock();
  bool got = g_ap.num_deaths_in > 0;
  if (got) {
    if (source && size)
      snprintf(source, size, "%s", g_ap.deaths_in[0]);
    memmove(g_ap.deaths_in[0], g_ap.deaths_in[1], sizeof(g_ap.deaths_in[0]) * (kMaxDeathsIn - 1));
    g_ap.num_deaths_in--;
  }
  Unlock();
  return got;
}

bool ApClient_PollText(char *buf, size_t size, ApTextKind *kind) {
  Lock();
  bool got = g_ap.num_texts > 0;
  if (got) {
    ApText *t = &g_ap.texts[g_ap.text_first];
    if (buf && size)
      snprintf(buf, size, "%s", t->text);
    if (kind)
      *kind = t->kind;
    g_ap.text_first = (g_ap.text_first + 1) % kMaxTexts;
    g_ap.num_texts--;
  }
  Unlock();
  return got;
}
