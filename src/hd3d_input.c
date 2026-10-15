// The 3D view: cameras, camera input, the View button, camera-relative
// movement and panes (see hd3d.h).
// All of it is display state, kept out of g_ram, g_mp, snapshots, the state
// hash and the joypad word. The one thing that reaches the game is
// Hd3d_RemapLocal: which way a local player's d-pad walks while its camera is
// turned, applied before the input is sent (so netplay stays in sync).
#include <SDL.h>
#include <math.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "config.h"
#include "bindings.h"
#include "session.h"
#include "instance.h"
#include "mp.h"
#include "ui.h"

enum {
  kViewTapMs = 400,     // a View press released sooner toggles first person
  kViewHoldMs = 500,    // held this long it resets the camera
  kFocusClickMs = 250,  // a click this soon after the window got focus only focuses it
};

#define kHintText "Right stick turns. L3/click: first person"

Cam3d g_hd3d_cam[kHd3dMaxLocal];

typedef struct ViewButton {
  bool down;     // as seen last time
  bool armed;    // pressed while it could act: its release or hold counts
  bool fired;    // the hold already reset the camera
  uint32 t0;     // when it was pressed
} ViewButton;

static struct {
  // Each local player's choice of the 3D view (the menu, F11 for player 1,
  // zelda3.ini). g_hd3d.enabled: any of them. Online, every computer has its
  // own choice: nothing of the 3D view is sent.
  bool want3d[kHd3dMaxLocal];
  // The player's choice of first person. A downed player watching a teammate
  // sees that teammate in third person: the choice waits for the revive.
  bool fp_wanted[kHd3dMaxLocal];
  Cam3dRemap remap[kHd3dMaxLocal];
  ViewButton view[kHd3dMaxLocal];
  // Right sticks set by the tests (instead of the controllers).
  bool stick_set[kHd3dMaxLocal];
  float stick[kHd3dMaxLocal][2];
  // The panes as main.c draws them: whose camera each one uses (until set:
  // one pane, pane v uses local v).
  bool panes_set;
  int8 pane_local[kHd3dMaxPanes];
  int npanes;
  // The mouse and the camera keys belong to local player 0, like the keyboard.
  float mouse_dx, mouse_dy;
  int wheel;
  bool right_held;
  bool keys[4];         // kKeys_View3dTurnLeft, TurnRight, TiltUp, TiltDown
  bool unfocused;
  bool focus_seen;      // a focus-gained event came (at focus_time)
  uint32 focus_time;
  bool was_relative;
  // The clock for taps and holds: SDL_GetTicks, or in the tests the sum of
  // the steps given to Hd3d_Update (so they don't depend on real time).
  bool test_clock;
  float test_ms;
  bool quiet;           // no toasts (the tests' pictures)
  bool hint_shown;
} g_in;

static uint32 Now(void) {
  return g_in.test_clock ? (uint32)g_in.test_ms : SDL_GetTicks();
}

static void Toast(const char *msg) {
  if (!g_in.quiet)
    Hd3d_Toast(msg);
}

static bool ValidLocal(int i) {
  return i >= 0 && i < kHd3dMaxLocal;
}

static float Clampf(float v, float lo, float hi) {
  return !(v >= lo) ? lo : v > hi ? hi : v;  // (NaN: lo)
}

// Degrees into [-180, 180).
static float Wrap180(float a) {
  if (!(a > -1e6f && a < 1e6f))
    return 0.0f;
  a -= 360.0f * floorf((a + 180.0f) * (1.0f / 360.0f));
  return a >= 180.0f ? a - 360.0f : a < -180.0f ? a + 360.0f : a;
}

// The player slot of local player i (-1: not playing).
static int LocalSlot(int i) {
  return ValidLocal(i) ? Session_GetStatus()->local_slot[i] : -1;
}

// Downed and watching a teammate (Ui_WatchedPlayer).
static bool WatchingOther(int i) {
  int p = LocalSlot(i);
  return p >= 0 && Ui_WatchedPlayer(i) != p;
}

// (Whether the 3D view is on or not: the camera is ready when it is.)
static bool SeesFirstPerson(int i) {
  return g_in.want3d[i] && g_in.fp_wanted[i] && !WatchingOther(i);
}

