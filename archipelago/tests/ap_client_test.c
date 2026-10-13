// Test driver for the Archipelago client (src/ap_client.c, ap_ws.c, ap_tls.c),
// through its public interface only. Run by run_client_tests.sh against
// mock_server.py, and by hand against a real MultiServer:
//
//   ap_client_test <scenario> <address> [args]   (see main() for the list)
//
// Prints PASS/FAIL lines; the exit code is the number of failures.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetProcessTimes
#undef FORCEINLINE    // src/types.h has its own
#else
#include <sys/resource.h>
#endif
#include <SDL.h>
#include "src/ap_client.h"

// The game gets stb_image's zlib decoder from glsl_shader.c; this program
// has no shaders, so it builds the decoder the same way here.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb/stb_image.h"

#define BASE kApIdBase

enum { kMaxLog = 512 };

static int g_failures;
static bool g_keep_texts;  // flood test: leave the client's queue alone
static struct {
  char text[256];
  ApTextKind kind;
} g_log[kMaxLog];
static int g_num_log;

static void Fail(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  printf("  FAIL: ");
  vprintf(fmt, va);
  printf("\n");
  va_end(va);
  fflush(stdout);
  g_failures++;
}

static void Note(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  printf("  ");
  vprintf(fmt, va);
  printf("\n");
  va_end(va);
  fflush(stdout);
}

#define EXPECT(cond, ...) do { if (!(cond)) Fail(__VA_ARGS__); } while (0)

static void SetEnv(const char *name, const char *value) {
#ifdef _WIN32
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s=%s", name, value ? value : "");
  _putenv(buf);
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

static void Drain(void) {
  if (g_keep_texts)
    return;
  char buf[256];
  ApTextKind kind;
  while (ApClient_PollText(buf, sizeof(buf), &kind)) {
    Note("text[%d]: %s", kind, buf);
    if (!strncmp(buf, "MOCK ERROR", 10))
      Fail("the mock server complained: %s", buf);
    if (g_num_log < kMaxLog) {
      snprintf(g_log[g_num_log].text, sizeof(g_log[0].text), "%s", buf);
      g_log[g_num_log++].kind = kind;
    }
  }
}

static int FindText(const char *substr) {
  for (int i = 0; i < g_num_log; i++)
    if (strstr(g_log[i].text, substr))
      return i;
  return -1;
}

static bool WaitText(const char *substr, int ms) {
  uint32_t end = SDL_GetTicks() + ms;
  do {
    Drain();
    if (FindText(substr) >= 0)
      return true;
    SDL_Delay(10);
  } while ((int32_t)(end - SDL_GetTicks()) > 0);
  Fail("no text with \"%s\" within %d ms", substr, ms);
  return false;
}

static void ExpectText(const char *exact, ApTextKind kind) {
  for (int i = 0; i < g_num_log; i++) {
    if (!strcmp(g_log[i].text, exact)) {
      EXPECT(g_log[i].kind == kind, "\"%s\" has kind %d, not %d", exact, g_log[i].kind, kind);
      return;
    }
  }
  Fail("no text \"%s\"", exact);
}

static void ExpectNoText(const char *substr) {
  int i = FindText(substr);
  EXPECT(i < 0, "unexpected text \"%s\"", i >= 0 ? g_log[i].text : "");
}

static bool WaitStatus(ApStatus want, int ms) {
  uint32_t end = SDL_GetTicks() + ms;
  char text[256];
  ApStatus s;
  do {
    Drain();
    s = ApClient_GetStatus(text, sizeof(text));
    if (s == want)
      return true;
    SDL_Delay(10);
  } while ((int32_t)(end - SDL_GetTicks()) > 0);
  Fail("status %d (\"%s\") instead of %d after %d ms", s, text, want, ms);
  return false;
}

static void ExpectStatusText(const char *substr) {
  char text[256];
  ApClient_GetStatus(text, sizeof(text));
  EXPECT(strstr(text, substr) != NULL, "status text \"%s\" lacks \"%s\"", text, substr);
}

static bool WaitItems(int n, int ms) {
  uint32_t end = SDL_GetTicks() + ms;
  do {
    Drain();
    if (ApClient_ItemCount() >= n)
      return true;
    SDL_Delay(10);
  } while ((int32_t)(end - SDL_GetTicks()) > 0);
  Fail("%d items instead of %d", ApClient_ItemCount(), n);
  return false;
}

static void ExpectItem(int i, int64 item, int64 location, int player) {
  int64 it, loc;
  int pl;
  if (!ApClient_GetItem(i, &it, &loc, &pl)) {
    Fail("no item %d", i);
    return;
  }
  EXPECT(it == item && loc == location && pl == player, "item %d is (%lld, %lld, %d), not (%lld, %lld, %d)", i,
         (long long)it, (long long)loc, pl, (long long)item, (long long)location, player);
}

static void Pump(int ms) {
  uint32_t end = SDL_GetTicks() + ms;
  while ((int32_t)(end - SDL_GetTicks()) > 0) {
    Drain();
    SDL_Delay(10);
  }
}

// CPU time this process has used, in seconds.
static double CpuSeconds(void) {
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return 0;
  uint64 k = (uint64)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime;
  uint64 u = (uint64)user.dwHighDateTime << 32 | user.dwLowDateTime;
  return (double)(k + u) / 1e7;
#else
  struct rusage r;
  getrusage(RUSAGE_SELF, &r);
  return r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6 + r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6;
#endif
}

static uint32 Fnv(const char *s) {
  uint32 h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (uint8)*s) * 16777619u;
  return h ? h : 1;
}

