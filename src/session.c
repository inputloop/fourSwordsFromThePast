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

enum {
  kProtoVersion = 3,
  kRingSize = 1024,
  kChunkSize = 1024,
  kChunksPerTick = 24,
  kMaxFramesPerPacket = 32,
  kHashInterval = 60,
  kTimeoutMs = 6000,
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
  uint32 last_recv;
  uint16 inputs[kSessionMaxLocal];
  uint32 need_frame;
  uint8 join_mask, leave_mask;
  uint32 ping_echo;
  // snapshot transfer
  bool loading;
  uint8 snap_id;
  uint8 *snap;
  uint32 snap_size;
  uint32 snap_have;
} HostClient;

static struct {
  SessionStatus st;
  NetSocket *sock;
  NetSocket *lan_sock;
  char name[24];
  uint32 frame;  // number of frames simulated
  MpFrameInput ring[kRingSize];
  uint32 ring_frame[kRingSize];
  bool ff;
  bool together;
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
  SessionLanGame lan[8];
  int nlan;
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

static void RunFrame(const MpFrameInput *fi, bool deterministic_audio) {
  ApplyEvents(fi);
  Inst_SetFriendlyFire(fi->flags & 1);
  Inst_SetIndependent(!(fi->flags & 4));
  g_wanted_zelda_features = fi->features;
  g_apui00_override = deterministic_audio ? (fi->flags >> 1) & 1 : -1;
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
      if (slot < 0) {
        int s = FindFreeSlot(reserved);
        if (s >= 0 && fi->nevents < kSessionMaxEvents) {
          reserved[s] = 1;
          AddEvent(fi, kSessEv_Join, s, owner, i);
        }
      }
      *join_mask &= ~(1 << i);
    }
    if ((*leave_mask & (1 << i))) {
      if (slot >= 0 && fi->nevents < kSessionMaxEvents)
        AddEvent(fi, kSessEv_Leave, slot, owner, i);
      *leave_mask &= ~(1 << i);
    }
  }
}

// Builds the next frame on the host (or offline).
static void BuildFrame(MpFrameInput *fi, const uint16 *local_inputs, uint32 features) {
  memset(fi, 0, sizeof(*fi));
  fi->features = features;
  fi->flags = (g_s.ff ? 1 : 0) | (ZeldaIsMusicPlaying() ? 2 : 0) | (g_s.together ? 4 : 0);
  uint8 reserved[kMpMaxPlayers] = { 0 };
  QueueJoinLeave(fi, 0, &g_s.pending_join_mask, &g_s.pending_leave_mask, reserved);
  for (int c = 0; c < kSessionMaxClients; c++) {
    HostClient *hc = &g_s.clients[c];
    if (hc->used)
      QueueJoinLeave(fi, hc->id, &hc->join_mask, &hc->leave_mask, reserved);
  }
  for (int p = 0; p < kMpMaxPlayers; p++) {
    uint8 owner = g_s.slot_owner[p], local = g_s.slot_local[p];
    if (owner == 0xff || local >= kSessionMaxLocal)
      continue;
    if (owner == 0) {
      fi->inputs[p] = local_inputs[local];
    } else {
      for (int c = 0; c < kSessionMaxClients; c++)
        if (g_s.clients[c].used && g_s.clients[c].id == owner)
          fi->inputs[p] = g_s.clients[c].inputs[local];
    }
  }
}

// --- frame encoding ---