// Whether local players i and j could share a pane: both seeing it in 3D,
// or both in 2D.
static bool SameView(int i, int j) {
  return g_in.want3d[i] == g_in.want3d[j];
}

// Which way player p's Link faces (RAM 0x2F: 0 up, 2 down, 4 left, 6 right).
// (Its world is bound for a moment, under the audio lock like any switch.)
static int PlayerFacing(int p) {
  int w = p >= 0 ? Inst_OfPlayer(p) : -1;
  if (w < 0)
    return 0;
  ZeldaApuLock();
  int home = Inst_Current();
  Inst_Switch(w);
  int facing = *Mp_PlayerRam(p, 0x2F);
  Inst_Switch(home);
  ZeldaApuUnlock();
  return facing;
}

// The camera shows first person exactly when the player sees it. Entering
// faces where the Link faces; leaving eases back (cam3d.c).
static void SyncFirstPerson(int i) {
  bool on = SeesFirstPerson(i);
  Cam3d *c = &g_hd3d_cam[i];
  if (c->first_person != on)
    Cam3d_SetFirstPerson(c, on, on ? PlayerFacing(LocalSlot(i)) : 0);
}

// The world local player i sees (-1: not playing).
static int ViewOf(int i) {
  return LocalSlot(i) < 0 ? -1 : Inst_ViewOfPlayer(Ui_WatchedPlayer(i));
}

// The camera local player i turns and walks by: its own in first person; in
// third person that of the pane it shares, which belongs to the first local
// player seeing that world in third person (as in Hd3d_ComputeViews).
static int CameraLocal(int i) {
  if (SeesFirstPerson(i))
    return i;
  int w = ViewOf(i);
  for (int j = 0; j < i && w >= 0; j++)
    if (ViewOf(j) == w && !SeesFirstPerson(j) && SameView(i, j))
      return j;
  return i;
}

void Hd3dInput_Tuning(Cam3dTuning *t) {
  t->perspective = g_hd3d.perspective;
  t->fov = g_hd3d.fov;
  t->stick_speed = g_hd3d.stick_speed;
  t->mouse_speed = g_hd3d.mouse_speed;
  t->invert_y = g_hd3d.invert_y;
  t->camera_returns = g_hd3d.camera_returns;
}

int Hd3dInput_PaneLocal(int view) {
  if (view < 0 || view >= kHd3dMaxPanes)
    return 0;
  int i = g_in.panes_set ? g_in.pane_local[view] : view;
  if (i == kHd3dPaneFlat)
    return -1;  // (no camera: never a local player's, so never the d-pad's)
  return ValidLocal(i) ? i : 0;  // (-1: nobody plays here; local 0's camera)
}

bool Hd3dInput_PaneFlat(int view) {
  return view >= 0 && view < kHd3dMaxPanes && g_in.panes_set && g_in.pane_local[view] == kHd3dPaneFlat;
}

int Hd3dInput_PaneCount(void) {
  return g_in.panes_set ? g_in.npanes : 1;
}

void Hd3dInput_FaceLink(int local, int facing) {
  if (!ValidLocal(local) || !g_hd3d_cam[local].first_person)
    return;
  // RAM 0x2F: 0 up, 2 down, 4 left, 6 right (as Cam3d_SetFirstPerson).
  static const float kFacingYaw[4] = { 0.0f, 180.0f, 270.0f, 90.0f };
  Cam3d *c = &g_hd3d_cam[local];
  float yaw = kFacingYaw[(facing >> 1) & 3];
  if (fabsf(Wrap180(c->fp_yaw - yaw)) <= 50.0f)
    return;
  c->fp_yaw = yaw;
  c->rate_yaw = 0.0f;
}

void Hd3dInput_ResetAll(void) {
  for (int i = 0; i < kHd3dMaxLocal; i++) {
    Cam3d_Reset(&g_hd3d_cam[i]);
    Cam3d_RemapReset(&g_in.remap[i]);
    g_in.fp_wanted[i] = false;
    g_in.stick_set[i] = false;
    g_in.stick[i][0] = g_in.stick[i][1] = 0.0f;
    memset(&g_in.view[i], 0, sizeof(g_in.view[i]));
  }
  g_in.panes_set = false;
  g_in.mouse_dx = g_in.mouse_dy = 0.0f;
  g_in.wheel = 0;
  g_in.right_held = false;
  memset(g_in.keys, 0, sizeof(g_in.keys));
}

