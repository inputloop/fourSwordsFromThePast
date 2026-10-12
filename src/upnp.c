#include "upnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "net.h"

static struct {
  NetAddr control_addr;
  char control_path[256];
  char service_type[96];
  uint16 mapped_port;
  bool mapped;
  int lease;           // seconds the mapping lives without renewal (0: for good)
  UpnpCallback *cb;
  uint16 want_port;
  SDL_atomic_t generation;  // bumped whenever hosting starts or stops: an older mapping thread stands down
  SDL_atomic_t threads;     // mapping threads alive
} g_upnp;

enum {
  kLeaseSeconds = 7200,  // the router forgets the mapping by itself if the game dies
  kRenewSeconds = 3600,
};

static const char *FindNoCase(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++)
    if (SDL_strncasecmp(hay, needle, n) == 0)
      return hay;
  return NULL;
}

// Splits "http://host:port/path" into an address and a path.
static bool ParseUrl(const char *url, NetAddr *addr, char *path, size_t path_size) {
  if (SDL_strncasecmp(url, "http://", 7) != 0)
    return false;
  const char *host = url + 7;
  const char *slash = strchr(host, '/');
  char hostport[128];
  size_t n = slash ? (size_t)(slash - host) : strlen(host);
  if (n >= sizeof(hostport))
    return false;
  memcpy(hostport, host, n);
  hostport[n] = 0;
  snprintf(path, path_size, "%s", slash ? slash : "/");
  return Net_ParseAddr(hostport, 80, addr);
}

static bool ExtractTag(const char *xml, const char *tag, char *out, size_t out_size) {
  char open[64], close[64];
  snprintf(open, sizeof(open), "<%s>", tag);
  snprintf(close, sizeof(close), "</%s>", tag);
  const char *a = FindNoCase(xml, open);
  if (!a)
    return false;
  a += strlen(open);
  const char *b = FindNoCase(a, close);
  if (!b)
    return false;
  size_t n = b - a;
  if (n >= out_size)
    n = out_size - 1;
  memcpy(out, a, n);
  out[n] = 0;
  // trim whitespace
  while (n && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == '\t'))
    out[--n] = 0;
  return true;
}

static bool Discover(char *location, size_t loc_size) {
  NetSocket *s = Net_Open(0, true);
  if (!s)
    return false;
  static const char *const kTargets[] = {
    "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
    "urn:schemas-upnp-org:service:WANIPConnection:1",
    "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
  };
  NetAddr mcast = { 0xEFFFFFFA, 1900 };  // 239.255.255.250
  char req[512];
  bool found = false;
  for (int attempt = 0; attempt < 2 && !found; attempt++) {
    for (size_t t = 0; t < sizeof(kTargets) / sizeof(kTargets[0]); t++) {
      int n = snprintf(req, sizeof(req),
                       "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",
                       kTargets[t]);
      Net_Send(s, &mcast, req, n);
    }
    uint32 start = SDL_GetTicks();
    char buf[1500];
    while (!found && SDL_GetTicks() - start < 2500) {
      int n = Net_Recv(s, NULL, buf, sizeof(buf) - 1);
      if (n <= 0) {
        SDL_Delay(20);
        continue;
      }
      buf[n] = 0;
      const char *loc = FindNoCase(buf, "\nLOCATION:");
      if (!loc)
        continue;
      loc += 10;
      while (*loc == ' ')
        loc++;
      size_t len = strcspn(loc, "\r\n");
      if (len >= loc_size)
        continue;
      memcpy(location, loc, len);
      location[len] = 0;
      found = true;
    }
  }
  Net_Close(s);
  return found;
}

static int HttpRequest(const NetAddr *addr, const char *req, int reqlen, char *resp, int maxlen) {
  int n = Net_TcpRequest(addr, req, reqlen, resp, maxlen, 3000);
  if (n <= 0)
    return -1;
  return n;
}

static bool FetchControlUrl(const char *location) {
  NetAddr addr;
  char path[256];
  if (!ParseUrl(location, &addr, path, sizeof(path)))
    return false;
  char req[512];
  int n = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nConnection: close\r\n\r\n", path,
                   addr.ip >> 24, (addr.ip >> 16) & 255, (addr.ip >> 8) & 255, addr.ip & 255, addr.port);
  char *resp = (char *)malloc(65536);
  if (HttpRequest(&addr, req, n, resp, 65536) <= 0) {
    free(resp);
    return false;
  }
  static const char *const kServices[] = {
    "urn:schemas-upnp-org:service:WANIPConnection:2",
    "urn:schemas-upnp-org:service:WANIPConnection:1",
    "urn:schemas-upnp-org:service:WANPPPConnection:1",
  };
  bool ok = false;
  for (size_t i = 0; i < sizeof(kServices) / sizeof(kServices[0]) && !ok; i++) {
    const char *svc = FindNoCase(resp, kServices[i]);
    if (!svc)
      continue;
    char control[200];
    if (!ExtractTag(svc, "controlURL", control, sizeof(control)))
      continue;
    snprintf(g_upnp.service_type, sizeof(g_upnp.service_type), "%s", kServices[i]);
    if (SDL_strncasecmp(control, "http://", 7) == 0) {
      ok = ParseUrl(control, &g_upnp.control_addr, g_upnp.control_path, sizeof(g_upnp.control_path));
    } else {
      g_upnp.control_addr = addr;
      snprintf(g_upnp.control_path, sizeof(g_upnp.control_path), "%s%s", control[0] == '/' ? "" : "/", control);
      ok = true;
    }
  }
  free(resp);
  return ok;
}

