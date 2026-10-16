#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "net.h"
#include "upnp.h"
#include "util.h"
#include "zelda_rtl.h"
#include "features.h"
#include "instance.h"
#include "randomizer.h"
#include "archipelago.h"
#include "story.h"
#include "variables.h"
#include "select_file.h"

enum {
  kProtoVersion = 21,
  kRingSize = 1024,
  kChunkSize = 1024,
  kChunksPerTick = 24,
  kMaxFramesPerPacket = 200,  // at most; what fits in a packet decides (see HostSend)
  kHashInterval = 60,
  kTimeoutMs = 6000,
  // Client side prediction: the client runs ahead of the frames confirmed by
  // the host, rolling back when a guess about other players' buttons was wrong.
  kRbSize = 128,         // saved states / inputs of the predicted frames (> kMaxLead + the input delay)
  kMaxLead = 90,         // never predict further ahead than this (a round trip plus the host's kMaxWait, or a slow link never settles)
  kInputsPerPacket = 8,  // each input is sent this many times, against packet loss
  kSchedSize = 64,       // host: client inputs stamped for upcoming frames
  kMarginWindow = 20,    // host: report the worst margin of this many packets
  kMaxWait = 30,         // host: frames to wait for a client's input before using a guess
  kStallMs = 500,        // host: a client this quiet isn't waited for
  kKeepSeatMs = kSessionKeepSeatSeconds * 1000,  // a dropped player's seat waits this long
  kRosterMs = 500,       // host: how often the clients get the list of players
};

enum {
  kPkt_Hello = 1,
  kPkt_Welcome,
  kPkt_Reject,
  kPkt_Input,
  kPkt_Frames,
  kPkt_SnapInfo,
  kPkt_SnapData,
  kPkt_Bye,
  kPkt_Discover,
  kPkt_Info,
  kPkt_Roster,
  kPkt_ApNotice,
};

typedef struct Writer {
  uint8 buf[1400];
  int pos;
} Writer;

static void Wr8(Writer *w, uint8 v) { w->buf[w->pos++] = v; }
static void Wr16(Writer *w, uint16 v) { Wr8(w, v); Wr8(w, v >> 8); }
static void Wr32(Writer *w, uint32 v) { Wr16(w, v); Wr16(w, v >> 16); }
static void WData(Writer *w, const void *d, int n) { memcpy(w->buf + w->pos, d, n); w->pos += n; }
static void WBegin(Writer *w, uint8 type) { w->pos = 0; Wr8(w, 'Z'); Wr8(w, '3'); Wr8(w, type); }

typedef struct Reader {
  const uint8 *p, *end;
  bool err;
} Reader;

static uint8 Rd8(Reader *r) { if (r->p >= r->end) { r->err = true; return 0; } return *r->p++; }
static uint16 Rd16(Reader *r) { uint16 a = Rd8(r); return a | Rd8(r) << 8; }
static uint32 Rd32(Reader *r) { uint32 a = Rd16(r); return a | (uint32)Rd16(r) << 16; }
static void RData(Reader *r, void *d, int n) {
  if (r->end - r->p < n) { r->err = true; memset(d, 0, n); return; }
  memcpy(d, r->p, n);
  r->p += n;
}

typedef struct HostClient {
  bool used;
  NetAddr addr;
  uint8 id;
  char name[24];
  uint32 token;       // lets it back into its seat after losing the connection
  bool parked;        // connection lost: its players stand still, the seat waits
  uint32 parked_at;
  bool ever_loaded;   // got into the game at least once (only then is a seat kept)
  uint16 ping_ms;     // as the client measures it
  uint32 last_recv;
  // Inputs the client sent ahead of time, for the frame they are meant for.
  uint16 sched[kSchedSize][kSessionMaxLocal];
  uint32 sched_frame[kSchedSize];
  // How many frames early the client's inputs arrive (negative: late).
  int margin_min, margin_count, margin_report;
  uint32 need_frame;
  uint8 join_mask, leave_mask;
  uint32 ping_echo;
  // snapshot transfer
  bool loading;
  uint8 snap_id;
  uint8 *snap;
  uint32 snap_size;
  uint32 snap_have;    // the client has everything up to here
  uint32 snap_cursor;  // sent up to here
} HostClient;

static struct {
  SessionStatus st;
  NetSocket *sock;
  NetSocket *lan_sock;
  NetSocket *disc_sock;  // hosting on another port: answers "Find LAN games" on the usual one
  char name[24];
  char host_name[24];    // clients: whom we play with
  uint32 frame;  // number of frames simulated (clients: including predicted ones)
  uint32 conf;   // frames up to this one are final (host: everyone's input is known)
  MpFrameInput ring[kRingSize];
  uint32 ring_frame[kRingSize];
  bool ff;
  uint32 rando;
  int input_delay;
  bool story;
  bool full_inv;
  uint16 host_in[16][kSessionMaxLocal];  // host: local joypads by frame, for the input delay
  uint8 slot_owner[kMpMaxPlayers];
  uint8 slot_local[kMpMaxPlayers];
  uint8 pending_join_mask, pending_leave_mask;
  // host
  HostClient clients[kSessionMaxClients];
  uint8 next_snap_id;
  // client
  NetAddr host;
  uint32 last_recv, last_hello;
  uint8 snap_id;
  uint8 *snap;
  uint32 snap_size, snap_have, snap_frame;
  bool snap_known;
  bool want_resync;
  uint8 *snap_chunk_have;
  // client prediction
  InstSnapshot *rb[kRbSize];        // state after frame f, for conf <= f <= frame
  MpFrameInput rb_in[kRbSize];      // what frame f was run with
  uint32 rb_hash[kRbSize];          // state hash after frame f, if f % kHashInterval == 0
  uint16 my_in[kRbSize][kSessionMaxLocal];  // local joypads for frame f
  uint32 heard;                     // frames up to this one were already heard
  int margin;                       // host's latest report of how early our inputs arrive
  bool margin_known;
  int lead_cooldown;
  int lead_debt;                    // frames still to catch up, a few per tick
  // host prediction: frame f as currently known, which slots' inputs are real
  // (not guessed) and who owned each slot when the frame was built
  MpFrameInput pend[kRbSize];
  uint8 pend_known[kRbSize];
  uint8 pend_owner[kRbSize][kMpMaxPlayers], pend_local[kRbSize][kMpMaxPlayers];
  bool host_rollback_pending;
  uint32 host_rollback;             // earliest frame whose input changed
  SessionLanGame lan[8];
  int nlan;
  // reconnecting (clients)
  uint32 token;                     // from the host's welcome
  uint32 reconnect_since;
  uint8 rejoin_locals;              // our extra local players to join again once back
  SessionPlayer roster[kMpMaxPlayers];  // the host's list of players
  bool roster_known;
  uint32 last_roster;               // host: when the clients last got the list
  bool in_host_world;               // clients: the game here is the host's
  // host: players removed from this game, so they can't knock their way back
  struct { uint32 token, ip; char name[24]; } kicked[8];
  int nkicked;
  // Archipelago lines: the host's latest (sent with the roster) and, on a
  // client, the ones received but not shown yet.
  struct { uint32 seq; uint8 kind; char text[96]; } notice[8];
  uint32 notice_seq;    // host: lines so far; client: the newest one received
  int notice_rd, notice_wr;
} g_s;

// --- snapshot compression (byte run length encoding) ---

static void RleCompress(const uint8 *src, size_t n, ByteArray *out) {
  size_t i = 0;
  while (i < n) {
    size_t run = 1;
    while (i + run < n && src[i + run] == src[i] && run < 130)
      run++;
    if (run >= 3) {
      ByteArray_AppendByte(out, 0x80 | (uint8)(run - 3));
      ByteArray_AppendByte(out, src[i]);
      i += run;
    } else {
      size_t start = i, lit = 0;
      while (i < n && lit < 128) {
        if (i + 2 < n && src[i] == src[i + 1] && src[i] == src[i + 2])
          break;
        i++, lit++;
      }
      ByteArray_AppendByte(out, (uint8)(lit - 1));
      ByteArray_AppendData(out, src + start, lit);
    }
  }
}

static bool RleDecompress(const uint8 *src, size_t n, ByteArray *out) {
  size_t i = 0;
  while (i < n) {
    uint8 c = src[i++];
    if (c & 0x80) {
      if (i >= n)
        return false;
      size_t run = (c & 0x7f) + 3;
      for (size_t j = 0; j < run; j++)
        ByteArray_AppendByte(out, src[i]);
      i++;
    } else {
      size_t lit = c + 1;
      if (i + lit > n)
        return false;
      ByteArray_AppendData(out, src + i, lit);
      i += lit;
    }
  }
  return true;
}

// --- slot bookkeeping ---

static void ResetSlots() {
  memset(g_s.slot_owner, 0xff, sizeof(g_s.slot_owner));
  memset(g_s.slot_local, 0xff, sizeof(g_s.slot_local));
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (Inst_IsJoined(p)) {
      g_s.slot_owner[p] = 0;
      g_s.slot_local[p] = p == 0 ? 0 : 0xff;
    }
  }
}

static void UpdateLocalSlots() {
  for (int i = 0; i < kSessionMaxLocal; i++)
    g_s.st.local_slot[i] = -1;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (g_s.slot_owner[p] == g_s.st.my_id && g_s.slot_local[p] < kSessionMaxLocal)
      g_s.st.local_slot[g_s.slot_local[p]] = p;
  }
  memcpy(g_s.st.slot_owner, g_s.slot_owner, sizeof(g_s.slot_owner));
}

static void ApplyEvents(const MpFrameInput *fi) {
  for (int i = 0; i < fi->nevents && i < kSessionMaxEvents; i++) {
    const uint8 *e = fi->events[i];
    if (e[0] == kSessEv_ApItem || e[0] == kSessEv_ApDeath || e[0] == kSessEv_ApRoom || e[0] == kSessEv_ApRoom2) {
      Ap_ApplyEvent(e);
      continue;
    }
    int slot = e[1];
    if (slot >= kMpMaxPlayers)
      continue;
    if (e[0] == kSessEv_Join) {
      if (!Inst_IsJoined(slot))
        Inst_Join(slot);
      g_s.slot_owner[slot] = e[2];
      g_s.slot_local[slot] = e[3];
    } else if (e[0] == kSessEv_Leave) {
      if (Inst_IsJoined(slot) && Inst_NumJoined() > 1) {
        Inst_Leave(slot);
        g_s.slot_owner[slot] = 0xff;
        g_s.slot_local[slot] = 0xff;
      }
    }
  }
  UpdateLocalSlots();
}

static bool g_last_was_replay;
void (*g_session_frame_hook)(uint32 f);
int g_session_dump_every;

