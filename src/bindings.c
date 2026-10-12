#include "bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "config.h"

BindProfile g_bind[kBindPlayers];

static const char *const kButtonNames[kBindButtons] = {
  "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Start", "Select",
};
// Bit of each button in the joypad word: B Y Select Start Up Down Left Right A X L R
static const uint8 kButtonBits[kBindButtons] = { 4, 5, 6, 7, 8, 0, 9, 1, 10, 11, 3, 2 };
// Legacy [KeyMap] Controls order: Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R
static const uint8 kLegacyOrder[12] = {
  kBind_Up, kBind_Down, kBind_Left, kBind_Right, kBind_Select, kBind_Start,
  kBind_A, kBind_B, kBind_X, kBind_Y, kBind_L, kBind_R,
};

static const char *const kPadNames[kGamepadBtn_Count] = {
  "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
  "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "LT", "RT",
};

const char *Bindings_ButtonName(int b) { return kButtonNames[b]; }
uint16 Bindings_ButtonBit(int b) { return 1 << kButtonBits[b]; }

const char *Bindings_GamepadButtonName(int pad_button) {
  return pad_button >= 0 && pad_button < kGamepadBtn_Count ? kPadNames[pad_button] : "";
}

static void ClearProfile(BindProfile *bp) {
  memset(bp->keys, 0, sizeof(bp->keys));
  memset(bp->pad, -1, sizeof(bp->pad));
}

void Bindings_ResetPlayer(int p) {
  BindProfile *bp = &g_bind[p];
  ClearProfile(bp);
  bp->controller = p;
  bp->deadzone = 30;
  bp->stick_moves = true;
  bp->rumble = true;
  // Controller buttons are placed like on a SNES pad (B bottom, A right).
  static const int8 kPad[kBindButtons] = {
    kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight,
    kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
    kGamepadBtn_Start, kGamepadBtn_Back,
  };
  for (int b = 0; b < kBindButtons; b++)
    bp->pad[b][0] = kPad[b];
  bp->pad[kBind_L][1] = kGamepadBtn_L2;
  bp->pad[kBind_R][1] = kGamepadBtn_R2;
  if (p == 0) {
    static const int32 kKeys[kBindButtons] = {
      SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_x, SDLK_z, SDLK_s, SDLK_a, SDLK_c, SDLK_v,
      SDLK_RETURN, SDLK_RSHIFT,
    };
    for (int b = 0; b < kBindButtons; b++)
      bp->keys[b][0] = kKeys[b];
  }
}

void Bindings_SetDefaults() {
  for (int p = 0; p < kBindPlayers; p++)
    Bindings_ResetPlayer(p);
}

void Bindings_SwapFaceButtons(int p) {
  BindProfile *bp = &g_bind[p];
  static const int8 kSwap[kGamepadBtn_Count] = {
    kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X,
  };
  for (int b = 0; b < kBindButtons; b++)
    for (int s = 0; s < kBindSlots; s++)
      if (bp->pad[b][s] >= kGamepadBtn_A && bp->pad[b][s] <= kGamepadBtn_Y)
        bp->pad[b][s] = kSwap[bp->pad[b][s]];
}

void Bindings_DescribeKeys(int p, int b, char *buf, size_t size) {
  buf[0] = 0;
  for (int s = 0; s < kBindSlots; s++) {
    if (!g_bind[p].keys[b][s])
      continue;
    size_t n = strlen(buf);
    snprintf(buf + n, size - n, "%s%s", n ? " " : "", SDL_GetKeyName(g_bind[p].keys[b][s]));
  }
  if (!buf[0])
    snprintf(buf, size, "-");
}

void Bindings_DescribePad(int p, int b, char *buf, size_t size) {
  buf[0] = 0;
  for (int s = 0; s < kBindSlots; s++) {
    if (g_bind[p].pad[b][s] < 0)
      continue;
    size_t n = strlen(buf);
    snprintf(buf + n, size - n, "%s%s", n ? " " : "", kPadNames[g_bind[p].pad[b][s]]);
  }
  if (!buf[0])
    snprintf(buf, size, "-");
}

void Bindings_SetKey(int p, int b, int32 key) {
  BindProfile *bp = &g_bind[p];
  for (int i = 0; i < kBindButtons; i++)
    for (int s = 0; s < kBindSlots; s++)
      if (bp->keys[i][s] == key)
        bp->keys[i][s] = 0;
  if (!bp->keys[b][0])
    bp->keys[b][0] = key;
  else if (!bp->keys[b][1])
    bp->keys[b][1] = key;
  else
    bp->keys[b][1] = bp->keys[b][0], bp->keys[b][0] = key;
}

