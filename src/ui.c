#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "config.h"
#include "features.h"
#include "mp.h"
#include "session.h"
#include "zelda_rtl.h"
#include "variables.h"

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

static const uint32 kPlayerColorRgb[kMpMaxPlayers] = { 0x40d040, 0xf04040, 0x4878ff, 0xc050ff };

// ---------------------------------------------------------------------------
// Menu model

enum {
  kScreen_None,
  kScreen_Main,
  kScreen_Multiplayer,
  kScreen_JoinAddress,
  kScreen_Lan,
  kScreen_Video,
  kScreen_Gameplay,
  kScreen_Controls,
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
  bool use_upnp;
  uint32 last_lan_refresh;
  char last_message[96];
  uint32 message_time;
  bool dirty_settings;
  bool want_quit_confirm;
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
  g_ui.screen = screen;
  g_ui.cursor = 0;
  g_ui.want_quit_confirm = false;
  if (screen == kScreen_JoinAddress)
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
  } else {
    static const char *const kStates[] = { "idle", "connecting", "downloading", "connected" };
    snprintf(buf, sizeof(buf), "Client: %s %s", kStates[st->client_state & 3], st->host_addr);
    Label(buf);
    snprintf(buf, sizeof(buf), "Ping %d ms, buffer %d", st->ping_ms, st->frames_buffered);
    Label(buf);
  }
  // Players
  for (int p = 0; p < kMpMaxPlayers; p++) {
    char label[48], value[48];
    snprintf(label, sizeof(label), "P%d", p + 1);
    if (!g_mp.p[p].joined) {
      snprintf(value, sizeof(value), "- open -");
    } else {
      bool mine = false;
      for (int i = 0; i < kSessionMaxLocal; i++)
        if (st->local_slot[i] == p)
          mine = true, snprintf(value, sizeof(value), "you (controller %d)%s", i + 1, g_mp.p[p].downed ? " DOWN" : "");
      if (!mine)
        snprintf(value, sizeof(value), "%s%s", st->slot_owner[p] == 0 ? "host" : "remote", g_mp.p[p].downed ? " DOWN" : "");
    }
    Label(label);
    snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s", value);
  }
  if (st->mode == kSessionOffline) {
    Item("Host a game", g_ui.use_upnp ? "with UPnP" : "LAN only");
    int a = Act();
    if (a == kAct_Left || a == kAct_Right) {
      g_ui.use_upnp = !g_ui.use_upnp;
      g_ui.dirty_settings = true;
    }
    else if (a == kAct_Select)
      Session_Host(g_config.mp_port, g_ui.use_upnp);
    Item("Join by address", g_ui.join_addr);
    if (Act() == kAct_Select)
      Goto(kScreen_JoinAddress);
    Item("Find LAN games", ">");
    if (Act() == kAct_Select) {
      Session_RefreshLan();
      Goto(kScreen_Lan);
    }
  } else {
    Item(st->mode == kSessionHost ? "Stop hosting" : "Leave game", NULL);
    if (Act() == kAct_Select)
      Session_Disconnect();
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
      g_ui.dirty_settings = true;
    }
  } else {
    Label("Friendly fire");
    snprintf(g_ui.lines[g_ui.nlines - 1].value, sizeof(g_ui.lines[0].value), "%s (host decides)", OnOff(g_mp.friendly_fire));
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "New controllers: press START to join");
}

static void ScreenJoinAddress() {
  snprintf(g_ui.title, sizeof(g_ui.title), "JOIN BY ADDRESS");
  Label("Type the host's address, then ENTER");
  char buf[72];
  snprintf(buf, sizeof(buf), "%s%s", g_ui.join_addr, (SDL_GetTicks() / 400) & 1 ? "_" : " ");
  Label(buf);
  Item("Connect", NULL);
  if (Act() == kAct_Select && g_ui.join_addr[0]) {
    Session_Join(g_ui.join_addr);
    Goto(kScreen_Multiplayer);
    return;
  }
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Multiplayer);
  snprintf(g_ui.footer, sizeof(g_ui.footer), "Example: 192.168.1.20 or myhost.net:47474");
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
      Session_Join(games[i].addr);
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
  Label("Shaders: set Shader= in zelda3.ini (OpenGL)");
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
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
}