static void RunFrame(const MpFrameInput *fi, bool deterministic_audio) {
  ApplyEvents(fi);
  if (g_session_frame_hook)
    g_session_frame_hook(g_s.frame + 1);
  Inst_SetFriendlyFire(fi->flags & 1);
  Story_SetEnabled(fi->flags & 4);
  Rando_SetNewGameSetting(fi->flags & 8 ? fi->rando : kRando_Off);
  FileSelect_SetFullInventory(fi->flags & 0x10);
  Ap_SetNewGameBind(fi->flags & 0x20 ? fi->ap : 0);
  Ap_SetNewGameWorld(fi->flags & 0x40 ? kApWorld_Alttp : kApWorld_Fsotp);
  g_wanted_zelda_features = fi->features;
  g_apui00_override = deterministic_audio ? (fi->flags >> 1) & 1 : -1;
  Ap_BeforeFrame();
  g_last_was_replay = Inst_RunFrame(fi->inputs);
  g_apui00_override = -1;
  g_s.frame++;
}

static int FindFreeSlot(const uint8 *reserved) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_s.slot_owner[p] == 0xff && !reserved[p] && !Inst_IsJoined(p))
      return p;
  return -1;
}

static int FindSlotOf(uint8 owner, uint8 local) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_s.slot_owner[p] == owner && g_s.slot_local[p] == local)
      return p;
  return -1;
}

static void AddEvent(MpFrameInput *fi, uint8 type, uint8 slot, uint8 owner, uint8 local) {
  if (fi->nevents >= kSessionMaxEvents)
    return;
  uint8 *e = fi->events[fi->nevents++];
  e[0] = type, e[1] = slot, e[2] = owner, e[3] = local;
}

static void QueueJoinLeave(MpFrameInput *fi, uint8 owner, uint8 *join_mask, uint8 *leave_mask, uint8 *reserved) {
  for (int i = 0; i < kSessionMaxLocal; i++) {
    int slot = FindSlotOf(owner, i);
    if ((*join_mask & (1 << i))) {
      bool done = slot >= 0;
      if (slot < 0) {
        int s = FindFreeSlot(reserved);
        if (s >= 0 && fi->nevents < kSessionMaxEvents) {
          reserved[s] = 1;
          AddEvent(fi, kSessEv_Join, s, owner, i);
          done = true;
        }
      }
      // (No free slot right now: the join waits for one.)
      if (done)
        *join_mask &= ~(1 << i);
    }
    if ((*leave_mask & (1 << i))) {
      if (slot >= 0 && fi->nevents < kSessionMaxEvents)
        AddEvent(fi, kSessEv_Leave, slot, owner, i);
      *leave_mask &= ~(1 << i);
    }
  }
}

static HostClient *FindClientById(uint8 id) {
  for (int c = 0; c < kSessionMaxClients; c++)
    if (g_s.clients[c].used && g_s.clients[c].id == id)
      return &g_s.clients[c];
  return NULL;
}

// ZELDA3_PLAIN_GUESS set: the guesses as they were, to compare.
static bool PlainGuess() {
  static int v = -1;
  if (v < 0)
    v = getenv("ZELDA3_PLAIN_GUESS") != NULL;
  return v != 0;
}

// A guess never hops down a ledge. Pushing into a ledge for 19 frames hops
// down it, so a guess that keeps pushing hops where the player let go in
// time, and the Link is seen hopping down and, on the correction, snapping
// back up over the ledge. So when a guessed push is about to hop (the Link's
// hop count, per Link at 0x375, is down to 1: the game reads the buttons at
// the end of a frame, so a guess acts on the frame after; letting go or
// turning resets the count to 19), the direction it pushes in
// (link_direction_last) is left out of the guess: the Link lets go for a
// frame and pushes on, never hopping. When the real buttons arrive the hop
// is simulated at its true time. (Only the guesses change: the simulation
// stays the same on every computer.)
static uint16 ShapeGuess(int p, uint16 guess) {
  enum { kDirUp = 0x10, kDirDown = 0x20, kDirLeft = 0x40, kDirRight = 0x80 };
  int i = Inst_OfPlayer(p), r = Inst_Current();
  if (i < 0 || !(guess & 0xf0) || PlainGuess())
    return guess;
  Inst_Switch(i);
  if (g_mp.p[p].joined && !g_mp.p[p].downed && *Mp_PlayerRam(p, 0x5D) == 0 && *Mp_PlayerRam(p, 0x375) <= 1) {
    uint8 dir = *Mp_PlayerRam(p, 0x26);  // (8 up, 4 down, 2 left, 1 right)
    guess &= ~((dir & 8 ? kDirUp : 0) | (dir & 4 ? kDirDown : 0) | (dir & 2 ? kDirLeft : 0) | (dir & 1 ? kDirRight : 0));
  }
  Inst_Switch(r);
  return guess;
}

// Builds the next frame on the host (or offline). *known gets the slots whose
// buttons are real; a client's buttons that haven't arrived yet are guessed.
static void BuildFrame(MpFrameInput *fi, const uint16 *local_inputs, uint32 features, uint8 *known) {
  memset(fi, 0, sizeof(*fi));
  *known = 0;
  memcpy(g_s.host_in[(g_s.frame + 1) & 15], local_inputs, sizeof(g_s.host_in[0]));
  fi->features = features;
  fi->flags = (g_s.ff ? 1 : 0) | (ZeldaIsMusicPlaying() ? 2 : 0) | (g_s.story ? 4 : 0) | (g_s.rando ? 8 : 0) |
              (g_s.full_inv ? 0x10 : 0);
  fi->rando = g_s.rando;
  fi->ap = Ap_HostBind();
  if (fi->ap)
    fi->flags |= 0x20 | (Ap_HostWorld() == kApWorld_Alttp ? 0x40 : 0);
  uint8 reserved[kMpMaxPlayers] = { 0 };
  QueueJoinLeave(fi, 0, &g_s.pending_join_mask, &g_s.pending_leave_mask, reserved);
  for (int c = 0; c < kSessionMaxClients; c++) {
    HostClient *hc = &g_s.clients[c];
    if (hc->used)
      QueueJoinLeave(fi, hc->id, &hc->join_mask, &hc->leave_mask, reserved);
  }
  // An item from Archipelago (one per frame), or a DeathLink.
  uint8 ev[4];
  if (fi->nevents < kSessionMaxEvents && Ap_NextEvent(ev))
    AddEvent(fi, ev[0], ev[1], ev[2], ev[3]);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    uint8 owner = g_s.slot_owner[p], local = g_s.slot_local[p];
    if (owner == 0xff || local >= kSessionMaxLocal)
      continue;
    if (owner == 0) {
      fi->inputs[p] = local_inputs[local];
      if (g_s.st.mode == kSessionHost)
        fi->inputs[p] = g_s.host_in[(g_s.frame + 1 - g_s.input_delay) & 15][local];
      *known |= 1 << p;
    } else {
      // The buttons the client meant for this frame, or else a guess: it keeps
      // holding what it held (corrected by a rollback when they arrive).
      uint32 f = g_s.frame + 1;
      HostClient *hc = FindClientById(owner);
      if (hc && (hc->parked || hc->loading)) {
        // Lost the connection, or downloading the game (again): its Link
        // stands still until it plays.
        fi->inputs[p] = 0;
        *known |= 1 << p;
      } else if (hc && hc->sched_frame[f % kSchedSize] == f) {
        fi->inputs[p] = hc->sched[f % kSchedSize][local];
        *known |= 1 << p;
      } else {
        fi->inputs[p] = ShapeGuess(p, g_s.pend[(f - 1) % kRbSize].inputs[p]);
      }
    }
  }
}

// --- frame encoding ---

// How many bytes WriteFrame writes for fi.
static int FrameSize(const MpFrameInput *fi) {
  return kMpMaxPlayers * 2 + 1 + 1 + 4 + (fi->flags & 0x80 ? 4 : 0) + (fi->flags & 8 ? 4 : 0) +
         (fi->flags & 0x20 ? 4 : 0) + fi->nevents * 4;
}

static void WriteFrame(Writer *w, const MpFrameInput *fi) {
  for (int i = 0; i < kMpMaxPlayers; i++)
    Wr16(w, fi->inputs[i]);
  Wr8(w, fi->flags);
  Wr8(w, fi->nevents);
  Wr32(w, fi->features);
  if (fi->flags & 0x80)
    Wr32(w, fi->hash);
  if (fi->flags & 8)
    Wr32(w, fi->rando);
  if (fi->flags & 0x20)
    Wr32(w, fi->ap);
  for (int i = 0; i < fi->nevents; i++)
    WData(w, fi->events[i], 4);
}

static void ReadFrame(Reader *r, MpFrameInput *fi) {
  memset(fi, 0, sizeof(*fi));
  for (int i = 0; i < kMpMaxPlayers; i++)
    fi->inputs[i] = Rd16(r);
  fi->flags = Rd8(r);
  fi->nevents = Rd8(r);
  fi->features = Rd32(r);
  if (fi->flags & 0x80)
    fi->hash = Rd32(r);
  if (fi->flags & 8)
    fi->rando = Rd32(r);
  if (fi->flags & 0x20)
    fi->ap = Rd32(r);
  if (fi->nevents > kSessionMaxEvents) {
    r->err = true;
    return;
  }
  for (int i = 0; i < fi->nevents; i++)
    RData(r, fi->events[i], 4);
}

// --- common ---

void Session_Init(const char *player_name) {
  memset(&g_s, 0, sizeof(g_s));
  snprintf(g_s.name, sizeof(g_s.name), "%s", player_name && player_name[0] ? player_name : "Link");
  g_s.st.mode = kSessionOffline;
  ResetSlots();
  UpdateLocalSlots();
  Net_Init();
}

const SessionStatus *Session_GetStatus() {
  return &g_s.st;
}

void Session_ResetCounters() {
  g_s.st.resyncs = g_s.st.rollbacks = g_s.st.own_corrections = 0;
  g_s.st.mispredicted = g_s.st.b_flips = g_s.st.replayed_frames = g_s.st.max_rollback = 0;
}

bool Session_IsNetplay() {
  return g_s.st.mode != kSessionOffline;
}

bool Session_AllowsLocalStateChanges() {
  return g_s.st.mode != kSessionClient;
}

void Session_SetFriendlyFire(bool on) {
  g_s.ff = on;
}

void Session_SetStory(bool on) {
  g_s.story = on;
}

bool Session_GetStory() {
  return g_s.story;
}

void Session_SetInputDelay(int frames) {
  g_s.input_delay = frames < 0 ? 0 : frames > 8 ? 8 : frames;
}

bool Session_GetFriendlyFire() {
  return g_s.st.mode == kSessionClient ? Inst_GetFriendlyFire() : g_s.ff;
}

void Session_SetRandomizer(uint32 setting) {
  g_s.rando = setting;
}

uint32 Session_GetRandomizer() {
  return g_s.st.mode == kSessionClient ? Rando_GetNewGameSetting() : g_s.rando;
}

void Session_SetFullInventory(bool on) {
  g_s.full_inv = on;
}

bool Session_GetFullInventory() {
  return g_s.st.mode == kSessionClient ? FileSelect_GetFullInventory() : g_s.full_inv;
}



void Session_LocalJoin(int i) {
  if (i < 0 || i >= kSessionMaxLocal || g_s.st.local_slot[i] >= 0)
    return;
  g_s.pending_join_mask |= 1 << i;
}

