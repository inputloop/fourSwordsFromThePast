#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSESOCKET closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define CLOSESOCKET close
#endif

// After the system headers: types.h defines a DWORD() macro that clashes with windows.h.
#include "net.h"

// Network conditions can be simulated for testing:
// ZELDA3_NETSIM=delay_ms,jitter_ms,loss_percent delays (and drops) received packets.
typedef struct SimPacket {
  uint32_t due;
  NetAddr from;
  int len;
  uint8_t data[1500];
} SimPacket;

enum { kSimMax = 512 };

struct NetSocket {
  SOCKET fd;
  SimPacket *sim;
  int nsim;
};

static int g_sim_delay = -1, g_sim_jitter, g_sim_loss;
static uint32_t g_sim_rand = 12345;

static uint32_t NowMs() {
#ifdef _WIN32
  return GetTickCount();
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

static uint32_t SimRand() {
  g_sim_rand = g_sim_rand * 1103515245 + 12345;
  return g_sim_rand >> 8;
}

static void SimInit() {
  if (g_sim_delay != -1)
    return;
  g_sim_delay = 0;
  const char *e = getenv("ZELDA3_NETSIM");
  if (e && sscanf(e, "%d,%d,%d", &g_sim_delay, &g_sim_jitter, &g_sim_loss) >= 1)
    fprintf(stderr, "Simulating network: %d ms delay, %d ms jitter, %d%% loss\n", g_sim_delay, g_sim_jitter, g_sim_loss);
  if (g_sim_delay < 0)
    g_sim_delay = 0;
  g_sim_rand ^= NowMs();
}

// Tests: the same as ZELDA3_NETSIM, from a script (before any socket is opened).
void Net_SetSim(int delay_ms, int jitter_ms, int loss_percent) {
  g_sim_delay = delay_ms < 0 ? 0 : delay_ms;
  g_sim_jitter = jitter_ms;
  g_sim_loss = loss_percent;
  g_sim_rand ^= NowMs();
}

bool Net_Init() {
#ifdef _WIN32
  static bool inited;
  if (!inited) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return false;
    inited = true;
  }
#endif
  return true;
}

static void SetNonBlocking(SOCKET fd) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

NetSocket *Net_Open(uint16_t port, bool broadcast) {
  if (!Net_Init())
    return NULL;
  SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == INVALID_SOCKET)
    return NULL;
  int one = 1;
  if (broadcast)
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
  struct sockaddr_in sa = { 0 };
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  sa.sin_port = htons(port);
  if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    CLOSESOCKET(fd);
    return NULL;
  }
  SetNonBlocking(fd);
  NetSocket *s = (NetSocket *)calloc(1, sizeof(NetSocket));
  s->fd = fd;
  SimInit();
  if (g_sim_delay || g_sim_jitter || g_sim_loss)
    s->sim = (SimPacket *)malloc(sizeof(SimPacket) * kSimMax);
  return s;
}

void Net_Close(NetSocket *s) {
  if (s) {
    CLOSESOCKET(s->fd);
    free(s->sim);
    free(s);
  }
}

uint16_t Net_LocalPort(NetSocket *s) {
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  if (getsockname(s->fd, (struct sockaddr *)&sa, &len) != 0)
    return 0;
  return ntohs(sa.sin_port);
}

// Tests: a cable pulled out. Nothing goes out and everything coming in is lost.
static bool g_blackout;

void Net_SetBlackout(bool on) {
  g_blackout = on;
}

bool Net_Send(NetSocket *s, const NetAddr *to, const void *data, int len) {
  if (g_blackout)
    return true;
  struct sockaddr_in sa = { 0 };
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(to->ip);
  sa.sin_port = htons(to->port);
  return sendto(s->fd, (const char *)data, len, 0, (struct sockaddr *)&sa, sizeof(sa)) == len;
}

static int RealRecv(NetSocket *s, NetAddr *from, void *data, int maxlen);

