#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include "config.h"
#include "features.h"
#include "mp.h"
#include "instance.h"
#include "snes/ppu.h"
#include "randomizer.h"
#include "hd2d.h"
#include "session.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "bindings.h"
#include "assets.h"
#include "messaging.h"
#include "select_file.h"
#include "archipelago.h"

// ---------------------------------------------------------------------------
// 5x7 pixel font

typedef struct Glyph {
  char c;
  const char *rows[7];
} Glyph;

static const Glyph kGlyphs[] = {
  {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
  {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
  {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
  {'3', {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "}},
  {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
  {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
  {'6', {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}},
  {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
  {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
  {'9', {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}},
  {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
  {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
  {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
  {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
  {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
  {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
  {'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"}},
  {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
  {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
  {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
  {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
  {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
  {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
  {'N', {"#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"}},
  {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
  {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
  {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
  {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
  {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
  {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
  {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
  {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
  {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "}},
  {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
  {'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
  {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
  {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
  {',', {"     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "}},
  {':', {"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "}},
  {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
  {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
  {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
  {'(', {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}},
  {')', {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}},
  {'!', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}},
  {'?', {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}},
  {'%', {"##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##"}},
  {'<', {"   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "}},
  {'>', {" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "}},
  {'_', {"     ", "     ", "     ", "     ", "     ", "     ", "#####"}},
  {'\'', {"  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     "}},
  {'"', {" # # ", " # # ", "     ", "     ", "     ", "     ", "     "}},
  {'=', {"     ", "     ", "#####", "     ", "#####", "     ", "     "}},
  {'*', {"     ", "  #  ", "# # #", " ### ", "# # #", "  #  ", "     "}},
  {'[', {" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "}},
  {']', {" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "}},
  {'#', {" # # ", " # # ", "#####", " # # ", "#####", " # # ", " # # "}},
};

static uint8 g_font[128][7];

static void BuildFont() {
  for (size_t i = 0; i < countof(kGlyphs); i++) {
    for (int y = 0; y < 7; y++) {
      uint8 bits = 0;
      for (int x = 0; x < 5; x++)
        if (kGlyphs[i].rows[y][x] == '#')
          bits |= 0x10 >> x;
      g_font[(uint8)kGlyphs[i].c][y] = bits;
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing

typedef struct Canvas {
  uint32 *pixels;
  int pitch;  // in pixels
  int width, height;  // virtual (unscaled) size
  int scale;
} Canvas;

static Canvas g_cv;

static void FillRect(int x, int y, int w, int h, uint32 color) {
  int s = g_cv.scale;
  int x0 = IntMax(x, 0) * s, y0 = IntMax(y, 0) * s;
  int x1 = IntMin(x + w, g_cv.width) * s, y1 = IntMin(y + h, g_cv.height) * s;
  for (int yy = y0; yy < y1; yy++) {
    uint32 *row = g_cv.pixels + yy * g_cv.pitch;
    for (int xx = x0; xx < x1; xx++)
      row[xx] = color;
  }
}

static void ShadeRect(int x, int y, int w, int h) {
  int s = g_cv.scale;
  int x0 = IntMax(x, 0) * s, y0 = IntMax(y, 0) * s;
  int x1 = IntMin(x + w, g_cv.width) * s, y1 = IntMin(y + h, g_cv.height) * s;
  for (int yy = y0; yy < y1; yy++) {
    uint32 *row = g_cv.pixels + yy * g_cv.pitch;
    for (int xx = x0; xx < x1; xx++)
      row[xx] = (row[xx] >> 2) & 0x3f3f3f;
  }
}

static int TextWidth(const char *s) {
  return (int)strlen(s) * 6 - 1;
}

static void DrawText(int x, int y, const char *s, uint32 color) {
  for (; *s; s++, x += 6) {
    uint8 c = (uint8)*s;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c >= 128)
      continue;
    for (int r = 0; r < 7; r++) {
      uint8 bits = g_font[c][r];
      for (int b = 0; b < 5; b++) {
        if (bits & (0x10 >> b)) {
          FillRect(x + b + 1, y + r + 1, 1, 1, 0x000000);
          FillRect(x + b, y + r, 1, 1, color);
        }
      }
    }
  }
}

static const char *const kHeart[7] = {
  " ## ## ",
  "#######",
  "#######",
  "#######",
  " ##### ",
  "  ###  ",
  "   #   ",
};

// fill: 0 empty, 1 half, 2 full
static void DrawHeart(int x, int y, int fill, uint32 color) {
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 7; c++) {
      if (kHeart[r][c] != '#')
        continue;
      bool edge = r == 0 || c == 0 || c == 6 || kHeart[r - 1][c] != '#' || kHeart[r][c - 1] != '#' ||
                  kHeart[r][c + 1] != '#' || r == 6 || kHeart[r + 1][c] != '#';
      uint32 col;
      if (edge)
        col = 0x101010;
      else if (fill == 2 || (fill == 1 && c < 4))
        col = color;
      else
        col = 0x404040;
      FillRect(x + c, y + r, 1, 1, col);
    }
  }
}


// ---------------------------------------------------------------------------
// Menu model

enum {
  kScreen_None,
  kScreen_Main,
  kScreen_Multiplayer,
  kScreen_JoinAddress,
  kScreen_Seed,
  kScreen_Lan,
  kScreen_Video,
  kScreen_Hd2d,
  kScreen_Gameplay,
  kScreen_Controls,
  kScreen_Bindings,
  kScreen_Players,
  kScreen_Archipelago,
  kScreen_ApEdit,
};

enum {
  kAct_None,
  kAct_Select,
  kAct_Left,
  kAct_Right,
};

typedef struct MenuLine {
  char label[48];
  char value[48];
  bool selectable;
} MenuLine;

static struct {
  UiHostFuncs host;
  int screen;
  int cursor;
  int action;
  int nlines;
  MenuLine lines[24];
  char title[48];
  char footer[96];
  uint16 prev_inputs[4];
  int repeat_ctr;
  char join_addr[64];
  uint32 seed_edit;     // the seed being typed in
  char seed_note[64];   // the result of writing a spoiler log
  bool use_upnp;
  uint32 last_lan_refresh;
  char last_message[96];
  uint32 message_time;
  bool dirty_settings;
  bool headless;  // the test runner: settings files aren't written
  // Spectating while down: the player each local player watches (-1: the
  // default), and whether it was picked with L/R.
  int8 watch[4];
  bool want_quit_confirm;
  int owner;  // the local player whose buttons run the menu
  uint8 minimap[kMpMaxPlayers];   // per player: the mini map is shown
  uint8 map_btn_held[kSessionMaxLocal];
  uint8 map_btn_eaten[kSessionMaxLocal];
  uint16 last_fallback;
  // Controls screen
  int bind_player;
  int capture_button;  // SNES button waiting for a key/controller button, -1 = none
  uint32 capture_start;
  uint16 raw_inputs[4];
  // Multiplayer and Players screens
  char toast[96];
  uint32 toast_time;
  int copy_file;          // keep a copy: which of our files (0-2)
  bool copy_confirm;      // that file is in use: press again
  bool own_file_used[3];
  bool own_files_read;
  int kick_confirm;       // seat + 1 waiting for a second press
  int kick_owner;         // ... and the computer in that seat then
  // Archipelago: the field being typed (0 server, 1 slot, 2 password), and
  // the latest lines from the room.
  int ap_field;
  struct { char text[96]; int kind; uint32 time; } ap_lines[4];
} g_ui;

static bool Item(const char *label, const char *value) {
  int i = g_ui.nlines;
  if (i >= (int)countof(g_ui.lines))
    return false;
  g_ui.nlines++;
  snprintf(g_ui.lines[i].label, sizeof(g_ui.lines[i].label), "%s", label);
  snprintf(g_ui.lines[i].value, sizeof(g_ui.lines[i].value), "%s", value ? value : "");
  g_ui.lines[i].selectable = true;
  return false;
}

static void Label(const char *label) {
  int i = g_ui.nlines;
  if (i >= (int)countof(g_ui.lines))
    return;
  g_ui.nlines++;
  snprintf(g_ui.lines[i].label, sizeof(g_ui.lines[i].label), "%s", label);
  g_ui.lines[i].value[0] = 0;
  g_ui.lines[i].selectable = false;
}

// Returns the action for the item that was just added if it has the cursor.
static int Act() {
  return (g_ui.nlines - 1 == g_ui.cursor) ? g_ui.action : kAct_None;
}

static void Goto(int screen) {
  // Settings are kept when the menu closes (zelda3.coop.ini).
  if (screen == kScreen_None && g_ui.dirty_settings)
    Ui_SaveSettings();
  g_ui.screen = screen;
  g_ui.cursor = 0;
  g_ui.seed_note[0] = 0;
  g_ui.want_quit_confirm = false;
  g_ui.copy_confirm = false;
  g_ui.own_files_read = false;
  g_ui.kick_confirm = 0;
  if (screen == kScreen_JoinAddress || screen == kScreen_Seed || screen == kScreen_ApEdit)
    SDL_StartTextInput();
  else
    SDL_StopTextInput();
}

static const char *OnOff(bool b) { return b ? "ON" : "OFF"; }

static bool ToggleBit(uint32 *v, uint32 bit, int act) {
  if (act == kAct_Select || act == kAct_Left || act == kAct_Right) {
    *v ^= bit;
    g_ui.dirty_settings = true;
    return true;
  }
  return false;
}

static const char *AspectName() {
  int e = g_config.extended_aspect_ratio;
  if (e == 0) return "4:3 (original)";
  if (e == (224 * 16 / 10 - 256) / 2) return "16:10";
  if (e == (224 * 16 / 9 - 256) / 2) return "16:9";
  if (e == (224 * 18 / 9 - 256) / 2) return "18:9 (ultrawide)";
  return "custom";
}

static void CycleAspect(int dir) {
  static const int kVals[4] = { 0, (224 * 16 / 10 - 256) / 2, (224 * 16 / 9 - 256) / 2, (224 * 18 / 9 - 256) / 2 };
  int cur = 0;
  for (int i = 0; i < 4; i++)
    if (kVals[i] == g_config.extended_aspect_ratio)
      cur = i;
  cur = (cur + dir + 4) % 4;
  g_config.extended_aspect_ratio = kVals[cur];
  g_config.extend_y = false;
  if (kVals[cur])
    g_config.features0 |= kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;
  else
    g_config.features0 &= ~(kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes);
  g_ui.dirty_settings = true;
  if (g_ui.host.apply_display)
    g_ui.host.apply_display();
}

static void ScreenMain() {
  snprintf(g_ui.title, sizeof(g_ui.title), "PAUSE MENU");
  Item("Resume", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_None);
  Item("Multiplayer", ">");
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
  Item("Video and HD options", ">");
  if (Act() == kAct_Select)
    Goto(kScreen_Video);
  Item("Gameplay options", ">");
  if (Act() == kAct_Select)
    Goto(kScreen_Gameplay);
  Item("Controls", ">");
  if (Act() == kAct_Select)
    Goto(kScreen_Bindings);
  Item("Controls help", ">");
  if (Act() == kAct_Select)
    Goto(kScreen_Controls);
  Item("Save settings", g_ui.dirty_settings ? "unsaved changes" : "");
  if (Act() == kAct_Select)
    Ui_SaveSettings();
  Item(g_ui.want_quit_confirm ? "Quit - press again to confirm" : "Quit game", NULL);
  if (Act() == kAct_Select) {
    if (g_ui.want_quit_confirm && g_ui.host.quit)
      g_ui.host.quit();
    g_ui.want_quit_confirm = true;
  }
}

// Joins a game and remembers the address for next time.
static void JoinGame(const char *addr) {
  if (addr != g_ui.join_addr)
    snprintf(g_ui.join_addr, sizeof(g_ui.join_addr), "%s", addr);
  if (Session_Join(addr))
    Ui_SaveSettings();
}

// Where player p is, in words: a dungeon, a part of the overworld, indoors.
static void LocationName(int p, char *buf, size_t size) {
  InstPlace pl;
  const uint8 *ram = Inst_PlayerWorldRam(p);
  if (!ram || !Inst_PlayerPlace(p, &pl)) {
    snprintf(buf, size, "-");
    return;
  }
  static const char *const kDungeons[14] = {
    "Hyrule Castle", "Hyrule Castle", "Eastern Palace", "Desert Palace", "Castle Tower", "Swamp Palace",
    "Palace of Darkness", "Misery Mire", "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town",
    "Turtle Rock", "Ganon's Tower",
  };
  // The overworld areas everybody knows (the big ones, by their first screen).
  static const struct { uint8 area; const char *name; } kAreas[] = {
    {0x00, "Lost Woods"}, {0x03, "Death Mountain"}, {0x05, "Death Mountain"}, {0x07, "Death Mountain"},
    {0x13, "Sanctuary"}, {0x14, "Graveyard"}, {0x18, "Kakariko Village"}, {0x1b, "Hyrule Castle"},
    {0x1e, "Eastern Palace"}, {0x2c, "Link's House"}, {0x30, "Desert of Mystery"}, {0x35, "Lake Hylia"},
    {0x40, "Skull Woods"}, {0x43, "Dark Death Mountain"}, {0x45, "Dark Death Mountain"}, {0x47, "Turtle Rock"},
    {0x58, "Village of Outcasts"}, {0x5b, "Pyramid"}, {0x5e, "Palace of Darkness"}, {0x6c, "Bomb Shop"},
    {0x70, "Misery Mire"}, {0x75, "Ice Lake"},
  };
  if (pl.space >= 0x20000) {
    snprintf(buf, size, "indoors");
  } else if (pl.space >= 0x10000) {
    int d = (pl.space & 0xff) >> 1;
    snprintf(buf, size, "%s", d < 14 ? kDungeons[d] : "a dungeon");
  } else {
    uint8 area = ram[0x40A];  // overworld_area_index
    snprintf(buf, size, "%s", area & 0x40 ? "Dark World" : "Light World");
    for (size_t i = 0; i < countof(kAreas); i++)
      if (kAreas[i].area == area)
        snprintf(buf, size, "%s", kAreas[i].name);
  }
}

// A short message at the bottom of the screen from the menu itself.
static void Toast(const char *msg) {
  snprintf(g_ui.toast, sizeof(g_ui.toast), "%s", msg);
  g_ui.toast_time = SDL_GetTicks();
}

// Our own save file: is file 1-3 in use? (Read when the menu shows it.)
static void ReadOwnFiles() {
  memset(g_ui.own_file_used, 0, sizeof(g_ui.own_file_used));
  FILE *f = fopen("saves/sram.dat", "rb");
  uint8 sram[0x500 * 3];
  if (f && fread(sram, 1, sizeof(sram), f) == sizeof(sram))
    for (int i = 0; i < 3; i++)
      g_ui.own_file_used[i] = WORD(sram[i * 0x500 + 0x3e5]) == 0x55aa;
  if (f)
    fclose(f);
  g_ui.own_files_read = true;
}

static void ScreenMultiplayer() {
  const SessionStatus *st = Session_GetStatus();
  char buf[64];
  snprintf(g_ui.title, sizeof(g_ui.title), "MULTIPLAYER");
  if (st->mode == kSessionOffline) {
    Label("Status: playing on this computer");
  } else if (st->mode == kSessionHost) {
    snprintf(buf, sizeof(buf), "Hosting - LAN %s", st->host_addr);
    Label(buf);
    if (st->public_addr[0]) {
      snprintf(buf, sizeof(buf), "Internet: %s", st->public_addr);
      Label(buf);
    }
    if (st->upnp_failed) {
      snprintf(buf, sizeof(buf), "Router: forward UDP port %u", g_config.mp_port ? g_config.mp_port : kSessionPort);
      Label(buf);
    }
    // The room code: what to send the others (select to copy it).
    char code[24];
    bool internet;
    if (Session_GetRoomCode(code, sizeof(code), &internet)) {
      Item(internet ? "Room code" : "Room code (LAN)", code);
      if (Act() == kAct_Select) {
        SDL_SetClipboardText(code);
        Toast("Room code copied: paste it to your friends");
      }
    }
  } else {
    static const char *const kStates[] = { "idle", "connecting", "downloading", "connected", "reconnecting" };
    snprintf(buf, sizeof(buf), "Client: %s %s", kStates[st->client_state < 5 ? st->client_state : 0], st->host_addr);
    Label(buf);
    if (st->client_state == 4)
      snprintf(buf, sizeof(buf), "Seat kept %d:%02d more", st->reconnect_seconds_left / 60, st->reconnect_seconds_left % 60);
    else
      snprintf(buf, sizeof(buf), "Ping %d ms, buffer %d", st->ping_ms, st->frames_buffered);
    Label(buf);
  }
  snprintf(buf, sizeof(buf), "%d of %d", Inst_NumJoined(), kMpMaxPlayers);
  Item("Players", buf);
  if (Act() == kAct_Select)
    Goto(kScreen_Players);
  Item("Archipelago", st->mode == kSessionClient ? "host's room" : Ap_HostBind() ? "connected" :
       Ap_IsConnecting() ? "connecting" : "off");
  if (Act() == kAct_Select)
    Goto(kScreen_Archipelago);
  if (st->mode == kSessionOffline) {
    Item("Host a game", g_ui.use_upnp ? "with UPnP" : "LAN only");
    int a = Act();
    if (a == kAct_Left || a == kAct_Right) {
      g_ui.use_upnp = !g_ui.use_upnp;
      g_ui.dirty_settings = true;
    }
    else if (a == kAct_Select && Session_Host(g_config.mp_port, g_ui.use_upnp))
      Ui_SaveSettings();
    Item("Join by code or address", g_ui.join_addr);
    if (Act() == kAct_Select)
      Goto(kScreen_JoinAddress);
    Item("Find LAN games", ">");
    if (Act() == kAct_Select) {
      Session_RefreshLan();
      Goto(kScreen_Lan);
    }
  } else {
    Item(st->mode == kSessionHost ? "Stop hosting" : st->client_state == 4 ? "Stop reconnecting" : "Leave game", NULL);
    if (Act() == kAct_Select)
      Session_Disconnect();
  }
  // Someone else's game: keep a copy of it in one of our own files.
  if (Session_CanKeepCopy()) {
    if (!g_ui.own_files_read)
      ReadOwnFiles();
    int fi = g_ui.copy_file;
    snprintf(buf, sizeof(buf), "< file %d, %s >", fi + 1,
             g_ui.copy_confirm ? "replace? again" : g_ui.own_file_used[fi] ? "in use" : "empty");
    Item("Keep a copy in", buf);
    int a = Act();
    if (a == kAct_Left || a == kAct_Right) {
      g_ui.copy_file = (fi + (a == kAct_Left ? 2 : 1)) % 3;
      g_ui.copy_confirm = false;
    } else if (a == kAct_Select) {
      if (g_ui.own_file_used[fi] && !g_ui.copy_confirm) {
        g_ui.copy_confirm = true;
      } else {
        char msg[96];
        Session_KeepCopy(fi + 1, "saves/sram.dat", msg, sizeof(msg));
        Toast(msg);
        g_ui.copy_confirm = false;
        ReadOwnFiles();
      }
    }
  }
  // Input delay (only changed while not online: the frame timing depends on it)
  snprintf(buf, sizeof(buf), "%d frame%s", g_config.mp_input_delay, g_config.mp_input_delay == 1 ? "" : "s");
  Item("Online input delay", buf);
  int ad = Act();
  if ((ad == kAct_Left || ad == kAct_Right || ad == kAct_Select) && st->mode == kSessionOffline) {
    int d = g_config.mp_input_delay + (ad == kAct_Left ? -1 : 1);
    g_config.mp_input_delay = d < 0 ? 8 : d > 8 ? 0 : d;
    Session_SetInputDelay(g_config.mp_input_delay);
    g_ui.dirty_settings = true;
  }
  // Local players on this machine
  int next_local = -1;
  for (int i = 1; i < kSessionMaxLocal && next_local < 0; i++)
    if (!Session_IsLocalJoined(i))
      next_local = i;
  if (next_local >= 0) {
    snprintf(buf, sizeof(buf), "controller %d", next_local + 1);
    Item("Add local player", buf);
    if (Act() == kAct_Select)
      Session_LocalJoin(next_local);
  }
  int last_local = -1;
  for (int i = 1; i < kSessionMaxLocal; i++)
    if (Session_IsLocalJoined(i))
      last_local = i;
  if (last_local >= 0) {
    snprintf(buf, sizeof(buf), "controller %d", last_local + 1);
    Item("Remove local player", buf);
    if (Act() == kAct_Select)
      Session_LocalLeave(last_local);
  }
  if (st->mode != kSessionClient) {
    Item("Friendly fire", OnOff(Session_GetFriendlyFire()));
    int a = Act();
    if (a != kAct_None) {
      Session_SetFriendlyFire(!Session_GetFriendlyFire());
      g_config.mp_friendly_fire = Session_GetFriendlyFire();
      g_ui.dirty_settings = true;
    }
  } else {
    Label("Friendly fire");
    snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s (host decides)", OnOff(Inst_GetFriendlyFire()));
  }
  Item("Name tags", OnOff(g_config.mp_name_tags));
  if (Act() != kAct_None) {
    g_config.mp_name_tags ^= 1;
    g_ui.dirty_settings = true;
  }
  Item("Player colors", g_config.mp_colorblind ? "color-blind" : "standard");
  if (Act() != kAct_None) {
    g_config.mp_colorblind ^= 1;
    g_mp_color_scheme = g_config.mp_colorblind;
    g_ui.dirty_settings = true;
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "New controllers: press START to join");
}

// Who plays: names, where they are, how far away, and (hosting) removing one.
static void ScreenPlayers() {
  const SessionStatus *st = Session_GetStatus();
  snprintf(g_ui.title, sizeof(g_ui.title), "PLAYERS");
  SessionPlayer seats[kMpMaxPlayers];
  Session_GetPlayers(seats);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    const SessionPlayer *sp = &seats[p];
    char label[48], value[48], where[32];
    if (sp->state == kSeat_Open) {
      snprintf(label, sizeof(label), "P%d", p + 1);
      Label(label);
      snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "- open -");
      continue;
    }
    snprintf(label, sizeof(label), "P%d %s", p + 1, sp->name);
    if (sp->state == kSeat_Reconnecting) {
      snprintf(value, sizeof(value), "reconnecting %d:%02d", sp->seconds_left / 60, sp->seconds_left % 60);
    } else {
      LocationName(p, where, sizeof(where));
      char ping[16] = "";
      bool mine = false;
      for (int i = 0; i < kSessionMaxLocal; i++)
        mine |= st->local_slot[i] == p;
      if (sp->owner != 0 && !mine)
        snprintf(ping, sizeof(ping), "%dms ", sp->ping_ms);
      else if (sp->owner == 0 && st->mode == kSessionClient)
        snprintf(ping, sizeof(ping), "host ");
      snprintf(value, sizeof(value), "%s%s%s", ping, Inst_PlayerDowned(p) ? "DOWN " : "", where);
    }
    // Hosting: a remote player's line removes them (press twice, on the same
    // player: the question goes away when someone else takes the seat or
    // the cursor moves on).
    bool kickable = st->mode == kSessionHost && sp->owner != 0;
    if (g_ui.kick_confirm == p + 1 && (!kickable || sp->owner != g_ui.kick_owner))
      g_ui.kick_confirm = 0;
    if (kickable) {
      Item(label, g_ui.kick_confirm == p + 1 ? "remove? press again" : value);
      int a = Act();
      if (g_ui.kick_confirm == p + 1 && g_ui.cursor != g_ui.nlines - 1)
        g_ui.kick_confirm = 0;
      if (a == kAct_Select) {
        if (g_ui.kick_confirm == p + 1) {
          Session_Kick(p);
          g_ui.kick_confirm = 0;
        } else {
          g_ui.kick_confirm = p + 1;
          g_ui.kick_owner = sp->owner;
        }
      }
    } else {
      Label(label);
      snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s", value);
    }
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
  if (st->mode == kSessionHost)
    snprintf(g_ui.footer, sizeof(g_ui.footer), "Select a player twice to remove them");
}

// Typing in a seed: hex digits from the keyboard, or Left/Right on a digit.
static void ScreenSeed() {
  snprintf(g_ui.title, sizeof(g_ui.title), "ENTER A SEED");
  Label("Type the 8 hex digits, or");
  Label("change each digit with Left/Right");
  char buf[16];
  for (int d = 0; d < 8; d++) {
    int shift = (7 - d) * 4, v = (g_ui.seed_edit >> shift) & 15;
    char name[16];
    snprintf(name, sizeof(name), "Digit %d", d + 1);
    snprintf(buf, sizeof(buf), "%X", v);
    Item(name, buf);
    int a = Act();
    if (a == kAct_Left || a == kAct_Right) {
      v = (v + (a == kAct_Right ? 1 : 15)) & 15;
      g_ui.seed_edit = (g_ui.seed_edit & ~(15u << shift)) | (uint32)v << shift;
    }
  }
  snprintf(buf, sizeof(buf), "%08X", g_ui.seed_edit);
  Label("Seed");
  snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s", buf);
  Item("Use this seed for new games", NULL);
  if (Act() == kAct_Select && g_ui.seed_edit > kRando_RandomSeed) {
    Session_SetRandomizer(g_ui.seed_edit);
    g_config.randomizer = g_ui.seed_edit;
    g_ui.dirty_settings = true;
    Goto(kScreen_Gameplay);
    return;
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Gameplay);
  snprintf(g_ui.footer, sizeof(g_ui.footer), g_ui.seed_edit <= kRando_RandomSeed ? "Seeds start at 00000002" : "Same seed = same item places");
}

static void ScreenJoinAddress() {
  snprintf(g_ui.title, sizeof(g_ui.title), "JOIN A GAME");
  Label("Type or paste (Ctrl+V) the room code");
  Label("or the host's address, then ENTER");
  char buf[72];
  // (A long name: its end, where the typing happens.)
  const char *addr = g_ui.join_addr;
  if (strlen(addr) > 34)
    addr += strlen(addr) - 34;
  snprintf(buf, sizeof(buf), "%s%s", addr, (SDL_GetTicks() / 400) & 1 ? "_" : " ");
  Label(buf);
  Item("Connect", NULL);
  if (Act() == kAct_Select && g_ui.join_addr[0]) {
    JoinGame(g_ui.join_addr);
    Goto(kScreen_Multiplayer);
    return;
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "Like FW00-00DS-EB52, 192.168.1.20 or host.net:47474");
}

// Archipelago (archipelago.gg): the room to connect to.
static char *ApField(int i) {
  return i == 0 ? g_ap_settings.server : i == 1 ? g_ap_settings.slot : g_ap_settings.password;
}

static size_t ApFieldSize(int i) {
  return i == 0 ? sizeof(g_ap_settings.server) : i == 1 ? sizeof(g_ap_settings.slot) : sizeof(g_ap_settings.password);
}

static void ScreenArchipelago() {
  snprintf(g_ui.title, sizeof(g_ui.title), "ARCHIPELAGO");
  char buf[96];
  if (Session_GetStatus()->mode == kSessionClient) {
    Label("In an online game, the host");
    Label("connects to the Archipelago room.");
    Item("Back", NULL);
    if (Act() == kAct_Select)
      Goto(kScreen_Multiplayer);
    return;
  }
  // The status on up to two lines (the menu is narrow).
  Ap_StatusLine(buf, sizeof(buf));
  char *cut = strlen(buf) > 30 ? strrchr(buf, ' ') : NULL;
  while (cut && cut - buf > 30) {
    *cut = 0;
    char *prev = strrchr(buf, ' ');
    *cut = ' ';
    cut = prev;
  }
  if (cut) {
    *cut = 0;
    Label(buf);
    Label(cut + 1);
  } else {
    Label(buf);
  }
  bool busy = Ap_IsConnecting();
  static const char *const kFields[3] = { "Server", "Slot name", "Password" };
  for (int i = 0; i < 3; i++) {
    const char *v = ApField(i);
    Item(kFields[i], i == 2 ? (v[0] ? "****" : "none") : v[0] ? v : "-");
    if (Act() == kAct_Select && !busy) {
      g_ui.ap_field = i;
      Goto(kScreen_ApEdit);
      return;
    }
  }
  static const char *const kDeathLink[3] = { "as the room says", "ON", "OFF" };
  Item("DeathLink", kDeathLink[g_ap_settings.death_link % 3]);
  int a = Act();
  if (a != kAct_None) {
    g_ap_settings.death_link = (g_ap_settings.death_link + (a == kAct_Left ? 2 : 1)) % 3;
    g_ui.dirty_settings = true;
  }
  if (!busy) {
    Item("Connect", NULL);
    if (Act() == kAct_Select) {
      if (!g_ap_settings.server[0] || !g_ap_settings.slot[0]) {
        Toast("Enter the server and your slot name first");
      } else {
        Ap_Connect();
        Ui_SaveSettings();
      }
    }
  } else {
    Item("Disconnect", NULL);
    if (Act() == kAct_Select)
      Ap_Disconnect();
  }
  // What the game in RAM is, and what new files will be.
  if (Ap_IsApFile())
    Label(Ap_FileBind() == Ap_HostBind() ? "This game: a file of this room" :
          Ap_HostBind() ? "This game: a file of ANOTHER room" : "This game: an Archipelago file");
  else if (Ap_HostBind())
    Label("New files: files of this room");
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "Connect, then make a new file");
}

static void ScreenApEdit() {
  static const char *const kTitles[3] = { "ARCHIPELAGO SERVER", "SLOT NAME", "ROOM PASSWORD" };
  snprintf(g_ui.title, sizeof(g_ui.title), "%s", kTitles[g_ui.ap_field % 3]);
  Label(g_ui.ap_field == 0 ? "Type or paste (Ctrl+V) the address" :
        g_ui.ap_field == 1 ? "Your name in the room (as in your YAML)" : "Leave empty if the room has none");
  char buf[72];
  const char *v = ApField(g_ui.ap_field);
  if (strlen(v) > 34)
    v += strlen(v) - 34;
  char shown[40];
  size_t len = strlen(v) < sizeof(shown) - 1 ? strlen(v) : sizeof(shown) - 1;
  memcpy(shown, v, len);
  shown[len] = 0;
  if (g_ui.ap_field == 2)
    memset(shown, '*', strlen(shown));
  snprintf(buf, sizeof(buf), "%s%s", shown, (SDL_GetTicks() / 400) & 1 ? "_" : " ");
  Label(buf);
  Item("Done", NULL);
  if (Act() == kAct_Select) {
    Goto(kScreen_Archipelago);
    return;
  }
  if (g_ui.ap_field == 0)
    snprintf(g_ui.footer, sizeof(g_ui.footer), "Like archipelago.gg:38281 or localhost");
}

static void ScreenLan() {
  snprintf(g_ui.title, sizeof(g_ui.title), "LAN GAMES");
  uint32 now = SDL_GetTicks();
  if (now - g_ui.last_lan_refresh > 1000) {
    g_ui.last_lan_refresh = now;
    Session_RefreshLan();
  }
  SessionLanGame games[8];
  int n = Session_GetLanGames(games, 8);
  if (n == 0)
    Label("Searching...");
  for (int i = 0; i < n; i++) {
    char value[48];
    snprintf(value, sizeof(value), "%d/4  %s", games[i].players, games[i].addr);
    Item(games[i].name, value);
    if (Act() == kAct_Select) {
      JoinGame(games[i].addr);
      Goto(kScreen_Multiplayer);
      return;
    }
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
}

static void ScreenVideo() {
  snprintf(g_ui.title, sizeof(g_ui.title), "VIDEO AND HD OPTIONS");
  int a;
  Item("Widescreen", AspectName());
  a = Act();
  if (a == kAct_Left)
    CycleAspect(-1);
  else if (a == kAct_Right || a == kAct_Select)
    CycleAspect(1);
  Item("HD world map (mode 7)", OnOff(g_config.enhanced_mode7));
  if (Act() != kAct_None) {
    g_config.enhanced_mode7 ^= 1;
    g_ui.dirty_settings = true;
    if (g_ui.host.apply_display)
      g_ui.host.apply_display();
  }
  Item("Smooth scaling", OnOff(g_config.linear_filtering));
  if (Act() != kAct_None) {
    g_config.linear_filtering ^= 1;
    g_ui.dirty_settings = true;
    if (g_ui.host.apply_display)
      g_ui.host.apply_display();
  }
  Item("2D-HD look (beta)", OnOff(g_config.hd2d));
  if (Act() != kAct_None) {
    g_config.hd2d ^= 1;
    g_hd2d.enabled = g_config.hd2d;
    g_ui.dirty_settings = true;
  }
  Item("2D-HD settings", ">");
  if (Act() == kAct_Select || Act() == kAct_Right)
    Goto(kScreen_Hd2d);
  Item("No sprite flicker", OnOff(g_config.no_sprite_limits));
  if (Act() != kAct_None) {
    g_config.no_sprite_limits ^= 1;
    g_ui.dirty_settings = true;
    if (g_ui.host.apply_display)
      g_ui.host.apply_display();
  }
  Item("Dim flashing effects", OnOff(g_config.features0 & kFeatures0_DimFlashes));
  ToggleBit(&g_config.features0, kFeatures0_DimFlashes, Act());
  Item("Fullscreen", NULL);
  if (Act() == kAct_Select && g_ui.host.toggle_fullscreen)
    g_ui.host.toggle_fullscreen();
  Item("Window size", "< >");
  a = Act();
  if ((a == kAct_Left || a == kAct_Right) && g_ui.host.change_window_scale)
    g_ui.host.change_window_scale(a == kAct_Left ? -1 : 1);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s", g_config.output_method == kOutputMethod_OpenGL ? "OpenGL" :
           g_config.output_method == kOutputMethod_OpenGL_ES ? "OpenGL ES" :
           g_config.output_method == kOutputMethod_SDLSoftware ? "Software" : "SDL");
  Item("Renderer (restart)", buf);
  if (Act() != kAct_None) {
    g_config.output_method = g_config.output_method == kOutputMethod_OpenGL ? kOutputMethod_SDL : kOutputMethod_OpenGL;
    g_ui.dirty_settings = true;
  }
  Label("Shaders: zelda3.ini Shader= (OpenGL)");
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
}

// A 0-100 strength changed in steps of 10 with Left/Right.
static void Strength(const char *label, uint8 *config, uint8 *live) {
  char buf[16];
  snprintf(buf, sizeof(buf), "< %3d%% >", *config);
  Item(label, buf);
  int a = Act(), v = *config;
  if (a == kAct_Left || a == kAct_Right || a == kAct_Select) {
    v = a == kAct_Left ? v - 10 : v + 10;
    v = v < 0 ? 0 : v > 100 ? (a == kAct_Select ? 0 : 100) : v;
    *config = *live = (uint8)v;
    g_ui.dirty_settings = true;
  }
}

static void ScreenHd2d() {
  snprintf(g_ui.title, sizeof(g_ui.title), "2D-HD LOOK (BETA)");
  Item("2D-HD look", OnOff(g_config.hd2d));
  if (Act() != kAct_None) {
    g_config.hd2d ^= 1;
    g_hd2d.enabled = g_config.hd2d;
    g_ui.dirty_settings = true;
  }
  Strength("Depth of field", &g_config.hd2d_depth_of_field, &g_hd2d.depth_of_field);
  Strength("Lighting", &g_config.hd2d_lighting, &g_hd2d.lighting);
  Strength("Bloom", &g_config.hd2d_bloom, &g_hd2d.bloom);
  Strength("Color grade", &g_config.hd2d_grade, &g_hd2d.grade);
  Strength("Vignette", &g_config.hd2d_vignette, &g_hd2d.vignette);
  Item("See Links behind scenery", OnOff(g_config.hd2d_see_through));
  if (Act() != kAct_None) {
    g_hd2d.see_through = g_config.hd2d_see_through ^= 1;
    g_ui.dirty_settings = true;
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Video);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "Left/Right: change strength");
}

static void ScreenGameplay() {
  static const struct { const char *name; uint32 bit; } kFeatures[] = {
    {"Switch items with L/R", kFeatures0_SwitchLR},
    {"Turn while dashing", kFeatures0_TurnWhileDashing},
    {"Mirror to Dark World", kFeatures0_MirrorToDarkworld},
    {"Collect items with sword", kFeatures0_CollectItemsWithSword},
    {"Break pots with sword", kFeatures0_BreakPotsWithSword},
    {"No low health beep", kFeatures0_DisableLowHealthBeep},
    {"Skip intro on keypress", kFeatures0_SkipIntroOnKeypress},
    {"Show max items in yellow", kFeatures0_ShowMaxItemsInYellow},
    {"More active bombs", kFeatures0_MoreActiveBombs},
    {"Carry more rupees", kFeatures0_CarryMoreRupees},
    {"Bug fixes", kFeatures0_MiscBugFixes},
    {"Cancel bird travel", kFeatures0_CancelBirdTravel},
  };
  snprintf(g_ui.title, sizeof(g_ui.title), "GAMEPLAY OPTIONS");
  bool client = Session_GetStatus()->mode == kSessionClient;
  // The randomizer: Off -> random seed -> the seed of the game being played -> Off
  // (An Archipelago file's seed is of no use: the multiworld placed its items.)
  bool ap_file = Ap_IsApFile();
  uint32 rando = Session_GetRandomizer(), current = ap_file ? 0 : Rando_CurrentSeed();
  char buf[48];
  if (rando == kRando_Off)
    snprintf(buf, sizeof(buf), "off");
  else if (rando == kRando_RandomSeed)
    snprintf(buf, sizeof(buf), "on, new seed");
  else
    snprintf(buf, sizeof(buf), "on, seed %08X", rando);
  Item("Randomize new games", buf);
  int a = Act();
  if (a != kAct_None && !client) {
    uint32 choices[4] = { kRando_Off, kRando_RandomSeed };
    int n = 2, at = 0;
    if (current)
      choices[n++] = current;
    if (rando > kRando_RandomSeed && rando != current)
      choices[n++] = rando;
    for (int i = 0; i < n; i++)
      if (choices[i] == rando)
        at = i;
    uint32 next = choices[(at + (a == kAct_Left ? n - 1 : 1)) % n];
    Session_SetRandomizer(next);
    g_config.randomizer = next;
    g_ui.dirty_settings = true;
  }
  if (!client) {
    Item("Enter a seed", ">");
    if (Act() == kAct_Select) {
      g_ui.seed_edit = rando > kRando_RandomSeed ? rando : current;
      Goto(kScreen_Seed);
      return;
    }
  }
  if (ap_file)
    snprintf(buf, sizeof(buf), "Archipelago");
  else if (current)
    snprintf(buf, sizeof(buf), "randomized, seed %08X", current);
  else
    snprintf(buf, sizeof(buf), "not randomized");
  Label("This game");
  snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s", buf);
  if (current) {
    Item("Write spoiler log", NULL);
    if (Act() == kAct_Select) {
      char path[32];
      snprintf(path, sizeof(path), "spoiler_%08X.txt", current);
      if (Rando_WriteSpoiler(current, path))
        snprintf(g_ui.seed_note, sizeof(g_ui.seed_note), "Wrote %s", path);
      else
        snprintf(g_ui.seed_note, sizeof(g_ui.seed_note), "Couldn't write %s", path);
    }
  }
  Item("Start new games with all items", OnOff(Session_GetFullInventory()));
  if (Act() != kAct_None && !client) {
    g_config.full_inventory = !g_config.full_inventory;
    Session_SetFullInventory(g_config.full_inventory);
    g_ui.dirty_settings = true;
  }
  Item("Four Swords opening story", OnOff(Session_GetStory()));
  if (Act() != kAct_None && !client) {
    g_config.four_swords_story = !g_config.four_swords_story;
    Session_SetStory(g_config.four_swords_story);
    g_ui.dirty_settings = true;
  }
  Item("Four Swords title", OnOff(g_config.four_swords_title));
  if (Act() != kAct_None) {
    g_config.four_swords_title = !g_config.four_swords_title;
    g_ui.dirty_settings = true;
  }
  for (size_t i = 0; i < countof(kFeatures); i++) {
    Item(kFeatures[i].name, OnOff(g_config.features0 & kFeatures[i].bit));
    if (!client)
      ToggleBit(&g_config.features0, kFeatures[i].bit, Act());
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
  if (client)
    snprintf(g_ui.footer, sizeof(g_ui.footer), "In an online game the host's options are used");
  else if (g_ui.seed_note[0])
    snprintf(g_ui.footer, sizeof(g_ui.footer), "%s", g_ui.seed_note);
  else
    snprintf(g_ui.footer, sizeof(g_ui.footer), "Randomizing applies to new save files");
}

static void ScreenControls() {
  snprintf(g_ui.title, sizeof(g_ui.title), "CONTROLS");
  Label("Default keys: arrows, X=A Z=B S=X A=Y");
  Label("  Enter=Start  Right Shift=Select");
  Label("Change them in Pause menu > Controls");
  Label("Menu: Esc, controller Guide, or");
  Label("  Back+Start on any controller");
  Label("Drop in: Start on a new controller");
  Label("Revive: stand next to a downed Link");
  Label("While down: L/R picks who to watch,");
  Label("  A sends a teammate a heart");
  Label("Lift teammate: A, A again throws");
  Label("Heavy rock: lift it together (both A)");
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
}

static void ScreenBindings() {
  int p = g_ui.bind_player;
  BindProfile *bp = &g_bind[p];
  char buf[64];
  snprintf(g_ui.title, sizeof(g_ui.title), "CONTROLS - PLAYER %d", p + 1);
  snprintf(buf, sizeof(buf), "< P%d >", p + 1);
  Item("Player", buf);
  int a = Act();
  if (a == kAct_Left || a == kAct_Right || a == kAct_Select) {
    g_ui.bind_player = (p + (a == kAct_Left ? 3 : 1)) & 3;
    return;
  }
  if (bp->controller == kBindNoController)
    snprintf(buf, sizeof(buf), "none (keyboard only)");
  else
    snprintf(buf, sizeof(buf), "controller %d", bp->controller + 1);
  Item("Uses", buf);
  a = Act();
  if (a != kAct_None) {
    int c = bp->controller == kBindNoController ? -1 : bp->controller;
    c += a == kAct_Left ? -1 : 1;
    c = c < -1 ? 3 : c > 3 ? -1 : c;
    bp->controller = c < 0 ? kBindNoController : c;
    g_ui.dirty_settings = true;
  }
  for (int b = 0; b < kBindButtons; b++) {
    char keys[40], pad[40];
    Bindings_DescribeKeys(p, b, keys, sizeof(keys));
    Bindings_DescribePad(p, b, pad, sizeof(pad));
    if (g_ui.capture_button == b)
      snprintf(buf, sizeof(buf), "press key or button...");
    else
      snprintf(buf, sizeof(buf), "%.18s / %.16s", keys, pad);
    Item(Bindings_ButtonName(b), buf);
    a = Act();
    if (a == kAct_Select) {
      g_ui.capture_button = b;
      g_ui.capture_start = SDL_GetTicks();
    } else if (a == kAct_Left || a == kAct_Right) {
      Bindings_Clear(p, b);
      g_ui.dirty_settings = true;
    }
  }
  snprintf(buf, sizeof(buf), "%s, deadzone %d%%", bp->stick_moves ? "moves" : "off", bp->deadzone);
  Item("Left stick", buf);
  a = Act();
  if (a == kAct_Select) {
    bp->stick_moves = !bp->stick_moves;
    g_ui.dirty_settings = true;
  } else if (a == kAct_Left || a == kAct_Right) {
    bp->deadzone = (uint8)IntMin(IntMax(bp->deadzone + (a == kAct_Left ? -5 : 5), 5), 90);
    g_ui.dirty_settings = true;
  }
  Item("Rumble", OnOff(bp->rumble));
  if (Act() != kAct_None) {
    bp->rumble = !bp->rumble;
    g_ui.dirty_settings = true;
  }
  Item("Swap A/B and X/Y on controller", NULL);
  if (Act() == kAct_Select) {
    Bindings_SwapFaceButtons(p);
    g_ui.dirty_settings = true;
  }
  Item("Reset this player", NULL);
  if (Act() == kAct_Select) {
    Bindings_ResetPlayer(p);
    g_ui.dirty_settings = true;
  }
  // Live view of what this player is pressing.
  uint16 in = Bindings_GetInput(p);
  buf[0] = 0;
  for (int b = 0; b < kBindButtons; b++) {
    if (in & Bindings_ButtonBit(b)) {
      size_t n = strlen(buf);
      snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? " " : "", Bindings_ButtonName(b));
    }
  }
  char line[80];
  snprintf(line, sizeof(line), "Pressed: %s", buf[0] ? buf : "-");
  Label(line);
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
  snprintf(g_ui.footer, sizeof(g_ui.footer), g_ui.capture_button >= 0 ? "Esc cancels  -  Delete clears" :
           "A: assign   Left/Right: clear");
}

static void BuildScreen() {
  g_ui.nlines = 0;
  g_ui.title[0] = 0;
  g_ui.footer[0] = 0;
  switch (g_ui.screen) {
  case kScreen_Main: ScreenMain(); break;
  case kScreen_Multiplayer: ScreenMultiplayer(); break;
  case kScreen_JoinAddress: ScreenJoinAddress(); break;
  case kScreen_Seed: ScreenSeed(); break;
  case kScreen_Lan: ScreenLan(); break;
  case kScreen_Video: ScreenVideo(); break;
  case kScreen_Hd2d: ScreenHd2d(); break;
  case kScreen_Gameplay: ScreenGameplay(); break;
  case kScreen_Controls: ScreenControls(); break;
  case kScreen_Bindings: ScreenBindings(); break;
  case kScreen_Players: ScreenPlayers(); break;
  case kScreen_Archipelago: ScreenArchipelago(); break;
  case kScreen_ApEdit: ScreenApEdit(); break;
  }
}

// ---------------------------------------------------------------------------

void Ui_Init(const UiHostFuncs *host) {
  memset(&g_ui, 0, sizeof(g_ui));
  if (host)
    g_ui.host = *host;
  g_ui.headless = host == NULL;
  memset(g_ui.watch, -1, sizeof(g_ui.watch));
  g_ui.use_upnp = g_config.mp_use_upnp;
  g_ui.capture_button = -1;
  if (g_config.mp_last_join)
    snprintf(g_ui.join_addr, sizeof(g_ui.join_addr), "%s", g_config.mp_last_join);
  BuildFont();
}

bool Ui_IsOpen() {
  return g_ui.screen != kScreen_None;
}

// The menu only stops the game for someone playing alone on this computer.
// In co-op (and always online) the game goes on: only the player who opened
// it stands still, the others keep playing.
bool Ui_PausesGame() {
  return Ui_IsOpen() && !Session_IsNetplay() && Inst_NumJoined() <= 1;
}

// owner: the local player opening it (0-3), whose buttons then run the menu.
static void ToggleBy(int owner) {
  if (Ui_IsOpen()) {
    Goto(kScreen_None);
  } else {
    g_ui.owner = owner;
    Goto(kScreen_Main);
  }
}

static void Toggle() {
  ToggleBy(0);
}

void Ui_ToggleFromController(int controller) {
  int owner = 0;
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (g_bind[i].controller == controller && Session_IsLocalJoined(i)) {
      owner = i;
      break;
    }
  ToggleBy(owner);
}

int Ui_MenuOwner() {
  return Ui_IsOpen() ? g_ui.owner : -1;
}

int Ui_MenuController() {
  return Ui_IsOpen() && g_bind[g_ui.owner].controller != kBindNoController ? g_bind[g_ui.owner].controller : -1;
}

static void MoveCursor(int dir) {
  BuildScreen();
  if (g_ui.nlines == 0)
    return;
  for (int i = 0; i < g_ui.nlines; i++) {
    g_ui.cursor = (g_ui.cursor + dir + g_ui.nlines) % g_ui.nlines;
    if (g_ui.lines[g_ui.cursor].selectable)
      break;
  }
}

// Players that can be watched by p while p is down: p's own Link and every
// teammate who is standing.
static bool CanWatch(int p, int q) {
  return q == p || (Inst_IsJoined(q) && !Inst_PlayerDowned(q));
}

int Ui_WatchedPlayer(int local_index) {
  int p = Session_GetStatus()->local_slot[local_index];
  if (p < 0 || !Inst_PlayerDowned(p)) {
    if (local_index >= 0 && local_index < 4)
      g_ui.watch[local_index] = -1;  // back up: the next time starts fresh
    return p;
  }
  int w = g_ui.watch[local_index];
  if (w >= 0 && w < kMpMaxPlayers && CanWatch(p, w))
    return w;
  // By default a ghost in a paused world (nobody standing there) watches a
  // standing teammate, otherwise its own Link.
  return Inst_SpectateTarget(p);
}

// L / R flip through who a downed player watches.
static void UpdateWatch(int i, uint16 pressed) {
  int p = Session_GetStatus()->local_slot[i];
  if (p < 0 || !Inst_PlayerDowned(p) || !(pressed & 0xc00))
    return;
  int cur = Ui_WatchedPlayer(i), dir = (pressed & 0x400) ? -1 : 1;
  for (int k = 1; k <= kMpMaxPlayers; k++) {
    int q = (cur + dir * k + kMpMaxPlayers * 2) % kMpMaxPlayers;
    if (CanWatch(p, q)) {
      g_ui.watch[i] = q;
      return;
    }
  }
}

// The map button (X) goes round: mini map, full screen map, no map. The
// press that turns the mini map on doesn't reach the game; the next one
// opens the game's map (and closing that leaves no map).
static void CycleMapButton(uint16 *local_inputs) {
  const SessionStatus *st = Session_GetStatus();
  for (int i = 0; i < kSessionMaxLocal; i++) {
    int p = st->local_slot[i];
    bool x = (local_inputs[i] & 0x200) != 0, press = x && !g_ui.map_btn_held[i];
    g_ui.map_btn_held[i] = x;
    if (p < 0 || p >= kMpMaxPlayers || (Ui_IsOpen() && i == g_ui.owner))
      continue;
    if (Inst_PlayerViewModule(p) == 14) {
      // The game's own maps, menus and text boxes: X is the game's. (The mini
      // map is back after a text box or the item menu; the full map ends the
      // cycle, so X there turns it off.)
      if (press)
        g_ui.minimap[p] = 0;
    } else if (press && !g_ui.minimap[p]) {
      g_ui.minimap[p] = 1;
      g_ui.map_btn_eaten[i] = 1;
    } else if (press) {
      g_ui.minimap[p] = 0;  // on to the full map: the game sees this press
    }
    if (!x)
      g_ui.map_btn_eaten[i] = 0;
    if (g_ui.map_btn_eaten[i])
      local_inputs[i] &= ~0x200;
  }
}

void Ui_Update(uint16 *local_inputs, uint16 fallback) {
  CycleMapButton(local_inputs);
  // Back + Start on any controller toggles the menu.
  uint16 all = 0, pressed = 0;
  for (int i = 0; i < 4; i++) {
    uint16 now = local_inputs[i];
    g_ui.raw_inputs[i] = now;
    if ((now & 0xc) == 0xc && (g_ui.prev_inputs[i] & 0xc) != 0xc && (!Ui_IsOpen() || i == g_ui.owner))
      ToggleBy(i);
    if (!Ui_IsOpen() || i != g_ui.owner)
      UpdateWatch(i, now & ~g_ui.prev_inputs[i]);
    else
      pressed |= now & ~g_ui.prev_inputs[i], all |= now;
    g_ui.prev_inputs[i] = now;
  }
  // Arrows/Enter/Backspace and the d-pad always work in the menu.
  pressed |= fallback & ~g_ui.last_fallback;
  all |= fallback;
  g_ui.last_fallback = fallback;
  if (!Ui_IsOpen())
    return;
  // While waiting for a key to assign, inputs only go to the assignment.
  if (g_ui.capture_button >= 0) {
    local_inputs[g_ui.owner] = 0;
    return;
  }

  // Auto repeat for held directions.
  if (all & 0xf0) {
    if (++g_ui.repeat_ctr > 20 && (g_ui.repeat_ctr & 3) == 0)
      pressed |= all & 0xf0;
  } else {
    g_ui.repeat_ctr = 0;
  }

  // Make sure the cursor is on something selectable.
  BuildScreen();
  if (g_ui.cursor >= g_ui.nlines || !g_ui.lines[g_ui.cursor].selectable)
    MoveCursor(1);

  if (pressed & 0x10)
    MoveCursor(-1);
  if (pressed & 0x20)
    MoveCursor(1);
  g_ui.action = (pressed & 0x100) ? kAct_Select :
                (pressed & 0x40) ? kAct_Left : (pressed & 0x80) ? kAct_Right : kAct_None;
  if (pressed & 1) {  // B goes back
    if (g_ui.screen == kScreen_Main)
      Goto(kScreen_None);
    else
      Goto(g_ui.screen == kScreen_JoinAddress || g_ui.screen == kScreen_Lan ||
           g_ui.screen == kScreen_Archipelago ? kScreen_Multiplayer :
           g_ui.screen == kScreen_ApEdit ? kScreen_Archipelago :
           g_ui.screen == kScreen_Hd2d ? kScreen_Video : kScreen_Main);
    g_ui.action = kAct_None;
  }
  if (g_ui.action != kAct_None && Ui_IsOpen()) {
    BuildScreen();
    g_ui.action = kAct_None;
  }
  // The player using the menu stands still meanwhile; the others play on.
  local_inputs[g_ui.owner] = 0;
}

bool Ui_HandleKey(int key, bool pressed) {
  if (!pressed)
    return false;
  if (g_ui.screen == kScreen_Bindings && g_ui.capture_button >= 0) {
    if (key == SDLK_DELETE)
      Bindings_Clear(g_ui.bind_player, g_ui.capture_button);
    else if (key != SDLK_ESCAPE)
      Bindings_SetKey(g_ui.bind_player, g_ui.capture_button, key);
    g_ui.capture_button = -1;
    g_ui.dirty_settings = true;
    return true;
  }
  if (g_ui.screen == kScreen_Seed && key == SDLK_BACKSPACE) {
    g_ui.seed_edit >>= 4;
    return true;
  }
  if (key == SDLK_ESCAPE) {
    if (g_ui.screen == kScreen_Seed)
      Goto(kScreen_Gameplay);
    else if (g_ui.screen == kScreen_JoinAddress)
      Goto(kScreen_Multiplayer);
    else if (g_ui.screen == kScreen_ApEdit)
      Goto(kScreen_Archipelago);
    else
      Toggle();
    return true;
  }
  if (g_ui.screen == kScreen_ApEdit) {
    char *v = ApField(g_ui.ap_field);
    size_t n = strlen(v);
    if (key == SDLK_BACKSPACE) {
      if (n) {
        v[n - 1] = 0;
        g_ui.dirty_settings = true;
      }
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      Goto(kScreen_Archipelago);
    } else if (key == SDLK_v && (SDL_GetModState() & KMOD_CTRL)) {
      char *clip = SDL_GetClipboardText();
      if (clip) {
        Ui_HandleText(clip);
        SDL_free(clip);
      }
    }
    return true;  // swallow other keys while typing
  }
  if (g_ui.screen == kScreen_JoinAddress) {
    size_t n = strlen(g_ui.join_addr);
    if (key == SDLK_BACKSPACE) {
      if (n)
        g_ui.join_addr[n - 1] = 0;
      return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      if (n) {
        JoinGame(g_ui.join_addr);
        Goto(kScreen_Multiplayer);
      }
      return true;
    }
    if (key == SDLK_v && (SDL_GetModState() & KMOD_CTRL)) {
      char *clip = SDL_GetClipboardText();
      if (clip) {
        Ui_HandleText(clip);
        SDL_free(clip);
      }
      return true;
    }
    return true;  // swallow other keys while typing
  }
  return false;
}

bool Ui_HandlePadButton(int controller, int pad_button) {
  if (g_ui.screen != kScreen_Bindings || g_ui.capture_button < 0)
    return false;
  // Ignore the press that started the capture.
  if (SDL_GetTicks() - g_ui.capture_start < 250)
    return true;
  if (pad_button != kGamepadBtn_Guide)
    Bindings_SetPad(g_ui.bind_player, g_ui.capture_button, pad_button);
  g_ui.capture_button = -1;
  g_ui.dirty_settings = true;
  return true;
}

void Ui_HandleText(const char *text) {
  if (g_ui.screen == kScreen_Seed) {
    for (; *text; text++) {
      char c = *text;
      int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (v >= 0)
        g_ui.seed_edit = g_ui.seed_edit << 4 | v;
    }
    return;
  }
  if (g_ui.screen == kScreen_ApEdit) {
    // Printable ASCII (the overlay font has nothing else); a pasted
    // address loses its spaces and line breaks.
    char *v = ApField(g_ui.ap_field);
    size_t n = strlen(v), size = ApFieldSize(g_ui.ap_field);
    for (; *text && n < size - 1; text++) {
      char c = *text;
      if (c > 32 && c < 127 || (c == ' ' && g_ui.ap_field != 0)) {
        v[n++] = c;
        g_ui.dirty_settings = true;  // (kept even if the screen is left with B)
      }
    }
    v[n] = 0;
    return;
  }
  if (g_ui.screen != kScreen_JoinAddress)
    return;
  size_t n = strlen(g_ui.join_addr);
  for (; *text && n < sizeof(g_ui.join_addr) - 1; text++) {
    char c = *text;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == ':' || c == '-')
      g_ui.join_addr[n++] = c;
  }
  g_ui.join_addr[n] = 0;
}

// ---------------------------------------------------------------------------
// Overlay rendering

static UiView g_views[4];
static int g_nviews;

void Ui_SetViews(const UiView *views, int n) {
  g_nviews = IntMin(n, 4);
  memcpy(g_views, views, g_nviews * sizeof(UiView));
}

// A filled triangle with its tip at (px, py), pointing along (ux, uy).
static void DrawArrow(int px, int py, float ux, float uy, int size, uint32 color) {
  for (int y = py - size; y <= py + size; y++) {
    for (int x = px - size; x <= px + size; x++) {
      float a = (x - px) * ux + (y - py) * uy;     // along the arrow (tip at 0)
      float b = -(x - px) * uy + (y - py) * ux;    // across it
      if (a <= 0 && a >= -size && fabsf(b) <= -a * 0.6f + 0.5f)
        FillRect(x, y, 1, 1, color);
    }
  }
}

// An arrow at the edge of a pane pointing at (sx, sy) (pane pixels, out of
// view), with a label behind it.
static void DrawEdgeArrow(const UiView *view, int sx, int sy, uint32 color, const char *label) {
  float cx = view->w * 0.5f, cy = view->h * 0.5f, dx = sx - cx, dy = sy - cy;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1)
    return;
  float ux = dx / len, uy = dy / len;
  // Where the line from the middle of the screen leaves it (a margin in).
  float t = 1e9f;
  if (fabsf(dx) > 0.01f) t = fminf(t, (cx - 10) / fabsf(dx));
  if (fabsf(dy) > 0.01f) t = fminf(t, (cy - 10) / fabsf(dy));
  int px = view->x + (int)(cx + dx * t), py = view->y + (int)(cy + dy * t);
  DrawArrow(px, py, ux, uy, 8, 0x000000);
  DrawArrow(px - (int)(ux * 1.5f), py - (int)(uy * 1.5f), ux, uy, 6, color);
  int tw = TextWidth(label);
  int lx = px - (int)(ux * 16) - tw / 2, ly = py - (int)(uy * 16) - 3;
  lx = IntMax(view->x + 1, IntMin(lx, view->x + view->w - tw - 1));
  ly = IntMax(view->y + 1, IntMin(ly, view->y + view->h - 8));
  ShadeRect(lx - 1, ly - 1, tw + 2, 9);
  DrawText(lx, ly, label, color);
}

// Is player p looking at the item menu, a map or the flute's map? They fill
// the screen, so nothing of the world is drawn over them.
static bool ViewIsFullScreenMenu(int p) {
  uint8 m = Inst_PlayerViewModule(p), s = Inst_PlayerViewSubmodule(p);
  return m == 14 && (s == 1 || s == 3 || s == 7 || s == 10);
}

// Arrows at the edge of each screen pointing at teammates who are out of
// view (in this world or another one), as long as they are somewhere the
// direction means something: the same overworld, dungeon or room.
static void DrawTeammateArrows() {
  if (Inst_NumJoined() < 2)
    return;
  for (int v = 0; v < g_nviews; v++) {
    const UiView *view = &g_views[v];
    InstPlace me, them;
    if (view->player < 0 || !Inst_PlayerPlace(view->player, &me))
      continue;
    uint8 m = Inst_PlayerViewModule(view->player);
    if (m < 6 || m == 0x14 || m == 0x1a || ViewIsFullScreenMenu(view->player))
      continue;
    int extra = (view->w - 256) / 2;  // widescreen: columns left of the camera
    for (int q = 0; q < kMpMaxPlayers; q++) {
      if (q == view->player || !Inst_PlayerPlace(q, &them) || them.space != me.space)
        continue;
      int sx = (int16)(them.x - me.cam_x) + extra, sy = (int16)(them.y - me.cam_y);
      if (sx >= -4 && sx < view->w + 4 && sy >= -4 && sy < view->h + 4)
        continue;  // on screen
      char label[4];
      snprintf(label, sizeof(label), "P%d", q + 1);
      DrawEdgeArrow(view, sx, sy, them.downed ? 0xa0a0a0 : Mp_PlayerColorRgb(q), label);
    }
  }
}

// A ring of 2x2 dots, clipped to the pane.
static void DrawRing(const UiView *view, int cx, int cy, int r, uint32 color) {
  int n = 8 + r * 4;
  for (int i = 0; i < n; i++) {
    float a = i * 6.2831853f / n;
    int x = cx + (int)lroundf(cosf(a) * r), y = cy + (int)lroundf(sinf(a) * r);
    if (x >= view->x && x + 2 <= view->x + view->w && y >= view->y && y + 2 <= view->y + view->h)
      FillRect(x, y, 2, 2, color);
  }
}

static void PingLabel(char *buf, size_t n, int q, int kind) {
  snprintf(buf, n, kind ? "P%d HELP" : "P%d!", q + 1);
}

// Ping markers: a pulsing ring where a player pinged (a blinking one for
// "help"), or an arrow at the edge of the screen pointing at it, for
// everybody in the same overworld, dungeon or house.
static void DrawPings() {
  if (Inst_NumJoined() < 2)
    return;
  for (int v = 0; v < g_nviews; v++) {
    const UiView *view = &g_views[v];
    InstPlace me;
    if (view->player < 0 || !Inst_PlayerPlace(view->player, &me))
      continue;
    uint8 m = Inst_PlayerViewModule(view->player);
    if (m != 7 && m != 9)
      continue;
    int extra = (view->w - 256) / 2;
    for (int q = 0; q < kMpMaxPlayers; q++) {
      InstPing pg;
      if (!Inst_PlayerPing(q, &pg) || pg.space != me.space)
        continue;
      if (pg.ttl < 30 && (pg.ttl & 4))
        continue;  // fading out
      int age = kMpPingFrames - pg.ttl;
      uint32 color = pg.kind && (pg.ttl & 8) ? 0xffffff : Mp_PlayerColorRgb(q);
      char label[12];
      PingLabel(label, sizeof(label), q, pg.kind);
      int sx = (int16)(pg.x - me.cam_x) + extra, sy = (int16)(pg.y - me.cam_y);
      if (sx < 2 || sx > view->w - 3 || sy < 2 || sy > view->h - 3) {
        DrawEdgeArrow(view, sx, sy, color, label);
        continue;
      }
      int px = view->x + sx, py = view->y + sy;
      // Two rings that keep growing out from the spot, and the spot itself.
      for (int k = 0; k < 2; k++) {
        int r = 3 + ((age + k * 15) % 30) / 2;
        DrawRing(view, px - 1, py, r + 1, 0x000000);
        DrawRing(view, px - 1, py - 1, r, color);
      }
      FillRect(px - 2, py - 2, 4, 4, 0x000000);
      FillRect(px - 1, py - 1, 2, 2, color);
      int tw = TextWidth(label);
      int lx = IntMax(view->x + 1, IntMin(px - tw / 2, view->x + view->w - tw - 1));
      int ly = py - 28 >= view->y + 1 ? py - 28 : IntMin(py + 20, view->y + view->h - 9);
      ShadeRect(lx - 1, ly - 1, tw + 2, 9);
      DrawText(lx, ly, label, color);
    }
  }
}

// ---------------------------------------------------------------------------
// Mini map. The world map is built once from the game's own mode 7 map
// (its 8x8 tiles and tilemap): 64x64 tiles, 512x512 pixels, one pixel per
// 8 pixels of the overworld.

static uint32 *g_world_map[2];  // light, dark (0xRRGGBB)

static uint32 SnesColor(uint16 c) {
  int r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
  return (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

static const uint32 *WorldMapImage(bool dark) {
  if (g_world_map[dark])
    return g_world_map[dark];
  uint32 *img = malloc(512 * 512 * 4);
  if (!img)
    return NULL;
  // The tilemap as the game lays it out in video memory (128 tiles wide):
  // the light world in four 32x32 quarters; the dark world replaces the
  // middle 32x32 tiles.
  static uint8 tilemap[128 * 128];
  memset(tilemap, 0xef, sizeof(tilemap));
  static const uint16 kQuarters[4] = { 0, 0x20, 0x1000, 0x1020 };
  const uint8 *src = kLightOverworldTilemap;
  for (int q = 0; q < 4; q++)
    for (int row = 0; row < 32; row++, src += 32)
      memcpy(tilemap + kQuarters[q] + row * 128, src, 32);
  if (dark)
    for (int row = 0; row < 32; row++)
      memcpy(tilemap + 0x810 + row * 128, kDarkOverworldTilemap + row * 32, 32);
  const uint16 *pal = kOverworldMapPaletteData + (dark ? 0x80 : 0);
  for (int y = 0; y < 512; y++) {
    for (int x = 0; x < 512; x++) {
      uint8 t = tilemap[(y >> 3) * 128 + (x >> 3)];
      uint8 c = kOverworldMapGfx[t * 64 + (y & 7) * 8 + (x & 7)];
      img[y * 512 + x] = SnesColor(pal[c]);
    }
  }
  return g_world_map[dark] = img;
}

void Ui_DebugWorldMap(const char *file, bool dark) {
  const uint32 *img = WorldMapImage(dark);
  FILE *f = fopen(file, "wb");
  if (!f || !img)
    return;
  fprintf(f, "P6\n512 512\n255\n");
  for (int i = 0; i < 512 * 512; i++) {
    uint8 rgb[3] = { img[i] >> 16, img[i] >> 8, img[i] };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void DrawDot(int x, int y, uint32 color, bool me) {
  FillRect(x - 2, y - 2, 5, 5, me && (SDL_GetTicks() / 250 & 1) ? 0xffffff : 0x000000);
  FillRect(x - 1, y - 1, 3, 3, color);
}

// A ping on the mini map: a small hollow square (it blinks).
static void DrawPingMark(int x, int y, uint32 color) {
  FillRect(x - 3, y - 3, 7, 7, 0x000000);
  FillRect(x - 2, y - 2, 5, 5, color);
  FillRect(x - 1, y - 1, 3, 3, 0x000000);
}

enum { kMiniW = 72, kMiniH = 60 };

static void DrawMiniMap(const UiView *view) {
  int p = view->player;
  InstPlace me, them;
  if (p < 0 || !g_ui.minimap[p] || !Inst_PlayerPlace(p, &me))
    return;
  int x0 = view->x + view->w - kMiniW - 3, y0 = view->y + 30;
  // Nothing to show in a house or cave, or without this floor's rooms: no box either.
  const uint32 *img = NULL;
  const uint8 *ram = NULL, *rooms = NULL;
  if (me.space < 0x10000) {
    img = WorldMapImage(me.space & 0x40);
    if (!img)
      return;
  } else {
    ram = Inst_PlayerWorldRam(p);
    if (!ram || me.space >= 0x20000 || !(rooms = DungMap_FloorRooms(me.space & 0xff, ram[0xA4])))
      return;
  }
  ShadeRect(x0 - 2, y0 - 2, kMiniW + 4, kMiniH + 4);
  if (me.space < 0x10000) {
    // Overworld: the game's world map (1 pixel = 16), around the player.
    int cx = 128 + me.x / 16, cy = 128 + me.y / 16;  // map pixels
    int left = cx - kMiniW / 2, top = cy - kMiniH / 2;
    for (int y = 0; y < kMiniH; y++) {
      int my = top + y;
      for (int x = 0; x < kMiniW; x++) {
        int mx = left + x;
        FillRect(x0 + x, y0 + y, 1, 1, mx >= 0 && mx < 512 && my >= 0 && my < 512 ? img[my * 512 + mx] : 0);
      }
    }
    for (int q = kMpMaxPlayers - 1; q >= -1; q--) {
      int who = q < 0 ? p : q;
      if ((q == p) || !Inst_PlayerPlace(who, &them) || them.space != me.space)
        continue;
      int dx = 128 + them.x / 16 - left, dy = 128 + them.y / 16 - top;
      if (dx >= 2 && dx < kMiniW - 2 && dy >= 2 && dy < kMiniH - 2)
        DrawDot(x0 + dx, y0 + dy, them.downed ? 0xa0a0a0 : Mp_PlayerColorRgb(who), who == p);
    }
    for (int q = 0; q < kMpMaxPlayers; q++) {
      InstPing pg;
      if (!Inst_PlayerPing(q, &pg) || pg.space != me.space || (pg.ttl & 8))
        continue;
      int dx = 128 + pg.x / 16 - left, dy = 128 + pg.y / 16 - top;
      if (dx >= 3 && dx < kMiniW - 3 && dy >= 3 && dy < kMiniH - 3)
        DrawPingMark(x0 + dx, y0 + dy, Mp_PlayerColorRgb(q));
    }
  } else {
    // In a dungeon: this floor's rooms as the dungeon map lays them out
    // (5x5), the ones seen filled in (all of them with the map).
    int palace = me.space & 0xff;
    bool have_map = WORD(ram[0xF368]) & kUpperBitmasks[(palace >> 1) & 15];  // link_dungeon_map
    uint8 room_here = ram[0xA0];  // dungeon_room_index (low byte)
    int cw = kMiniW / 5, ch = kMiniH / 5;
    for (int k = 0; k < 25; k++) {
      uint8 r = rooms[k];
      if (r == 0x0f)
        continue;
      bool seen = WORD(ram[0xF000 + r * 2]) & 0xf, here = r == room_here;  // save_dung_info: quadrants seen
      if (seen || here || have_map)
        FillRect(x0 + (k % 5) * cw + 1, y0 + (k / 5) * ch + 1, cw - 2, ch - 2,
                 here ? 0x8090c0 : seen ? 0x58607a : 0x30343c);
    }
    for (int q = kMpMaxPlayers - 1; q >= -1; q--) {
      int who = q < 0 ? p : q;
      const uint8 *qr = Inst_PlayerWorldRam(who);
      if (q == p || !Inst_PlayerPlace(who, &them) || them.space != me.space || !qr || qr[0xA4] != ram[0xA4])
        continue;
      int k = 0;
      while (k < 25 && rooms[k] != qr[0xA0])
        k++;
      if (k == 25)
        continue;
      DrawDot(x0 + (k % 5) * cw + 2 + (them.x & 511) * (cw - 4) / 512, y0 + (k / 5) * ch + 2 + (them.y & 511) * (ch - 4) / 512,
              them.downed ? 0xa0a0a0 : Mp_PlayerColorRgb(who), who == p);
    }
    for (int q = 0; q < kMpMaxPlayers; q++) {
      InstPing pg;
      const uint8 *qr = Inst_PlayerWorldRam(q);
      if (!Inst_PlayerPing(q, &pg) || pg.space != me.space || (pg.ttl & 8) || !qr || qr[0xA4] != ram[0xA4])
        continue;
      int k = 0;
      while (k < 25 && rooms[k] != qr[0xA0])
        k++;
      if (k < 25)
        DrawPingMark(x0 + (k % 5) * cw + 2 + (pg.x & 511) * (cw - 4) / 512, y0 + (k / 5) * ch + 2 + (pg.y & 511) * (ch - 4) / 512,
                     Mp_PlayerColorRgb(q));
    }
  }
}

static bool LowHealth(int hp, int cap);
static void DrawTextFit(int x, int y, const char *s, uint32 col, int max_w);

// Three players on this computer in three worlds: the fourth quarter of the
// window shows the whole team. Both overworld maps with everybody's dot, and
// for each player their hearts, where they are and whether they are down.
static void DrawTeamPanel(int qx, int qy, int qw, int qh) {
  enum { kMap = 96 };
  ShadeRect(qx, qy, qw, qh);
  DrawText(qx + (qw - TextWidth("TEAM")) / 2, qy + 4, "TEAM", 0xf8d878);
  int gap = (qw - 2 * kMap) / 3;
  for (int dark = 0; dark < 2; dark++) {
    const uint32 *img = WorldMapImage(dark);
    int mx = qx + gap + dark * (kMap + gap), my = qy + 16;
    if (!img)
      continue;
    // (The overworld is the middle 256x256 of the 512x512 map picture.)
    for (int y = 0; y < kMap; y++)
      for (int x = 0; x < kMap; x++)
        FillRect(mx + x, my + y, 1, 1, img[(128 + y * 256 / kMap) * 512 + 128 + x * 256 / kMap]);
    DrawText(mx + 2, my + kMap + 2, dark ? "DARK WORLD" : "LIGHT WORLD", 0xb8c8f8);
    for (int p = kMpMaxPlayers - 1; p >= 0; p--) {
      InstPlace pl;
      if (!Inst_PlayerPlace(p, &pl) || pl.space >= 0x10000 || ((pl.space & 0x40) != 0) != dark)
        continue;
      int dx = (pl.x / 16) * kMap / 256, dy = (pl.y / 16) * kMap / 256;
      if (dx >= 2 && dx < kMap - 2 && dy >= 2 && dy < kMap - 2)
        DrawDot(mx + dx, my + dy, Inst_PlayerDowned(p) ? 0xa0a0a0 : Mp_PlayerColorRgb(p), false);
    }
  }
  SessionPlayer seats[kMpMaxPlayers];
  Session_GetPlayers(seats);
  int y = qy + 16 + kMap + 14;
  int cap = link_health_capacity;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Inst_IsJoined(p))
      continue;
    char name[24], where[32], buf[64];
    if (Session_IsNetplay() && seats[p].name[0])
      snprintf(name, sizeof(name), "P%d %.8s", p + 1, seats[p].name);
    else
      snprintf(name, sizeof(name), "P%d", p + 1);
    DrawText(qx + 6, y, name, Mp_PlayerColorRgb(p));
    int hp = Inst_PlayerHealth(p);
    if (Inst_PlayerDowned(p)) {
      DrawText(qx + 70, y, "DOWN", 0xffffff);
    } else {
      DrawHeart(qx + 70, y - 1, 2, LowHealth(hp, cap) && (SDL_GetTicks() / 300 & 1) ? 0xffffff : 0xf04060);
      snprintf(buf, sizeof(buf), (hp & 7) >= 4 ? "%d.5/%d" : "%d/%d", hp / 8, cap / 8);
      DrawText(qx + 80, y, buf, 0xffffff);
    }
    if (seats[p].state == kSeat_Reconnecting)
      snprintf(where, sizeof(where), "reconnecting");
    else
      LocationName(p, where, sizeof(where));
    DrawTextFit(qx + 126, y, where, 0xb8c8f8, qw - 132);
    y += 12;
  }
}

static void DrawMiniMaps() {
  if (main_module_index == 0x14 || main_module_index == 0x1a)
    return;
  for (int v = 0; v < g_nviews; v++)
    if (g_views[v].player < 0 || !ViewIsFullScreenMenu(g_views[v].player))
      DrawMiniMap(&g_views[v]);
}

// A small P1-P4 tag over every Link on screen (an option).
static void DrawNameTags() {
  if (!g_config.mp_name_tags || Inst_NumJoined() < 2)
    return;
  for (int v = 0; v < g_nviews; v++) {
    const UiView *view = &g_views[v];
    InstPlace me, them;
    if (view->player < 0 || !Inst_PlayerPlace(view->player, &me))
      continue;
    // (This pane's world, not the one the sound comes from.)
    uint8 m = Inst_PlayerViewModule(view->player);
    if (m != 7 && m != 9)
      continue;
    int extra = (view->w - 256) / 2;
    for (int q = 0; q < kMpMaxPlayers; q++) {
      if (!Inst_PlayerPlace(q, &them) || them.space != me.space)
        continue;
      char label[4];
      snprintf(label, sizeof(label), "P%d", q + 1);
      int tw = TextWidth(label);
      int x = view->x + (int16)(them.x - me.cam_x) + extra + 2 - tw / 2;
      int y = view->y + (int16)(them.y - me.cam_y) - 22;
      if (x < view->x || x + tw > view->x + view->w || y < view->y || y + 8 > view->y + view->h)
        continue;
      ShadeRect(x - 1, y - 1, tw + 2, 9);
      DrawText(x, y, label, them.downed ? 0xa0a0a0 : Mp_PlayerColorRgb(q));
    }
  }
}

// On the file select screen: will a new file be randomized, and how?
static void DrawSeedNotice() {
  if (main_module_index != 1 && main_module_index != 4)  // file select, naming a file
    return;
  uint32 rando = Rando_GetNewGameSetting();  // what new files actually get
  char buf[3][48];
  int n = 0;
  if (Ap_GetNewGameBind())
    snprintf(buf[n++], sizeof(buf[0]), "New files: Archipelago (this room)");
  else if (rando == kRando_RandomSeed)
    snprintf(buf[n++], sizeof(buf[0]), "New files: randomized, new seed");
  else if (rando != kRando_Off)
    snprintf(buf[n++], sizeof(buf[0]), "New files: randomized, seed %08X", rando);
  if (FileSelect_GetFullInventory())
    snprintf(buf[n++], sizeof(buf[0]), "New files: all items (for testing)");
  for (int i = 0; i < n; i++) {
    int w = TextWidth(buf[i]) + 8, x = (g_cv.width - w) / 2, y = g_cv.height - 14 * (n - i);
    ShadeRect(x, y, w, 12);
    DrawText(x + 4, y + 3, buf[i], 0xffe080);
  }
}

// Each player's selected item, as the HUD's item box would show it for them
// in their own world (worked out again when it changes, and twice a second
// for counts).
static struct { uint8 item; bool ok; uint32 when; uint32 rgba[16 * 16]; } g_icons[kMpMaxPlayers];

static bool PlayerIcon(int p) {
  uint8 item = Inst_PlayerItem(p);
  uint32 now = SDL_GetTicks();
  if (item != g_icons[p].item || now - g_icons[p].when > 500) {
    g_icons[p].item = item;
    g_icons[p].when = now;
    g_icons[p].ok = item && Inst_PlayerItemIcon(p, g_icons[p].rgba);
  }
  return g_icons[p].ok;
}

static void DrawIcon(int x, int y, const uint32 *rgba) {
  for (int i = 0; i < 16 * 16; i++)
    if (rgba[i])
      FillRect(x + (i & 15), y + (i >> 4), 1, 1, rgba[i] & 0xffffff);
}

// Low on hearts, the way the game beeps for it (fewer than 1, 2 or 3 hearts
// with more containers).
static bool LowHealth(int hp, int cap) {
  int c = IntMin(cap >> 3, 20);
  int limit = c < 8 ? 9 : c < 15 ? 17 : 25;
  return hp < limit && hp < cap;
}

static void DrawCornerHud() {
  if (Inst_NumJoined() < 2 || main_module_index < 6 || main_module_index == 0x14 || main_module_index == 0x1a)
    return;
  int cap = link_health_capacity;
  int hearts = IntMin(IntMax(cap / 8, 1), 20);
  int per_row = 10;
  int rows = (hearts + per_row - 1) / per_row;
  // Online, each corner shows the player's name instead of P1-P4 (the
  // color says which player it is).
  SessionPlayer seats[kMpMaxPlayers];
  Session_GetPlayers(seats);
  bool names = Session_IsNetplay();
  int max_w = (g_cv.width - 3) / 2;  // two panels side by side never overlap
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Inst_IsJoined(p))
      continue;
    char label[12];
    int panel_w = 0, lw = 0;
    // The name if it fits, else P1-P4.
    for (int k = 0; k < 2; k++) {
      if (k == 0 && names && seats[p].name[0])
        snprintf(label, sizeof(label), "%.6s", seats[p].name);
      else
        snprintf(label, sizeof(label), "P%d", p + 1);
      lw = TextWidth(label) + 5;
      panel_w = IntMax(lw + 2 + IntMin(hearts, per_row) * 8, lw + 56) + 18;  // (room for "DOWN" and its bar; the item)
      if (panel_w <= max_w)
        break;
    }
    bool has_icon = !Inst_PlayerDowned(p) && PlayerIcon(p);
    int panel_h = IntMax(5 + rows * 8, 17);  // hearts, then the magic meter; the item is 16 high
    bool right = p & 1, bottom = p >= 2;
    int x = right ? g_cv.width - panel_w - 1 : 1;
    int y = bottom ? g_cv.height - panel_h - 1 : 1;
    ShadeRect(x, y, panel_w, panel_h);
    DrawText(x + 2, y + 2 + (rows - 1) * 4, label, Mp_PlayerColorRgb(p));
    int hx = x + lw;
    if (has_icon)
      DrawIcon(x + panel_w - 17, y + 1, g_icons[p].rgba);
    if (Inst_PlayerDowned(p)) {
      DrawText(hx, y + 2, "DOWN", 0xffffff);
      // A heart at the end: A would send a teammate a heart now.
      bool heart = Inst_PlayerCanSendHeart(p);
      int bar = panel_w - 18 - (hx + 28 - x) - (heart ? 10 : 0);
      int w = bar * Inst_PlayerReviveCtr(p) / kMpReviveFrames;
      FillRect(hx + 26, y + 4, bar, 3, 0x303030);
      FillRect(hx + 26, y + 4, w, 3, Mp_PlayerColorRgb(p));
      if (heart)
        DrawHeart(x + panel_w - 27, y + 1, 2, 0xf04060);
      continue;
    }
    int hp = Inst_PlayerHealth(p);
    // Low on hearts: they flash, for this player only.
    uint32 color = LowHealth(hp, cap) && (SDL_GetTicks() / 300 & 1) ? 0xffffff : Mp_PlayerColorRgb(p);
    for (int i = 0; i < hearts; i++) {
      int fill = hp >= (i + 1) * 8 ? 2 : hp >= i * 8 + 4 ? 1 : 0;
      DrawHeart(hx + (i % per_row) * 8, y + 1 + (i / per_row) * 8, fill, color);
    }
    int bar_w = panel_w - (hx - x) - 20, magic = IntMin(Inst_PlayerMagic(p), 128);
    FillRect(hx, y + panel_h - 4, bar_w, 2, 0x303030);
    FillRect(hx, y + panel_h - 4, bar_w * magic / 128, 2, 0x40c840);
  }
}

// A local player who is down watches someone; L/R switches.
static bool SpectateMessage(char *buf, size_t size) {
  const SessionStatus *st = Session_GetStatus();
  for (int i = 0; i < kSessionMaxLocal; i++) {
    int p = st->local_slot[i];
    if (p < 0 || !Inst_PlayerDowned(p))
      continue;
    int q = Ui_WatchedPlayer(i);
    if (q != p)
      snprintf(buf, size, "P%d down: watching P%d  (L/R: switch)", p + 1, q + 1);
    else
      snprintf(buf, size, "P%d down: wait for a revive  (L/R: watch)", p + 1);
    return true;
  }
  return false;
}

static void DrawNotifications() {
  const SessionStatus *st = Session_GetStatus();
  uint32 now = SDL_GetTicks();
  if (strcmp(st->message, g_ui.last_message) != 0) {
    snprintf(g_ui.last_message, sizeof(g_ui.last_message), "%s", st->message);
    g_ui.message_time = now;
  }
  char buf[96];
  const char *msg = NULL;
  if (st->mode == kSessionClient && st->client_state == 1) {
    snprintf(buf, sizeof(buf), "Connecting to %s...", st->host_addr);
    msg = buf;
  } else if (st->mode == kSessionClient && st->client_state == 2) {
    snprintf(buf, sizeof(buf), "Downloading the host's game %d%%", st->snapshot_progress);
    msg = buf;
  } else if (st->mode == kSessionClient && st->client_state == 4) {
    snprintf(buf, sizeof(buf), "Connection lost: reconnecting... %d:%02d", st->reconnect_seconds_left / 60,
             st->reconnect_seconds_left % 60);
    msg = buf;
  } else if (g_ui.toast[0] && now - g_ui.toast_time < 3000) {
    msg = g_ui.toast;
  } else if (g_mp.exit_blocked_timer && Mp_Active()) {
    msg = "Everyone has to head that way!";
  } else if (SpectateMessage(buf, sizeof(buf))) {
    msg = buf;
  } else if (g_ui.last_message[0] && now - g_ui.message_time < 4000) {
    msg = g_ui.last_message;
  } else if (st->mode == kSessionHost) {
    // A seat waiting for its player to reconnect.
    SessionPlayer seats[kMpMaxPlayers];
    Session_GetPlayers(seats);
    for (int p = 0; p < kMpMaxPlayers && !msg; p++) {
      if (seats[p].state == kSeat_Reconnecting) {
        snprintf(buf, sizeof(buf), "P%d %s is reconnecting... %d:%02d", p + 1, seats[p].name, seats[p].seconds_left / 60,
                 seats[p].seconds_left % 60);
        msg = buf;
      }
    }
  }
  if (msg) {
    int w = TextWidth(msg) + 8;
    int x = (g_cv.width - w) / 2, y = g_cv.height - 40;
    ShadeRect(x, y, w, 12);
    DrawText(x + 4, y + 3, msg, 0xffffff);
  }
}

// Archipelago: items sent and received, hints and the room's messages, a
// few at a time above the other notices.
static void DrawApNotices() {
  uint32 now = SDL_GetTicks();
  char text[96];
  int kind;
  while (Ap_PollNotice(text, sizeof(text), &kind)) {
    memmove(&g_ui.ap_lines[1], &g_ui.ap_lines[0], sizeof(g_ui.ap_lines[0]) * 3);
    snprintf(g_ui.ap_lines[0].text, sizeof(g_ui.ap_lines[0].text), "%s", text);
    g_ui.ap_lines[0].kind = kind;
    g_ui.ap_lines[0].time = now;
  }
  static const uint32 kColors[] = { 0xffffff, 0xd8a8ff, 0x90c0ff, 0xff9090, 0xff6060 };
  int y = g_cv.height - 54;
  for (int i = 0; i < 4; i++) {
    if (!g_ui.ap_lines[i].text[0] || now - g_ui.ap_lines[i].time > 7000)
      continue;
    // A line too wide for the screen goes on two, broken at a space.
    char rows[2][96];
    int nrows = 1, maxw = g_cv.width - 16;
    snprintf(rows[0], sizeof(rows[0]), "%s", g_ui.ap_lines[i].text);
    if (TextWidth(rows[0]) > maxw) {
      for (char *sp = strrchr(rows[0], ' '); sp; sp = strrchr(rows[0], ' ')) {
        *sp = 0;
        if (TextWidth(rows[0]) <= maxw) {
          snprintf(rows[1], sizeof(rows[1]), "%s", g_ui.ap_lines[i].text + (sp - rows[0]) + 1);
          nrows = 2;
          break;
        }
      }
      if (nrows == 1)
        snprintf(rows[0], sizeof(rows[0]), "%s", g_ui.ap_lines[i].text);
    }
    for (int r = nrows - 1; r >= 0; r--) {
      int w = IntMin(TextWidth(rows[r]) + 8, g_cv.width - 8);
      int x = (g_cv.width - w) / 2;
      ShadeRect(x, y, w, 12);
      DrawTextFit(x + 4, y + 3, rows[r], kColors[g_ui.ap_lines[i].kind % 5], w - 8);
      y -= 12;
    }
    y -= 1;
  }
}

static uint32 ScaleRgb(uint32 c, int num, int den) {
  return ((c >> 16 & 0xff) * num / den) << 16 | ((c >> 8 & 0xff) * num / den) << 8 | (c & 0xff) * num / den;
}

// Big outlined letters with a color per font row (a gradient).
static void DrawBigText(int x, int y, const char *s, int scale, const uint32 *row_colors, uint32 outline) {
  for (int pass = 0; pass < 2; pass++) {
    int cx = x;
    for (const char *t = s; *t; t++, cx += 6 * scale) {
      uint8 c = (uint8)*t;
      if (c >= 'a' && c <= 'z')
        c -= 32;
      if (c >= 128)
        continue;
      for (int r = 0; r < 7; r++) {
        for (int b = 0; b < 5; b++) {
          if (!(g_font[c][r] & (0x10 >> b)))
            continue;
          int px = cx + b * scale, py = y + r * scale;
          if (pass == 0)
            FillRect(px - 1, py - 1, scale + 2, scale + 2, outline);
          else
            FillRect(px, py, scale, scale, row_colors[r]);
        }
      }
    }
  }
}

// "Four Swords of the Past" in the black band above the title picture.
static void DrawTitleBanner() {
  if (!g_config.four_swords_title || main_module_index != 0 ||
      submodule_index < 6 || submodule_index > 8)  // from the sword coming down to the finished title
    return;
  int bright = INIDISP_copy & 0x80 ? 0 : INIDISP_copy & 0xf;
  if (bright == 0)
    return;
  static const uint32 kGold[7] = { 0xfff8c0, 0xfff070, 0xf8d838, 0xf0c020, 0xe0a010, 0xc88008, 0xa86000 };
  static const uint32 kSilver[7] = { 0xffffff, 0xf0f0f8, 0xe0e0f0, 0xd0d0e8, 0xc0c0e0, 0xa8a8d0, 0x9090c0 };
  uint32 gold[7], silver[7];
  for (int i = 0; i < 7; i++) {
    gold[i] = ScaleRgb(kGold[i], bright, 15);
    silver[i] = ScaleRgb(kSilver[i], bright, 15);
  }
  uint32 outline = ScaleRgb(0x301000, bright, 15);
  // The game picture is centered in the window (widescreen adds columns on both sides).
  const char *top = "FOUR SWORDS", *bottom = "OF THE PAST";
  int top_w = (int)strlen(top) * 12 - 2, bottom_w = TextWidth(bottom);
  int cx = g_cv.width / 2;
  DrawBigText(cx - top_w / 2, 3, top, 2, gold, outline);
  int by = 21;
  DrawBigText(cx - bottom_w / 2, by, bottom, 1, silver, outline);
  // A small sword on either side of "of the past".
  for (int side = -1; side <= 1; side += 2) {
    int x0 = side < 0 ? cx - bottom_w / 2 - 34 : cx + bottom_w / 2 + 6;
    FillRect(x0, by + 3, 28, 1, silver[2]);  // blade
    int hilt = side < 0 ? x0 + 22 : x0 + 4;
    FillRect(hilt, by + 1, 1, 5, gold[3]);   // cross-guard
    FillRect(side < 0 ? x0 + 23 : x0, by + 3, 5, 1, gold[5]);  // grip
  }
}

// The pane of the player whose menu is open (the whole picture with one pane).
static const UiView *OwnerView() {
  static UiView whole;
  whole.x = whole.y = 0, whole.w = g_cv.width, whole.h = g_cv.height, whole.player = -1;
  int p = g_ui.owner >= 0 && g_ui.owner < kSessionMaxLocal ? Session_GetStatus()->local_slot[g_ui.owner] : -1;
  for (int v = 0; v < g_nviews && g_nviews > 1; v++)
    if (p >= 0 && g_views[v].player == p)
      return &g_views[v];
  return &whole;
}

// Text that fits in the box: the end is cut and marked.
static void DrawTextFit(int x, int y, const char *s, uint32 col, int max_w) {
  if (TextWidth(s) <= max_w) {
    DrawText(x, y, s, col);
    return;
  }
  char buf[80];
  int n = max_w / 6 - 2;
  if (n < 1)
    n = 1;
  snprintf(buf, sizeof(buf), "%.*s..", n, s);
  DrawText(x, y, buf, col);
}

static void DrawMenu() {
  if (!Ui_IsOpen())
    return;
  BuildScreen();
  const UiView *mv = OwnerView();
  int w = 236, line_h = 10;
  int h = 24 + g_ui.nlines * line_h + (g_ui.footer[0] ? 12 : 0);
  // (20 or more lines on a 224-line screen: a little tighter)
  while (h > mv->h && line_h > 8) {
    line_h--;
    h = 24 + g_ui.nlines * line_h + (g_ui.footer[0] ? 12 : 0);
  }
  int x = mv->x + (mv->w - w) / 2, y = mv->y + IntMax((mv->h - h) / 2, 2);
  ShadeRect(x, y, w, h);
  ShadeRect(x, y, w, h);
  FillRect(x, y, w, 1, 0xe0c060);
  FillRect(x, y + h - 1, w, 1, 0xe0c060);
  DrawText(x + (w - TextWidth(g_ui.title)) / 2, y + 5, g_ui.title, 0xf8d878);
  for (int i = 0; i < g_ui.nlines; i++) {
    MenuLine *l = &g_ui.lines[i];
    int ly = y + 20 + i * line_h;
    bool sel = i == g_ui.cursor && l->selectable;
    if (sel)
      FillRect(x + 4, ly - 2, w - 8, line_h, 0x304878);
    uint32 col = !l->selectable ? 0xa0a0a0 : sel ? 0xffffff : 0xd8d8d8;
    int vw = l->value[0] ? IntMin(TextWidth(l->value), w / 2) : 0;
    DrawTextFit(x + 8, ly, l->label, col, w - 16 - (vw ? vw + 6 : 0));
    if (l->value[0]) {
      int vx = x + w - 8 - vw;
      DrawTextFit(vx, ly, l->value, sel ? 0xf8d878 : 0xb8c8f8, vw);
    }
  }
  if (g_ui.footer[0]) {
    int fw = IntMin(TextWidth(g_ui.footer), w - 8);
    DrawTextFit(x + (w - fw) / 2, y + h - 12, g_ui.footer, 0x90a0b0, fw);
  }
}

void Ui_Draw(uint8 *pixels, int pitch, int width, int height, int scale) {
  g_cv.pixels = (uint32 *)pixels;
  g_cv.pitch = pitch / 4;
  g_cv.scale = scale;
  g_cv.width = width / scale;
  g_cv.height = height / scale;
  DrawTitleBanner();
  DrawSeedNotice();
  DrawTeammateArrows();
  DrawPings();
  DrawNameTags();
  DrawMiniMaps();
  if (g_nviews == 3)
    DrawTeamPanel(g_views[0].w, g_views[0].h, g_views[0].w, g_views[0].h);
  DrawCornerHud();
  DrawNotifications();
  DrawApNotices();
  DrawMenu();
}

void Ui_SaveSettings() {
  if (g_ui.headless)
    return;
  FILE *f = fopen("zelda3.coop.ini", "w");
  if (!f)
    return;
  fprintf(f, "# Written by the in-game options menu. Overrides zelda3.ini.\n");
  fprintf(f, "[General]\n");
  // The widescreen setting as it was set (height, sprite and visual fix
  // options included); anything else stays as zelda3.ini says.
  int e = g_config.extended_aspect_ratio, ah = g_config.extend_y ? 240 : 224;
  const char *ar = e == 0 ? "4:3" : e == (ah * 16 / 10 - 256) / 2 ? "16:10" : e == (ah * 16 / 9 - 256) / 2 ? "16:9" :
                   e == (ah * 18 / 9 - 256) / 2 ? "18:9" : NULL;
  if (ar)
    fprintf(f, "ExtendedAspectRatio = %s%s%s%s\n", g_config.extend_y ? "extend_y, " : "", ar,
            e && !(g_config.features0 & kFeatures0_ExtendScreen64) ? ", unchanged_sprites" : "",
            e && !(g_config.features0 & kFeatures0_WidescreenVisualFixes) ? ", no_visual_fixes" : "");
  fprintf(f, "[Graphics]\n");
  fprintf(f, "EnhancedMode7 = %d\n", g_config.enhanced_mode7);
  fprintf(f, "LinearFiltering = %d\n", g_config.linear_filtering);
  fprintf(f, "NoSpriteLimits = %d\n", g_config.no_sprite_limits);
  fprintf(f, "Hd2d = %d\n", g_config.hd2d);
  fprintf(f, "Hd2dDepthOfField = %d\nHd2dLighting = %d\nHd2dBloom = %d\nHd2dGrade = %d\nHd2dVignette = %d\n",
          g_config.hd2d_depth_of_field, g_config.hd2d_lighting, g_config.hd2d_bloom, g_config.hd2d_grade,
          g_config.hd2d_vignette);
  fprintf(f, "Hd2dSeeThrough = %d\n", g_config.hd2d_see_through);
  fprintf(f, "DimFlashes = %d\n", (g_config.features0 & kFeatures0_DimFlashes) != 0);
  fprintf(f, "OutputMethod = %s\n", g_config.output_method == kOutputMethod_OpenGL ? "OpenGL" :
          g_config.output_method == kOutputMethod_OpenGL_ES ? "OpenGL ES" :
          g_config.output_method == kOutputMethod_SDLSoftware ? "SDL-Software" : "SDL");
  fprintf(f, "[Features]\n");
  static const struct { const char *key; uint32 bit; } kKeys[] = {
    {"ItemSwitchLR", kFeatures0_SwitchLR}, {"TurnWhileDashing", kFeatures0_TurnWhileDashing},
    {"MirrorToDarkworld", kFeatures0_MirrorToDarkworld}, {"CollectItemsWithSword", kFeatures0_CollectItemsWithSword},
    {"BreakPotsWithSword", kFeatures0_BreakPotsWithSword}, {"DisableLowHealthBeep", kFeatures0_DisableLowHealthBeep},
    {"SkipIntroOnKeypress", kFeatures0_SkipIntroOnKeypress}, {"ShowMaxItemsInYellow", kFeatures0_ShowMaxItemsInYellow},
    {"MoreActiveBombs", kFeatures0_MoreActiveBombs}, {"CarryMoreRupees", kFeatures0_CarryMoreRupees},
    {"MiscBugFixes", kFeatures0_MiscBugFixes}, {"CancelBirdTravel", kFeatures0_CancelBirdTravel},
  };
  for (size_t i = 0; i < countof(kKeys); i++)
    fprintf(f, "%s = %d\n", kKeys[i].key, (g_config.features0 & kKeys[i].bit) != 0);
  fprintf(f, "FourSwordsStory = %d\n", g_config.four_swords_story);
  fprintf(f, "FourSwordsTitle = %d\n", g_config.four_swords_title);
  fprintf(f, "FullInventory = %d\n", g_config.full_inventory);
  // (This computer's own settings: a client's session shows the host's.)
  uint32 rando = g_config.randomizer;
  if (rando > kRando_RandomSeed)
    fprintf(f, "Randomizer = seed:%08X\n", rando);
  else
    fprintf(f, "Randomizer = %s\n", rando ? "random" : "off");
  Bindings_WriteConfig(f);
  fprintf(f, "[Multiplayer]\n");
  fprintf(f, "FriendlyFire = %d\n", g_config.mp_friendly_fire);
  fprintf(f, "UseUpnp = %d\n", g_ui.use_upnp);
  fprintf(f, "Port = %d\n", g_config.mp_port);
  fprintf(f, "InputDelay = %d\n", g_config.mp_input_delay);
  fprintf(f, "NameTags = %d\n", g_config.mp_name_tags);
  fprintf(f, "ColorBlindColors = %d\n", g_config.mp_colorblind);
  if (g_config.mp_player_name)
    fprintf(f, "PlayerName = %s\n", g_config.mp_player_name);
  fprintf(f, "LastJoinAddress = %s\n", g_ui.join_addr);
  // (Not the password: it would sit in a plain text file.)
  // (Empty ones aren't written: they would hide what zelda3.ini says.)
  fprintf(f, "[Archipelago]\n");
  if (g_ap_settings.server[0])
    fprintf(f, "Server = %s\n", g_ap_settings.server);
  if (g_ap_settings.slot[0])
    fprintf(f, "Slot = %s\n", g_ap_settings.slot);
  fprintf(f, "DeathLink = %s\n", g_ap_settings.death_link == 1 ? "on" : g_ap_settings.death_link == 2 ? "off" : "auto");
  fclose(f);
  g_ui.dirty_settings = false;
}

