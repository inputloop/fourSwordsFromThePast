// WebSocket (RFC 6455) client for the Archipelago connection, over TCP or
// TLS. Messages the server compresses (permessage-deflate) are inflated;
// what we send is never compressed.
#ifndef ZELDA3_AP_WS_H_
#define ZELDA3_AP_WS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL.h>  // SDL_atomic_t

typedef struct ApWs ApWs;

typedef enum ApWsFail {
  kApWsFail_None,
  kApWsFail_Resolve,    // the host name wasn't found
  kApWsFail_Connect,    // no TCP connection (refused, unreachable, timed out)
  kApWsFail_NoTls,      // wss:// without a TLS library
  kApWsFail_BadCert,    // the TLS certificate was rejected
  kApWsFail_Protocol,   // connected, but the TLS or WebSocket handshake failed
                        // (often a sign that ws:// and wss:// got mixed up)
  kApWsFail_Http,       // the server answered HTTP, but not with a WebSocket
  kApWsFail_Cancelled,  // *cancel was set
} ApWsFail;

// Largest message accepted from the server, after inflating (the same as
// Archipelago's own clients).
enum { kApWsMaxMessage = 16 << 20 };

// Connects to ws://host:port/path (wss:// with tls). Blocks for up to
// timeout_ms, but returns soon after *cancel is set (SDL_AtomicSet); it is also
// watched later, while sending. On failure: NULL, *fail and a sentence in err.
ApWs *ApWs_Connect(const char *host, int port, const char *path, bool tls, int timeout_ms,
                   SDL_atomic_t *cancel, ApWsFail *fail, char *err, size_t err_size);
bool ApWs_SendText(ApWs *ws, const char *text, size_t len);
bool ApWs_SendPing(ApWs *ws);
// 1: *msg holds a whole message (malloc'd, NUL-terminated, the caller frees
// it). 0: none within timeout_ms. -1: the connection is gone (ApWs_Error
// says why). Pings are answered in here.
int ApWs_Poll(ApWs *ws, int timeout_ms, char **msg, size_t *len);
bool ApWs_Compressed(ApWs *ws);
// SDL_GetTicks() of the last bytes from the server.
uint32_t ApWs_LastReceive(ApWs *ws);
const char *ApWs_Error(ApWs *ws);
// Says goodbye (a close frame, if the socket takes it) and frees everything.
void ApWs_Close(ApWs *ws);

// Seconds since 1970, with fractions (the time stamp DeathLink uses). Here
// because this file can include the system headers that tell the time.
double ApWs_WallClock(void);

#endif  // ZELDA3_AP_WS_H_
