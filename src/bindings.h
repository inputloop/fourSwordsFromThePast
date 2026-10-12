// Per-player control bindings: keyboard keys and controller buttons for each
// SNES button, which controller a player uses, and stick settings.
#ifndef ZELDA3_BINDINGS_H_
#define ZELDA3_BINDINGS_H_

#include "types.h"

enum {
  kBindPlayers = 4,
  kBindButtons = 13,
  kBindSlots = 2,       // alternatives per button and device
  kBindNoController = 0xff,
};

// The SNES buttons in the order shown in the menu, then the co-op Ping
// button (joypad bit 12, which the game never sees).
enum {
  kBind_Up, kBind_Down, kBind_Left, kBind_Right,
  kBind_A, kBind_B, kBind_X, kBind_Y, kBind_L, kBind_R,
  kBind_Start, kBind_Select, kBind_Ping,
};

typedef struct BindProfile {
  int32 keys[kBindButtons][kBindSlots];   // SDL keycodes, 0 = none
  int8 pad[kBindButtons][kBindSlots];     // kGamepadBtn_*, -1 = none
  uint8 controller;                       // physical controller index, or kBindNoController
  uint8 deadzone;                         // percent, for the left stick
  bool stick_moves;                       // left stick acts as the d-pad
  bool rumble;                            // the controller rumbles when this player is hurt
} BindProfile;

extern BindProfile g_bind[kBindPlayers];

void Bindings_SetDefaults();
void Bindings_ResetPlayer(int player);
// Swaps A/B and X/Y on the controller (Xbox <-> Nintendo layout).
void Bindings_SwapFaceButtons(int player);

const char *Bindings_ButtonName(int b);
uint16 Bindings_ButtonBit(int b);  // bit in the joypad word the game reads
const char *Bindings_GamepadButtonName(int pad_button);
void Bindings_DescribeKeys(int player, int b, char *buf, size_t size);
void Bindings_DescribePad(int player, int b, char *buf, size_t size);

// Assigns a key/button to a SNES button: fills a free slot or replaces the first one.
// Also removes it from other buttons of that player so one key does one thing.
void Bindings_SetKey(int player, int b, int32 key);
void Bindings_SetPad(int player, int b, int pad_button);
void Bindings_Clear(int player, int b);

// Runtime input state
void Bindings_OnKey(int32 key, bool pressed);
bool Bindings_IsGameKey(int32 key);
void Bindings_OnPadButton(int controller, int pad_button, bool pressed);
void Bindings_OnPadAxis(int controller, int axis_x, int axis_y);
void Bindings_ResetController(int controller);
uint16 Bindings_GetInput(int player);
// Inputs for the overlay menu from fixed keys/buttons that work regardless of
// bindings: the arrow keys and the d-pad of that controller (-1: any).
uint16 Bindings_GetMenuFallbackInput(int owner, int controller);

// Config file: keys "P1Keys", "P1Pad", "P1Controller", "P1Deadzone", "P1Stick", "P1Rumble".
bool Bindings_ParseConfig(const char *key, char *value);
void Bindings_WriteConfig(void *file);
// Called by the legacy [KeyMap]/[GamepadMap] Controls settings for player 1.
void Bindings_LegacyKey(int b_snes_order, int32 key);
void Bindings_LegacyPad(int b_snes_order, int pad_button);

#endif  // ZELDA3_BINDINGS_H_
