// Minimal cross platform UDP networking.
#ifndef ZELDA3_NET_H_
#define ZELDA3_NET_H_

#include "types.h"

typedef struct NetAddr {
  uint32 ip;    // host byte order
  uint16 port;  // host byte order
} NetAddr;

typedef struct NetSocket NetSocket;

bool Net_Init();
// Binds a non-blocking UDP socket. port 0 picks any free port.
NetSocket *Net_Open(uint16 port, bool broadcast);
void Net_Close(NetSocket *s);
uint16 Net_LocalPort(NetSocket *s);
bool Net_Send(NetSocket *s, const NetAddr *to, const void *data, int len);
// Returns number of bytes received or -1 if nothing is pending.
int Net_Recv(NetSocket *s, NetAddr *from, void *data, int maxlen);
// Parses "host", "host:port" or "a.b.c.d:port". Resolves DNS names.
bool Net_ParseAddr(const char *str, uint16 default_port, NetAddr *out);
void Net_AddrToString(const NetAddr *a, char *buf, size_t bufsize);
bool Net_AddrEqual(const NetAddr *a, const NetAddr *b);
// Best guess at this machine's LAN IP (0 if unknown).
uint32 Net_GetLocalIp();

// TCP helpers used by UPnP (blocking, with timeout).
int Net_TcpRequest(const NetAddr *to, const char *request, int reqlen, char *response, int maxlen, int timeout_ms);

#endif  // ZELDA3_NET_H_