static int SoapCall(const char *action, const char *args, char *resp, int maxlen) {
  char body[1024];
  int blen = snprintf(body, sizeof(body),
      "<?xml version=\"1.0\"?>\r\n"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
      action, g_upnp.service_type, args, action);
  char req[2048];
  const NetAddr *a = &g_upnp.control_addr;
  int n = snprintf(req, sizeof(req),
      "POST %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
      "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
      g_upnp.control_path, a->ip >> 24, (a->ip >> 16) & 255, (a->ip >> 8) & 255, a->ip & 255, a->port,
      g_upnp.service_type, action, blen, body);
  int r = HttpRequest(a, req, n, resp, maxlen);
  if (r <= 0)
    return -1;
  return strncmp(resp + 9, "200", 3) == 0 ? 200 : atoi(resp + 9);
}

static bool AddMapping(uint16 port, int lease) {
  uint32 local = Net_GetLocalIp();
  char args[512], resp[4096];
  snprintf(args, sizeof(args),
           "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>"
           "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%u.%u.%u.%u</NewInternalClient>"
           "<NewEnabled>1</NewEnabled><NewPortMappingDescription>Zelda3 co-op</NewPortMappingDescription>"
           "<NewLeaseDuration>%d</NewLeaseDuration>",
           port, port, local >> 24, (local >> 16) & 255, (local >> 8) & 255, local & 255, lease);
  return SoapCall("AddPortMapping", args, resp, sizeof(resp)) == 200;
}

static void DeleteMapping(uint16 port) {
  char args[256], resp[2048];
  snprintf(args, sizeof(args),
           "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>",
           port);
  SoapCall("DeletePortMapping", args, resp, sizeof(resp));
}

// A mapping with a lease first (some routers only take permanent ones).
static bool MapPort(uint16 port, char *external_ip, size_t ip_size, int gen) {
  char location[256];
  if (!Discover(location, sizeof(location)) || !FetchControlUrl(location))
    return false;
  if (SDL_AtomicGet(&g_upnp.generation) != gen)
    return false;  // hosting stopped meanwhile
  g_upnp.lease = kLeaseSeconds;
  if (!AddMapping(port, kLeaseSeconds)) {
    g_upnp.lease = 0;
    if (!AddMapping(port, 0))
      return false;
  }
  g_upnp.mapped = true;
  g_upnp.mapped_port = port;
  external_ip[0] = 0;
  char resp[4096];
  if (SoapCall("GetExternalIPAddress", "", resp, sizeof(resp)) == 200)
    ExtractTag(resp, "NewExternalIPAddress", external_ip, ip_size);
  return true;
}

bool Upnp_MapPort(uint16 port, char *external_ip, size_t ip_size) {
  return MapPort(port, external_ip, ip_size, SDL_AtomicGet(&g_upnp.generation));
}

// Maps the port, keeps the lease alive until hosting stops, then removes the
// mapping: all on this thread, so the game never waits for the router.
static int SDLCALL UpnpThread(void *arg) {
  int gen = SDL_AtomicGet(&g_upnp.generation);
  uint16 port = g_upnp.want_port;
  // An earlier thread still at it (hosting stopped and started again quickly):
  // let it remove its mapping first, or it would remove ours.
  for (int i = 0; i < 400 && SDL_AtomicGet(&g_upnp.threads) > 1; i++)
    SDL_Delay(25);
  char ip[64] = { 0 };
  bool ok = MapPort(port, ip, sizeof(ip), gen);
  if (SDL_AtomicGet(&g_upnp.generation) != gen) {
    if (ok)
      DeleteMapping(port);
  } else {
    if (g_upnp.cb)
      g_upnp.cb(ok, ip[0] ? ip : "your public IP", port);
    while (ok) {
      // Look now and then whether hosting stopped; renew well before the lease runs out.
      for (int i = 0; i < kRenewSeconds * 4 && SDL_AtomicGet(&g_upnp.generation) == gen; i++)
        SDL_Delay(250);
      if (SDL_AtomicGet(&g_upnp.generation) != gen)
        break;
      if (g_upnp.lease)
        AddMapping(port, g_upnp.lease);
    }
    if (ok) {
      DeleteMapping(port);
      g_upnp.mapped = false;
    }
  }
  SDL_AtomicDecRef(&g_upnp.threads);
  return 0;
}

void Upnp_MapPortAsync(uint16 port, UpnpCallback *cb) {
  SDL_AtomicIncRef(&g_upnp.generation);
  g_upnp.cb = cb;
  g_upnp.want_port = port;
  SDL_AtomicIncRef(&g_upnp.threads);
  SDL_Thread *t = SDL_CreateThread(&UpnpThread, "upnp", NULL);
  if (t) {
    SDL_DetachThread(t);
  } else {
    SDL_AtomicDecRef(&g_upnp.threads);
    if (cb)
      cb(false, "", port);
  }
}

void Upnp_UnmapPort() {
  // The mapping thread notices and removes the mapping; nothing to wait for.
  // (With the game closing before it gets to it, the lease runs out.)
  SDL_AtomicIncRef(&g_upnp.generation);
}
