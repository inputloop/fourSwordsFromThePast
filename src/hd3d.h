// The 3D view (beta): the world as a little diorama built from the game's
// own tiles, with Links, enemies and items standing in it as flat sprites, a
// camera on the right stick or mouse, and a first-person view.
// It only changes the picture. The one thing it changes in the game is which
// way the d-pad walks for a local player whose camera is turned: that is done
// on this computer before the input is sent (main.c, between Ui_Update and
// Session_Tick), so netplay stays in sync.
#ifndef ZELDA3_HD3D_H_
#define ZELDA3_HD3D_H_

#include "types.h"

typedef struct Hd3dSettings {
  bool enabled;              // the 3D view
  bool camera_returns;       // third person: back to the classic view 2 s after letting go
  bool turn_controls;        // third person: the d-pad follows a turned camera (first person always does)
  bool mouse_look;           // first person: the mouse turns the view (the cursor is captured)
  bool fade_scenery;         // turned camera: scenery between the camera and your Link is faded
  bool invert_y;             // camera up/down inverted
  uint8 stick_speed;         // percent, 25-200
  uint8 mouse_speed;         // 0-100
  uint8 fov;                 // first person vertical field of view, degrees, 50-90
  uint8 perspective;         // 0-100: 0 nearly flat, 100 strong
} Hd3dSettings;
extern Hd3dSettings g_hd3d;

// --- Drawing (hd2d.c) ---
// Draws pane `view` in 3D if it can. False: nothing drawn, the 2D path must run.
bool Hd3d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view);
// After the 2D path drew pane `view`: a short fade out of 3D, and notes the pane was 2D.
void Hd3d_After2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view);
// Split screen: between Hd3d_BeginPanes(true) and Hd3d_FinishPanes(),
// Hd3d_DrawFrame only reads the world (under the audio lock); Hd3d_FinishPanes
// then draws the 3D panes outside the lock. Hd3d_BeginPanes(false): draw at once.
void Hd3d_BeginPanes(bool deferred);
void Hd3d_FinishPanes(void);

// --- Panes (main.c, mptest) ---
// The panes: one per world seen, plus one for each local player in first
// person. views[] gets world instances, pane_local[] the local player whose
// camera each pane uses. With 3D off it is exactly the old one-pane-per-world list.
int Hd3d_ComputeViews(int *views, int8 *pane_local, int max);
void Hd3d_SetPanes(const int8 *pane_local, int n);   // before drawing (default: pane v uses local v)
// A pane_local for a pane that is always drawn in 2D through nobody's camera
// ("Watch everyone": the world of a player on another computer).
enum { kHd3dPaneFlat = -2 };
bool Hd3d_PaneIs3D(int view);                        // the last picture of pane view was 3D

// --- Input (main.c once per loop; mptest's tick does the same) ---
void Hd3d_Update(float dt);                          // cameras, in real seconds (clamped to 0.05)
void Hd3d_HandleViewButtons(void);                   // View taps (first person) and holds (reset)
void Hd3d_RemapInputs(uint16 *local_inputs);         // after Ui_Update, before Session_Tick
uint16 Hd3d_RemapLocal(int local, int player, uint16 input);  // one local player (also mptest run/drawrun)
void Hd3d_OnMouseButton(int button, bool pressed, int clicks, uint32 mods, bool touch);  // SDL_BUTTON_*, KMOD_*
void Hd3d_OnMouseMotion(int xrel, int yrel);
void Hd3d_OnMouseWheel(int dy);
void Hd3d_OnWindowFocus(bool gained);
void Hd3d_OnKeyCommand(int command, bool pressed);   // the kKeys_View3d* hotkeys (press and release)
bool Hd3d_WantsRelativeMouse(void);
void Hd3d_ToggleFirstPerson(int local);
void Hd3d_ResetCamera(int local);
// Each local player chooses the 3D view or the flat picture for their own
// pane (the menu for the player using it, F11 for player 1). A 2D player
// and a 3D player in one world get a pane each. g_hd3d.enabled: any of them.
void Hd3d_SetLocalEnabled(int local, bool on);
bool Hd3d_LocalEnabled(int local);
void Hd3d_SetEnabled(bool on);                       // every local player (zelda3.ini's Hd3d, the tests)

// --- Overlay (ui.c) ---
// Where the point `above` px over the ground at world (x, y) is in pane
// view (pane pixels). False: the pane isn't 3D (use the 2D math), or the
// point is behind the camera (*behind is set; sx, sy point its way).
bool Hd3d_Project(int view, int x, int y, int above, int *sx, int *sy, bool *behind);
float Hd3d_ScaleAt(int view, int x, int y);          // pane pixels per world pixel there (ping rings)
void Hd3d_Toast(const char *msg);                    // (ui.c implements it: Ui_Toast)

// --- Game side, display only: which object drew which OAM entries ---
// Called from the game's draw loops; does nothing while g_hd3d_tagging is
// off. Kept per world, never in a snapshot or the state hash.
enum {
  kHd3dTag_None, kHd3dTag_Link, kHd3dTag_Sprite, kHd3dTag_Ancilla, kHd3dTag_Garnish,
  kHd3dTag_Follower, kHd3dTag_PushBlock,
};
extern bool g_hd3d_tagging;
void Hd3d_TagFrameStart(void);
void Hd3d_TagBegin(uint8 kind, uint8 index);
void Hd3d_TagEnd(void);

// --- (Tests) ---
typedef struct Hd3dCamState { float yaw, pitch, zoom; bool first_person; } Hd3dCamState;
void Hd3d_SetCamera(int local, const Hd3dCamState *c);   // right away, no easing
void Hd3d_GetCamera(int local, Hd3dCamState *c);
void Hd3d_SetRightStick(int local, float x, float y);    // overrides the pad until set back to 0,0
void Hd3d_PressViewButton(int local, int hold_ms);       // a tap (or a hold) of the View button
void Hd3d_SetTestCamera(bool on);                        // parallel oblique camera, no fog or extras
const char *Hd3d_LastWhy2D(int view);                    // "none" if pane view was drawn in 3D
void Hd3d_GetStats(int view, int *quads, int *cards);
int Hd3d_StandZAt(int view, int x, int y);               // what stands at picture point (x, y) of pane view stands on (-1: not 3D)
void Hd3d_DumpScene(const char *ppm_path);               // (Debug) the bound world's heights
void Hd3d_SetLocalEnabledQuiet(int local, bool on);      // the flag alone, cameras untouched (a check)
void Hd3d_ResetForTests(void);                           // 3D off, defaults, cameras reset, no toasts

#endif  // ZELDA3_HD3D_H_
