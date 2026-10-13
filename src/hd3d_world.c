// The 3D view: rooms and overworld areas as 3D scenes (see hd3d_internal.h).
#include <SDL.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"

Hd3dScene *Hd3dScene_Update(const int32 decode_rect[4], int *why) {
  *why = kHd3dWhy_Room;
  return NULL;
}

Hd3dScene *Hd3dScene_Previous(void) {
  return NULL;
}

void Hd3dScene_GetInfo(const Hd3dScene *s, Hd3dSceneInfo *out) {
  memset(out, 0, sizeof(*out));
}

int Hd3dScene_WalkZ(const Hd3dScene *s, int layer) {
  return layer ? kHd3dLowerFloor : 0;
}

float Hd3dScene_StepRise(Hd3dScene *s, float dt, bool drawn) {
  return 1.0f;
}

void Hd3dScene_Textures(Hd3dScene *s, const uint32 *lut, R3dTexture *bg2, R3dTexture *bg1) {
  memset(bg2, 0, sizeof(*bg2));
  memset(bg1, 0, sizeof(*bg1));
}

int Hd3dScene_Emit(Hd3dScene *s, const Hd3dEmit *e, R3dQuad *out, int max) {
  return 0;
}

void Hd3dScene_Dump(const Hd3dScene *s, const char *ppm_path) {
}
