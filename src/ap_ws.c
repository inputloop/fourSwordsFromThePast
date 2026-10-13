// WebSocket client (RFC 6455) for the Archipelago connection.
//
// Archipelago servers offer permessage-deflate (RFC 7692). We ask for
// server_no_context_takeover, so every compressed message stands alone and
// can be inflated in one go with the zlib decoder of stb_image, which the
// game has anyway (src/glsl_shader.c). We send uncompressed frames, which
// the extension allows.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ApSocket;
#define AP_BAD_SOCKET INVALID_SOCKET
#define CloseSocket closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
typedef int ApSocket;
#define AP_BAD_SOCKET (-1)
#define CloseSocket close
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include "ap_ws.h"
#include "ap_tls.h"
#include "third_party/stb/stb_image.h"  // only the declarations: the code is in glsl_shader.c

#if defined(MSG_NOSIGNAL)
#define AP_SEND_FLAGS MSG_NOSIGNAL
#else
#define AP_SEND_FLAGS 0
#endif

enum {
  kSliceMs = 50,
  kSendTimeoutMs = 15000,
  kMaxHeader = 16384,
  kRecvChunk = 65536,
  kMiB = 1 << 20,
};

struct ApWs {
  ApSocket fd;
  ApTls *tls;
  SDL_atomic_t *cancel;
  bool deflate, broken;
  uint32_t rng, last_rx;
  uint8_t *in;                 // bytes from the server, not parsed yet: [in_pos, in_len)
  size_t in_pos, in_len, in_cap;
  uint8_t *msg;                // the message being put together from its frames
  size_t msg_len, msg_cap;
  bool in_msg, msg_compressed;
  char error[256];
};

static void SetError(ApWs *ws, const char *fmt, ...) {
  ws->broken = true;
  if (ws->error[0])
    return;  // the first reason is the real one
  va_list va;
  va_start(va, fmt);
  vsnprintf(ws->error, sizeof(ws->error), fmt, va);
  va_end(va);
}

static void SetErr(char *err, size_t err_size, const char *fmt, ...) {
  if (!err || !err_size)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(err, err_size, fmt, va);
  va_end(va);
}

static bool TimeUp(uint32_t deadline) {
  return (int32_t)(deadline - SDL_GetTicks()) <= 0;
}

static bool Cancelled(SDL_atomic_t *cancel) {
  return cancel && SDL_AtomicGet(cancel);
}

// 1: ready, 0: not yet, -1: cancelled, out of time or broken.
static int WaitSlice(ApSocket fd, bool for_write, uint32_t deadline, SDL_atomic_t *cancel) {
  if (Cancelled(cancel) || TimeUp(deadline))
    return -1;
  int left = (int)(deadline - SDL_GetTicks());
  return ApTls_WaitSocket((intptr_t)fd, for_write, left < kSliceMs ? left : kSliceMs);
}

static bool WouldBlock(void) {
#ifdef _WIN32
  int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static int LastSocketError(void) {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

static bool NetStartup(void) {
#ifdef _WIN32
  static bool started;
  if (!started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return false;
    started = true;
  }
#else
  // A send on a socket the server closed would otherwise kill the game with
  // SIGPIPE (OpenSSL writes with plain write()). Leave custom handlers alone.
  struct sigaction sa;
  if (sigaction(SIGPIPE, NULL, &sa) == 0 && sa.sa_handler == SIG_DFL)
    signal(SIGPIPE, SIG_IGN);
#endif
  return true;
}

static void SetSocketOptions(ApSocket fd) {
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
#else
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
  int one_nosig = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one_nosig, sizeof(one_nosig));
#endif
#endif
  int one = 1;  // small JSON messages: send them now
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

// ---- SHA-1 and base64, only for Sec-WebSocket-Key/Accept ----

static uint32_t Rol(uint32_t x, int n) {
  return x << n | x >> (32 - n);
}

static void Sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
  size_t total = ((len + 8) / 64 + 1) * 64;
  uint8_t *m = calloc(total, 1);
  memcpy(m, data, len);
  m[len] = 0x80;
  for (int i = 0; i < 8; i++)
    m[total - 1 - i] = (uint8_t)(((uint64_t)len * 8) >> (8 * i));
  for (size_t off = 0; off < total; off += 64) {
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 16; i++)
      w[i] = (uint32_t)m[off + 4 * i] << 24 | m[off + 4 * i + 1] << 16 | m[off + 4 * i + 2] << 8 | m[off + 4 * i + 3];
    for (int i = 16; i < 80; i++)
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20)
        f = (b & c) | (~b & d), k = 0x5A827999;
      else if (i < 40)
        f = b ^ c ^ d, k = 0x6ED9EBA1;
      else if (i < 60)
        f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
      else
        f = b ^ c ^ d, k = 0xCA62C1D6;
      uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d, d = c, c = Rol(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  }
  free(m);
  for (int i = 0; i < 20; i++)
    out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}