int Net_Recv(NetSocket *s, NetAddr *from, void *data, int maxlen) {
  if (g_blackout) {
    while (RealRecv(s, NULL, data, maxlen) >= 0) {}
    return -1;
  }
  if (!s->sim)
    return RealRecv(s, from, data, maxlen);
  uint32_t now = NowMs();
  SimPacket *p;
  while (s->nsim < kSimMax && (p = &s->sim[s->nsim], (p->len = RealRecv(s, &p->from, p->data, sizeof(p->data))) >= 0)) {
    if ((int)(SimRand() % 100) < g_sim_loss)
      continue;
    p->due = now + g_sim_delay + (g_sim_jitter ? SimRand() % (g_sim_jitter + 1) : 0);
    s->nsim++;
  }
  // Packets arrive in the order of their due times, so jitter reorders them.
  int best = -1;
  for (int i = 0; i < s->nsim; i++)
    if ((int32_t)(now - s->sim[i].due) >= 0 && (best < 0 || (int32_t)(s->sim[i].due - s->sim[best].due) < 0))
      best = i;
  if (best < 0)
    return -1;
  p = &s->sim[best];
  int n = p->len < maxlen ? p->len : maxlen;
  memcpy(data, p->data, n);
  if (from)
    *from = p->from;
  s->sim[best] = s->sim[--s->nsim];
  return n;
}

static int RealRecv(NetSocket *s, NetAddr *from, void *data, int maxlen) {
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  int n = recvfrom(s->fd, (char *)data, maxlen, 0, (struct sockaddr *)&sa, &len);
  if (n < 0)
    return -1;
  if (from) {
    from->ip = ntohl(sa.sin_addr.s_addr);
    from->port = ntohs(sa.sin_port);
  }
  return n;
}

bool Net_ParseAddr(const char *str, uint16_t default_port, NetAddr *out) {
  if (!Net_Init())
    return false;
  char host[256];
  snprintf(host, sizeof(host), "%s", str);
  uint16_t port = default_port;
  char *colon = strrchr(host, ':');
  if (colon) {
    *colon = 0;
    int p = atoi(colon + 1);
    port = p > 0 && p < 65536 ? (uint16_t)p : default_port;  // ("host:" keeps the usual port)
  }
  if (host[0] == 0)
    return false;
  struct addrinfo hints = { 0 }, *res = NULL;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res)
    return false;
  out->ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
  out->port = port;
  freeaddrinfo(res);
  return true;
}

void Net_AddrToString(const NetAddr *a, char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "%u.%u.%u.%u:%u", a->ip >> 24, (a->ip >> 16) & 255, (a->ip >> 8) & 255, a->ip & 255, a->port);
}

bool Net_AddrEqual(const NetAddr *a, const NetAddr *b) {
  return a->ip == b->ip && a->port == b->port;
}

uint32_t Net_GetLocalIp() {
  // Connecting a UDP socket sends nothing but makes the OS pick the
  // outgoing interface, whose address we can then read back.
  if (!Net_Init())
    return 0;
  SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == INVALID_SOCKET)
    return 0;
  struct sockaddr_in sa = { 0 };
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(0x08080808);
  sa.sin_port = htons(53);
  uint32_t ip = 0;
  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
    socklen_t len = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &len) == 0)
      ip = ntohl(sa.sin_addr.s_addr);
  }
  CLOSESOCKET(fd);
  return ip;
}

int Net_TcpRequest(const NetAddr *to, const char *request, int reqlen, char *response, int maxlen, int timeout_ms) {
  if (!Net_Init())
    return -1;
  SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == INVALID_SOCKET)
    return -1;
#ifdef _WIN32
  DWORD tv = timeout_ms;
#else
  struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
#endif
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
  struct sockaddr_in sa = { 0 };
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(to->ip);
  sa.sin_port = htons(to->port);
  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    CLOSESOCKET(fd);
    return -1;
  }
  int sent = 0;
  while (sent < reqlen) {
    int n = send(fd, request + sent, reqlen - sent, 0);
    if (n <= 0) {
      CLOSESOCKET(fd);
      return -1;
    }
    sent += n;
  }
  int total = 0;
  while (total < maxlen - 1) {
    int n = recv(fd, response + total, maxlen - 1 - total, 0);
    if (n <= 0)
      break;
    total += n;
  }
  response[total] = 0;
  CLOSESOCKET(fd);
  return total;
}
