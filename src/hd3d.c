// The 3D view (beta): drawing a pane in 3D (see hd3d.h).
#include <SDL.h>
#include <string.h>
#include "hd3d_internal.h"
#include "hd2d.h"
#include "zelda_rtl.h"
#include "variables.h"

Hd3dSettings g_hd3d;

const char *const kHd3dWhyNames[kHd3dWhy_Count] = {
  "none", "off", "renderer", "scale", "mode", "blank", "mosaic", "window", "module", "room", "size", "memory",
};

bool Hd3d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  return false;
}

void Hd3d_After2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
}

void Hd3d_BeginPanes(bool deferred) {
}

void Hd3d_FinishPanes(void) {
}

bool Hd3d_PaneIs3D(int view) {
  return false;
}

bool Hd3d_Project(int view, int x, int y, int above, int *sx, int *sy, bool *behind) {
  return false;
}

float Hd3d_ScaleAt(int view, int x, int y) {
  return 1.0f;
}

void Hd3d_SetTestCamera(bool on) {
}

const char *Hd3d_LastWhy2D(int view) {
  return kHd3dWhyNames[kHd3dWhy_Off];
}

void Hd3d_GetStats(int view, int *quads, int *cards) {
  *quads = *cards = 0;
}

void Hd3d_DumpScene(const char *ppm_path) {
}

bool Hd3d_LocalSees3D(int local) {
  return false;
}

bool Hd3d_TestCameraOn(void) {
  return false;
}