static void ScreenControls() {
  snprintf(g_ui.title, sizeof(g_ui.title), "CONTROLS");
  Label("Keyboard: arrows, X=A Z=B S=X A=Y");
  Label("  Enter=Start  Right Shift=Select");
  Label("Menu: Esc, controller Guide, or");
  Label("  Back+Start on any controller");
  Label("Drop in: press Start on a new controller");
  Label("Revive: stand next to a downed Link");
  Item("Back", NULL);
  if (Act() == kAct_Select)
    Goto(kScreen_Main);
}

static void BuildScreen() {
  g_ui.nlines = 0;
  g_ui.title[0] = 0;
  g_ui.footer[0] = 0;
  switch (g_ui.screen) {
  case kScreen_Main: ScreenMain(); break;
  case kScreen_Multiplayer: ScreenMultiplayer(); break;
  case kScreen_JoinAddress: ScreenJoinAddress(); break;
  case kScreen_Lan: ScreenLan(); break;
  case kScreen_Video: ScreenVideo(); break;
  case kScreen_Gameplay: ScreenGameplay(); break;
  case kScreen_Controls: ScreenControls(); break;
  }
}

// ---------------------------------------------------------------------------

void Ui_Init(const UiHostFuncs *host) {
  memset(&g_ui, 0, sizeof(g_ui));
  if (host)
    g_ui.host = *host;
  g_ui.use_upnp = g_config.mp_use_upnp;
  if (g_config.mp_last_join)
    snprintf(g_ui.join_addr, sizeof(g_ui.join_addr), "%s", g_config.mp_last_join);
  BuildFont();
}

bool Ui_IsOpen() {
  return g_ui.screen != kScreen_None;
}

bool Ui_PausesGame() {
  return Ui_IsOpen() && !Session_IsNetplay();
}