void Session_LocalLeave(int i) {
  if (i < 0 || i >= kSessionMaxLocal || g_s.st.local_slot[i] < 0)
    return;
  g_s.pending_leave_mask |= 1 << i;
}

bool Session_IsLocalJoined(int i) {
  return i >= 0 && i < kSessionMaxLocal && g_s.st.local_slot[i] >= 0;
}

static void SetMessage(const char *msg) {
  snprintf(g_s.st.message, sizeof(g_s.st.message), "%s", msg);
  printf("[netplay] %s\n", msg);
}

// --- host ---

// The public address: from the router, or from a STUN server when the
// router won't open the port (forward it by hand). Found on the UPnP thread
// and handed to the game loop, which takes it while still hosting on that port.
static SDL_SpinLock g_upnp_lock;
static struct { bool ready, ok; uint16 port; char addr[48]; } g_upnp_result;

static void UpnpDone(bool ok, const char *external_ip, uint16 port) {
  char buf[48];
  if (external_ip[0])
    snprintf(buf, sizeof(buf), "%s:%u", external_ip, port);
  else if (ok)
    snprintf(buf, sizeof(buf), "your public IP:%u", port);
  else
    snprintf(buf, sizeof(buf), "unknown - forward UDP %u", port);
  SDL_AtomicLock(&g_upnp_lock);
  memcpy(g_upnp_result.addr, buf, sizeof(buf));
  g_upnp_result.ok = ok;
  g_upnp_result.port = port;
  g_upnp_result.ready = true;
  SDL_AtomicUnlock(&g_upnp_lock);
}

static void TakeUpnpResult() {
  SDL_AtomicLock(&g_upnp_lock);
  if (g_upnp_result.ready) {
    g_upnp_result.ready = false;
    if (g_s.st.mode == kSessionHost && g_s.sock && g_upnp_result.port == Net_LocalPort(g_s.sock)) {
      memcpy(g_s.st.public_addr, g_upnp_result.addr, sizeof(g_s.st.public_addr));
      g_s.st.upnp_failed = !g_upnp_result.ok;
    }
  }
  SDL_AtomicUnlock(&g_upnp_lock);
}

static void SaveRollbackState(uint32 f);

// Everything simulated so far becomes final (hosting starts, or the host
// loaded a save state).
static void HostResetPrediction() {
  while ((int32)(g_s.conf - g_s.frame) < 0) {
    g_s.conf++;
    g_s.ring[g_s.conf & (kRingSize - 1)] = g_s.pend[g_s.conf % kRbSize];
    g_s.ring_frame[g_s.conf & (kRingSize - 1)] = g_s.conf;
  }
  g_s.conf = g_s.frame;
  uint32 i = g_s.frame % kRbSize;
  g_s.pend[i] = g_s.ring[g_s.frame & (kRingSize - 1)];
  g_s.pend_known[i] = 0xff;
  memcpy(g_s.pend_owner[(g_s.frame + 1) % kRbSize], g_s.slot_owner, kMpMaxPlayers);
  memcpy(g_s.pend_local[(g_s.frame + 1) % kRbSize], g_s.slot_local, kMpMaxPlayers);
  g_s.host_rollback_pending = false;
  g_s.heard = g_s.frame;
  SaveRollbackState(g_s.frame);
}

bool Session_Host(uint16 port, bool use_upnp) {
  if (g_s.st.mode != kSessionOffline)
    Session_Disconnect();
  g_s.sock = Net_Open(port ? port : kSessionPort, true);
  if (!g_s.sock) {
    SetMessage("Unable to open the network port");
    return false;
  }
  // Hosting on another port: still answer "Find LAN games" on the usual one.
  if (Net_LocalPort(g_s.sock) != kSessionPort)
    g_s.disc_sock = Net_Open(kSessionPort, true);
  g_s.st.mode = kSessionHost;
  g_s.st.my_id = 0;
  HostResetPrediction();
  uint32 ip = Net_GetLocalIp();
  NetAddr a = { ip, Net_LocalPort(g_s.sock) };
  Net_AddrToString(&a, g_s.st.host_addr, sizeof(g_s.st.host_addr));
  g_s.st.public_addr[0] = 0;
  g_s.st.upnp_failed = false;
  g_s.nkicked = 0;
  SDL_AtomicLock(&g_upnp_lock);
  g_upnp_result.ready = false;
  SDL_AtomicUnlock(&g_upnp_lock);
  if (use_upnp) {
    snprintf(g_s.st.public_addr, sizeof(g_s.st.public_addr), "Opening router port...");
    Upnp_MapPortAsync(a.port, &UpnpDone);
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "Hosting on %s", g_s.st.host_addr);
  SetMessage(msg);
  return true;
}

static void HostStartSnapshot(HostClient *hc) {
  ByteArray raw = { 0 }, comp = { 0 };
  // The snapshot is of the newest final frame: later frames may still change.
  const uint8 *owner = g_s.slot_owner, *local = g_s.slot_local;
  if (g_s.conf != g_s.frame && g_s.rb[g_s.conf % kRbSize]) {
    static InstSnapshot *live;
    if (!live)
      live = Inst_SnapshotNew();
    Inst_SnapshotSave(live);
    Inst_SnapshotLoad(g_s.rb[g_s.conf % kRbSize]);
    Inst_SaveState(&raw);
    Inst_SnapshotLoad(live);
    // Who owned the slots right after that frame.
    owner = g_s.pend_owner[(g_s.conf + 1) % kRbSize];
    local = g_s.pend_local[(g_s.conf + 1) % kRbSize];
  } else {
    Inst_SaveState(&raw);
  }
  // header: frame, slot owners/locals, then the compressed state
  uint8 hdr[4 + 8];
  memcpy(hdr, &g_s.conf, 4);
  memcpy(hdr + 4, owner, 4);
  memcpy(hdr + 8, local, 4);
  ByteArray_AppendData(&comp, hdr, sizeof(hdr));
  RleCompress(raw.data, raw.size, &comp);
  ByteArray_Destroy(&raw);
  free(hc->snap);
  hc->snap = comp.data;
  hc->snap_size = (uint32)comp.size;
  hc->snap_have = 0;
  hc->snap_cursor = 0;
  hc->snap_id = ++g_s.next_snap_id;
  hc->loading = true;
  hc->need_frame = g_s.conf + 1;
}

static void HostDropClient(HostClient *hc, const char *why) {
  char msg[96];
  snprintf(msg, sizeof(msg), "%s %s", hc->name, why);
  SetMessage(msg);
  // Its players leave in the next frame (see Session_Tick).
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_s.slot_owner[p] == hc->id)
      g_s.slot_owner[p] = 0xfe;
  free(hc->snap);
  hc->snap = NULL;
  hc->used = false;
}

static HostClient *HostFindClient(const NetAddr *a) {
  for (int c = 0; c < kSessionMaxClients; c++)
    if (g_s.clients[c].used && Net_AddrEqual(&g_s.clients[c].addr, a))
      return &g_s.clients[c];
  return NULL;
}

// A player coming back (maybe from another address or port): by the token
// we gave it. After its game was restarted the token is gone; then by name,
// but only when exactly one seat fits: a waiting seat of that name, or one
// that has gone quiet (we may not have noticed yet that it was gone) from
// the same internet address. (Everybody is called "Link" by default: a
// stranger must never get someone else's seat.) *by_name says which it was.
static HostClient *HostFindReturning(uint32 token, const char *name, const NetAddr *from, bool *by_name) {
  *by_name = false;
  if (token) {
    for (int c = 0; c < kSessionMaxClients; c++)
      if (g_s.clients[c].used && g_s.clients[c].token == token)
        return &g_s.clients[c];
    return NULL;
  }
  uint32 now = SDL_GetTicks();
  HostClient *found = NULL;
  int n = 0;
  for (int c = 0; c < kSessionMaxClients; c++) {
    HostClient *hc = &g_s.clients[c];
    if (hc->used && hc->ever_loaded && !strcmp(hc->name, name) &&
        (hc->parked || (now - hc->last_recv > 1000 && hc->addr.ip == from->ip)))
      found = hc, n++;
  }
  *by_name = n == 1;
  return n == 1 ? found : NULL;
}

// Removed by the host: by token, or by name from the same address.
static bool HostWasKicked(uint32 token, const char *name, const NetAddr *from) {
  for (int i = 0; i < g_s.nkicked; i++)
    if ((token && g_s.kicked[i].token == token) || (g_s.kicked[i].ip == from->ip && !strcmp(g_s.kicked[i].name, name)))
      return true;
  return false;
}

static bool HostClientHasSeat(const HostClient *hc) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_s.slot_owner[p] == hc->id)
      return true;
  return false;
}

static uint32 NewToken() {
  static uint32 x;
  x = (x ^ SDL_GetTicks() ^ (uint32)SDL_GetPerformanceCounter()) * 2654435761u + 0x9e3779b9;
  return x ? x : 1;
}

// The connection to a playing client is lost: its players stand still and
// its seat waits kKeepSeatMs for it to come back.
static void HostParkClient(HostClient *hc) {
  char msg[96];
  snprintf(msg, sizeof(msg), "%s lost the connection: keeping the seat %d min", hc->name, kSessionKeepSeatSeconds / 60);
  SetMessage(msg);
  hc->parked = true;
  hc->parked_at = SDL_GetTicks();
  free(hc->snap);
  hc->snap = NULL;
  hc->loading = false;
  hc->need_frame = 0;
  hc->join_mask = hc->leave_mask = 0;
  memset(hc->sched_frame, 0, sizeof(hc->sched_frame));
}

// A client's buttons for a frame the host already ran with a guess. If the
// guess was wrong, the host rolls back to that frame.
static void HostLateInput(HostClient *hc, uint32 f, const uint16 *in) {
  uint32 i = f % kRbSize;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (g_s.pend_owner[i][p] != hc->id || g_s.pend_local[i][p] >= kSessionMaxLocal || (g_s.pend_known[i] >> p & 1))
      continue;
    g_s.pend_known[i] |= 1 << p;
    uint16 v = in[g_s.pend_local[i][p]];
    if (g_s.pend[i].inputs[p] != v) {
      g_s.pend[i].inputs[p] = v;
      if (!g_s.host_rollback_pending || (int32)(f - g_s.host_rollback) < 0)
        g_s.host_rollback = f;
      g_s.host_rollback_pending = true;
    }
  }
}