static void WriteFrame(Writer *w, const MpFrameInput *fi) {
  for (int i = 0; i < kMpMaxPlayers; i++)
    Wr16(w, fi->inputs[i]);
  Wr8(w, fi->flags);
  Wr8(w, fi->nevents);
  Wr32(w, fi->features);
  if (fi->flags & 0x80)
    Wr32(w, fi->hash);
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

bool Session_IsNetplay() {
  return g_s.st.mode != kSessionOffline;
}

bool Session_AllowsLocalStateChanges() {
  return g_s.st.mode != kSessionClient;
}

void Session_SetFriendlyFire(bool on) {
  g_s.ff = on;
}

bool Session_GetFriendlyFire() {
  return g_s.st.mode == kSessionClient ? Inst_GetFriendlyFire() : g_s.ff;
}

void Session_SetRoamTogether(bool on) {
  g_s.together = on;
}

bool Session_GetRoamTogether() {
  return g_s.st.mode == kSessionClient ? !Inst_GetIndependent() : g_s.together;
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

static void UpnpDone(bool ok, const char *external_ip, uint16 port) {
  if (ok)
    snprintf(g_s.st.public_addr, sizeof(g_s.st.public_addr), "%s:%u", external_ip, port);
  else
    snprintf(g_s.st.public_addr, sizeof(g_s.st.public_addr), "UPnP unavailable - forward UDP %u", port);
}

bool Session_Host(uint16 port, bool use_upnp) {
  if (g_s.st.mode != kSessionOffline)
    Session_Disconnect();
  g_s.sock = Net_Open(port ? port : kSessionPort, true);
  if (!g_s.sock) {
    SetMessage("Unable to open the network port");
    return false;
  }
  g_s.st.mode = kSessionHost;
  g_s.st.my_id = 0;
  uint32 ip = Net_GetLocalIp();
  NetAddr a = { ip, Net_LocalPort(g_s.sock) };
  Net_AddrToString(&a, g_s.st.host_addr, sizeof(g_s.st.host_addr));
  g_s.st.public_addr[0] = 0;
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
  Inst_SaveState(&raw);
  // header: frame, slot owners/locals, then the compressed state
  uint8 hdr[4 + 8];
  memcpy(hdr, &g_s.frame, 4);
  memcpy(hdr + 4, g_s.slot_owner, 4);
  memcpy(hdr + 8, g_s.slot_local, 4);
  ByteArray_AppendData(&comp, hdr, sizeof(hdr));
  RleCompress(raw.data, raw.size, &comp);
  ByteArray_Destroy(&raw);
  free(hc->snap);
  hc->snap = comp.data;
  hc->snap_size = (uint32)comp.size;
  hc->snap_have = 0;
  hc->snap_id = ++g_s.next_snap_id;
  hc->loading = true;
  hc->need_frame = g_s.frame + 1;
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
    if (r->err)
      return;
    if (ver != kProtoVersion) {
      WBegin(&w, kPkt_Reject);
      WData(&w, "Different game version", 23);
      Net_Send(g_s.sock, from, w.buf, w.pos);
      return;
    }
    if (!hc) {
      for (int c = 0; c < kSessionMaxClients && !hc; c++) {
        if (!g_s.clients[c].used) {
          hc = &g_s.clients[c];
          memset(hc, 0, sizeof(*hc));
          hc->used = true;
          hc->addr = *from;
          hc->id = c + 1;
          memcpy(hc->name, name, 24);
          hc->join_mask = 1;
          HostStartSnapshot(hc);
          char msg[96];
          snprintf(msg, sizeof(msg), "%s joined", name);
          SetMessage(msg);
        }
      }
      if (!hc || Inst_NumJoined() >= kMpMaxPlayers) {
        if (hc)
          hc->used = false;
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
    Net_Send(g_s.sock, from, w.buf, w.pos);
  } else if (type == kPkt_Input && hc) {
    hc->last_recv = SDL_GetTicks();
    Rd8(r);  // id
    uint32 need = Rd32(r);
    uint16 inputs[kSessionMaxLocal];
    for (int i = 0; i < kSessionMaxLocal; i++)
      inputs[i] = Rd16(r);
    uint8 join = Rd8(r), leave = Rd8(r);
    uint8 snap_id = Rd8(r);
    uint32 snap_have = Rd32(r);
    uint8 resync = Rd8(r);
    uint32 ping = Rd32(r);
    if (r->err)
      return;
    memcpy(hc->inputs, inputs, sizeof(inputs));
    hc->join_mask |= join;
    hc->leave_mask |= leave;
    hc->ping_echo = ping;
    if (hc->loading && snap_id == hc->snap_id) {
      if (snap_have > hc->snap_have)
        hc->snap_have = snap_have;
      if (hc->snap_have >= hc->snap_size) {
        hc->loading = false;
        free(hc->snap);
        hc->snap = NULL;
      }
    }
    if (!hc->loading) {
      hc->need_frame = need;
      // Too far behind to catch up from the frame history.
      if (resync || g_s.frame - need + 1 >= kRingSize - 64)
        HostStartSnapshot(hc);
    }
  } else if (type == kPkt_Bye && hc) {
    HostDropClient(hc, "left");
  }
}

static void HostSend() {
  Writer w;
  uint32 now = SDL_GetTicks();
  for (int c = 0; c < kSessionMaxClients; c++) {
    HostClient *hc = &g_s.clients[c];
    if (!hc->used)
      continue;
    if (now - hc->last_recv > kTimeoutMs) {
      HostDropClient(hc, "timed out");
      continue;
    }
    if (hc->loading) {
      WBegin(&w, kPkt_SnapInfo);
      Wr8(&w, hc->snap_id);
      Wr32(&w, hc->snap_size);
      Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      for (uint32 i = 0, off = hc->snap_have; i < kChunksPerTick && off < hc->snap_size; i++, off += kChunkSize) {
        uint32 n = hc->snap_size - off < kChunkSize ? hc->snap_size - off : kChunkSize;
        WBegin(&w, kPkt_SnapData);
        Wr8(&w, hc->snap_id);
        Wr32(&w, off);
        Wr16(&w, n);
        WData(&w, hc->snap + off, n);
        Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
      }
    }
    // Frames after the snapshot are sent even while loading so they are ready.
    uint32 first = hc->need_frame;
    if (first == 0 || first > g_s.frame)
      continue;
    if (g_s.frame - first >= kRingSize - 1)
      first = g_s.frame - (kRingSize - 2);
    int count = g_s.frame - first + 1;
    if (count > kMaxFramesPerPacket)
      count = kMaxFramesPerPacket;
    WBegin(&w, kPkt_Frames);
    Wr32(&w, first);
    Wr8(&w, count);
    Wr32(&w, hc->ping_echo);
    for (int i = 0; i < count; i++)
      WriteFrame(&w, &g_s.ring[(first + i) & (kRingSize - 1)]);
    Net_Send(g_s.sock, &hc->addr, w.buf, w.pos);
  }
}

void Session_ResyncAll() {
  if (g_s.st.mode != kSessionHost)
    return;
  for (int c = 0; c < kSessionMaxClients; c++)
    if (g_s.clients[c].used)
      HostStartSnapshot(&g_s.clients[c]);
}

// --- client ---

bool Session_Join(const char *address) {
  if (g_s.st.mode != kSessionOffline)
    Session_Disconnect();
  NetAddr a;
  if (!Net_ParseAddr(address, kSessionPort, &a)) {
    SetMessage("Unknown address");
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
  g_s.frame = frame;
  UpdateLocalSlots();
  g_s.st.client_state = 3;
  g_s.want_resync = false;
  SetMessage("Connected");
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
    if (g_s.st.client_state == 1) {
      g_s.st.my_id = id;
      g_s.st.client_state = 2;
      SetMessage("Downloading the host's game...");
    }
  } else if (type == kPkt_Reject) {
    char why[64] = { 0 };
    RData(r, why, (int)(r->end - r->p) < 63 ? (int)(r->end - r->p) : 63);
    Session_Disconnect();
    SetMessage(why);
  } else if (type == kPkt_SnapInfo && g_s.st.client_state >= 2) {
    uint8 id = Rd8(r);
    uint32 size = Rd32(r);
    if (r->err || size > 4 << 20)
      return;
    if (!g_s.snap_known || id != g_s.snap_id) {
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
    if (echo)
      g_s.st.ping_ms = SDL_GetTicks() - echo;
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
  if (g_s.st.client_state == 1) {
    if (now - g_s.last_hello > 250) {
      g_s.last_hello = now;
      WBegin(&w, kPkt_Hello);
      Wr16(&w, kProtoVersion);
      WData(&w, g_s.name, 24);
      Net_Send(g_s.sock, &g_s.host, w.buf, w.pos);
    }
    return;
  }
  WBegin(&w, kPkt_Input);
  Wr8(&w, g_s.st.my_id);
  uint32 need = 0;
  if (g_s.st.client_state == 3) {
    need = g_s.frame + 1;
  } else if (g_s.snap_known && g_s.snap_have >= 4) {
    memcpy(&need, g_s.snap, 4);
    need++;
  }
  Wr32(&w, need);
  for (int i = 0; i < kSessionMaxLocal; i++)
    Wr16(&w, g_s.st.local_slot[i] >= 0 ? local_inputs[i] : 0);
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
  if (g_s.st.mode == kSessionHost)
    Upnp_UnmapPort();
  // Keep playing locally: remote players drop out.
  int me = g_s.st.mode == kSessionClient ? g_s.st.my_id : 0;
  bool was_playing = g_s.st.mode == kSessionHost || g_s.st.client_state == 3;
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
  g_s.st.host_addr[0] = 0;
  g_s.st.public_addr[0] = 0;
  g_sram_write_disabled = false;
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
    if (g_s.st.mode == kSessionHost && s == g_s.sock)
      HostHandlePacket(&from, &r, buf[2]);
    else
      ClientHandlePacket(&from, &r, buf[2]);
    if (!g_s.sock && s != g_s.lan_sock)
      break;  // disconnected while handling
  }
}

int Session_Tick(const uint16 *local_inputs, uint32 wanted_features) {
  PollSocket(g_s.sock);
  PollSocket(g_s.lan_sock);

  if (g_s.st.mode != kSessionClient) {
    MpFrameInput fi;
    BuildFrame(&fi, local_inputs, wanted_features);
    // Players of dropped clients leave.
    for (int p = 0; p < kMpMaxPlayers; p++)
      if (g_s.slot_owner[p] == 0xfe && fi.nevents < kSessionMaxEvents)
        AddEvent(&fi, kSessEv_Leave, p, 0xff, 0xff);
    bool host = g_s.st.mode == kSessionHost;
    RunFrame(&fi, host);
    if (host && g_s.frame % kHashInterval == 0) {
      fi.flags |= 0x80;
      fi.hash = Inst_HashState();
    }
    g_s.ring[g_s.frame & (kRingSize - 1)] = fi;
    g_s.ring_frame[g_s.frame & (kRingSize - 1)] = g_s.frame;
    if (host)
      HostSend();
    return 1;
  }

  // Client
  if (SDL_GetTicks() - g_s.last_recv > kTimeoutMs) {
    Session_Disconnect();
    SetMessage("Lost connection to the host");
    return 0;
  }
  ClientSendInput(local_inputs);
  if (g_s.st.client_state != 3)
    return 0;
  int avail = 0;
  while (avail < kRingSize - 1 && g_s.ring_frame[(g_s.frame + 1 + avail) & (kRingSize - 1)] == g_s.frame + 1 + avail)
    avail++;
  g_s.st.frames_buffered = avail;
  int run = avail == 0 ? 0 : avail > 30 ? 16 : avail > 4 ? 2 : 1;
  for (int i = 0; i < run && g_s.st.client_state == 3; i++) {
    MpFrameInput fi = g_s.ring[(g_s.frame + 1) & (kRingSize - 1)];
    RunFrame(&fi, true);
    if ((fi.flags & 0x80) && Inst_HashState() != fi.hash) {
      char msg[96];
      snprintf(msg, sizeof(msg), "Out of sync at frame %u, resyncing", g_s.frame);
      SetMessage(msg);
      g_s.want_resync = true;
      g_s.st.client_state = 2;
      g_s.st.resyncs++;
    }
  }
  return run;
}

bool Session_LastFrameWasReplay() {
  return g_last_was_replay;
}