void Bindings_SetPad(int p, int b, int pad_button) {
  BindProfile *bp = &g_bind[p];
  for (int i = 0; i < kBindButtons; i++)
    for (int s = 0; s < kBindSlots; s++)
      if (bp->pad[i][s] == pad_button)
        bp->pad[i][s] = -1;
  if (bp->pad[b][0] < 0)
    bp->pad[b][0] = pad_button;
  else if (bp->pad[b][1] < 0)
    bp->pad[b][1] = pad_button;
  else
    bp->pad[b][1] = bp->pad[b][0], bp->pad[b][0] = pad_button;
}

void Bindings_Clear(int p, int b) {
  for (int s = 0; s < kBindSlots; s++) {
    g_bind[p].keys[b][s] = 0;
    g_bind[p].pad[b][s] = -1;
  }
}

// --- runtime state ---

enum { kMaxDownKeys = 32, kMaxControllers = 8 };
static int32 g_down_keys[kMaxDownKeys];
static uint32 g_pad_down[kMaxControllers];
static int16 g_pad_axis[kMaxControllers][2];

void Bindings_OnKey(int32 key, bool pressed) {
  int free_slot = -1;
  for (int i = 0; i < kMaxDownKeys; i++) {
    if (g_down_keys[i] == key) {
      if (!pressed)
        g_down_keys[i] = 0;
      return;
    }
    if (!g_down_keys[i] && free_slot < 0)
      free_slot = i;
  }
  if (pressed && free_slot >= 0)
    g_down_keys[free_slot] = key;
}

static bool IsKeyDown(int32 key) {
  if (!key)
    return false;
  for (int i = 0; i < kMaxDownKeys; i++)
    if (g_down_keys[i] == key)
      return true;
  return false;
}

bool Bindings_IsGameKey(int32 key) {
  for (int p = 0; p < kBindPlayers; p++)
    for (int b = 0; b < kBindButtons; b++)
      for (int s = 0; s < kBindSlots; s++)
        if (g_bind[p].keys[b][s] == key)
          return true;
  return false;
}

void Bindings_OnPadButton(int c, int pad_button, bool pressed) {
  if (c < 0 || c >= kMaxControllers || pad_button < 0 || pad_button >= kGamepadBtn_Count)
    return;
  if (pressed)
    g_pad_down[c] |= 1 << pad_button;
  else
    g_pad_down[c] &= ~(1 << pad_button);
}

void Bindings_OnPadAxis(int c, int axis_x, int axis_y) {
  if (c < 0 || c >= kMaxControllers)
    return;
  g_pad_axis[c][0] = axis_x;
  g_pad_axis[c][1] = axis_y;
}

void Bindings_ResetController(int c) {
  if (c < 0 || c >= kMaxControllers)
    return;
  g_pad_down[c] = 0;
  g_pad_axis[c][0] = g_pad_axis[c][1] = 0;
}

static uint16 StickToDirections(int x, int y, int deadzone_percent) {
  int dz = 32767 * deadzone_percent / 100;
  if ((int64)x * x + (int64)y * y < (int64)dz * dz)
    return 0;
  // 8 directions: a direction counts when the stick points within 67.5 degrees of it.
  uint16 r = 0;
  int ax = abs(x), ay = abs(y);
  if (ay * 5 > ax * 2)
    r |= y < 0 ? 1 << 4 : 1 << 5;
  if (ax * 5 > ay * 2)
    r |= x < 0 ? 1 << 6 : 1 << 7;
  return r;
}

uint16 Bindings_GetInput(int p) {
  const BindProfile *bp = &g_bind[p];
  uint16 r = 0;
  int c = bp->controller;
  uint32 pad = c < kMaxControllers ? g_pad_down[c] : 0;
  for (int b = 0; b < kBindButtons; b++) {
    for (int s = 0; s < kBindSlots; s++) {
      if (IsKeyDown(bp->keys[b][s]) || (bp->pad[b][s] >= 0 && (pad >> bp->pad[b][s] & 1)))
        r |= 1 << kButtonBits[b];
    }
  }
  if (c < kMaxControllers && bp->stick_moves)
    r |= StickToDirections(g_pad_axis[c][0], g_pad_axis[c][1], bp->deadzone);
  return r;
}

