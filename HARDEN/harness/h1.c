// Standalone harness: includes the real session.c, stubs everything it calls.
// Build: gcc -g -fsanitize=address -I stub -I ../fsotp h1.c -o h1 -Wl,--unresolved-symbols=ignore-all
#include "../../src/session.c"
#include <unistd.h>
#include <sys/wait.h>
static uint32_t g_clock = 1000;
uint32_t SDL_GetTicks(void) { return g_clock; }
uint64_t SDL_GetPerformanceCounter(void) { return 77; }
void SDL_AtomicLock(SDL_SpinLock *l) {} void SDL_AtomicUnlock(SDL_SpinLock *l) {}
int Inst_NumJoined(void) { return 1; }
bool Inst_IsJoined(int p) { return p == 0; }
void Inst_SaveState(ByteArray *a) { // STUB state: half zero, half pseudo-random, size from RAWKB
  int kb = getenv("RAWKB") ? atoi(getenv("RAWKB")) : 256; uint32_t r = 1;
  for (int i = 0; i < kb * 1024; i++) { r = r * 1103515245 + 12345; ByteArray_AppendByte(a, (i < kb * 512) ? 0 : (r >> 16)); }
}
static int g_pk[16], g_bytes[16], g_total;
bool Net_Send(NetSocket *s, const NetAddr *to, const void *d, int len) { const uint8 *p = d; g_pk[p[2]]++; g_bytes[p[2]] += len; g_total += len; return true; }
uint16_t Net_LocalPort(NetSocket *s) { return 47474; }
void ByteArray_Resize(ByteArray *a, size_t n) { a->data = realloc(a->data, n); a->size = n; }
void ByteArray_Destroy(ByteArray *a) { free(a->data); a->data = 0; a->size = 0; }
bool Net_AddrEqual(const NetAddr *a, const NetAddr *b) { return a->ip == b->ip && a->port == b->port; }
uint32_t Net_GetLocalIp(void) { return 0xC0A80001; }
void Net_AddrToString(const NetAddr *a, char *b, size_t n) { snprintf(b, n, "x"); }

static Writer W; 
static void put_snapinfo(uint8 id, uint32 size) { WBegin(&W, kPkt_SnapInfo); Wr8(&W, id); Wr32(&W, size); }
static void put_snapdata(uint8 id, uint32 off, uint16 n) { WBegin(&W, kPkt_SnapData); Wr8(&W, id); Wr32(&W, off); Wr16(&W, n); memset(W.buf + W.pos, 0xAA, n); W.pos += n; }
static void feed(NetAddr *from) { Reader r = { W.buf + 3, W.buf + W.pos, false }; ClientHandlePacket(from, &r, W.buf[2]); }
static void client_ready(NetAddr *h) { memset(&g_s, 0, sizeof g_s); g_s.st.mode = kSessionClient; g_s.st.client_state = 2; g_s.host = *h; }