static void Base64(const uint8_t *src, int len, char *dst) {
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < len; i += 3) {
    uint32_t v = src[i] << 16 | (i + 1 < len ? src[i + 1] << 8 : 0) | (i + 2 < len ? src[i + 2] : 0);
    *dst++ = t[v >> 18];
    *dst++ = t[(v >> 12) & 63];
    *dst++ = i + 1 < len ? t[(v >> 6) & 63] : '=';
    *dst++ = i + 2 < len ? t[v & 63] : '=';
  }
  *dst = 0;
}

// Masking keys only have to be unguessable for scripts in a browser, which
// don't exist here; xorshift is plenty.
static uint32_t Rand(ApWs *ws) {
  uint32_t x = ws->rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return ws->rng = x;
}

// ---- host name lookup ----
// getaddrinfo can't be interrupted and may take many seconds without a
// network, so it runs on a thread of its own that is simply left behind
// when the client stops.

// Shared by the looking-up thread and the one waiting for it; the last of
// the two to let go frees it.
typedef struct Resolve {
  SDL_mutex *mutex;
  int refs;
  bool done;
  char host[256], port[8];
  struct addrinfo *res;
} Resolve;

static void ResolveRelease(Resolve *r) {
  SDL_LockMutex(r->mutex);
  bool last = --r->refs == 0;
  SDL_UnlockMutex(r->mutex);
  if (last) {
    if (r->res)
      freeaddrinfo(r->res);
    SDL_DestroyMutex(r->mutex);
    free(r);
  }
}

static int SDLCALL ResolveThread(void *arg) {
  Resolve *r = arg;
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (getaddrinfo(r->host, r->port, &hints, &res) != 0)
    res = NULL;
  SDL_LockMutex(r->mutex);
  r->res = res;
  r->done = true;
  SDL_UnlockMutex(r->mutex);
  ResolveRelease(r);
  return 0;
}

// NULL with *cancelled false: the name wasn't found.
static struct addrinfo *ResolveHost(const char *host, int port, uint32_t deadline,
                                    SDL_atomic_t *cancel, bool *cancelled) {
  Resolve *r = calloc(1, sizeof(Resolve));
  r->mutex = SDL_CreateMutex();
  r->refs = 2;
  snprintf(r->host, sizeof(r->host), "%s", host);
  snprintf(r->port, sizeof(r->port), "%d", port);
  SDL_Thread *thread = SDL_CreateThread(ResolveThread, "ap_dns", r);
  if (thread)
    SDL_DetachThread(thread);
  else
    ResolveThread(r);
  struct addrinfo *res = NULL;
  for (;;) {
    SDL_LockMutex(r->mutex);
    bool done = r->done;
    if (done)
      res = r->res, r->res = NULL;
    SDL_UnlockMutex(r->mutex);
    *cancelled = !done && (Cancelled(cancel) || TimeUp(deadline));
    if (done || *cancelled)
      break;
    SDL_Delay(10);
  }
  ResolveRelease(r);
  return res;
}