// Is controller c the one some other local player's profile uses?
static bool PadOfAnotherPlayer(int c, int owner) {
  for (int i = 0; i < kBindPlayers; i++)
    if (i != owner && g_bind[i].controller == c)
      return true;
  return false;
}

uint16 Bindings_GetMenuFallbackInput(int owner, int controller) {
  uint16 r = 0;
  // The keyboard's arrows, Enter and Backspace are player 1's: the others
  // may be playing on it while player 1 is in the menu.
  if (owner <= 0) {
    if (IsKeyDown(SDLK_UP)) r |= 1 << 4;
    if (IsKeyDown(SDLK_DOWN)) r |= 1 << 5;
    if (IsKeyDown(SDLK_LEFT)) r |= 1 << 6;
    if (IsKeyDown(SDLK_RIGHT)) r |= 1 << 7;
    if (IsKeyDown(SDLK_RETURN) || IsKeyDown(SDLK_KP_ENTER)) r |= 1 << 8;  // select
    if (IsKeyDown(SDLK_BACKSPACE)) r |= 1 << 0;  // back
  }
  for (int c = 0; c < kMaxControllers; c++) {
    // Only the menu user's controller; with none of its own, any pad that
    // isn't some other player's.
    if (controller >= 0 ? c != controller : PadOfAnotherPlayer(c, owner))
      continue;
    uint32 d = g_pad_down[c];
    if (d >> kGamepadBtn_DpadUp & 1) r |= 1 << 4;
    if (d >> kGamepadBtn_DpadDown & 1) r |= 1 << 5;
    if (d >> kGamepadBtn_DpadLeft & 1) r |= 1 << 6;
    if (d >> kGamepadBtn_DpadRight & 1) r |= 1 << 7;
    r |= StickToDirections(g_pad_axis[c][0], g_pad_axis[c][1], 50);
  }
  return r;
}

// --- config ---

// A key without a usable name (the comma, or one SDL can't name) is written
// as its code: "key44". (Not "#44": the ini reader takes '#' for a comment
// and would drop the rest of the line.)
static void WriteKeyName(FILE *f, int32 key) {
  const char *name = SDL_GetKeyName(key);
  if (!name[0] || strpbrk(name, ",|#"))
    fprintf(f, "key%d", (int)key);
  else
    fprintf(f, "%s", name);
}

static int32 ParseKeyName(const char *s) {
  if (!SDL_strncasecmp(s, "key", 3) && s[3] >= '0' && s[3] <= '9')  // (not "Keypad 0")
    return atoi(s + 3);
  if (s[0] == '#')  // (older files)
    return atoi(s + 1);
  return SDL_GetKeyFromName(s);
}

static int ParsePadName(const char *s) {
  for (int i = 0; i < kGamepadBtn_Count; i++)
    if (!SDL_strcasecmp(s, kPadNames[i]))
      return i;
  if (!SDL_strcasecmp(s, "L1") || !SDL_strcasecmp(s, "Lb")) return kGamepadBtn_L1;
  if (!SDL_strcasecmp(s, "R1") || !SDL_strcasecmp(s, "Rb")) return kGamepadBtn_R1;
  if (!SDL_strcasecmp(s, "L2")) return kGamepadBtn_L2;
  if (!SDL_strcasecmp(s, "R2")) return kGamepadBtn_R2;
  return -1;
}

static char *Trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
    *--e = 0;
  return s;
}

// value: comma separated list of the 12 buttons in menu order, slots separated by '|'
static void ParseButtonList(char *value, int p, bool keys) {
  char *save_b = NULL;
  int b = 0;
  for (char *tok = SDL_strtokr(value, ",", &save_b); tok && b < kBindButtons; tok = SDL_strtokr(NULL, ",", &save_b), b++) {
    char *save_s = NULL;
    int s = 0;
    for (int i = 0; i < kBindSlots; i++) {
      if (keys) g_bind[p].keys[b][i] = 0; else g_bind[p].pad[b][i] = -1;
    }
    for (char *it = SDL_strtokr(tok, "|", &save_s); it && s < kBindSlots; it = SDL_strtokr(NULL, "|", &save_s)) {
      it = Trim(it);
      if (!it[0] || !strcmp(it, "-"))
        continue;
      if (keys) {
        int32 k = ParseKeyName(it);
        if (k != SDLK_UNKNOWN)
          g_bind[p].keys[b][s++] = k;
      } else {
        int pb = ParsePadName(it);
        if (pb >= 0)
          g_bind[p].pad[b][s++] = pb;
      }
    }
  }
}