// --- Panes ---

int Hd3d_ComputeViews(int *views, int8 *pane_local, int max) {
  // With 3D off nobody sees first person: one pane per world, as always.
  // A player seeing a world in 2D and one seeing it in 3D get a pane each.
  int n = 0;
  bool tp[kHd3dMaxPanes] = { 0 };
  for (int i = 0; i < kHd3dMaxLocal && n < max && n < kHd3dMaxPanes; i++) {
    int w = ViewOf(i);
    if (w < 0)
      continue;
    if (g_hd3d.enabled)
      SyncFirstPerson(i);
    bool fp = g_hd3d.enabled && SeesFirstPerson(i);
    bool shared = false;
    for (int v = 0; v < n && !fp && !shared; v++)
      shared = tp[v] && views[v] == w && SameView(i, pane_local[v]);
    if (shared)
      continue;
    views[n] = w, pane_local[n] = (int8)i, tp[n] = !fp;
    n++;
  }
  if (n == 0 && max > 0)
    views[n] = Inst_Current(), pane_local[n] = -1, n++;
  return n;
}

void Hd3d_SetPanes(const int8 *pane_local, int n) {
  n = n < 1 ? 1 : n > kHd3dMaxPanes ? kHd3dMaxPanes : n;
  for (int v = 0; v < kHd3dMaxPanes; v++)
    g_in.pane_local[v] = (int8)(v < n ? pane_local[v] : v);
  g_in.npanes = n;
  g_in.panes_set = true;
}

// --- Input ---

void Hd3d_Update(float dt) {
  dt = Clampf(dt, 0.0f, 0.05f);
  if (g_in.test_clock)
    g_in.test_ms += dt * 1000.0f;
  bool on = g_hd3d.enabled;
  int owner = Ui_MenuOwner();
  // The mouse turns local player 0's camera while it is captured: mouse look
  // in first person, or a drag with the right button. (Not the first motion
  // of a capture: it can be the jump of the cursor.)
  bool relative = Hd3d_WantsRelativeMouse();
  bool mouse = relative && g_in.was_relative && owner != 0;
  g_in.was_relative = relative;
  Cam3dInput in[kHd3dMaxLocal];
  memset(in, 0, sizeof(in));
  for (int i = 0; i < kHd3dMaxLocal && on; i++) {
    SyncFirstPerson(i);
    // The menu's owner is busy with the menu; a player seeing 2D has no camera.
    if (LocalSlot(i) < 0 || i == owner || !g_in.want3d[i])
      continue;
    Cam3dInput *t = &in[CameraLocal(i)];
    float x = g_in.stick[i][0], y = g_in.stick[i][1];
    if (!g_in.stick_set[i])
      Bindings_GetRightStick(i, &x, &y);
    t->stick_x += x, t->stick_y += y;
    if (i == 0) {
      if (mouse)
        t->mouse_dx += g_in.mouse_dx, t->mouse_dy += g_in.mouse_dy;
      t->key_turn += (float)(g_in.keys[1] - g_in.keys[0]);
      t->key_tilt += (float)(g_in.keys[2] - g_in.keys[3]);
      t->wheel += g_in.wheel;
    }
  }
  g_in.mouse_dx = g_in.mouse_dy = 0.0f;
  g_in.wheel = 0;
  Cam3dTuning tuning;
  Hd3dInput_Tuning(&tuning);
  for (int i = 0; i < kHd3dMaxLocal; i++)
    Cam3d_Update(&g_hd3d_cam[i], &in[i], &tuning, dt);
  // The first time a pane shows 3D: how to use it.
  for (int v = 0; v < g_in.npanes && on && !g_in.hint_shown; v++) {
    if (Hd3d_PaneIs3D(v)) {
      g_in.hint_shown = true;
      Toast(kHintText);
    }
  }
}