static void StartAs(const char *server, const char *slot, const char *password, const char *game) {
  ApConfig cfg = { server, slot, password, game, NULL };
  g_num_log = 0;
  ApClient_Start(&cfg);
}

static void StopTimed(int max_ms) {
  uint32_t t0 = SDL_GetTicks();
  ApClient_Stop();
  uint32_t dt = SDL_GetTicks() - t0;
  Note("ApClient_Stop took %u ms", dt);
  EXPECT((int)dt <= max_ms, "ApClient_Stop took %u ms (limit %d)", dt, max_ms);
  EXPECT(ApClient_GetStatus(NULL, 0) == kApStatus_Off, "status isn't Off after Stop");
}

// ---- scenarios against mock_server.py ----

static void ExpectRoom(int death_link) {
  ApRoom room;
  if (!ApClient_GetRoom(&room)) {
    Fail("no room");
    return;
  }
  EXPECT(!strcmp(room.seed_name, "MockSeed123"), "seed %s", room.seed_name);
  EXPECT(!strcmp(room.slot_name, "Link"), "slot name %s", room.slot_name);
  EXPECT(room.slot == 1 && room.team == 0, "slot %d team %d", room.slot, room.team);
  EXPECT(room.slot_data_version == 1 && room.slot_data_goal == 0, "slot_data version %d goal %d",
         room.slot_data_version, room.slot_data_goal);
  EXPECT(room.slot_data_death_link == death_link, "slot_data death_link %d", room.slot_data_death_link);
  EXPECT(room.bind_hash == Fnv("MockSeed123/1/7"), "bind hash %08x", room.bind_hash);
}

// Connect, read texts and items, check locations, reach the goal.
static void Basic(const char *addr, const char *scheme) {
  int64 first = BASE + 1;
  StartAs(addr, "Link", NULL, NULL);
  EXPECT(!ApClient_GetRoom(NULL), "a room before connecting");
  ApClient_CheckLocations(&first, 1);  // before the connection: sent once connected
  ApClient_CheckLocations(&first, 1);  // twice: still one check
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  ExpectStatusText(scheme);
  ExpectStatusText("as Link");
  ExpectRoom(0);
  WaitText("MOCK OK texts", 5000);
  WaitItems(3, 5000);
  ExpectItem(0, BASE + 0x49, -2, 0);
  ExpectItem(1, BASE + 0x0A, 2000, 2);
  ExpectItem(2, BASE + 0x12, BASE + 1, 1);
  WaitText("Found your own", 5000);
  ExpectText("Received Hookshot from Alice (Alice's Chest)", kApText_Progression);
  ExpectText("Received Rupees (20) from Alice (Haunted Well)", kApText_Normal);
  ExpectText("Hint: Link's Hammer is at Haunted Well in Alice's World. (unspecified)", kApText_Useful);
  ExpectText("[Server]: Gr??e ?", kApText_Normal);
  ExpectText("Alice: hello", kApText_Normal);
  ExpectText("Found your own Lamp", kApText_Progression);
  ExpectText("Received Bow (cheat)", kApText_Progression);
  ExpectNoText("Lamp to Bob");
  ExpectNoText("has joined");
  ExpectNoText("Now that you are");
  ExpectNoText("Hammer (cheat)");  // to slot 1 of another team
  int64 more[2] = { BASE + 2, BASE + 3 };
  ApClient_CheckLocations(more, 2);
  WaitText("Sent Rubber Duck to Alice", 5000);
  ExpectText("Sent Bow to Bob", kApText_Progression);
  ExpectText("Sent Rubber Duck to Alice", kApText_Trap);
  ApClient_SetGoalReached();
  WaitText("Link (Team #1) has completed their goal.", 5000);
  EXPECT(ApClient_ItemCount() == 3, "%d items", ApClient_ItemCount());
  StopTimed(500);
  EXPECT(ApClient_GetRoom(NULL), "the room is gone after Stop");
}