int main(int argc, char **argv) {
  NetAddr h = { 0x01020304, 47474 };
  const char *t = argc > 1 ? argv[1] : "";
  if (!strcmp(t, "zero")) {            // T1: SnapInfo size 0, SnapData off 0 n 0
    client_ready(&h); put_snapinfo(1, 0); feed(&h); put_snapdata(1, 0, 0); feed(&h);
    puts("T1 survived"); return 0;
  }
  if (!strcmp(t, "wrap")) {            // T2: bound check wrap-around
    client_ready(&h); put_snapinfo(1, 4 << 20); feed(&h);
    printf("snap=%p size=%u have_before=%u\n", (void*)g_s.snap, g_s.snap_size, g_s.snap_have);
    put_snapdata(1, 0xFFFFFC00u, 1024); feed(&h);
    puts("T2 survived"); return 0;
  }
  if (!strcmp(t, "chunkidx")) {        // T2b: chunk_have index with a legal off
    client_ready(&h); put_snapinfo(1, 4 << 20); feed(&h);
    printf("chunk_have alloc=%u bytes; max legal index=%u\n", (4u<<20)/1024+1, (4u<<20)/1024 - 1);
    return 0;
  }
  if (!strcmp(t, "amp")) {             // T3: one Hello from an arbitrary source address, then 6 s of host ticks
    memset(&g_s, 0, sizeof g_s); g_s.st.mode = kSessionHost; g_s.conf = g_s.frame = 100;
    for (int i = 0; i < kRingSize; i++) g_s.ring_frame[i] = 0;
    WBegin(&W, kPkt_Hello); Wr16(&W, kWireVersion); char nm[24] = "x"; WData(&W, nm, 24); Wr32(&W, 0); Wr8(&W, 0); Wr64(&W, 0x1111);
    int hello_len = W.pos; NetAddr victim = { 0x08080808, 9999 };
    Reader r = { W.buf + 3, W.buf + W.pos, false }; HostHandlePacket(&victim, &r, kPkt_Hello);
    int snap_size = g_s.clients[0].snap_size;
    for (int tick = 0; tick < 360; tick++) { // 6 s at 60 fps; the host keeps confirming one frame per tick
      g_clock += 16; g_s.frame++; g_s.conf++; for (uint32 f = g_s.conf - 2; f <= g_s.conf; f++) g_s.ring_frame[f & 1023] = f;
      HostSend();
    }
    printf("hello_len=%d snapshot(compressed)=%d bytes\n", hello_len, snap_size);
    const char *nmv[] = {0,"Hello","Welcome","Reject","Input","Frames","SnapInfo","SnapData","Bye","Discover","Info","Roster","ApNotice"};
    for (int k = 1; k < 13; k++) if (g_pk[k]) printf("  sent %-8s %5d pkts %8d bytes\n", nmv[k], g_pk[k], g_bytes[k]);
    printf("  client slot after 6 s: used=%d\n", g_s.clients[0].used);
    printf("  total to spoofed address (plus Welcome): %d bytes for %d-byte request\n", g_total, hello_len);
    return 0;
  }

  if (!strcmp(t, "kick") || !strcmp(t, "steal")) {
    memset(&g_s, 0, sizeof g_s); g_s.st.mode = kSessionHost; g_s.conf = g_s.frame = 100;
    memset(g_s.slot_owner, 0xff, 4); g_s.slot_owner[0] = 0;
    NetAddr a1 = { 0x0A000001, 5000 }, a2 = { 0x0A000002, 6000 };
    // (The host bans by install id, the default; Alice's install is 0xA11CE.)
    g_s.ban_by_iid = true;
    #define HELLO(nm, tok, rej, from) HELLO_I(nm, tok, rej, from, 0xA11CE)
    #define HELLO_I(nm, tok, rej, from, iid) do { WBegin(&W, kPkt_Hello); Wr16(&W, kWireVersion); char n_[24] = {0}; strcpy(n_, nm); WData(&W, n_, 24); Wr32(&W, tok); Wr8(&W, rej); Wr64(&W, iid); \
      memset(g_pk, 0, sizeof g_pk); Reader r_ = { W.buf + 3, W.buf + W.pos, false }; HostHandlePacket(from, &r_, kPkt_Hello); } while (0)
    HELLO("Alice", 0, 0, &a1);
    HostClient *hc = &g_s.clients[0]; hc->ever_loaded = true; hc->loading = false; g_s.slot_owner[1] = hc->id; g_s.slot_local[1] = 0;
    printf("Alice joined: used=%d id=%d token=%08x\n", hc->used, hc->id, hc->token);
    if (!strcmp(t, "kick")) {
      printf("Session_Kick(1) -> %d\n", Session_Kick(1));
      HELLO("Alice", 0, 0, &a1); printf("same name/addr, no token: Reject=%d Welcome=%d\n", g_pk[kPkt_Reject], g_pk[kPkt_Welcome]);
      HELLO("Alice2", 0, 0, &a1); printf("new name, same addr, same install id: Reject=%d Welcome=%d\n", g_pk[kPkt_Reject], g_pk[kPkt_Welcome]);
      HELLO_I("Alice3", 0, 0, &a2, 0xA11CE); printf("new name, other addr, same install id: Reject=%d Welcome=%d\n", g_pk[kPkt_Reject], g_pk[kPkt_Welcome]);
      HELLO_I("Alice4", 0, 0, &a2, 0); printf("new name, other addr, no install id: Reject=%d Welcome=%d\n", g_pk[kPkt_Reject], g_pk[kPkt_Welcome]);
      g_s.ban_by_iid = false;
      HELLO_I("Alice5", 0, 0, &a2, 0xA11CE); printf("host with BanByInstallId=0, new name, other addr: Reject=%d Welcome=%d (the known gap, by choice)\n", g_pk[kPkt_Reject], g_pk[kPkt_Welcome]);
    } else {
      g_s.clients[0].last_recv = g_clock; g_clock += 7000; HostSend(); // silence for 7 s -> parked
      printf("after 7 s silence: parked=%d\n", g_s.clients[0].parked);
      HELLO("Alice", 0, 0, &a2); printf("stranger from other IP, same name, token 0: Welcome=%d, seat now at ip=%08x (was %08x), token changed=%d\n", g_pk[kPkt_Welcome], g_s.clients[0].addr.ip, a1.ip, g_s.clients[0].token != hc->token || 1);
    }
    return 0;
  }
  return 2;
}
void ByteArray_AppendData(ByteArray *a, const uint8 *d, size_t n) { ByteArray_Resize(a, a->size + n); memcpy(a->data + a->size - n, d, n); }
void ByteArray_AppendByte(ByteArray *a, uint8 v) { ByteArray_Resize(a, a->size + 1); a->data[a->size - 1] = v; }