bool Bindings_ParseConfig(const char *key, char *value) {
  if ((key[0] != 'P' && key[0] != 'p') || key[1] < '1' || key[1] > '4')
    return false;
  int p = key[1] - '1';
  const char *k = key + 2;
  if (!SDL_strcasecmp(k, "Keys")) {
    ParseButtonList(value, p, true);
  } else if (!SDL_strcasecmp(k, "Pad")) {
    ParseButtonList(value, p, false);
  } else if (!SDL_strcasecmp(k, "Controller")) {
    int c = atoi(value);
    g_bind[p].controller = c >= 1 && c <= kMaxControllers ? c - 1 : kBindNoController;
  } else if (!SDL_strcasecmp(k, "Deadzone")) {
    g_bind[p].deadzone = (uint8)IntMin(IntMax(atoi(value), 5), 90);
  } else if (!SDL_strcasecmp(k, "Stick")) {
    g_bind[p].stick_moves = atoi(value) != 0;
  } else if (!SDL_strcasecmp(k, "Rumble")) {
    g_bind[p].rumble = atoi(value) != 0;
  } else {
    return false;
  }
  return true;
}

void Bindings_WriteConfig(void *file) {
  FILE *f = (FILE *)file;
  fprintf(f, "[Controls]\n");
  fprintf(f, "# Buttons in order: Up, Down, Left, Right, A, B, X, Y, L, R, Start, Select.\n");
  fprintf(f, "# Use | between alternatives. Controller buttons: A B X Y Back Start LB RB LT RT L3 R3 DpadUp...\n");
  for (int p = 0; p < kBindPlayers; p++) {
    BindProfile *bp = &g_bind[p];
    fprintf(f, "P%dKeys = ", p + 1);
    for (int b = 0; b < kBindButtons; b++) {
      bool any = false;
      for (int s = 0; s < kBindSlots; s++) {
        if (!bp->keys[b][s])
          continue;
        if (any)
          fprintf(f, "|");
        WriteKeyName(f, bp->keys[b][s]);
        any = true;
      }
      if (!any)
        fprintf(f, "-");
      fprintf(f, b + 1 < kBindButtons ? ", " : "\n");
    }
    fprintf(f, "P%dPad = ", p + 1);
    for (int b = 0; b < kBindButtons; b++) {
      bool any = false;
      for (int s = 0; s < kBindSlots; s++) {
        if (bp->pad[b][s] < 0)
          continue;
        fprintf(f, "%s%s", any ? "|" : "", kPadNames[bp->pad[b][s]]);
        any = true;
      }
      if (!any)
        fprintf(f, "-");
      fprintf(f, b + 1 < kBindButtons ? ", " : "\n");
    }
    if (bp->controller == kBindNoController)
      fprintf(f, "P%dController = 0\n", p + 1);
    else
      fprintf(f, "P%dController = %d\n", p + 1, bp->controller + 1);
    fprintf(f, "P%dDeadzone = %d\n", p + 1, bp->deadzone);
    fprintf(f, "P%dStick = %d\n", p + 1, bp->stick_moves);
    fprintf(f, "P%dRumble = %d\n", p + 1, bp->rumble);
  }
}

static bool g_legacy_key_seen[kBindButtons], g_legacy_pad_seen[kBindButtons];

void Bindings_LegacyKey(int i, int32 key) {
  if (i < 0 || i >= 12)
    return;
  int b = kLegacyOrder[i];
  if (!g_legacy_key_seen[b]) {
    g_legacy_key_seen[b] = true;
    g_bind[0].keys[b][0] = g_bind[0].keys[b][1] = 0;
  }
  Bindings_SetKey(0, b, key);
}

void Bindings_LegacyPad(int i, int pad_button) {
  if (i < 0 || i >= 12)
    return;
  int b = kLegacyOrder[i];
  if (!g_legacy_pad_seen[b]) {
    g_legacy_pad_seen[b] = true;
    for (int p = 0; p < kBindPlayers; p++)
      g_bind[p].pad[b][0] = g_bind[p].pad[b][1] = -1;
  }
  for (int p = 0; p < kBindPlayers; p++)
    Bindings_SetPad(p, b, pad_button);
}