static void Tls(const char *port, const char *ca_file) {
  char addr[128];
  Note("wss:// with the test CA");
  SetEnv("ZELDA3_AP_CAFILE", ca_file);
  snprintf(addr, sizeof(addr), "wss://localhost:%s", port);
  Basic(addr, "(wss)");

  Note("no scheme: ws:// fails, wss:// works");
  snprintf(addr, sizeof(addr), "localhost:%s", port);
  StartAs(addr, "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Connected, 10000))
    ExpectStatusText("(wss)");
  StopTimed(500);

  Note("without the CA, the certificate is rejected");
  SetEnv("ZELDA3_AP_CAFILE", NULL);
  snprintf(addr, sizeof(addr), "wss://localhost:%s", port);
  StartAs(addr, "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Retrying, 10000))
    ExpectStatusText("certificate");
  WaitText("Can't connect", 2000);
  StopTimed(500);

  Note("the certificate is for localhost, not 127.0.0.1");
  SetEnv("ZELDA3_AP_CAFILE", ca_file);
  snprintf(addr, sizeof(addr), "wss://127.0.0.1:%s", port);
  StartAs(addr, "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Retrying, 10000))
    ExpectStatusText("certificate");
  StopTimed(500);
}

static void Gap(const char *addr) {
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  WaitText("MOCK OK sync", 5000);
  WaitItems(3, 2000);
  Pump(200);
  EXPECT(ApClient_ItemCount() == 3, "%d items after the Sync", ApClient_ItemCount());
  ExpectItem(0, BASE + 0x49, -2, 0);
  ExpectItem(1, BASE + 0x0A, 2000, 2);
  ExpectItem(2, BASE + 0x12, 2001, 3);
  StopTimed(500);
}

static void Reconnect(const char *addr) {
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  int64 ids[2] = { BASE + 1, BASE + 2 };
  ApClient_CheckLocations(ids, 2);
  ApClient_SetGoalReached();
  if (!WaitStatus(kApStatus_Retrying, 5000))
    return;
  EXPECT(ApClient_GetRoom(NULL), "the room is gone while retrying");
  WaitText("Lost connection", 1000);
  ExpectStatusText("retrying in");
  uint32_t t0 = SDL_GetTicks();
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  Note("reconnected after %u ms", SDL_GetTicks() - t0);
  WaitText("MOCK OK resend", 5000);
  WaitText("Reconnected", 1000);
  WaitItems(4, 2000);
  Pump(200);
  EXPECT(ApClient_ItemCount() == 4, "%d items", ApClient_ItemCount());
  ExpectItem(0, BASE + 0x49, -2, 0);
  ExpectItem(1, BASE + 0x0A, 2000, 2);
  ExpectItem(2, BASE + 0x12, BASE + 1, 1);
  ExpectItem(3, BASE + 0x09, 2001, 3);
  StopTimed(500);
}

static void Refused(const char *addr) {
  static const char *const kCases[][2] = {
    { "Nobody", "no slot named \"Nobody\"" },
    { "Alice", "doesn't play" },
    { "VersionTest", "newer client version" },
    { "HandlingTest", "receives items" },
    { "UnknownTest", "SomethingNew" },
  };
  for (int i = 0; i < 5; i++) {
    Note("slot %s", kCases[i][0]);
    StartAs(addr, kCases[i][0], NULL, NULL);
    if (WaitStatus(kApStatus_Failed, 10000)) {
      ExpectStatusText(kCases[i][1]);
      Pump(100);
      int t = FindText(kCases[i][1]);
      EXPECT(t >= 0 && g_log[t].kind == kApText_Error, "no error line for %s", kCases[i][0]);
      EXPECT(!ApClient_GetRoom(NULL), "a room after a refusal");
    }
    StopTimed(500);
  }
}

static void Password(const char *addr) {
  StartAs(addr, "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Failed, 10000))
    ExpectStatusText("needs a password");
  StartAs(addr, "Link", "wrong", NULL);
  if (WaitStatus(kApStatus_Failed, 10000))
    ExpectStatusText("Wrong password");
  StartAs(addr, "Link", "secret", NULL);
  WaitStatus(kApStatus_Connected, 10000);
  StopTimed(500);
}

static void BigDataPackage(const char *addr) {
  uint32_t t0 = SDL_GetTicks();
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 20000))
    return;
  Note("connected after %u ms", SDL_GetTicks() - t0);
  WaitText("MOCK OK big", 5000);
  ExpectText("Received Lamp from Alice (Big Location number 12345 somewhere in a very large world map)",
             kApText_Progression);
  ExpectText("Sent Big Item number 29999 with a rather long name to take up room to Alice", kApText_Normal);
  StopTimed(500);
}

static void DeathLink(const char *addr) {
  char source[64];
  ApClient_SetDeathLink(true);
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  ExpectRoom(1);
  ApClient_SendDeath("Link fell into a pit");
  WaitText("MOCK OK death Link fell into a pit", 5000);
  Pump(300);
  EXPECT(ApClient_PollDeath(source, sizeof(source)) && !strcmp(source, "Alice"), "no death from Alice");
  EXPECT(!ApClient_PollDeath(source, sizeof(source)), "a second death (%s): our own echo?", source);
  ApClient_SetDeathLink(false);
  WaitText("MOCK OK tags AP", 5000);
  WaitText("MOCK OK after", 5000);
  Pump(200);
  EXPECT(!ApClient_PollDeath(source, sizeof(source)), "a death while DeathLink is off");
  ApClient_SendDeath("ignored");  // off: nothing goes out (the mock would complain)
  ApClient_SetDeathLink(true);
  WaitText("MOCK OK tags AP,DeathLink", 5000);
  Pump(300);
  StopTimed(500);
  ApClient_SetDeathLink(false);
}

static void Ping(const char *addr) {
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  uint32_t end = SDL_GetTicks() + 10000;
  while (FindText("MOCK OK pings answered") < 0 && (int32_t)(end - SDL_GetTicks()) > 0) {
    Drain();
    if (ApClient_GetStatus(NULL, 0) != kApStatus_Connected) {
      Fail("the connection broke: pings not answered?");
      break;
    }
    SDL_Delay(10);
  }
  WaitText("MOCK OK pings answered", 100);
  ExpectNoText("Lost connection");
  StopTimed(500);
}

static void Flood(const char *addr) {
  StartAs(addr, "Link", NULL, NULL);
  g_keep_texts = true;
  WaitStatus(kApStatus_Connected, 10000);
  SDL_Delay(1500);
  g_keep_texts = false;
  Drain();
  int first = FindText("line ");
  EXPECT(g_num_log == 32, "%d lines kept, not 32", g_num_log);
  EXPECT(first >= 0 && !strcmp(g_log[first].text, "line 18"), "oldest line kept: %s", first >= 0 ? g_log[first].text : "none");
  EXPECT(g_num_log && !strcmp(g_log[g_num_log - 1].text, "line 49"), "newest line: %s", g_num_log ? g_log[g_num_log - 1].text : "none");
  StopTimed(500);
}

// A server that takes the connection and never answers: stopping must not wait for it.
static void Hang(const char *port) {
  static const char *const kSchemes[] = { "ws://", "wss://", "" };
  for (int i = 0; i < 3; i++) {
    char addr[128];
    snprintf(addr, sizeof(addr), "%s127.0.0.1:%s", kSchemes[i], port);
    Note("%s", addr);
    StartAs(addr, "Link", NULL, NULL);
    Pump(400);
    EXPECT(ApClient_GetStatus(NULL, 0) == kApStatus_Connecting, "not connecting");
    StopTimed(300);
  }
}

static void NoServer(const char *port) {
  char addr[128];
  snprintf(addr, sizeof(addr), "127.0.0.1:%s", port);
  StartAs(addr, "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Retrying, 5000)) {
    ExpectStatusText("refused");
    ExpectStatusText("retrying in");
    WaitText("Can't connect to 127.0.0.1", 1000);
    EXPECT(g_num_log && g_log[0].kind == kApText_Error, "the first line isn't an error");
  }
  Pump(2500);  // the second attempt: no second line
  EXPECT(g_num_log == 1, "%d lines for one outage", g_num_log);
  StopTimed(300);

  StartAs("ws://no-such-host.invalid", "Link", NULL, NULL);
  if (WaitStatus(kApStatus_Retrying, 15000))
    ExpectStatusText("Can't find the server");
  StopTimed(300);
}

// The end of a TLS record arrives seconds after the rest: the network thread
// has to wait for it, not spin.
static void SlowTls(const char *port, const char *ca_file) {
  char addr[128];
  SetEnv("ZELDA3_AP_CAFILE", ca_file);
  snprintf(addr, sizeof(addr), "wss://localhost:%s", port);
  StartAs(addr, "Link", NULL, NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  double cpu = CpuSeconds();
  uint32_t t0 = SDL_GetTicks();
  if (WaitText("MOCK OK held", 15000)) {
    uint32_t wall = SDL_GetTicks() - t0;
    double used = CpuSeconds() - cpu;
    Note("%u ms for the held-back record, %.2f s of CPU time", wall, used);
    EXPECT(wall >= 2000, "the record wasn't held back (%u ms)", wall);
    EXPECT(used < wall / 1000.0 * 0.25, "%.2f s of CPU time in %u ms: the network thread spins", used, wall);
  }
  StopTimed(500);
}

// No scheme in the address: ws:// fails, wss:// works. When the connection
// breaks, someone in the middle shows a certificate we don't trust and
// offers plain ws://: the client must not take it.
static void Downgrade(const char *port, const char *ca_file) {
  char addr[128], text[256];
  SetEnv("ZELDA3_AP_CAFILE", ca_file);
  snprintf(addr, sizeof(addr), "localhost:%s", port);
  StartAs(addr, "Link", "secretpw", NULL);
  if (!WaitStatus(kApStatus_Connected, 10000))
    return;
  ExpectStatusText("(wss)");
  if (!WaitStatus(kApStatus_Retrying, 5000))
    return;
  // The next attempt (2 s later) meets the impostor: no connection again.
  uint32_t end = SDL_GetTicks() + 5000;
  bool cert = false;
  while ((int32_t)(end - SDL_GetTicks()) > 0) {
    Drain();
    ApStatus s = ApClient_GetStatus(text, sizeof(text));
    if (s == kApStatus_Connected) {
      Fail("connected again: %s", text);
      break;
    }
    cert |= strstr(text, "certificate") != NULL;
    SDL_Delay(10);
  }
  EXPECT(cert, "the status never named the certificate (\"%s\")", text);
  StopTimed(500);
}

// What no real server sends: a lot of games, a lot of data package, a
// message of millions of values, frames that never end. The client must
// stay quick to stop (and get through the first two).
static void Hostile(const char *port) {
  static const char *const kCases[] = { "roominfo", "datapackage", "numbers", "frames" };
  for (int i = 0; i < 4; i++) {
    char addr[128], ok[64];
    snprintf(addr, sizeof(addr), "ws://127.0.0.1:%s/%s", port, kCases[i]);
    Note("%s", kCases[i]);
    uint32_t t0 = SDL_GetTicks();
    StartAs(addr, "Link", NULL, NULL);
    if (WaitStatus(kApStatus_Connected, i < 2 ? 3000 : 10000))
      Note("connected after %u ms", SDL_GetTicks() - t0);
    if (i >= 2) {
      snprintf(ok, sizeof(ok), "MOCK OK %s", kCases[i]);
      WaitText(ok, 5000);
      Pump(300);  // the client is busy with it now
    }
    StopTimed(300);
  }
}

static void Addresses(void) {
  // What players paste: the room page's "/connect" line (the menu drops the
  // spaces), quotes, spaces and line breaks around it.
  static const char *const kPasted[][2] = {
    { "/connect archipelago.gg:38281", "archipelago.gg:38281" },
    { "'/connect archipelago.gg:38281'", "archipelago.gg:38281" },
    { "/connectarchipelago.gg:38281", "archipelago.gg:38281" },
    { "/Connect wss://localhost:1234", "localhost:1234" },
    { " archipelago.gg:12345 ", "archipelago.gg:12345" },
    { "\narchipelago.gg:12345\r\n", "archipelago.gg:12345" },
    { "archipelago.gg : 12345", "archipelago.gg:12345" },
    { "\"localhost:38281\"", "localhost:38281" },
  };
  for (size_t i = 0; i < sizeof(kPasted) / sizeof(kPasted[0]); i++) {
    char text[256], want[300];
    StartAs(kPasted[i][0], "Link", NULL, NULL);
    ApStatus s = ApClient_GetStatus(text, sizeof(text));
    snprintf(want, sizeof(want), "Connecting to %s...", kPasted[i][1]);
    EXPECT(s == kApStatus_Connecting && !strcmp(text, want), "\"%s\": status %d \"%s\", not \"%s\"", kPasted[i][0],
           s, text, want);
    ApClient_Stop();
  }
  static const char *const kCases[][3] = {
    { "https://archipelago.gg/room/abc", "Link", "room page" },
    { "ws://example.com:99999", "Link", "Bad port" },
    { "example.com:", "Link", "Bad port" },
    { "ftp://example.com", "Link", "Unknown kind of address" },
    { "", "Link", "Enter the server's address" },
    { "   ", "Link", "Enter the server's address" },
    { "Link:pw@example.com", "Link", "own fields" },
    { "[::1", "Link", "Bad server address" },
    { "localhost", "", "Enter your slot name" },
    { "localhost", "   ", "Enter your slot name" },
  };
  for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
    StartAs(kCases[i][0], kCases[i][1], NULL, NULL);
    EXPECT(ApClient_GetStatus(NULL, 0) == kApStatus_Failed, "\"%s\" isn't refused", kCases[i][0]);
    ExpectStatusText(kCases[i][2]);
    ApClient_Stop();
  }
  ApConfig empty = { NULL, NULL, NULL, NULL, NULL };
  ApClient_Start(&empty);
  EXPECT(ApClient_GetStatus(NULL, 0) == kApStatus_Failed, "an empty config isn't refused");
  ApClient_Stop();
}

// ---- against a real MultiServer ----

static void PrintRoom(void) {
  ApRoom r;
  if (ApClient_GetRoom(&r))
    Note("room: seed %s, slot %d (%s), team %d, slot_data version %d death_link %d goal %d, bind %08x",
         r.seed_name, r.slot, r.slot_name, r.team, r.slot_data_version, r.slot_data_death_link,
         r.slot_data_goal, r.bind_hash);
}

static void PrintItems(void) {
  for (int i = 0; i < ApClient_ItemCount(); i++) {
    int64 item, loc;
    int player;
    ApClient_GetItem(i, &item, &loc, &player);
    Note("item %d: %lld at %lld from slot %d", i, (long long)item, (long long)loc, player);
  }
}

static bool RealConnect(const char *addr, const char *slot, const char *game) {
  StartAs(addr, slot, NULL, game);
  if (!WaitStatus(kApStatus_Connected, 15000))
    return false;
  char text[256];
  ApClient_GetStatus(text, sizeof(text));
  Note("%s", text);
  PrintRoom();
  ApRoom r;
  ApClient_GetRoom(&r);
  char key[128];
  snprintf(key, sizeof(key), "%s/%d/7", r.seed_name, r.slot);
  EXPECT(r.bind_hash == Fnv(key), "bind hash");
  EXPECT(!strcmp(r.slot_name, slot), "slot name");
  return true;
}

static void RealChecks(const char *addr, const char *slot, const char *game, int argc, char **argv) {
  if (!RealConnect(addr, slot, game))
    return;
  int64 ids[256];
  int n = 0;
  for (int i = 0; i < argc && n < 256; i++)
    ids[n++] = strtoll(argv[i], NULL, 10);
  ApClient_CheckLocations(ids, n);
  uint32_t end = SDL_GetTicks() + 10000;
  int lines = 0;
  while ((int32_t)(end - SDL_GetTicks()) > 0) {
    Drain();
    lines = 0;
    for (int i = 0; i < g_num_log; i++)
      lines += !strncmp(g_log[i].text, "Sent ", 5) || !strncmp(g_log[i].text, "Found your own ", 15);
    if (lines >= n)
      break;
    SDL_Delay(10);
  }
  EXPECT(lines == n, "%d sent/found lines for %d checks", lines, n);
  // The goal's line may be pushed out of the client's 32 lines right away by
  // what the server releases and collects next: the server's log tells.
  ApClient_SetGoalReached();
  Pump(2000);
  PrintItems();
  StopTimed(500);
}

static void RealItems(const char *addr, const char *slot, const char *game, int min_items) {
  if (!RealConnect(addr, slot, game))
    return;
  WaitItems(min_items, 20000);
  WaitText("Received ", 5000);
  Pump(500);
  PrintItems();
  StopTimed(500);
}

static void RealDeathSend(const char *addr, const char *slot, const char *game) {
  ApClient_SetDeathLink(true);
  if (!RealConnect(addr, slot, game))
    return;
  Pump(3000);  // time for the other client to connect
  ApClient_SendDeath("fell off the board");
  Pump(3000);
  char source[64];
  EXPECT(!ApClient_PollDeath(source, sizeof(source)), "got a death (%s): our own echo?", source);
  StopTimed(500);
}

static void RealDeathWait(const char *addr, const char *slot, const char *game, const char *from) {
  ApClient_SetDeathLink(true);
  if (!RealConnect(addr, slot, game))
    return;
  char source[64] = "";
  uint32_t end = SDL_GetTicks() + 20000;
  bool got = false;
  while (!got && (int32_t)(end - SDL_GetTicks()) > 0) {
    Drain();
    got = ApClient_PollDeath(source, sizeof(source));
    SDL_Delay(10);
  }
  EXPECT(got && !strcmp(source, from), "no death from %s (got \"%s\")", from, source);
  if (got)
    Note("died with %s", source);
  EXPECT(!ApClient_PollDeath(source, sizeof(source)), "a second death");
  StopTimed(500);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: ap_client_test SCENARIO ARGS\n"
            "  basic|nocompress|gap|reconnect|refused|password|bigdp|deathlink|ping|flood ADDRESS\n"
            "  tls|slowtls|downgrade PORT CAFILE | hang|noserver|hostile PORT | addresses\n"
            "  real-checks ADDRESS SLOT GAME LOCATION...\n"
            "  real-items ADDRESS SLOT GAME MIN_ITEMS\n"
            "  real-death-send ADDRESS SLOT GAME\n"
            "  real-death-wait ADDRESS SLOT GAME FROM\n");
    return 2;
  }
  SDL_Init(0);
  const char *sc = argv[1], *a = argc > 2 ? argv[2] : "";
  printf("== %s\n", sc);
  fflush(stdout);
  if (!strcmp(sc, "basic") || !strcmp(sc, "nocompress"))
    Basic(a, "(ws)");
  else if (!strcmp(sc, "tls") && argc > 3)
    Tls(a, argv[3]);
  else if (!strcmp(sc, "gap"))
    Gap(a);
  else if (!strcmp(sc, "reconnect"))
    Reconnect(a);
  else if (!strcmp(sc, "refused"))
    Refused(a);
  else if (!strcmp(sc, "password"))
    Password(a);
  else if (!strcmp(sc, "bigdp"))
    BigDataPackage(a);
  else if (!strcmp(sc, "deathlink"))
    DeathLink(a);
  else if (!strcmp(sc, "ping"))
    Ping(a);
  else if (!strcmp(sc, "flood"))
    Flood(a);
  else if (!strcmp(sc, "hang"))
    Hang(a);
  else if (!strcmp(sc, "noserver"))
    NoServer(a);
  else if (!strcmp(sc, "slowtls") && argc > 3)
    SlowTls(a, argv[3]);
  else if (!strcmp(sc, "downgrade") && argc > 3)
    Downgrade(a, argv[3]);
  else if (!strcmp(sc, "hostile"))
    Hostile(a);
  else if (!strcmp(sc, "addresses"))
    Addresses();
  else if (!strcmp(sc, "real-checks") && argc > 4)
    RealChecks(a, argv[3], argv[4], argc - 5, argv + 5);
  else if (!strcmp(sc, "real-items") && argc > 5)
    RealItems(a, argv[3], argv[4], atoi(argv[5]));
  else if (!strcmp(sc, "real-death-send") && argc > 4)
    RealDeathSend(a, argv[3], argv[4]);
  else if (!strcmp(sc, "real-death-wait") && argc > 5)
    RealDeathWait(a, argv[3], argv[4], argv[5]);
  else {
    fprintf(stderr, "unknown scenario or missing arguments: %s\n", sc);
    return 2;
  }
  ApClient_Stop();
  printf("%s %s\n", g_failures ? "FAIL" : "PASS", sc);
  return g_failures;
}
