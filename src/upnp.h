// Opens a UDP port on the router through UPnP IGD so internet players can connect.
#ifndef ZELDA3_UPNP_H_
#define ZELDA3_UPNP_H_

#include "types.h"

typedef void UpnpCallback(bool ok, const char *external_ip, uint16 port);

// Runs discovery and port mapping on a background thread.
void Upnp_MapPortAsync(uint16 port, UpnpCallback *cb);
// Removes the mapping created by Upnp_MapPortAsync (on its thread: returns at once).
void Upnp_UnmapPort();

// Exposed for testing: synchronous version. Returns true on success.
bool Upnp_MapPort(uint16 port, char *external_ip, size_t ip_size);

// When the router won't open the port, the public address can still be
// learned from a STUN server ("host:port"; NULL or "" never asks). The
// callback then gets ok = false with that address (the port has to be
// forwarded by hand).
void Upnp_SetStunServer(const char *server);
// Asks a STUN server for this computer's public IPv4 address (blocking, a
// few seconds at most).
bool Upnp_StunPublicIp(const char *server, char *ip_out, size_t ip_size);

#endif  // ZELDA3_UPNP_H_