static void HostHandlePacket(const NetAddr *from, Reader *r, uint8 type) {
  Writer w;
  HostClient *hc = HostFindClient(from);
  if (type == kPkt_Discover) {
    WBegin(&w, kPkt_Info);
    WData(&w, g_s.name, 24);
    Wr8(&w, Inst_NumJoined());
    Wr16(&w, Net_LocalPort(g_s.sock));
    Net_Send(g_s.sock, from, w.buf, w.pos);
  } else if (type == kPkt_Hello) {
    uint16 ver = Rd16(r);
    char name[24];
    RData(r, name, 24);
    name[23] = 0;
    uint32 token = Rd32(r);
    uint8 rejoin = Rd8(r);
    if (ver != kProtoVersion) {
      WBegin(&w, kPkt_Reject);
      WData(&w, "Different game version", 23);
      Net_Send(g_s.sock, from, w.buf, w.pos);
      return;
    }
    if (r->err)
      return;
    if (HostWasKicked(token, name, from)) {
      static const char kWhy[] = "The host removed you from the game";
      WBegin(&w, kPkt_Reject);
      WData(&w, kWhy, sizeof(kWhy));
      Net_Send(g_s.sock, from, w.buf, w.pos);
      return;
    }
    // A waiting seat found by its old address still has to be the same player.
    if (hc && hc->parked && token != hc->token && (token || strcmp(name, hc->name)))
      hc = NULL;
    bool moved = false, by_name = false;
    if (!hc)
      moved = (hc = HostFindReturning(token, name, from, &by_name)) != NULL;
    if (hc && (hc->parked || moved || rejoin)) {
      // Back after losing the connection: the same seat, and a fresh copy
      // of the game (it missed what happened meanwhile, and a download it
      // was in the middle of starts over). Its Link stands still until then.
      if (hc->parked || moved) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s is back", hc->name);
        SetMessage(msg);
      }
      hc->addr = *from;
      hc->parked = false;
      hc->margin_count = 0;
      hc->margin_report = -128;
      // (Found by name: a new token, so an old copy of that game can't take
      // the seat back.)
      if (by_name)
        hc->token = NewToken();
      memset(hc->sched_frame, 0, sizeof(hc->sched_frame));
      HostStartSnapshot(hc);
    }
    if (!hc) {
      // Room for one more? Players whose join is still queued count too.
      int pending = 0;
      for (int c = 0; c < kSessionMaxClients; c++)
        if (g_s.clients[c].used && (g_s.clients[c].join_mask & 1) && FindSlotOf(g_s.clients[c].id, 0) < 0)
          pending++;
      if (Inst_NumJoined() + pending >= kMpMaxPlayers) {
        WBegin(&w, kPkt_Reject);
        WData(&w, "Game is full", 13);
        Net_Send(g_s.sock, from, w.buf, w.pos);
        return;
      }
      for (int c = 0; c < kSessionMaxClients && !hc; c++) {
        if (!g_s.clients[c].used) {
          hc = &g_s.clients[c];
          memset(hc, 0, sizeof(*hc));
          hc->used = true;
          hc->addr = *from;
          hc->id = c + 1;
          memcpy(hc->name, name, 24);
          hc->token = NewToken();
          hc->join_mask = 1;
          hc->margin_report = -128;  // nothing to report yet
          HostStartSnapshot(hc);
          char msg[96];
          snprintf(msg, sizeof(msg), "%s joined", name);
          SetMessage(msg);
        }
      }
      if (!hc) {
        WBegin(&w, kPkt_Reject);
        WData(&w, "Game is full", 13);
        Net_Send(g_s.sock, from, w.buf, w.pos);
        return;
      }
    }
    hc->last_recv = SDL_GetTicks();
    WBegin(&w, kPkt_Welcome);
    Wr8(&w, hc->id);
    WData(&w, g_s.name, 24);
    Wr32(&w, hc->token);
    Net_Send(g_s.sock, from, w.buf, w.pos);
  } else if (type == kPkt_Input && hc && !hc->parked) {
    hc->last_recv = SDL_GetTicks();
    Rd8(r);  // id
    uint32 need = Rd32(r);
    uint32 first = Rd32(r);
    int count = Rd8(r);
    if (count > kInputsPerPacket)
      return;
    uint16 inputs[kInputsPerPacket][kSessionMaxLocal];
    for (int k = 0; k < count; k++)
      for (int i = 0; i < kSessionMaxLocal; i++)
        inputs[k][i] = Rd16(r);
    uint8 join = Rd8(r), leave = Rd8(r);
    uint8 snap_id = Rd8(r);
    uint32 snap_have = Rd32(r);
    uint8 resync = Rd8(r);
    uint32 ping = Rd32(r);
    uint16 ping_ms = Rd16(r);
    if (r->err)
      return;
    hc->ping_ms = ping_ms;
    for (int k = 0; k < count; k++) {
      uint32 f = first + k;
      if ((int32)(f - g_s.frame) > 0 && (int32)(f - g_s.frame) < kSchedSize) {
        memcpy(hc->sched[f % kSchedSize], inputs[k], sizeof(inputs[k]));
        hc->sched_frame[f % kSchedSize] = f;
      } else if ((int32)(f - g_s.conf) > 0 && (int32)(f - g_s.frame) <= 0) {
        HostLateInput(hc, f, inputs[k]);
      }
    }
    if (count) {
      // The next frame built is g_s.frame + 1.
      int margin = (int)(first + count - 1 - (g_s.frame + 1));
      margin = margin < -100 ? -100 : margin > 100 ? 100 : margin;
      if (hc->margin_count == 0 || margin < hc->margin_min)
        hc->margin_min = margin;
      if (++hc->margin_count >= kMarginWindow) {
        hc->margin_report = hc->margin_min;
        hc->margin_count = 0;
      }
    }
    hc->join_mask |= join;
    hc->leave_mask |= leave;
    hc->ping_echo = ping;
    if (hc->loading && snap_id == hc->snap_id) {
      if (snap_have > hc->snap_have)
        hc->snap_have = snap_have;
      else if (hc->snap_cursor >= hc->snap_size)
        hc->snap_cursor = hc->snap_have;  // everything went out once: fill the holes
      if (hc->snap_have >= hc->snap_size) {
        hc->loading = false;
        hc->ever_loaded = true;
        // Measure how early its buttons arrive afresh, from now on.
        hc->margin_count = 0;
        hc->margin_report = -128;
        free(hc->snap);
        hc->snap = NULL;
      }
    }
    if (!hc->loading) {
      hc->need_frame = need;
      // Too far behind to catch up from the frame history.
      if (resync || g_s.conf - need + 1 >= kRingSize - 64)
        HostStartSnapshot(hc);
    }
  } else if (type == kPkt_Bye && hc) {
    HostDropClient(hc, "left");
  }
}

static void HostRoster(SessionPlayer *out);

static void HostSend() {
  Writer w;
  uint32 now = SDL_GetTicks();
  // Every so often: who is playing, for the clients' player lists.
  bool roster = now - g_s.last_roster >= kRosterMs;
  SessionPlayer seats[kMpMaxPlayers];
  if (roster) {
    g_s.last_roster = now;
    HostRoster(seats);
  }
  for (int c = 0; c < kSessionMaxClients; c++) {
    HostClient *hc = &g_s.clients[c];
    if (!hc->used)
      continue;
    if (hc->parked) {
      if (now - hc->parked_at > kKeepSeatMs)
        HostDropClient(hc, "didn't come back");
      continue;
    }
    if (now - hc->last_recv > kTimeoutMs) {
      // A player in the game keeps the seat for a while; one that never got
      // in (still downloading its first copy) is just dropped.
      if (hc->ever_loaded && HostClientHasSeat(hc))
        HostParkClient(hc);
      else
        HostDropClient(hc, "timed out");
      continue;
    }
    if (roster) {
      WBegin(&w, kPkt_Roster);
      for (int p = 0; p < kMpMaxPlayers; p++) {
        Wr8(&w, seats[p].state);
        Wr8(&w, seats[p].owner);
        Wr8(&w, seats[p].local);
        Wr8(&w, seats[p].seconds_left);
        Wr16(&w, seats[p].ping_ms);
        WData(&w, seats[p].name, 16);
      }
      Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      // The latest Archipelago lines, again and again (the client keeps the new ones).
      if (g_s.notice_seq) {
        WBegin(&w, kPkt_ApNotice);
        int n = g_s.notice_seq < 4 ? g_s.notice_seq : 4;
        Wr8(&w, n);
        for (int i = n; i > 0; i--) {
          const char *t = g_s.notice[(g_s.notice_seq - i) % 8].text;
          int len = (int)strlen(t);
          Wr32(&w, g_s.notice[(g_s.notice_seq - i) % 8].seq);
          Wr8(&w, g_s.notice[(g_s.notice_seq - i) % 8].kind);
          Wr8(&w, len);
          WData(&w, t, len);
        }
        Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      }
    }
    if (hc->loading) {
      WBegin(&w, kPkt_SnapInfo);
      Wr8(&w, hc->snap_id);
      Wr32(&w, hc->snap_size);
      Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      // Each chunk goes out once (not again every tick until it is acknowledged,
      // which would flood a slow uplink); lost ones are sent again once
      // everything went out and the acknowledgements stop advancing.
      uint32 off = hc->snap_cursor > hc->snap_have ? hc->snap_cursor : hc->snap_have;
      for (uint32 i = 0; i < kChunksPerTick && off < hc->snap_size; i++, off += kChunkSize) {
        uint32 n = hc->snap_size - off < kChunkSize ? hc->snap_size - off : kChunkSize;
        WBegin(&w, kPkt_SnapData);
        Wr8(&w, hc->snap_id);
        Wr32(&w, off);
        Wr16(&w, n);
        WData(&w, hc->snap + off, n);
        Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      }
      hc->snap_cursor = off;
    }
    // Frames after the snapshot are sent even while loading so they are ready.
    uint32 first = hc->need_frame;
    if (first == 0 || first > g_s.conf)
      continue;
    if (g_s.conf - first >= kRingSize - 1)
      first = g_s.conf - (kRingSize - 2);
    // As many frames as fit in one packet. The frames a client still needs
    // (from the one it last asked for, which we learn a round trip late) pile
    // up over a round trip: with fewer per packet than the round trip holds
    // in frames, the stream can never catch up on a slow link and the client
    // falls ever further behind.
    int count = 0, bytes = 3 + 4 + 1 + 4 + 1 + 1;
    while ((int32)(g_s.conf - (first + count)) >= 0 && count < kMaxFramesPerPacket) {
      int n = FrameSize(&g_s.ring[(first + count) & (kRingSize - 1)]);
      if (bytes + n > (int)sizeof(w.buf))
        break;
      bytes += n;
      count++;
    }
    WBegin(&w, kPkt_Frames);
    Wr32(&w, first);
    Wr8(&w, count);
    Wr32(&w, hc->ping_echo);
    Wr8(&w, (uint8)(int8)hc->margin_report);
    Wr8(&w, hc->margin_report != -128);
    for (int i = 0; i < count; i++)
      WriteFrame(&w, &g_s.ring[(first + i) & (kRingSize - 1)]);
    Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
  }
}

void Session_ResyncAll() {
  if (g_s.st.mode != kSessionHost)
    return;
  HostResetPrediction();
  for (int c = 0; c < kSessionMaxClients; c++)
    if (g_s.clients[c].used)
      HostStartSnapshot(&g_s.clients[c]);
}

// --- client ---