static ApSocket ConnectTcp(const char *host, int port, uint32_t deadline, SDL_atomic_t *cancel,
                           ApWsFail *fail, char *err, size_t err_size) {
  bool cancelled;
  struct addrinfo *res = ResolveHost(host, port, deadline, cancel, &cancelled);
  if (!res) {
    if (cancelled && Cancelled(cancel))
      *fail = kApWsFail_Cancelled, SetErr(err, err_size, "Stopped");
    else if (cancelled)
      *fail = kApWsFail_Resolve, SetErr(err, err_size, "Looking up %s took too long", host);
    else
      *fail = kApWsFail_Resolve, SetErr(err, err_size, "Can't find the server %s", host);
    return AP_BAD_SOCKET;
  }
  ApSocket fd = AP_BAD_SOCKET;
  int last_error = 0;
  bool timed_out = false;
  for (struct addrinfo *ai = res; ai && fd == AP_BAD_SOCKET; ai = ai->ai_next) {
    ApSocket s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == AP_BAD_SOCKET) {
      last_error = LastSocketError();
      continue;
    }
    SetSocketOptions(s);
    bool ok = false;
    if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
      ok = true;
    } else {
      int e = LastSocketError();
#ifdef _WIN32
      bool pending = e == WSAEWOULDBLOCK;
#else
      bool pending = e == EINPROGRESS || e == EINTR;
#endif
      // Leave time for the other addresses (often IPv6 first, then IPv4).
      uint32_t until = ai->ai_next && (int32_t)(deadline - SDL_GetTicks()) > 5000 ? SDL_GetTicks() + 5000 : deadline;
      int w = 0;
      while (pending && (w = WaitSlice(s, true, until, cancel)) == 0) {}
      if (pending && w > 0) {
        int so_error = 0;
        socklen_t so_len = sizeof(so_error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_len);
        ok = so_error == 0;
        last_error = so_error;
      } else {
        if (pending)
          timed_out = true;
        last_error = pending ? 0 : e;
      }
    }
    if (ok)
      fd = s;
    else
      CloseSocket(s);
    if (Cancelled(cancel))
      break;
  }
  freeaddrinfo(res);
  if (fd != AP_BAD_SOCKET)
    return fd;
  *fail = kApWsFail_Connect;
#ifdef _WIN32
  bool refused = last_error == WSAECONNREFUSED;
#else
  bool refused = last_error == ECONNREFUSED;
#endif
  if (Cancelled(cancel))
    *fail = kApWsFail_Cancelled, SetErr(err, err_size, "Stopped");
  else if (refused)
    SetErr(err, err_size, "Connection refused (is a server running on port %d?)", port);
  else if (timed_out || !last_error)
    SetErr(err, err_size, "The server didn't answer (timed out)");
  else
#ifdef _WIN32
    SetErr(err, err_size, "Can't connect (error %d)", last_error);
#else
    SetErr(err, err_size, "Can't connect: %s", strerror(last_error));
#endif
  return AP_BAD_SOCKET;
}

// ---- raw I/O ----

// >0: bytes, 0: nothing now, -1: closed or broken.
static int RawRecv(ApWs *ws, void *buf, int len) {
  if (ws->tls)
    return ApTls_Recv(ws->tls, buf, len);
  int r = recv(ws->fd, buf, len, 0);
  if (r > 0)
    return r;
  return r < 0 && WouldBlock() ? 0 : -1;
}

static int RawSend(ApWs *ws, const void *buf, int len) {
  if (ws->tls)
    return ApTls_Send(ws->tls, buf, len);
  int r = send(ws->fd, buf, len, AP_SEND_FLAGS);
  if (r >= 0)
    return r;
  return WouldBlock() ? 0 : -1;
}

static bool SendAll(ApWs *ws, const void *data, size_t len) {
  if (ws->broken)
    return false;
  const char *p = data;
  uint32_t deadline = SDL_GetTicks() + kSendTimeoutMs;
  while (len) {
    int n = RawSend(ws, p, len > kMiB ? kMiB : (int)len);
    if (n > 0) {
      p += n, len -= n;
    } else if (n < 0) {
      SetError(ws, "The connection broke while sending");
      return false;
    } else if (WaitSlice(ws->fd, true, deadline, ws->cancel) < 0) {
      SetError(ws, Cancelled(ws->cancel) ? "Stopped" : "The server stopped taking data");
      return false;
    }
  }
  return true;
}

// Room for `want` more bytes at the end of the input buffer.
static void Reserve(ApWs *ws, size_t want) {
  if (ws->in_pos) {
    memmove(ws->in, ws->in + ws->in_pos, ws->in_len - ws->in_pos);
    ws->in_len -= ws->in_pos;
    ws->in_pos = 0;
  }
  if (ws->in_cap - ws->in_len >= want)
    return;
  size_t cap = ws->in_cap ? ws->in_cap : kRecvChunk * 2;
  while (cap - ws->in_len < want)
    cap *= 2;
  ws->in = realloc(ws->in, cap);
  ws->in_cap = cap;
}

