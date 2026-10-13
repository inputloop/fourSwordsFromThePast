// The 3D view: cameras, camera input, the View button, camera-relative
// movement and panes (see hd3d.h).
#include <SDL.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"

Cam3d g_hd3d_cam[kHd3dMaxLocal];

void Hd3dInput_Tuning(Cam3dTuning *t) {
  t->perspective = g_hd3d.perspective;
  t->fov = g_hd3d.fov;
  t->stick_speed = g_hd3d.stick_speed;
  t->mouse_speed = g_hd3d.mouse_speed;
  t->invert_y = g_hd3d.invert_y;
  t->camera_returns = g_hd3d.camera_returns;
}

int Hd3dInput_PaneLocal(int view) {
  return view;
}

int Hd3dInput_PaneCount(void) {
  return 1;
}

void Hd3dInput_ResetAll(void) {
  for (int i = 0; i < kHd3dMaxLocal; i++)
    Cam3d_Reset(&g_hd3d_cam[i]);
}

int Hd3d_ComputeViews(int *views, int8 *pane_local, int max) {
  return 0;
}

void Hd3d_SetPanes(const int8 *pane_local, int n) {
}

void Hd3d_Update(float dt) {
}

void Hd3d_HandleViewButtons(void) {
}

void Hd3d_RemapInputs(uint16 *local_inputs) {
}

uint16 Hd3d_RemapLocal(int local, int player, uint16 input) {
  return input;
}

void Hd3d_OnMouseButton(int button, bool pressed, int clicks, uint32 mods, bool touch) {
}

void Hd3d_OnMouseMotion(int xrel, int yrel) {
}

void Hd3d_OnMouseWheel(int dy) {
}

void Hd3d_OnWindowFocus(bool gained) {
}

void Hd3d_OnKeyCommand(int command, bool pressed) {
}

bool Hd3d_WantsRelativeMouse(void) {
  return false;
}

void Hd3d_ToggleFirstPerson(int local) {
}

void Hd3d_ResetCamera(int local) {
}

void Hd3d_SetEnabled(bool on) {
  g_hd3d.enabled = on;
}

void Hd3d_SetCamera(int local, const Hd3dCamState *c) {
}

void Hd3d_GetCamera(int local, Hd3dCamState *c) {
  memset(c, 0, sizeof(*c));
}

void Hd3d_SetRightStick(int local, float x, float y) {
}

void Hd3d_PressViewButton(int local, int hold_ms) {
}

void Hd3d_ResetForTests(void) {
  g_hd3d.enabled = false;
  Hd3dInput_ResetAll();
}