bool Session_Join(const char *address) {
  if (g_s.st.mode != kSessionOffline)
    Session_Disconnect();
  NetAddr a;
  if (!Session_DecodeRoomCode(address, &a.ip, &a.port) && !Net_ParseAddr(address, kSessionPort, &a)) {
    SetMessage("Unknown room code or address");
    return false;
  }
  g_s.sock = Net_Open(0, false);
  if (!g_s.sock) {
    SetMessage("Unable to open a network socket");
    return false;
  }
  g_s.host = a;
  g_s.st.mode = kSessionClient;
  g_s.st.client_state = 1;
  g_s.st.my_id = 0xfd;  // not assigned yet
  g_s.token = 0;
  g_s.rejoin_locals = 0;
  g_s.roster_known = false;
  g_s.in_host_world = false;
  g_s.last_recv = SDL_GetTicks();
  g_s.last_hello = 0;
  g_s.snap_known = false;
  Net_AddrToString(&a, g_s.st.host_addr, sizeof(g_s.st.host_addr));
  g_sram_write_disabled = true;
  char msg[96];
  snprintf(msg, sizeof(msg), "Connecting to %s...", g_s.st.host_addr);
  SetMessage(msg);
  return true;
}

static void SaveRollbackState(uint32 f) {
  InstSnapshot **sn = &g_s.rb[f % kRbSize];
  if (!*sn)
    *sn = Inst_SnapshotNew();
  Inst_SnapshotSave(*sn);
  g_s.rb_hash[f % kRbSize] = f % kHashInterval == 0 || g_session_dump_every ? Inst_HashState() : 0;
}

// Test instrumentation: the confirmed state after frame f, as last simulated.
static void DumpConfirmed(uint32 f) {
  if (!g_session_dump_every || f % g_session_dump_every || !g_s.rb[f % kRbSize])
    return;
  printf("DUMP %u %08x", f, g_s.rb_hash[f % kRbSize]);
  Inst_SnapshotPrintSprites(g_s.rb[f % kRbSize]);
  printf("\n");
}

static bool SameFrameInput(const MpFrameInput *a, const MpFrameInput *b) {
  return !memcmp(a->inputs, b->inputs, sizeof(a->inputs)) && (a->flags & 0x7f) == (b->flags & 0x7f) &&
         a->nevents == 0 && b->nevents == 0 && a->features == b->features && a->rando == b->rando &&
         a->ap == b->ap;
}

// Our guess at frame f: the other players keep holding what they held in the
// newest confirmed frame (but for a ledge, ShapeGuess), and ours are the
// buttons we pressed for frame f.
static void PredictFrame(uint32 f, MpFrameInput *fi) {
  *fi = g_s.ring[g_s.conf & (kRingSize - 1)];
  fi->flags &= ~0x80;
  fi->nevents = 0;
  uint8 mine = 0;
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (g_s.st.local_slot[i] >= 0) {
      fi->inputs[g_s.st.local_slot[i]] = g_s.my_in[f % kRbSize][i];
      mine |= 1 << g_s.st.local_slot[i];
    }
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (!(mine >> p & 1))
      fi->inputs[p] = ShapeGuess(p, fi->inputs[p]);
}

static bool HostFrameAvailable(uint32 f) {
  return g_s.ring_frame[f & (kRingSize - 1)] == f && (int32)(f - g_s.conf) > 0 &&
         (int32)(f - g_s.conf) < kRingSize - 1;
}

// Runs the next frame (g_s.frame + 1) with fi and remembers it for rollbacks.
static void ClientRunFrame(const MpFrameInput *fi) {
  uint32 f = g_s.frame + 1;
  bool hear = (int32)(f - g_s.heard) > 0;
  Inst_SetAudioOutput(hear);
  RunFrame(fi, true);
  Inst_SetAudioOutput(true);
  if (hear)
    g_s.heard = f;
  g_s.rb_in[f % kRbSize] = *fi;
  SaveRollbackState(f);
}

// Confirms frame f (run with the host's input). Returns false on a desync.
static bool ClientConfirm(uint32 f) {
  const MpFrameInput *fi = &g_s.ring[f & (kRingSize - 1)];
  g_s.conf = f;
  DumpConfirmed(f);
  if ((fi->flags & 0x80) && g_s.rb_hash[f % kRbSize] != fi->hash) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Out of sync at frame %u, resyncing", f);
    SetMessage(msg);
    g_s.want_resync = true;
    g_s.st.client_state = 2;
    g_s.st.resyncs++;
    return false;
  }
  return true;
}

static bool IsLocalSlot(int p) {
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (g_s.st.local_slot[i] == p)
      return true;
  return false;
}

static int Decay(int v) {
  int d = v / 4;
  return v - (d ? d : v > 0 ? 1 : v < 0 ? -1 : 0);
}

// Positions of all players, to smooth out corrections after a rollback.
typedef struct PlayerPositions {
  int world[kMpMaxPlayers];
  uint16 x[kMpMaxPlayers], y[kMpMaxPlayers];
} PlayerPositions;

static void GetPlayerPositions(PlayerPositions *pp) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    pp->world[p] = Inst_PlayerPos(p, &pp->x[p], &pp->y[p]);
}

// Links were shown in the wrong place. Rather than jumping, they slide over
// to where they really are. (Mostly other players' Links; our own buttons are
// never guessed, but in a fight what a teammate really did can change what an
// enemy did to our Link, a knockback say, and then ours slides too.)
static void SmoothCorrections(const PlayerPositions *was) {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    uint16 x, y;
    if (was->world[p] < 0 || Inst_PlayerPos(p, &x, &y) != was->world[p])
      continue;
    int dx = g_mp_draw_offset[p][0] + (int16)(was->x[p] - x);
    int dy = g_mp_draw_offset[p][1] + (int16)(was->y[p] - y);
    bool small = abs(dx) < 64 && abs(dy) < 64;  // bigger: a real teleport
    g_mp_draw_offset[p][0] = small ? dx : 0;
    g_mp_draw_offset[p][1] = small ? dy : 0;
  }
}

// One display frame of a client: take in the host's frames (rolling back if
// we guessed wrong) and predict ahead so our own Link reacts right away.
static int ClientTick(const uint16 *local_inputs) {
  int shown = 0;
  for (int p = 0; p < kMpMaxPlayers; p++)
    for (int k = 0; k < 2; k++)
      g_mp_draw_offset[p][k] = Decay(g_mp_draw_offset[p][k]);
  // 1. Frames from the host that we already predicted: were we right?
  uint32 mismatch = 0;
  while (HostFrameAvailable(g_s.conf + 1) && (int32)(g_s.conf + 1 - g_s.frame) <= 0) {
    uint32 f = g_s.conf + 1;
    if (!SameFrameInput(&g_s.ring[f & (kRingSize - 1)], &g_s.rb_in[f % kRbSize])) {
      for (int p = 0; p < kMpMaxPlayers; p++)
        if (IsLocalSlot(p) && g_s.ring[f & (kRingSize - 1)].inputs[p] != g_s.rb_in[f % kRbSize].inputs[p])
          g_s.st.own_corrections++;
      mismatch = f;
      break;
    }
    if (!ClientConfirm(f))
      return 0;
  }
  if (mismatch) {
    // Go back to the last confirmed frame and simulate again with what the
    // host says really happened, then our guesses up to where we were.
    uint32 end = g_s.frame;
    PlayerPositions was;
    GetPlayerPositions(&was);
    Inst_SnapshotLoad(g_s.rb[g_s.conf % kRbSize]);
    g_s.frame = g_s.conf;
    g_s.st.replayed_frames += end - g_s.conf;
    if ((int)(end - g_s.conf) > g_s.st.max_rollback)
      g_s.st.max_rollback = end - g_s.conf;
    // Session slots only change in confirmed frames; restore them too.
    while ((int32)(g_s.frame - end) < 0) {
      uint32 f = g_s.frame + 1;
      if (HostFrameAvailable(f) && f == g_s.conf + 1) {
        // Was our guess for this frame wrong? (Statistics.)
        const MpFrameInput *h = &g_s.ring[f & (kRingSize - 1)], *g = &g_s.rb_in[f % kRbSize];
        if (!SameFrameInput(h, g)) {
          g_s.st.mispredicted++;
          for (int p = 0; p < kMpMaxPlayers; p++)
            if (!IsLocalSlot(p) && ((h->inputs[p] ^ g->inputs[p]) & 1)) { g_s.st.b_flips++; break; }
        }
        ClientRunFrame(&g_s.ring[f & (kRingSize - 1)]);
        if (!ClientConfirm(f))
          return 0;
      } else {
        MpFrameInput fi;
        PredictFrame(f, &fi);
        ClientRunFrame(&fi);
      }
    }
    g_s.st.rollbacks++;
    SmoothCorrections(&was);
  }
  // 2. Behind the host (just connected or stalled): catch up quickly.
  int caught_up = 0;
  while (HostFrameAvailable(g_s.frame + 1) && g_s.frame == g_s.conf && caught_up < 16) {
    uint32 f = g_s.frame + 1;
    memcpy(g_s.my_in[(f + g_s.input_delay) % kRbSize], local_inputs, sizeof(g_s.my_in[0]));
    ClientRunFrame(&g_s.ring[f & (kRingSize - 1)]);
    if (!ClientConfirm(f))
      return 0;
    caught_up++;
  }
  shown += caught_up;
  int behind = 0;
  while (HostFrameAvailable(g_s.frame + 1 + behind))
    behind++;
  // 3. Predict new frames. Normally one per display frame; the host tells us
  // how early our inputs arrive and we run a little faster or slower so they
  // arrive 1-3 frames before the host needs them.
  int adv = behind > 2 ? 0 : 1;
  if (adv && g_s.margin_known && g_s.lead_cooldown <= 0) {
    int rtt_frames = g_s.st.ping_ms * 60 / 1000;
    if (g_s.margin < 1) {
      g_s.lead_debt = 1 - g_s.margin < 30 ? 1 - g_s.margin : 30;
      g_s.lead_cooldown = kMarginWindow + rtt_frames + 4;
    } else if (g_s.margin > 4) {
      adv = 0;
      g_s.lead_debt = 0;
      g_s.lead_cooldown = kMarginWindow + rtt_frames + 4;
    }
  }
  // Catching up is spread over several ticks: one input packet (the newest
  // kInputsPerPacket frames) must still cover every frame run since the
  // last one, or the host never hears some of our buttons and waits.
  if (adv && g_s.lead_debt > 0) {
    int extra = g_s.lead_debt < 3 ? g_s.lead_debt : 3;
    adv += extra;
    g_s.lead_debt -= extra;
  }
  if (g_s.lead_cooldown > 0)
    g_s.lead_cooldown--;
  for (int i = 0; i < adv && (int32)(g_s.frame - g_s.conf) < kMaxLead && g_s.st.client_state == 3; i++) {
    uint32 f = g_s.frame + 1;
    memcpy(g_s.my_in[(f + g_s.input_delay) % kRbSize], local_inputs, sizeof(g_s.my_in[0]));
    if (HostFrameAvailable(f) && f == g_s.conf + 1) {
      ClientRunFrame(&g_s.ring[f & (kRingSize - 1)]);
      if (!ClientConfirm(f))
        return shown;
    } else {
      MpFrameInput fi;
      PredictFrame(f, &fi);
      ClientRunFrame(&fi);
    }
    shown++;
  }
  g_s.st.frames_buffered = behind;
  g_s.st.predicted = g_s.frame - g_s.conf;
  g_s.st.margin = g_s.margin_known ? g_s.margin : -99;
  g_s.st.lead_debt = g_s.lead_debt;
  return shown;
}

