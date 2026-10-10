// Opens a UDP port on the router through UPnP IGD so internet players can connect.
#ifndef ZELDA3_UPNP_H_
#define ZELDA3_UPNP_H_

#include "types.h"

typedef void UpnpCallback(bool ok, const char *external_ip, uint16 port);

// Runs discovery and port mapping on a background thread.
void Upnp_MapPortAsync(uint16 port, UpnpCallback *cb);
// Removes the mapping created by Upnp_MapPortAsync (best effort, blocking but short).
void Upnp_UnmapPort();

// Exposed for testing: synchronous version. Returns true on success.
bool Upnp_MapPort(uint16 port, char *external_ip, size_t ip_size);

#endif  // ZELDA3_UPNP_H_