// Reads what has arrived. 1: got bytes, 0: nothing within timeout_ms,
// -1: closed or broken.
static int Fill(ApWs *ws, int timeout_ms) {
  if (!(ws->tls && ApTls_Pending(ws->tls))) {
    int w = ApTls_WaitSocket((intptr_t)ws->fd, false, timeout_ms);
    if (w < 0) {
      SetError(ws, "Network error");
      return -1;
    }
    if (w == 0)
      return 0;
  }
  Reserve(ws, 4096 + 1);  // +1: room for a NUL while reading the HTTP answer
  int n = RawRecv(ws, ws->in + ws->in_len, (int)(ws->in_cap - ws->in_len - 1));
  if (n < 0) {
    SetError(ws, "The server closed the connection");
    return -1;
  }
  if (n == 0)
    return 0;
  ws->in_len += n;
  ws->last_rx = SDL_GetTicks();
  return 1;
}

// ---- handshake ----

static const char *SkipSpaces(const char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

// Checks the server's Sec-WebSocket-Extensions value (from our one offer,
// only permessage-deflate can come back, and it has to keep our condition).
static bool ParseExtensions(const char *v, size_t len, bool *deflate) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%.*s", (int)len, v);
  char *save_ext = NULL;
  for (char *ext = SDL_strtokr(buf, ",", &save_ext); ext; ext = SDL_strtokr(NULL, ",", &save_ext)) {
    char *save_param = NULL;
    char *name = SDL_strtokr(ext, ";", &save_param);
    if (!name)
      continue;
    name = (char *)SkipSpaces(name);
    size_t n = strlen(name);
    while (n && (name[n - 1] == ' ' || name[n - 1] == '\t'))
      name[--n] = 0;
    if (!n)
      continue;
    if (SDL_strcasecmp(name, "permessage-deflate") || *deflate)
      return false;
    bool no_context = false;
    for (char *p = SDL_strtokr(NULL, ";", &save_param); p; p = SDL_strtokr(NULL, ";", &save_param)) {
      p = (char *)SkipSpaces(p);
      if (!SDL_strncasecmp(p, "server_no_context_takeover", 26))
        no_context = true;
      else if (SDL_strncasecmp(p, "client_no_context_takeover", 26) &&
               SDL_strncasecmp(p, "server_max_window_bits", 22) &&  // any window fits the inflater
               SDL_strncasecmp(p, "client_max_window_bits", 22) &&  // we don't compress
               *p)
        return false;
    }
    if (!no_context)
      return false;  // shared history between messages: one-shot inflating can't do that
    *deflate = true;
  }
  return true;
}