// One display frame of the host. The host runs ahead like a client: when a
// client's buttons arrive after the host has already guessed them, it rolls
// back and simulates again with the real ones. A frame becomes final, and is
// sent to the clients, once everyone's buttons for it are known. So nobody's
// own Link is ever moved by a guess (no rubber banding).
static int HostTick(const uint16 *local_inputs, uint32 features) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    for (int k = 0; k < 2; k++)
      g_mp_draw_offset[p][k] = Decay(g_mp_draw_offset[p][k]);
  if (g_s.host_rollback_pending && (int32)(g_s.host_rollback - g_s.conf) > 0 && (int32)(g_s.host_rollback - g_s.frame) <= 0) {
    PlayerPositions was;
    GetPlayerPositions(&was);
    uint32 end = g_s.frame;
    Inst_SnapshotLoad(g_s.rb[(g_s.host_rollback - 1) % kRbSize]);
    g_s.frame = g_s.host_rollback - 1;
    g_s.st.replayed_frames += end - g_s.frame;
    if ((int)(end - g_s.frame) > g_s.st.max_rollback)
      g_s.st.max_rollback = end - g_s.frame;
    while ((int32)(g_s.frame - end) < 0) {
      uint32 f = g_s.frame + 1, i = f % kRbSize;
      // Buttons still unknown: the guess follows the frame before (but for
      // a ledge, ShapeGuess).
      for (int p = 0; p < kMpMaxPlayers; p++)
        if (!(g_s.pend_known[i] >> p & 1))
          g_s.pend[i].inputs[p] = ShapeGuess(p, g_s.pend[(f - 1) % kRbSize].inputs[p]);
      // (Statistics: was the guess for a client's buttons wrong?)
      if (memcmp(&g_s.pend[i].inputs, &g_s.rb_in[i].inputs, sizeof(g_s.pend[i].inputs)) != 0) {
        g_s.st.mispredicted++;
        for (int p = 0; p < kMpMaxPlayers; p++)
          if (g_s.pend_owner[i][p] != 0 && ((g_s.pend[i].inputs[p] ^ g_s.rb_in[i].inputs[p]) & 1)) { g_s.st.b_flips++; break; }
      }
      ClientRunFrame(&g_s.pend[i]);
    }
    g_s.st.rollbacks++;
    SmoothCorrections(&was);
  }
  g_s.host_rollback_pending = false;

  // The next frame.
  uint32 f = g_s.frame + 1, i = f % kRbSize;
  memcpy(g_s.pend_owner[i], g_s.slot_owner, kMpMaxPlayers);
  memcpy(g_s.pend_local[i], g_s.slot_local, kMpMaxPlayers);
  BuildFrame(&g_s.pend[i], local_inputs, features, &g_s.pend_known[i]);
  // Players of dropped clients leave.
  for (int p = 0; p < kMpMaxPlayers; p++)
    if (g_s.slot_owner[p] == 0xfe && g_s.pend[i].nevents < kSessionMaxEvents)
      AddEvent(&g_s.pend[i], kSessEv_Leave, p, 0xff, 0xff);
  ClientRunFrame(&g_s.pend[i]);

  // Frames whose buttons are all known become final. A client that is still
  // loading, or has gone quiet, isn't waited for (nor for longer than kMaxWait).
  uint32 now = SDL_GetTicks();
  while ((int32)(g_s.conf - g_s.frame) < 0) {
    uint32 cf = g_s.conf + 1, ci = cf % kRbSize;
    bool ready = true;
    for (int p = 0; p < kMpMaxPlayers; p++) {
      uint8 owner = g_s.pend_owner[ci][p];
      HostClient *hc = owner && owner < 0xfe ? FindClientById(owner) : NULL;
      if (hc && !hc->loading && now - hc->last_recv < kStallMs && !(g_s.pend_known[ci] >> p & 1))
        ready = false;
    }
    if (!ready && g_s.frame - cf < kMaxWait)
      break;
    bool missing = false;
    for (int p = 0; p < kMpMaxPlayers; p++)
      missing |= g_s.pend_owner[ci][p] != 0 && g_s.pend_owner[ci][p] < 0xfe && !(g_s.pend_known[ci] >> p & 1);
    HostClient *hc1 = FindClientById(1);
    if (missing && hc1 && !hc1->loading && g_session_dump_every) {
      // (Tests, with dumpevery on: a frame made final without a client's buttons.)
      static int n;
      if (n++ < 80) {
        HostClient *hc = hc1;
        printf("WAITED-OUT frame %u (host frame %u, lead %d) known=%x %s silent=%u ms margin_report=%d\n", cf, g_s.frame, (int)(g_s.frame - cf), g_s.pend_known[ci],
               ready ? "client stalled/loading" : "kMaxWait", hc ? now - hc->last_recv : 0, hc ? hc->margin_report : -99);
      }
    }
    MpFrameInput fi = g_s.pend[ci];
    if (cf % kHashInterval == 0) {
      fi.flags |= 0x80;
      fi.hash = g_s.rb_hash[ci];
    }
    g_s.ring[cf & (kRingSize - 1)] = fi;
    g_s.ring_frame[cf & (kRingSize - 1)] = cf;
    g_s.conf = cf;
    DumpConfirmed(cf);
  }
  g_s.st.predicted = g_s.frame - g_s.conf;
  HostSend();
  return 1;
}

static void ClientFinishSnapshot() {
  ByteArray raw = { 0 };
  if (g_s.snap_size < 12 || !RleDecompress(g_s.snap + 12, g_s.snap_size - 12, &raw)) {
    SetMessage("Received a broken snapshot, retrying");
    g_s.want_resync = true;
    ByteArray_Destroy(&raw);
    return;
  }
  uint32 frame;
  memcpy(&frame, g_s.snap, 4);
  memcpy(g_s.slot_owner, g_s.snap + 4, 4);
  memcpy(g_s.slot_local, g_s.snap + 8, 4);
  if (!Inst_LoadState(raw.data, raw.size))
    SetMessage("Snapshot size mismatch");
  ByteArray_Destroy(&raw);
  g_s.frame = g_s.conf = g_s.snap_frame = g_s.heard = frame;
  memset(g_s.my_in, 0, sizeof(g_s.my_in));
  g_s.margin_known = false;
  g_s.lead_cooldown = 0;
  g_s.lead_debt = 0;
  memset(g_mp_draw_offset, 0, sizeof(g_mp_draw_offset));
  SaveRollbackState(frame);
  UpdateLocalSlots();
  g_s.st.client_state = 3;
  g_s.want_resync = false;
  g_s.in_host_world = true;
  // Back after a lost connection with a new host (our seats are gone): our
  // other local players join again too.
  for (int i = 1; i < kSessionMaxLocal; i++)
    if ((g_s.rejoin_locals >> i & 1) && g_s.st.local_slot[i] < 0)
      g_s.pending_join_mask |= 1 << i;
  g_s.rejoin_locals = 0;
  char msg[64];
  snprintf(msg, sizeof(msg), "Connected to %s", g_s.host_name[0] ? g_s.host_name : "the host");
  SetMessage(msg);
}

static void ClientHandlePacket(const NetAddr *from, Reader *r, uint8 type) {
  if (type == kPkt_Info) {
    char name[25] = { 0 };
    RData(r, name, 24);
    uint8 players = Rd8(r);
    uint16 port = Rd16(r);
    if (r->err)
      return;
    NetAddr a = { from->ip, port };
    // The host on this same machine answers both the broadcast and localhost.
    if (a.ip == 0x7f000001 && Net_GetLocalIp())
      a.ip = Net_GetLocalIp();
    char addr[32];
    Net_AddrToString(&a, addr, sizeof(addr));
    int i;
    for (i = 0; i < g_s.nlan && strcmp(g_s.lan[i].addr, addr); i++) {}
    if (i == g_s.nlan && g_s.nlan < 8)
      g_s.nlan++;
    if (i < g_s.nlan) {
      snprintf(g_s.lan[i].name, sizeof(g_s.lan[i].name), "%s", name);
      snprintf(g_s.lan[i].addr, sizeof(g_s.lan[i].addr), "%s", addr);
      g_s.lan[i].players = players;
      g_s.lan[i].last_seen = SDL_GetTicks();
    }
    return;
  }
  if (g_s.st.mode != kSessionClient || !Net_AddrEqual(from, &g_s.host))
    return;
  g_s.last_recv = SDL_GetTicks();
  if (type == kPkt_Welcome) {
    uint8 id = Rd8(r);
    RData(r, g_s.host_name, 24);
    g_s.host_name[23] = 0;
    uint32 token = Rd32(r);
    if (!r->err && (g_s.st.client_state == 1 || g_s.st.client_state == 4)) {
      SetMessage(g_s.st.client_state == 4 ? "Back in: downloading the game again..." : "Downloading the host's game...");
      // A new token: a host that was restarted, which numbers its
      // Archipelago lines from 1 again.
      if (token != g_s.token)
        g_s.notice_seq = 0;
      g_s.st.my_id = id;
      g_s.token = token;
      g_s.st.client_state = 2;
      g_s.snap_known = false;
      g_s.want_resync = false;
    }
  } else if (type == kPkt_Roster) {
    SessionPlayer seats[kMpMaxPlayers];
    for (int p = 0; p < kMpMaxPlayers; p++) {
      seats[p].state = Rd8(r);
      seats[p].owner = Rd8(r);
      seats[p].local = Rd8(r);
      seats[p].seconds_left = Rd8(r);
      seats[p].ping_ms = Rd16(r);
      RData(r, seats[p].name, 16);
      seats[p].name[15] = 0;
    }
    if (!r->err) {
      memcpy(g_s.roster, seats, sizeof(seats));
      g_s.roster_known = true;
    }
  } else if (type == kPkt_ApNotice) {
    int n = Rd8(r);
    for (int i = 0; i < n && !r->err; i++) {
      uint32 seq = Rd32(r);
      uint8 kind = Rd8(r), len = Rd8(r);
      char text[96] = { 0 };
      if (len >= sizeof(text) || r->end - r->p < len)
        break;
      RData(r, text, len);
      if (r->err || (int32)(seq - g_s.notice_seq) <= 0)
        continue;
      g_s.notice_seq = seq;
      int k = g_s.notice_wr++ % 8;
      g_s.notice[k].seq = seq;
      g_s.notice[k].kind = kind;
      memcpy(g_s.notice[k].text, text, sizeof(text));
      if (g_s.notice_wr - g_s.notice_rd > 8)
        g_s.notice_rd = g_s.notice_wr - 8;
    }
  } else if (type == kPkt_Reject) {
    char why[64] = { 0 };
    RData(r, why, (int)(r->end - r->p) < 63 ? (int)(r->end - r->p) : 63);
    Session_Disconnect();
    SetMessage(why);
  } else if (type == kPkt_SnapInfo && (g_s.st.client_state == 2 || g_s.st.client_state == 3)) {
    uint8 id = Rd8(r);
    uint32 size = Rd32(r);
    if (r->err || size > 4 << 20)
      return;
    if (!g_s.snap_known || id != g_s.snap_id) {
      g_s.want_resync = false;  // the host heard us: stop asking
      free(g_s.snap);
      free(g_s.snap_chunk_have);
      g_s.snap = (uint8 *)malloc(size);
      g_s.snap_chunk_have = (uint8 *)calloc(size / kChunkSize + 1, 1);
      g_s.snap_size = size;
      g_s.snap_have = 0;
      g_s.snap_id = id;
      g_s.snap_known = true;
      if (g_s.st.client_state == 3) {
        g_s.st.client_state = 2;
        g_s.st.resyncs++;
      }
    }
  } else if (type == kPkt_SnapData && g_s.snap_known && g_s.st.client_state == 2) {
    uint8 id = Rd8(r);
    uint32 off = Rd32(r);
    uint16 n = Rd16(r);
    if (r->err || id != g_s.snap_id || off % kChunkSize || off + n > g_s.snap_size || r->end - r->p < n)
      return;
    memcpy(g_s.snap + off, r->p, n);
    g_s.snap_chunk_have[off / kChunkSize] = 1;
    while (g_s.snap_have < g_s.snap_size && g_s.snap_chunk_have[g_s.snap_have / kChunkSize])
      g_s.snap_have = g_s.snap_have + kChunkSize < g_s.snap_size ? g_s.snap_have + kChunkSize : g_s.snap_size;
    g_s.st.snapshot_progress = (int)((uint64)g_s.snap_have * 100 / g_s.snap_size);
    if (g_s.snap_have >= g_s.snap_size) {
      memcpy(&g_s.snap_frame, g_s.snap, 4);
      ClientFinishSnapshot();
    }
  } else if (type == kPkt_Frames) {
    uint32 first = Rd32(r);
    int count = Rd8(r);
    uint32 echo = Rd32(r);
    int margin = (int8)Rd8(r);
    bool margin_valid = Rd8(r);
    if (echo)
      g_s.st.ping_ms = SDL_GetTicks() - echo;
    if (margin_valid && !r->err) {
      static int n;
      if (g_session_dump_every && margin != g_s.margin && n++ < 300)
        printf("MARGIN report=%d frame=%u conf=%u pred=%d debt=%d cooldown=%d ping=%d\n", margin, g_s.frame, g_s.conf, (int)(g_s.frame - g_s.conf), g_s.lead_debt, g_s.lead_cooldown, g_s.st.ping_ms);
      g_s.margin = margin;
      g_s.margin_known = true;
    }
    for (int i = 0; i < count; i++) {
      MpFrameInput fi;
      ReadFrame(r, &fi);
      if (r->err)
        break;
      uint32 f = first + i;
      g_s.ring[f & (kRingSize - 1)] = fi;
      g_s.ring_frame[f & (kRingSize - 1)] = f;
    }
  } else if (type == kPkt_Bye) {
    Session_Disconnect();
    SetMessage("The host ended the game");
  }
}

