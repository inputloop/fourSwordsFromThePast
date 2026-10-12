// Minimal cross platform UDP networking.
#ifndef ZELDA3_NET_H_
#define ZELDA3_NET_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct NetAddr {
  uint32_t ip;    // host byte order
  uint16_t port;  // host byte order
} NetAddr;

typedef struct NetSocket NetSocket;

bool Net_Init();
// Simulated network conditions (tests), like ZELDA3_NETSIM.
void Net_SetSim(int delay_ms, int jitter_ms, int loss_percent);
// Tests: drop everything sent and received (a pulled cable) while on.
void Net_SetBlackout(bool on);
// Binds a non-blocking UDP socket. port 0 picks any free port.
NetSocket *Net_Open(uint16_t port, bool broadcast);
void Net_Close(NetSocket *s);
uint16_t Net_LocalPort(NetSocket *s);
bool Net_Send(NetSocket *s, const NetAddr *to, const void *data, int len);
// Returns number of bytes received or -1 if nothing is pending.
int Net_Recv(NetSocket *s, NetAddr *from, void *data, int maxlen);
// Parses "host", "host:port" or "a.b.c.d:port". Resolves DNS names.
bool Net_ParseAddr(const char *str, uint16_t default_port, NetAddr *out);
void Net_AddrToString(const NetAddr *a, char *buf, size_t bufsize);
bool Net_AddrEqual(const NetAddr *a, const NetAddr *b);
// Best guess at this machine's LAN IP (0 if unknown).
uint32_t Net_GetLocalIp();

// TCP helpers used by UPnP (blocking, with timeout).
int Net_TcpRequest(const NetAddr *to, const char *request, int reqlen, char *response, int maxlen, int timeout_ms);

#endif  // ZELDA3_NET_H_
