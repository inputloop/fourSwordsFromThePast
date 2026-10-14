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
// The player whose world local player i's screen shows: itself, or while it
// is down, whoever it chose to watch with L/R (display only).
int Ui_WatchedPlayer(int local_index);
// A controller button press; returns true if the menu used it (e.g. while assigning buttons).
bool Ui_HandlePadButton(int controller, int pad_button);
// Keyboard/text events (used for the menu toggle key and typing addresses).
// Returns true if the key was consumed.
bool Ui_HandleKey(int sdl_keycode, bool pressed);
// The Guide button of a controller opens/closes the menu for its player.
void Ui_ToggleFromController(int controller);
// The controller of the player using the menu, or -1.
int Ui_MenuController();
// The local player whose menu is open, or -1.
int Ui_MenuOwner();
void Ui_HandleText(const char *text);
bool Ui_IsOpen();
// Local player i is looking at the check tracker (shown after closing the
// game's map in a randomized file; never pauses the game).
bool Ui_TrackerShown(int local_index);
// True when the game should not advance (menu open while playing alone).
bool Ui_PausesGame();
// The screens on the window (one, or one per pane of a split screen), in
// overlay pixels, whose eyes each one looks through (-1: nobody's), and the
// local player whose 3D camera it uses (-1: nobody's).
typedef struct UiView {
  int x, y, w, h;
  int player;
  int local;
} UiView;
void Ui_SetViews(const UiView *views, int n);
// "Watch everyone" (display only): every joined player's world on screen,
// up to four panes. F12 (KeyMap WatchEveryone) and the Multiplayer menu.
bool Ui_WatchEveryone();
void Ui_ToggleWatchEveryone();
// The panes to draw, given those of the players on this computer (n of them,
// from Hd3d_ComputeViews): with "Watch everyone" on, after them one pane for
// each other joined player's world that no pane shows yet (up to 4 in all),
// with pane_local kHd3dPaneFlat (always drawn in 2D). players[] gets whose
// eyes each pane looks through (UiView.player). Returns the new count.
int Ui_AddWatchedPanes(int *views, int8 *pane_local, int *players, int n, int max);
// Draws the overlay into a 32-bit XRGB buffer. scale is the render scale (1 or 4).
void Ui_Draw(uint8 *pixels, int pitch, int width, int height, int scale);
// Writes the settings changed through the menu to zelda3.coop.ini.
void Ui_SaveSettings();
// A short message at the bottom of the screen for a few seconds.
void Ui_Toast(const char *msg);

#endif  // ZELDA3_UI_H_
