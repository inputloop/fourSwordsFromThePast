// On-screen overlay: corner health display for co-op, notifications and the
// in-game Options / Multiplayer menu. Drawn on top of the rendered frame and
// never part of the game simulation.
#ifndef ZELDA3_UI_H_
#define ZELDA3_UI_H_

#include "types.h"

typedef struct UiHostFuncs {
  // Re-applies window/renderer settings from g_config (aspect ratio, filtering, ...).
  void (*apply_display)(void);
  void (*toggle_fullscreen)(void);
  void (*change_window_scale)(int delta);
  void (*quit)(void);
} UiHostFuncs;

void Ui_Init(const UiHostFuncs *host);
// Processes menu navigation. Clears inputs that the menu consumed.
// fallback holds menu-only inputs from keys/buttons that work regardless of bindings.
void Ui_Update(uint16 *local_inputs, uint16 fallback);
// A controller button press; returns true if the menu used it (e.g. while assigning buttons).
bool Ui_HandlePadButton(int controller, int pad_button);
// Keyboard/text events (used for the menu toggle key and typing addresses).
// Returns true if the key was consumed.
bool Ui_HandleKey(int sdl_keycode, bool pressed);
void Ui_HandleText(const char *text);
bool Ui_IsOpen();
// True when the game should not advance (menu open while playing alone).
bool Ui_PausesGame();
// Draws the overlay into a 32-bit XRGB buffer. scale is the render scale (1 or 4).
void Ui_Draw(uint8 *pixels, int pitch, int width, int height, int scale);
// Writes the settings changed through the menu to zelda3.coop.ini.
void Ui_SaveSettings();

#endif  // ZELDA3_UI_H_
