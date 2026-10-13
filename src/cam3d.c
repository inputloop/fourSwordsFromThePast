// The 3D view's camera and camera-relative movement (see cam3d.h).
#include <math.h>
#include <string.h>
#include "cam3d.h"

void Cam3d_Reset(Cam3d *c) {
  memset(c, 0, sizeof(*c));
  c->pitch = 45, c->zoom = 1, c->fp_pitch = 8;
}

void Cam3d_SetFirstPerson(Cam3d *c, bool on, int facing) {
  c->first_person = on;
}

void Cam3d_Update(Cam3d *c, const Cam3dInput *in, const Cam3dTuning *t, float dt) {
}

bool Cam3d_IsDefaultView(const Cam3d *c) {
  return !c->first_person;
}

bool Cam3d_IsTurned(const Cam3d *c) {
  return false;
}

void Cam3d_MakeView(const Cam3d *c, const Cam3dPose *p, const Cam3dTuning *t, R3dCamera *out) {
  memset(out, 0, sizeof(*out));
}

void Cam3d_MakeTestView(const Cam3dPose *p, R3dCamera *out) {
  memset(out, 0, sizeof(*out));
}

uint16 Cam3d_Remap(Cam3dRemap *r, const Cam3d *c, uint16 input, float move_x, float move_y) {
  return input;
}

void Cam3d_RemapReset(Cam3dRemap *r) {
  memset(r, 0, sizeof(*r));
  r->sector = -1;
}

uint16 Cam3d_Rotate(Cam3dRemap *r, uint16 input, float move_x, float move_y, float yaw) {
  return input;
}