static void Toggle() {
  if (Ui_IsOpen())
    Goto(kScreen_None);
  else
    Goto(kScreen_Main);
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

void Ui_Update(uint16 *local_inputs) {
  // Back + Start on any controller toggles the menu.
  uint16 all = 0, pressed = 0;
  for (int i = 0; i < 4; i++) {
    uint16 now = local_inputs[i];
    if ((now & 0xc) == 0xc && (g_ui.prev_inputs[i] & 0xc) != 0xc)
      Toggle();
    pressed |= now & ~g_ui.prev_inputs[i];
    all |= now;
    g_ui.prev_inputs[i] = now;
  }
  if (!Ui_IsOpen())
    return;

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
  g_ui.action = (pressed & 0x100) || ((pressed & 8) && !(all & 4)) ? kAct_Select :
                (pressed & 0x40) ? kAct_Left : (pressed & 0x80) ? kAct_Right : kAct_None;
  if (pressed & 1) {  // B goes back
    if (g_ui.screen == kScreen_Main)
      Goto(kScreen_None);
    else
      Goto(g_ui.screen == kScreen_JoinAddress || g_ui.screen == kScreen_Lan ? kScreen_Multiplayer : kScreen_Main);
    g_ui.action = kAct_None;
  }
  if (g_ui.action != kAct_None && Ui_IsOpen()) {
    BuildScreen();
    g_ui.action = kAct_None;
  }
  // The game doesn't see inputs while the menu is open.
  for (int i = 0; i < 4; i++)
    local_inputs[i] = 0;
}

bool Ui_HandleKey(int key, bool pressed) {
  if (!pressed)
    return false;
  if (key == SDLK_ESCAPE) {
    if (g_ui.screen == kScreen_JoinAddress)
      Goto(kScreen_Multiplayer);
    else
      Toggle();
    return true;
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
        Session_Join(g_ui.join_addr);
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

void Ui_HandleText(const char *text) {
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

static void DrawCornerHud() {
  if (!Mp_Active() || main_module_index < 6 || main_module_index == 0x14 || main_module_index == 0x1a)
    return;
  int cap = link_health_capacity;
  int hearts = IntMin(IntMax(cap / 8, 1), 20);
  int per_row = 10;
  int rows = (hearts + per_row - 1) / per_row;
  int panel_w = 18 + IntMin(hearts, per_row) * 8;
  int panel_h = 2 + rows * 8;
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    bool right = p & 1, bottom = p >= 2;
    int x = right ? g_cv.width - panel_w - 1 : 1;
    int y = bottom ? g_cv.height - panel_h - 1 : 1;
    ShadeRect(x, y, panel_w, panel_h);
    char label[4];
    snprintf(label, sizeof(label), "P%d", p + 1);
    DrawText(x + 2, y + 2 + (rows - 1) * 4, label, kPlayerColorRgb[p]);
    if (g_mp.p[p].downed) {
      DrawText(x + 18, y + 2, "DOWN", 0xffffff);
      int w = (panel_w - 20) * g_mp.p[p].revive_ctr / kMpReviveFrames;
      FillRect(x + 44, y + 4, panel_w - 46, 3, 0x303030);
      FillRect(x + 44, y + 4, IntMin(w, panel_w - 46), 3, kPlayerColorRgb[p]);
      continue;
    }
    int hp = Mp_PlayerHealth(p);
    for (int i = 0; i < hearts; i++) {
      int fill = hp >= (i + 1) * 8 ? 2 : hp >= i * 8 + 4 ? 1 : 0;
      DrawHeart(x + 16 + (i % per_row) * 8, y + 1 + (i / per_row) * 8, fill, kPlayerColorRgb[p]);
    }
  }
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
  } else if (g_mp.exit_blocked_timer && Mp_Active()) {
    msg = "Everyone has to head that way!";
  } else if (g_ui.last_message[0] && now - g_ui.message_time < 4000) {
    msg = g_ui.last_message;
  }
  if (msg) {
    int w = TextWidth(msg) + 8;
    int x = (g_cv.width - w) / 2, y = g_cv.height - 40;
    ShadeRect(x, y, w, 12);
    DrawText(x + 4, y + 3, msg, 0xffffff);
  }
}

static void DrawMenu() {
  if (!Ui_IsOpen())
    return;
  BuildScreen();
  int w = 236, line_h = 10;
  int h = 24 + g_ui.nlines * line_h + (g_ui.footer[0] ? 12 : 0);
  int x = (g_cv.width - w) / 2, y = IntMax((g_cv.height - h) / 2, 2);
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
    DrawText(x + 8, ly, l->label, col);
    if (l->value[0]) {
      int vx = x + w - 8 - TextWidth(l->value);
      DrawText(vx, ly, l->value, sel ? 0xf8d878 : 0xb8c8f8);
    }
  }
  if (g_ui.footer[0])
    DrawText(x + (w - TextWidth(g_ui.footer)) / 2, y + h - 12, g_ui.footer, 0x90a0b0);
}

void Ui_Draw(uint8 *pixels, int pitch, int width, int height, int scale) {
  g_cv.pixels = (uint32 *)pixels;
  g_cv.pitch = pitch / 4;
  g_cv.scale = scale;
  g_cv.width = width / scale;
  g_cv.height = height / scale;
  DrawCornerHud();
  DrawNotifications();
  DrawMenu();
}

void Ui_SaveSettings() {
  FILE *f = fopen("zelda3.coop.ini", "w");
  if (!f)
    return;
  fprintf(f, "# Written by the in-game options menu. Overrides zelda3.ini.\n");
  fprintf(f, "[General]\n");
  int e = g_config.extended_aspect_ratio;
  fprintf(f, "ExtendedAspectRatio = %s\n", e == (224 * 16 / 10 - 256) / 2 ? "16:10" : e == (224 * 16 / 9 - 256) / 2 ? "16:9" :
          e == (224 * 18 / 9 - 256) / 2 ? "18:9" : "4:3");
  fprintf(f, "[Graphics]\n");
  fprintf(f, "EnhancedMode7 = %d\n", g_config.enhanced_mode7);
  fprintf(f, "LinearFiltering = %d\n", g_config.linear_filtering);
  fprintf(f, "NoSpriteLimits = %d\n", g_config.no_sprite_limits);
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
  fprintf(f, "[Multiplayer]\n");
  fprintf(f, "FriendlyFire = %d\n", Session_GetFriendlyFire());
  fprintf(f, "UseUpnp = %d\n", g_ui.use_upnp);
  fprintf(f, "Port = %d\n", g_config.mp_port);
  if (g_config.mp_player_name)
    fprintf(f, "PlayerName = %s\n", g_config.mp_player_name);
  fprintf(f, "LastJoinAddress = %s\n", g_ui.join_addr);
  fclose(f);
  g_ui.dirty_settings = false;
}