static void ClientSendInput(const uint16 *local_inputs) {
  Writer w;
  uint32 now = SDL_GetTicks();
  if (g_s.st.client_state == 1 || g_s.st.client_state == 4) {
    // Knock until the host answers (reconnecting: with the token it gave
    // us, so we get our seat back).
    if (now - g_s.last_hello > (g_s.st.client_state == 1 ? 250u : 500u)) {
      g_s.last_hello = now;
      WBegin(&w, kPkt_Hello);
      Wr16(&w, kProtoVersion);
      WData(&w, g_s.name, 24);
      Wr32(&w, g_s.token);
      Wr8(&w, g_s.st.client_state == 4);
      Net_Send(g_s.sock, &g_s.host, w.buf, w.pos);
    }
    return;
  }
  WBegin(&w, kPkt_Input);
  Wr8(&w, g_s.st.my_id);
  uint32 need = 0;
  if (g_s.st.client_state == 3) {
    need = g_s.conf + 1;
  } else if (g_s.snap_known && g_s.snap_have >= 4) {
    memcpy(&need, g_s.snap, 4);
    need++;
  }
  Wr32(&w, need);
  // The joypads of the newest frames, each stamped with the frame it is for.
  // (With an input delay we already know them a few frames ahead.)
  uint32 last = g_s.frame + g_s.input_delay;
  int count = 0;
  if (g_s.st.client_state == 3)
    count = last - g_s.snap_frame < kInputsPerPacket ? last - g_s.snap_frame : kInputsPerPacket;
  uint32 first = last - count + 1;
  Wr32(&w, first);
  Wr8(&w, count);
  for (int k = 0; k < count; k++)
    for (int i = 0; i < kSessionMaxLocal; i++)
      Wr16(&w, g_s.my_in[(first + k) % kRbSize][i]);
  // Only ask to join/leave until the host has done it.
  uint8 join = 0, leave = 0;
  for (int i = 0; i < kSessionMaxLocal; i++) {
    if ((g_s.pending_join_mask & (1 << i)) && g_s.st.local_slot[i] >= 0)
      g_s.pending_join_mask &= ~(1 << i);
    if ((g_s.pending_leave_mask & (1 << i)) && g_s.st.local_slot[i] < 0)
      g_s.pending_leave_mask &= ~(1 << i);
  }
  if (g_s.st.client_state == 3)
    join = g_s.pending_join_mask, leave = g_s.pending_leave_mask;
  Wr8(&w, join);
  Wr8(&w, leave);
  Wr8(&w, g_s.snap_id);
  Wr32(&w, g_s.snap_have);
  Wr8(&w, g_s.want_resync);
  Wr32(&w, now | 1);
  Wr16(&w, (uint16)IntMin(g_s.st.ping_ms, 65535));
  Net_Send(g_s.sock, &g_s.host, w.buf, w.pos);
}

void Session_Disconnect() {
  if (g_s.sock) {
    Writer w;
    WBegin(&w, kPkt_Bye);
    if (g_s.st.mode == kSessionClient) {
      for (int i = 0; i < 3; i++)
        Net_Send(g_s.sock, &g_s.host, w.buf, w.pos);
    } else if (g_s.st.mode == kSessionHost) {
      for (int c = 0; c < kSessionMaxClients; c++)
        if (g_s.clients[c].used)
          for (int i = 0; i < 3; i++)
            Net_Send(g_s.sock, &g_s.clients[c].addr, w.buf, w.pos);
    }
    Net_Close(g_s.sock);
    g_s.sock = NULL;
  }
  if (g_s.disc_sock) {
    Net_Close(g_s.disc_sock);
    g_s.disc_sock = NULL;
  }
  if (g_s.st.mode == kSessionHost)
    Upnp_UnmapPort();
  // Keep playing locally: remote players drop out.
  int me = g_s.st.mode == kSessionClient ? g_s.st.my_id : 0;
  // (A client reconnecting or downloading again still has the host's world.)
  bool was_client = g_s.st.mode == kSessionClient && g_s.in_host_world;
  bool was_playing = g_s.st.mode == kSessionHost || was_client;
  for (int c = 0; c < kSessionMaxClients; c++) {
    free(g_s.clients[c].snap);
    memset(&g_s.clients[c], 0, sizeof(g_s.clients[c]));
  }
  if (was_playing) {
    for (int p = 0; p < kMpMaxPlayers; p++) {
      if (Inst_IsJoined(p) && g_s.slot_owner[p] != me && Inst_NumJoined() > 1) {
        Inst_Leave(p);
        g_s.slot_owner[p] = 0xff;
        g_s.slot_local[p] = 0xff;
      }
    }
    for (int p = 0; p < kMpMaxPlayers; p++)
      if (g_s.slot_owner[p] == me)
        g_s.slot_owner[p] = 0;
  } else {
    ResetSlots();
  }
  if (g_s.st.mode != kSessionOffline)
    SetMessage("Disconnected");
  g_s.st.mode = kSessionOffline;
  g_s.st.client_state = 0;
  g_s.st.my_id = 0;
  g_s.st.ping_ms = 0;
  g_s.st.reconnect_seconds_left = 0;
  g_s.token = 0;
  g_s.roster_known = false;
  g_s.in_host_world = false;
  g_s.notice_seq = 0;
  g_s.notice_rd = g_s.notice_wr = 0;
  g_s.st.host_addr[0] = 0;
  g_s.st.public_addr[0] = 0;
  // The world we played in as a client is the host's, saves included: it
  // must never be written over our own files. (Our own come back with a
  // reset, see ZeldaReadSram.)
  // (Hosting a world we got from someone else's game: still not ours.)
  if (was_client)
    g_sram_foreign = true;
  else if (!g_sram_foreign)
    g_sram_write_disabled = false;
  memset(g_mp_draw_offset, 0, sizeof(g_mp_draw_offset));
  UpdateLocalSlots();
  free(g_s.snap);
  free(g_s.snap_chunk_have);
  g_s.snap = NULL;
  g_s.snap_chunk_have = NULL;
  g_s.snap_known = false;
}

// --- LAN discovery ---

void Session_RefreshLan() {
  if (!g_s.lan_sock)
    g_s.lan_sock = Net_Open(0, true);
  if (!g_s.lan_sock)
    return;
  Writer w;
  WBegin(&w, kPkt_Discover);
  NetAddr bc = { 0xffffffff, kSessionPort }, lo = { 0x7f000001, kSessionPort };
  Net_Send(g_s.lan_sock, &bc, w.buf, w.pos);
  Net_Send(g_s.lan_sock, &lo, w.buf, w.pos);
}

int Session_GetLanGames(SessionLanGame *out, int max) {
  uint32 now = SDL_GetTicks();
  int n = 0;
  for (int i = 0; i < g_s.nlan && n < max; i++)
    if (now - g_s.lan[i].last_seen < 5000)
      out[n++] = g_s.lan[i];
  return n;
}

// --- main tick ---

static void PollSocket(NetSocket *s) {
  uint8 buf[2048];
  NetAddr from;
  int n;
  while (s && (n = Net_Recv(s, &from, buf, sizeof(buf))) >= 0) {
    if (n < 3 || buf[0] != 'Z' || buf[1] != '3')
      continue;
    Reader r = { buf + 3, buf + n, false };
    if (s == g_s.disc_sock) {
      if (g_s.st.mode == kSessionHost && buf[2] == kPkt_Discover)
        HostHandlePacket(&from, &r, buf[2]);
    } else if (g_s.st.mode == kSessionHost && s == g_s.sock) {
      HostHandlePacket(&from, &r, buf[2]);
    } else {
      ClientHandlePacket(&from, &r, buf[2]);
    }
    if (!g_s.sock && s != g_s.lan_sock)
      break;  // disconnected while handling
  }
}

