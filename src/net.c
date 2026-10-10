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

struct NetSocket {
  SOCKET fd;
};

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
  return s;
}

void Net_Close(NetSocket *s) {
  if (s) {
    CLOSESOCKET(s->fd);
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

bool Net_Send(NetSocket *s, const NetAddr *to, const void *data, int len) {
  struct sockaddr_in sa = { 0 };
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(to->ip);
  sa.sin_port = htons(to->port);
  return sendto(s->fd, (const char *)data, len, 0, (struct sockaddr *)&sa, sizeof(sa)) == len;
}

int Net_Recv(NetSocket *s, NetAddr *from, void *data, int maxlen) {
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
    port = (uint16_t)atoi(colon + 1);
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