static void ToggleFp(int i, bool toast) {
  g_in.fp_wanted[i] = !g_in.fp_wanted[i];
  SyncFirstPerson(i);
  if (toast)
    Toast(g_in.fp_wanted[i] ? "First person" : "Third person");
}

// Who may use the View button: a player in the game with the 3D view on,
// and not busy with the menu.
static bool CanUseView(int i) {
  return g_in.want3d[i] && LocalSlot(i) >= 0 && i != Ui_MenuOwner();
}

void Hd3d_HandleViewButtons(void) {
  uint32 now = Now();
  for (int i = 0; i < kHd3dMaxLocal; i++) {
    ViewButton *vb = &g_in.view[i];
    bool down = Bindings_ButtonDown(i, kBind_View), can = CanUseView(i);
    if (down && !vb->down) {
      vb->armed = can, vb->fired = false, vb->t0 = now;
    } else if (down && vb->armed && can && !vb->fired && now - vb->t0 >= kViewHoldMs) {
      vb->fired = true;
      Hd3d_ResetCamera(i);
    } else if (!down && vb->down && vb->armed && can && !vb->fired && now - vb->t0 < kViewTapMs) {
      ToggleFp(i, false);
    }
    if (!down || !can)
      vb->armed = false;
    vb->down = down;
  }
}

// The d-pad reaches the game as it is. A walk in progress is latched
// unturned, so the remap coming on under it (the 3D view back on, first
// person entered with "Controls turn with camera" off) keeps its way until
// the d-pad is let go.
static uint16 Unturned(Cam3dRemap *r, uint16 input) {
  if (input & 0xf0)
    r->latched = true, r->latched_yaw = 0.0f, r->latched_fp = false;
  else
    Cam3d_RemapReset(r);
  return input;
}

// Whether the camera cl that local i walks by was seen in 3D. The panes are
// last frame's: right after first person was entered or left (the View
// button is handled before the panes are drawn again) cl has no pane yet,
// and what the player saw was the pane that showed i's world.
static bool CameraSees3D(int i, int cl) {
  if (!g_in.want3d[i])
    return false;
  int n = IntMin(Hd3dInput_PaneCount(), kHd3dMaxPanes), w = ViewOf(i);
  for (int v = 0; v < n; v++)
    if (Hd3dInput_PaneLocal(v) == cl)
      return Hd3d_PaneIs3D(v);
  for (int v = 0; v < n && w >= 0; v++)
    if (ViewOf(Hd3dInput_PaneLocal(v)) == w)
      return Hd3d_PaneIs3D(v);
  return false;
}

uint16 Hd3d_RemapLocal(int local, int player, uint16 input) {
  if (!ValidLocal(local))
    return input;
  Cam3dRemap *r = &g_in.remap[local];
  // A player seeing 2D or a ghost watching a teammate is never turned.
  if (!g_in.want3d[local] || player < 0 || Ui_WatchedPlayer(local) != player)
    return Unturned(r, input);
  SyncFirstPerson(local);
  int cl = CameraLocal(local);
  SyncFirstPerson(cl);
  const Cam3d *c = &g_hd3d_cam[cl];
  uint8 m = Inst_PlayerViewModule(player), s = Inst_PlayerViewSubmodule(player);
  // Only in the world's own modules (not in menus or text boxes: a direction
  // held through one turns with the camera afterwards).
  if (!(m == 7 || m == 9 || m == 11)) {
    Cam3d_RemapReset(r);
    return input;
  }
  // Where the camera was really seen in 3D while walking around, and in
  // third person only with "Controls turn with camera". (Through the game's
  // own scrolls and transitions a walk keeps its way, also where they are
  // 2D: they ignore the d-pad until their last frame, whose word the game
  // then plays on the first frame of plain play.)
  bool sees = CameraSees3D(local, cl);
  if ((s == 0 && !sees) || !(c->first_person || g_hd3d.turn_controls))
    return Unturned(r, input);
  // A 2D transition with no walk latched yet (it went through a module that
  // let go of it, entering a house say, or a direction pressed again during
  // a scroll): unturned, but not latched, so the walk is latched against
  // the real picture once plain play shows it.
  if (s != 0 && !sees && !r->latched) {
    Cam3d_RemapReset(r);
    return input;
  }
  // The left stick's own direction when it moves Link (a single rounding to
  // 8 directions); keys and the d-pad as they are.
  float mx = 0.0f, my = 0.0f;
  if (!(Bindings_GetDigitalInput(local) & 0xF0))
    Bindings_GetMoveStick(local, &mx, &my);
  return Cam3d_Remap(r, c, input, mx, my);
}