int Session_Tick(const uint16 *local_inputs, uint32 wanted_features) {
  TakeUpnpResult();
  PollSocket(g_s.sock);
  PollSocket(g_s.lan_sock);
  PollSocket(g_s.disc_sock);

  if (g_s.st.mode == kSessionHost)
    return HostTick(local_inputs, wanted_features);
  if (g_s.st.mode == kSessionOffline) {
    MpFrameInput fi;
    uint8 known;
    BuildFrame(&fi, local_inputs, wanted_features, &known);
    RunFrame(&fi, false);
    g_s.ring[g_s.frame & (kRingSize - 1)] = fi;
    g_s.ring_frame[g_s.frame & (kRingSize - 1)] = g_s.frame;
    g_s.conf = g_s.frame;
    return 1;
  }

  // Client
  uint32 now = SDL_GetTicks();
  if (g_s.st.client_state == 4) {
    // Reconnecting: the game waits; the host keeps our seat this long.
    uint32 gone = now - g_s.reconnect_since;
    g_s.st.reconnect_seconds_left = gone < kKeepSeatMs ? (kKeepSeatMs - gone + 999) / 1000 : 0;
    if (gone > kKeepSeatMs) {
      Session_Disconnect();
      SetMessage("Couldn't get back into the game");
      return 0;
    }
  } else if (now - g_s.last_recv > kTimeoutMs) {
    if (g_s.st.client_state == 1) {
      Session_Disconnect();
      SetMessage("No answer: check the room code or address");
      return 0;
    }
    // Lost the host: keep trying to get back in, with our seat kept.
    g_s.st.client_state = 4;
    g_s.reconnect_since = now;
    g_s.last_hello = 0;
    g_s.snap_known = false;
    g_s.rejoin_locals = 0;
    for (int i = 1; i < kSessionMaxLocal; i++)
      if (g_s.st.local_slot[i] >= 0)
        g_s.rejoin_locals |= 1 << i;
    SetMessage("Lost the connection: reconnecting...");
  }
  int run = g_s.st.client_state == 3 ? ClientTick(local_inputs) : 0;
  ClientSendInput(local_inputs);
  return run;
}

bool Session_LastFrameWasReplay() {
  return g_last_was_replay;
}

const uint8 *Session_ConfirmedSaveArea() {
  if (g_s.conf != g_s.frame && g_s.rb[g_s.conf % kRbSize])
    return Inst_SnapshotSaveArea(g_s.rb[g_s.conf % kRbSize]);
  return Inst_SaveArea();
}

void Session_ShareNotice(const char *text, int kind) {
  int k = g_s.notice_seq % 8;
  g_s.notice[k].seq = ++g_s.notice_seq;
  g_s.notice[k].kind = kind;
  snprintf(g_s.notice[k].text, sizeof(g_s.notice[k].text), "%s", text);
}

bool Session_PollSharedNotice(char *buf, size_t size, int *kind) {
  if (g_s.notice_rd == g_s.notice_wr)
    return false;
  int k = g_s.notice_rd++ % 8;
  snprintf(buf, size, "%s", g_s.notice[k].text);
  *kind = g_s.notice[k].kind;
  return true;
}

// --- players, kicking, names ---

void Session_SetName(const char *player_name) {
  snprintf(g_s.name, sizeof(g_s.name), "%s", player_name && player_name[0] ? player_name : "Link");
}

const char *Session_GetName(void) {
  return g_s.name;
}

static void SeatName(SessionPlayer *sp, const char *name) {
  if (sp->local == 0)
    snprintf(sp->name, sizeof(sp->name), "%s", name);
  else if (sp->owner == 0)
    snprintf(sp->name, sizeof(sp->name), "Player %d", sp->local + 1);
  else
    snprintf(sp->name, sizeof(sp->name), "%.11s (%c)", name, (char)('1' + (sp->local & 3)));
}

// The seats as this computer knows them (the host knows everything).
static void HostRoster(SessionPlayer *out) {
  uint32 now = SDL_GetTicks();
  memset(out, 0, sizeof(SessionPlayer) * kMpMaxPlayers);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    SessionPlayer *sp = &out[p];
    uint8 owner = g_s.slot_owner[p];
    if (!Inst_IsJoined(p) || owner >= 0xfe)
      continue;
    sp->state = kSeat_Playing;
    sp->owner = owner;
    sp->local = g_s.slot_local[p] < kSessionMaxLocal ? g_s.slot_local[p] : 0;
    HostClient *hc = owner ? FindClientById(owner) : NULL;
    if (!hc) {
      // (A client before the host's first list: the host, us, or someone else.)
      bool client = g_s.st.mode == kSessionClient;
      if (client && owner != 0 && owner != g_s.st.my_id)
        snprintf(sp->name, sizeof(sp->name), "Player %d", p + 1);
      else
        SeatName(sp, client && owner == 0 ? g_s.host_name : g_s.name);
      continue;
    }
    SeatName(sp, hc->name);
    sp->ping_ms = hc->ping_ms;
    if (hc->parked) {
      uint32 gone = now - hc->parked_at;
      sp->state = kSeat_Reconnecting;
      sp->seconds_left = gone < kKeepSeatMs ? (kKeepSeatMs - gone + 999) / 1000 : 0;
    }
  }
}

void Session_GetPlayers(SessionPlayer out[kMpMaxPlayers]) {
  if (g_s.st.mode == kSessionClient && g_s.roster_known) {
    memcpy(out, g_s.roster, sizeof(g_s.roster));
    // (Seats can change between two lists.)
    for (int p = 0; p < kMpMaxPlayers; p++)
      if (!Inst_IsJoined(p))
        memset(&out[p], 0, sizeof(out[p]));
    return;
  }
  HostRoster(out);
}

bool Session_Kick(int slot) {
  if (g_s.st.mode != kSessionHost || slot < 0 || slot >= kMpMaxPlayers)
    return false;
  uint8 owner = g_s.slot_owner[slot];
  HostClient *hc = owner && owner < 0xfe ? FindClientById(owner) : NULL;
  if (!hc)
    return false;
  Writer w;
  static const char kWhy[] = "The host removed you from the game";
  WBegin(&w, kPkt_Reject);
  WData(&w, kWhy, sizeof(kWhy));
  for (int i = 0; i < 3; i++)
    Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
  // It can't knock its way back in (a lost rejection, or reconnecting).
  int k = g_s.nkicked < (int)countof(g_s.kicked) ? g_s.nkicked++ : (int)countof(g_s.kicked) - 1;
  g_s.kicked[k].token = hc->token;
  g_s.kicked[k].ip = hc->addr.ip;
  memcpy(g_s.kicked[k].name, hc->name, sizeof(g_s.kicked[k].name));
  HostDropClient(hc, "was removed from the game");
  return true;
}

// --- room codes ---
// 32 bits of address, 16 of port and a 12 bit check, in 12 characters of
// Crockford's base 32 (no I, L, O or U, so nothing looks alike).

static const char kCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static uint32 CodeCheck(uint64 v) {
  uint32 h = 2166136261u;
  for (int i = 0; i < 6; i++)
    h = (h ^ (uint8)(v >> (i * 8))) * 16777619u;
  return (h ^ h >> 12 ^ h >> 24) & 0xfff;
}

void Session_EncodeRoomCode(uint32 ip, uint16 port, char *out, size_t size) {
  uint64 v = (uint64)ip << 16 | port;
  v = v << 12 | CodeCheck(v);
  char buf[16];
  int n = 0;
  for (int i = 11; i >= 0; i--) {
    buf[n++] = kCodeAlphabet[(v >> (i * 5)) & 31];
    if (i == 8 || i == 4)
      buf[n++] = '-';
  }
  buf[n] = 0;
  snprintf(out, size, "%s", buf);
}

bool Session_DecodeRoomCode(const char *code, uint32 *ip, uint16 *port) {
  uint64 v = 0;
  int n = 0;
  for (const char *s = code; *s; s++) {
    char c = *s;
    if (c == '-' || c == ' ')
      continue;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    const char *t = c ? strchr(kCodeAlphabet, c) : NULL;
    if (!t || ++n > 12)
      return false;
    v = v << 5 | (uint64)(t - kCodeAlphabet);
  }
  if (n != 12 || CodeCheck(v >> 12) != (v & 0xfff))
    return false;
  *ip = (uint32)(v >> 28);
  *port = (uint16)(v >> 12);
  return *ip != 0 && *port != 0;
}

static bool ParseIpPort(const char *s, uint32 *ip, uint16 *port) {
  unsigned a, b, c, d, p;
  char tail;
  if (sscanf(s, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail) != 5 || (a | b | c | d) > 255 || !p || p > 65535)
    return false;
  *ip = a << 24 | b << 16 | c << 8 | d;
  *port = (uint16)p;
  return true;
}

bool Session_GetRoomCode(char *out, size_t size, bool *internet) {
  uint32 ip;
  uint16 port;
  if (g_s.st.mode != kSessionHost)
    return false;
  *internet = ParseIpPort(g_s.st.public_addr, &ip, &port);
  if (!*internet && !ParseIpPort(g_s.st.host_addr, &ip, &port))
    return false;
  Session_EncodeRoomCode(ip, port, out, size);
  return true;
}

// --- keeping a copy of the host's game ---

bool Session_CanKeepCopy() {
  return g_s.st.mode == kSessionClient ? g_s.in_host_world : g_sram_foreign;
}

bool Session_KeepCopy(int file, const char *path, char *msg, size_t msg_size) {
  if (!Session_CanKeepCopy() || file < 1 || file > 3) {
    snprintf(msg, msg_size, "Nothing to keep a copy of");
    return false;
  }
  // Our own save file as it is on disk (the one in memory is the host's).
  uint8 sram[8192] = { 0 };
  FILE *f = fopen(path, "rb");
  if (f) {
    if (fread(sram, 1, sizeof(sram), f) != sizeof(sram))
      memset(sram, 0, sizeof(sram));
    fclose(f);
  }
  // The game's progress as it is right now (what saving would write), in
  // the world of our own Link.
  int me = g_s.st.local_slot[0];
  const uint8 *ram = me >= 0 ? Inst_PlayerWorldRam(me) : NULL;
  if (!ram)
    ram = g_ram;
  uint8 *slot = sram + (file - 1) * 0x500;
  memcpy(slot, ram + 0xF000, 0x500);
  if (WORD(slot[0x3e5]) != 0x55aa)
    WORD(slot[0x3e5]) = 0x55aa;  // (a file in use)
  uint16 t = 0x5a5a;
  for (int i = 0; i < 0x4fe; i += 2)
    t -= WORD(slot[i]);
  WORD(slot[0x4fe]) = t;
  memcpy(slot + 0xf00, slot, 0x500);  // the backup copy
  char bak[512];
  snprintf(bak, sizeof(bak), "%.500s.bak", path);
  remove(bak);
  rename(path, bak);
  f = fopen(path, "wb");
  bool ok = f && fwrite(sram, 1, sizeof(sram), f) == sizeof(sram);
  if (f)
    fclose(f);
  if (!ok)
    rename(bak, path);
  if (ok)
    snprintf(msg, msg_size, "Kept a copy of %s's game in file %d", g_s.host_name[0] ? g_s.host_name : "the host", file);
  else
    snprintf(msg, msg_size, "Couldn't write the copy to file %d", file);
  SetMessage(msg);
  return ok;
}