static ApWsFail Handshake(ApWs *ws, const char *host, int port, const char *path, bool tls,
                          uint32_t deadline, char *err, size_t err_size) {
  uint8_t nonce[16];
  char key[32], expect[32], req[1024], host_header[300];
  for (int i = 0; i < 16; i++)
    nonce[i] = (uint8_t)Rand(ws);
  Base64(nonce, 16, key);
  bool v6 = strchr(host, ':') != NULL;
  if (port == (tls ? 443 : 80))
    snprintf(host_header, sizeof(host_header), v6 ? "[%s]" : "%s", host);
  else
    snprintf(host_header, sizeof(host_header), v6 ? "[%s]:%d" : "%s:%d", host, port);
  int n = snprintf(req, sizeof(req),
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: %s\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
      "User-Agent: Four Swords of the Past\r\n"
      "\r\n", path && *path ? path : "/", host_header, key);
  if (!SendAll(ws, req, n)) {
    SetErr(err, err_size, "%s", ws->error);
    return Cancelled(ws->cancel) ? kApWsFail_Cancelled : kApWsFail_Protocol;
  }
  char *end = NULL;
  while (!end) {
    int r = WaitSlice(ws->fd, false, deadline, ws->cancel);
    if (r < 0 && Cancelled(ws->cancel)) {
      SetErr(err, err_size, "Stopped");
      return kApWsFail_Cancelled;
    }
    if (r < 0) {
      SetErr(err, err_size, "The server didn't answer the WebSocket request");
      return kApWsFail_Protocol;
    }
    r = Fill(ws, 0);
    if (r < 0) {
      SetErr(err, err_size, "The server closed the connection without answering");
      return kApWsFail_Protocol;
    }
    if (r == 0)
      continue;
    ws->in[ws->in_len] = 0;
    if (ws->in_len >= 5 && memcmp(ws->in, "HTTP/", 5)) {
      SetErr(err, err_size, "The server's answer isn't HTTP");
      return kApWsFail_Protocol;
    }
    end = strstr((char *)ws->in, "\r\n\r\n");
    if (!end && ws->in_len > kMaxHeader) {
      SetErr(err, err_size, "The server's answer is too long");
      return kApWsFail_Http;
    }
  }
  int status = 0;
  sscanf((char *)ws->in, "HTTP/%*s %d", &status);
  if (status != 101) {
    SetErr(err, err_size, "The server answered HTTP %d instead of opening a WebSocket", status);
    return kApWsFail_Http;
  }
  char cat[128];
  uint8_t digest[20];
  snprintf(cat, sizeof(cat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  Sha1((uint8_t *)cat, strlen(cat), digest);
  Base64(digest, 20, expect);
  bool accept_ok = false, upgrade_ok = false, extensions_ok = true;
  for (const char *line = strstr((char *)ws->in, "\r\n") + 2; line < end; line = strstr(line, "\r\n") + 2) {
    const char *eol = strstr(line, "\r\n");
    const char *colon = memchr(line, ':', eol - line);
    if (!colon)
      continue;
    size_t name_len = colon - line;
    const char *v = SkipSpaces(colon + 1);
    size_t v_len = eol - v;
    while (v_len && (v[v_len - 1] == ' ' || v[v_len - 1] == '\t'))
      v_len--;
    if (name_len == 20 && !SDL_strncasecmp(line, "Sec-WebSocket-Accept", 20))
      accept_ok = v_len == strlen(expect) && !memcmp(v, expect, v_len);
    else if (name_len == 7 && !SDL_strncasecmp(line, "Upgrade", 7))
      upgrade_ok = v_len == 9 && !SDL_strncasecmp(v, "websocket", 9);
    else if (name_len == 24 && !SDL_strncasecmp(line, "Sec-WebSocket-Extensions", 24))
      extensions_ok = extensions_ok && ParseExtensions(v, v_len, &ws->deflate);
  }
  if (!accept_ok || !upgrade_ok || !extensions_ok) {
    SetErr(err, err_size, "The server's WebSocket answer is broken (%s)",
           !upgrade_ok ? "Upgrade" : !accept_ok ? "Sec-WebSocket-Accept" : "Sec-WebSocket-Extensions");
    return kApWsFail_Http;
  }
  ws->in_pos = (uint8_t *)end + 4 - ws->in;  // anything after the header already is frames
  return kApWsFail_None;
}

ApWs *ApWs_Connect(const char *host, int port, const char *path, bool tls, int timeout_ms,
                   SDL_atomic_t *cancel, ApWsFail *fail, char *err, size_t err_size) {
  uint32_t deadline = SDL_GetTicks() + timeout_ms;
  if (!NetStartup()) {
    *fail = kApWsFail_Connect;
    SetErr(err, err_size, "The network isn't available");
    return NULL;
  }
  ApSocket fd = ConnectTcp(host, port, deadline, cancel, fail, err, err_size);
  if (fd == AP_BAD_SOCKET)
    return NULL;
  ApWs *ws = calloc(1, sizeof(ApWs));
  ws->fd = fd;
  ws->cancel = cancel;
  ws->rng = (uint32_t)SDL_GetPerformanceCounter() ^ (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)ws ^ 0x9E3779B9;
  if (!ws->rng)
    ws->rng = 1;
  ws->last_rx = SDL_GetTicks();
  if (tls) {
    ApTlsResult r;
    int left = (int)(deadline - SDL_GetTicks());
    ws->tls = ApTls_Connect((intptr_t)fd, host, left > 0 ? left : 1, cancel, &r, err, err_size);
    if (!ws->tls) {
      *fail = r == kApTls_NoLibrary ? kApWsFail_NoTls : r == kApTls_BadCert ? kApWsFail_BadCert :
              r == kApTls_Cancelled && Cancelled(cancel) ? kApWsFail_Cancelled : kApWsFail_Protocol;
      ApWs_Close(ws);
      return NULL;
    }
  }
  *fail = Handshake(ws, host, port, path, tls, deadline, err, err_size);
  if (*fail != kApWsFail_None) {
    ApWs_Close(ws);
    return NULL;
  }
  return ws;
}

// ---- frames ----

static bool SendFrame(ApWs *ws, int op, const void *data, size_t len) {
  uint8_t *buf = malloc(len + 14);
  size_t hl = 0;
  buf[hl++] = 0x80 | op;  // FIN, and RSV1 clear: never compressed
  if (len < 126) {
    buf[hl++] = 0x80 | (uint8_t)len;
  } else if (len < 65536) {
    buf[hl++] = 0x80 | 126;
    buf[hl++] = (uint8_t)(len >> 8);
    buf[hl++] = (uint8_t)len;
  } else {
    buf[hl++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--)
      buf[hl++] = (uint8_t)((uint64_t)len >> (8 * i));
  }
  uint32_t key = Rand(ws);
  uint8_t *mask = buf + hl;
  memcpy(mask, &key, 4);
  hl += 4;
  for (size_t i = 0; i < len; i++)
    buf[hl + i] = ((const uint8_t *)data)[i] ^ mask[i & 3];
  bool ok = SendAll(ws, buf, hl + len);
  free(buf);
  return ok;
}

bool ApWs_SendText(ApWs *ws, const char *text, size_t len) {
  return SendFrame(ws, 1, text, len);
}

bool ApWs_SendPing(ApWs *ws) {
  return SendFrame(ws, 9, "ap", 2);
}

// Inflates a whole compressed message. The sender cut off the 00 00 ff ff
// that ends every message (RFC 7692 7.2.1); put it back, and add an empty
// final block so that the decoder sees the stream end.
static char *Inflate(ApWs *ws, size_t *out_len) {
  static const uint8_t kTail[9] = { 0, 0, 0xff, 0xff, 1, 0, 0, 0xff, 0xff };
  memcpy(ws->msg + ws->msg_len, kTail, 9);  // msg_cap always leaves room for this
  int in_len = (int)ws->msg_len + 9;
  // The size isn't known up front: guess, and try again bigger (up to the limit,
  // so that a small message can't make us allocate gigabytes).
  size_t guess = ws->msg_len * 4 + 4096;
  for (;;) {
    if (guess > kApWsMaxMessage)
      guess = kApWsMaxMessage;
    char *out = malloc(guess + 1);
    int n = stbi_zlib_decode_noheader_buffer(out, (int)guess, (const char *)ws->msg, in_len);
    if (n >= 0) {
      out[n] = 0;
      *out_len = n;
      return out;
    }
    free(out);
    if (guess >= kApWsMaxMessage)
      return NULL;
    guess *= 2;
  }
}

// Takes one frame from the input. 1: a message is complete, 0: more bytes
// needed, 2: the frame was used up (look for another), -1: broken.
static int ParseFrame(ApWs *ws, char **out, size_t *out_len) {
  size_t avail = ws->in_len - ws->in_pos;
  if (avail < 2)
    return 0;
  const uint8_t *p = ws->in + ws->in_pos;
  int fin = p[0] >> 7, rsv1 = (p[0] >> 6) & 1, rsv23 = (p[0] >> 4) & 3, op = p[0] & 15;
  uint64_t len = p[1] & 127;
  size_t hl = 2;
  if (p[1] & 0x80) {
    SetError(ws, "The server sent a masked frame");
    return -1;
  }
  if (len == 126) {
    if (avail < 4)
      return 0;
    len = p[2] << 8 | p[3];
    hl = 4;
  } else if (len == 127) {
    if (avail < 10)
      return 0;
    len = 0;
    for (int i = 0; i < 8; i++)
      len = len << 8 | p[2 + i];
    hl = 10;
  }
  if (len > kApWsMaxMessage) {
    SetError(ws, "The server sent a message over 16 MiB");
    return -1;
  }
  if (avail < hl + len) {
    Reserve(ws, hl + (size_t)len - avail);  // so that the rest comes in big reads
    return 0;
  }
  const uint8_t *payload = p + hl;
  ws->in_pos += hl + (size_t)len;
  if (rsv23 || (rsv1 && (op != 1 && op != 2))) {
    SetError(ws, "The server sent a frame with unknown flags");
    return -1;
  }
  if (op >= 8) {  // control frames: never fragmented, at most 125 bytes
    if (!fin || len > 125) {
      SetError(ws, "The server sent a broken control frame");
      return -1;
    }
    if (op == 9) {
      return SendFrame(ws, 10, payload, (size_t)len) ? 2 : -1;
    } else if (op == 8) {
      int code = len >= 2 ? payload[0] << 8 | payload[1] : 0;
      SendFrame(ws, 8, payload, len >= 2 ? 2 : 0);
      ws->error[0] = 0;  // the server's reason, not a failed echo
      if (code)
        SetError(ws, "The server closed the connection (%d%s%.*s)", code, len > 2 ? ": " : "",
                 (int)(len > 2 ? len - 2 : 0), payload + 2);
      else
        SetError(ws, "The server closed the connection");
      return -1;
    } else if (op == 10) {
      return 2;
    }
    SetError(ws, "The server sent an unknown control frame");
    return -1;
  }
  if (op == 1 || op == 2) {
    if (ws->in_msg) {
      SetError(ws, "The server started a message inside another one");
      return -1;
    }
    if (rsv1 && !ws->deflate) {
      SetError(ws, "The server compressed a message without asking");
      return -1;
    }
    ws->in_msg = true;
    ws->msg_compressed = rsv1;
    ws->msg_len = 0;
  } else if (op != 0 || !ws->in_msg) {
    SetError(ws, "The server sent a stray frame");
    return -1;
  }
  if (ws->msg_len + len > kApWsMaxMessage) {
    SetError(ws, "The server sent a message over 16 MiB");
    return -1;
  }
  if (ws->msg_len + len + 16 > ws->msg_cap) {
    ws->msg_cap = (ws->msg_len + (size_t)len + 16) * 3 / 2;
    ws->msg = realloc(ws->msg, ws->msg_cap);
  }
  memcpy(ws->msg + ws->msg_len, payload, (size_t)len);
  ws->msg_len += (size_t)len;
  if (!fin)
    return 2;
  ws->in_msg = false;
  if (ws->msg_compressed) {
    *out = Inflate(ws, out_len);
    if (!*out) {
      SetError(ws, "A compressed message from the server couldn't be read");
      return -1;
    }
  } else {
    ws->msg[ws->msg_len] = 0;
    *out = (char *)ws->msg;  // hand over the buffer itself
    *out_len = ws->msg_len;
    ws->msg = NULL;
    ws->msg_len = ws->msg_cap = 0;
  }
  // Don't keep multi-megabyte buffers around for the small messages that follow.
  if (ws->msg_cap > kMiB) {
    free(ws->msg);
    ws->msg = NULL;
    ws->msg_cap = 0;
  }
  if (ws->in_pos == ws->in_len) {
    ws->in_pos = ws->in_len = 0;
    if (ws->in_cap > kMiB) {
      free(ws->in);
      ws->in = NULL;
      ws->in_cap = 0;
    }
  }
  return 1;
}

int ApWs_Poll(ApWs *ws, int timeout_ms, char **msg, size_t *len) {
  if (ws->broken)
    return -1;
  uint32_t start = SDL_GetTicks();
  for (;;) {
    int r;
    while ((r = ParseFrame(ws, msg, len)) == 2) {}
    if (r)
      return r;
    int left = timeout_ms - (int)(SDL_GetTicks() - start);
    r = Fill(ws, left > 0 ? left : 0);
    if (r < 0)
      return -1;
    if (r == 0 && (int)(SDL_GetTicks() - start) >= timeout_ms)
      return 0;
  }
}

bool ApWs_Compressed(ApWs *ws) {
  return ws->deflate;
}

uint32_t ApWs_LastReceive(ApWs *ws) {
  return ws->last_rx;
}

const char *ApWs_Error(ApWs *ws) {
  return ws->error[0] ? ws->error : "The connection broke";
}

double ApWs_WallClock(void) {
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t t = (uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime;  // 100 ns steps since 1601
  return (double)(t - 116444736000000000ULL) / 1e7;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

void ApWs_Close(ApWs *ws) {
  if (!ws)
    return;
  if (!ws->broken) {
    // One try only: no waiting for a server that has stopped reading.
    static const uint8_t kClose[8] = { 0x88, 0x82, 0, 0, 0, 0, 0x03, 0xe8 };  // 1000, zero mask
    RawSend(ws, kClose, (int)sizeof(kClose));
  }
  if (ws->tls)
    ApTls_Close(ws->tls);
  CloseSocket(ws->fd);
  free(ws->in);
  free(ws->msg);
  free(ws);
}