void Hd3d_RemapInputs(uint16 *local_inputs) {
  for (int i = 0; i < kHd3dMaxLocal; i++)
    local_inputs[i] = Hd3d_RemapLocal(i, LocalSlot(i), local_inputs[i]);
}

void Hd3d_OnMouseButton(int button, bool pressed, int clicks, uint32 mods, bool touch) {
  if (touch)
    return;
  if (button == SDL_BUTTON_RIGHT) {
    g_in.right_held = pressed;
    return;
  }
  // A click that only brings the window to the front, a Shift+double-click
  // (borderless), a click for the menu or in a 2D picture does nothing.
  if (!pressed || !g_in.want3d[0] || Ui_IsOpen() || LocalSlot(0) < 0 ||
      (mods & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) || (g_in.focus_seen && Now() - g_in.focus_time <= kFocusClickMs))
    return;
  if (button == SDL_BUTTON_LEFT && clicks == 1 && Hd3d_LocalSees3D(0))  // (local 0's pane is its own)
    ToggleFp(0, false);
  else if (button == SDL_BUTTON_MIDDLE)
    Hd3d_ResetCamera(0);
}

void Hd3d_OnMouseMotion(int xrel, int yrel) {
  g_in.mouse_dx = Clampf(g_in.mouse_dx + (float)xrel, -10000.0f, 10000.0f);
  g_in.mouse_dy = Clampf(g_in.mouse_dy + (float)yrel, -10000.0f, 10000.0f);
}

void Hd3d_OnMouseWheel(int dy) {
  g_in.wheel = IntMin(IntMax(g_in.wheel + dy, -100), 100);
}

void Hd3d_OnWindowFocus(bool gained) {
  g_in.unfocused = !gained;
  if (gained) {
    g_in.focus_seen = true;
    g_in.focus_time = Now();
  } else {
    // Buttons let go of elsewhere never come back up here.
    g_in.right_held = false;
    memset(g_in.keys, 0, sizeof(g_in.keys));
    g_in.mouse_dx = g_in.mouse_dy = 0.0f;
    g_in.wheel = 0;
  }
}

void Hd3d_OnKeyCommand(int command, bool pressed) {
  switch (command) {
  case kKeys_View3d:
    // A quick comparison for player 1 (the keyboard's): not saved.
    if (pressed)
      Hd3d_SetLocalEnabled(0, !g_in.want3d[0]);
    break;
  case kKeys_View3dTurnLeft:
  case kKeys_View3dTurnRight:
  case kKeys_View3dTiltUp:
  case kKeys_View3dTiltDown:
    g_in.keys[command - kKeys_View3dTurnLeft] = pressed;
    break;
  case kKeys_View3dReset:
    if (pressed && CanUseView(0))
      Hd3d_ResetCamera(0);
    break;
  }
}

bool Hd3d_WantsRelativeMouse(void) {
  return g_in.want3d[0] && !g_in.unfocused && !Ui_IsOpen() && LocalSlot(0) >= 0 && Hd3d_LocalSees3D(0) &&
         ((SeesFirstPerson(0) && g_hd3d.mouse_look) || g_in.right_held);
}

void Hd3d_ToggleFirstPerson(int local) {
  if (ValidLocal(local))
    ToggleFp(local, true);
}

static void ResetThirdPerson(Cam3d *c) {
  c->yaw = 0.0f, c->pitch = 45.0f, c->zoom = 1.0f;
  c->rate_yaw = c->rate_pitch = 0.0f;
  c->idle = 0.0f;
}

// Back to the classic view: third person at the original angle, for this
// player's own camera and the shared one it sees.
void Hd3d_ResetCamera(int local) {
  if (!ValidLocal(local))
    return;
  g_in.fp_wanted[local] = false;
  SyncFirstPerson(local);
  ResetThirdPerson(&g_hd3d_cam[local]);
  ResetThirdPerson(&g_hd3d_cam[CameraLocal(local)]);
  Toast("Camera reset");
}

// The flag of local player i, and g_hd3d.enabled from all of them.
static void SetWant3D(int i, bool on) {
  g_in.want3d[i] = on;
  bool any = false;
  for (int j = 0; j < kHd3dMaxLocal; j++)
    any |= g_in.want3d[j];
  g_hd3d.enabled = any;
  g_hd3d_tagging = any;
}

void Hd3d_SetLocalEnabled(int local, bool on) {
  if (!ValidLocal(local))
    return;
  SetWant3D(local, on);
  // (First person is entered again when 3D comes back, facing where Link
  // faces then: fp_wanted is kept.)
  if (!on)
    Cam3d_SetFirstPerson(&g_hd3d_cam[local], false, 0);
}

void Hd3d_SetEnabled(bool on) {
  for (int i = 0; i < kHd3dMaxLocal; i++)
    Hd3d_SetLocalEnabled(i, on);
}

bool Hd3d_LocalEnabled(int local) {
  return ValidLocal(local) && g_in.want3d[local];
}

// --- (Tests) ---

void Hd3d_SetCamera(int local, const Hd3dCamState *s) {
  if (!ValidLocal(local))
    return;
  Cam3d *c = &g_hd3d_cam[local];
  g_in.fp_wanted[local] = s->first_person;
  SyncFirstPerson(local);
  if (s->first_person) {
    float yaw = Wrap180(s->yaw);
    c->fp_yaw = yaw < 0.0f ? yaw + 360.0f : yaw;
    c->fp_pitch = Clampf(s->pitch, -60.0f, 45.0f);
  } else {
    c->yaw = Wrap180(s->yaw);
    c->pitch = Clampf(s->pitch, 10.0f, 75.0f);
    c->zoom = Clampf(s->zoom, 0.75f, 2.0f);
  }
  c->blend = c->first_person ? 1.0f : 0.0f;
  c->rate_yaw = c->rate_pitch = 0.0f;
  c->idle = 0.0f;
}

void Hd3d_GetCamera(int local, Hd3dCamState *s) {
  memset(s, 0, sizeof(*s));
  if (!ValidLocal(local))
    return;
  const Cam3d *c = &g_hd3d_cam[local];
  s->first_person = g_in.fp_wanted[local];
  s->yaw = c->first_person ? c->fp_yaw : c->yaw;
  s->pitch = c->first_person ? c->fp_pitch : c->pitch;
  s->zoom = c->zoom;
}

void Hd3d_SetRightStick(int local, float x, float y) {
  if (!ValidLocal(local))
    return;
  g_in.stick_set[local] = x != 0.0f || y != 0.0f;
  g_in.stick[local][0] = Clampf(x, -1.0f, 1.0f);
  g_in.stick[local][1] = Clampf(y, -1.0f, 1.0f);
}

void Hd3d_PressViewButton(int local, int hold_ms) {
  if (!ValidLocal(local) || !CanUseView(local))
    return;
  if (hold_ms >= kViewHoldMs)
    Hd3d_ResetCamera(local);
  else if (hold_ms < kViewTapMs)
    ToggleFp(local, false);
}

void Hd3d_SetLocalEnabledQuiet(int local, bool on) {
  if (ValidLocal(local))
    SetWant3D(local, on);
}

void Hd3d_ResetForTests(void) {
  // As zelda3.ini's defaults, but 3D off and Camera returns off (a camera
  // set by a test stays where it was put).
  static const Hd3dSettings kTestSettings = {
    false, false, true, true, true, false,  // enabled, camera_returns, turn_controls, mouse_look, fade_scenery, invert_y
    100, 50, 65, 50,                        // stick_speed, mouse_speed, fov, perspective
  };
  g_hd3d = kTestSettings;
  Hd3d_SetEnabled(false);
  Hd3dInput_ResetAll();
  g_in.unfocused = false;
  g_in.focus_seen = false;
  g_in.was_relative = false;
  g_in.test_clock = true;
  g_in.test_ms = 0.0f;
  g_in.quiet = true;
}
