// The 3D view's camera and camera-relative movement (see cam3d.h).
// World axes: x east, y south, z up, in SNES pixels. A camera with yaw p
// (degrees, 0 looks north, + turns right) and pitch t (degrees below the
// horizon) looks along
//   fwd   = (cos t sin p, -cos t cos p, -sin t)
//   right = (cos p, sin p, 0)
//   up    = (sin t sin p, -sin t cos p, cos t)
// and r3d projects a point d = point - eye to
//   sx = cx + focal * (d . right) / (d . fwd)
//   sy = cy - focal * stretch_y * (d . up) / (d . fwd)   (ortho: without the divides).
// Only sinf, cosf, sqrtf, fabsf and floorf are used (TCC and MSVCRT).
#include <math.h>
#include <string.h>
#include "cam3d.h"

#define kDegToRad 0.017453292f
#define kSqrt2 1.41421356f
#define kHalfSqrt2 0.70710678f
#define kCos30 0.8660254f

// Third person: the pitch range, the original view and the zoom range.
#define kTpPitchMin 30.0f
#define kTpPitchMax 75.0f
#define kTpPitch 45.0f
#define kZoomMin 0.75f
#define kZoomMax 2.0f
#define kZoomStep 1.1f       // per wheel step
// First person.
#define kFpPitchMin -60.0f
#define kFpPitchMax 45.0f
#define kFpPitchEnter 8.0f   // looking a little down on entering
#define kBlendTime 0.3f      // seconds between third and first person
// Right stick: radial deadzone, full deflection, rates in degrees per second.
#define kStickDead 0.18f
#define kStickFull 0.95f
#define kTpYawRate 150.0f
#define kTpPitchRate 90.0f
#define kFpYawRate 180.0f
#define kFpPitchRate 120.0f
#define kRateTau 0.05f       // the stick rates ease toward the stick (seconds)
#define kMouseDegPerPx 0.3f  // at mouse speed 50
#define kKeyTurnRate 120.0f
#define kKeyTiltRate 90.0f
// Camera returns: after this long without camera input, a critically damped
// spring takes the third-person view back, and snaps once this close.
#define kReturnDelay 2.0f
#define kReturnOmega 6.0f
#define kSnapAngle 0.5f
#define kSnapZoom 0.005f
#define kTurnedAngle 10.0f   // Cam3d_IsTurned
#define kIdentityYaw 0.5f    // the d-pad is left alone under this yaw
// Near planes: the third-person eye is hundreds of px away, the first-person
// eye at least 7 px from any wall. Far: past anything a scene holds.
#define kNearTp 8.0f
#define kNearFp 2.0f
#define kFar 8192.0f
// The oblique test camera sits this far back along y and z (about 4096 px
// along its view, so every depth is positive; whole numbers keep eye - point
// exact in float).
#define kTestBack 2896.0f

static const Cam3dTuning kDefaultTuning = { 50, 65, 100, 50, false, true };

// NaN and infinities become 0 (input from outside must not poison the camera).
static float Finite(float v) {
  return v > -1e30f && v < 1e30f ? v : 0.0f;
}

static float Clampf(float v, float lo, float hi) {
  if (!(v >= lo))  // (NaN too)
    return lo;
  return v > hi ? hi : v;
}

// Degrees into [-180, 180).
static float Wrap180(float a) {
  a = Finite(a);
  a -= 360.0f * floorf((a + 180.0f) * (1.0f / 360.0f));
  if (a >= 180.0f)
    a -= 360.0f;
  else if (a < -180.0f)
    a += 360.0f;
  return a >= -180.0f && a < 180.0f ? a : 0.0f;  // (only huge angles miss)
}

// Degrees into [0, 360).
static float Wrap360(float a) {
  a = Wrap180(a);
  return a < 0.0f ? a + 360.0f : a;
}

static float SmoothStep01(float x) {
  x = Clampf(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// e^-a for a >= 0 without expf: halve a until it is small, take the series,
// square back up.
static float ExpNeg(float a) {
  if (!(a > 0.0f))
    return 1.0f;
  int n = 0;
  while (a > 0.125f && n < 24)
    a *= 0.5f, n++;
  float e = 1.0f - a * (1.0f - a * (0.5f - a * (1.0f / 6.0f - a * (1.0f / 24.0f))));
  while (n-- > 0)
    e *= e;
  return e;
}

// The right stick past its radial deadzone: 0 inside 0.18, full from 0.95,
// t * sqrt(t) between (fine control near the middle), same direction.
static void ShapeStick(float x, float y, float *ox, float *oy) {
  x = Clampf(Finite(x), -1.0f, 1.0f), y = Clampf(Finite(y), -1.0f, 1.0f);
  float m = sqrtf(x * x + y * y);
  *ox = *oy = 0.0f;
  if (!(m > kStickDead))
    return;
  float t = Clampf((m - kStickDead) * (1.0f / (kStickFull - kStickDead)), 0.0f, 1.0f);
  float k = t * sqrtf(t) / m;
  *ox = x * k, *oy = y * k;
}

static void ResetThirdPerson(Cam3d *c) {
  c->yaw = 0.0f, c->pitch = kTpPitch, c->zoom = 1.0f;
}

void Cam3d_Reset(Cam3d *c) {
  memset(c, 0, sizeof(*c));
  ResetThirdPerson(c);
  c->fp_pitch = kFpPitchEnter;
}

void Cam3d_SetFirstPerson(Cam3d *c, bool on, int facing) {
  if (on == c->first_person)
    return;  // (entering again does not re-aim)
  if (on) {
    // RAM 0x2F: 0 up, 2 down, 4 left, 6 right.
    static const float kFacingYaw[4] = { 0.0f, 180.0f, 270.0f, 90.0f };
    c->fp_yaw = kFacingYaw[(facing >> 1) & 3];
    c->fp_pitch = kFpPitchEnter;
  }
  c->first_person = on;
  c->rate_yaw = c->rate_pitch = 0.0f;
  c->idle = 0.0f;
}

void Cam3d_Update(Cam3d *c, const Cam3dInput *in, const Cam3dTuning *t, float dt) {
  static const Cam3dInput kNoInput;
  if (!in)
    in = &kNoInput;
  if (!t)
    t = &kDefaultTuning;
  dt = Clampf(Finite(dt), 0.0f, 1.0f);
  bool fp = c->first_person;
  // Leaving first person with Camera returns: the blend eases back to the
  // original view, and camera input waits for it (0.3 s).
  bool returning = !fp && c->blend > 0.0f && t->camera_returns;
  if (returning)
    ResetThirdPerson(c), c->rate_yaw = c->rate_pitch = 0.0f;

  float sx, sy;
  ShapeStick(in->stick_x, in->stick_y, &sx, &sy);
  float speed = Clampf((float)t->stick_speed, 25.0f, 200.0f) * 0.01f;
  float invert = t->invert_y ? -1.0f : 1.0f;
  float mouse = kMouseDegPerPx * Clampf((float)t->mouse_speed, 0.0f, 100.0f) * (1.0f / 50.0f);
  float mdx = Finite(in->mouse_dx) * mouse, mdy = Finite(in->mouse_dy) * mouse;
  float key_turn = Clampf(Finite(in->key_turn), -1.0f, 1.0f), key_tilt = Clampf(Finite(in->key_tilt), -1.0f, 1.0f);
  int wheel = fp ? 0 : in->wheel < -20 ? -20 : in->wheel > 20 ? 20 : in->wheel;
  bool active = sx != 0.0f || sy != 0.0f || mdx != 0.0f || mdy != 0.0f || key_turn != 0.0f || key_tilt != 0.0f ||
                wheel != 0;
  if (returning)
    sx = sy = mdx = mdy = key_turn = key_tilt = 0.0f, wheel = 0;

  // Stick rates ease toward the stick (tau 50 ms, the same curve at any dt).
  // Stick up (y < 0) looks up, which lowers the pitch; invert flips it.
  float want_yaw = sx * (fp ? kFpYawRate : kTpYawRate) * speed;
  float want_pitch = sy * (fp ? kFpPitchRate : kTpPitchRate) * speed * invert;
  float ease = 1.0f - ExpNeg(dt * (1.0f / kRateTau));
  c->rate_yaw = Finite(c->rate_yaw + (want_yaw - c->rate_yaw) * ease);
  c->rate_pitch = Finite(c->rate_pitch + (want_pitch - c->rate_pitch) * ease);

  // Mouse: no smoothing (down looks down). Keys: + turns right, + tilts up.
  float dyaw = c->rate_yaw * dt + mdx + key_turn * kKeyTurnRate * dt;
  float dpitch = c->rate_pitch * dt + mdy * invert - key_tilt * kKeyTiltRate * dt;
  float lo = fp ? kFpPitchMin : kTpPitchMin, hi = fp ? kFpPitchMax : kTpPitchMax;
  float *pitch = fp ? &c->fp_pitch : &c->pitch;
  float p = Clampf(Finite(*pitch) + dpitch, lo, hi);
  // At a limit, the stick rate pushing past it is dropped (no lag coming back).
  if ((p <= lo && c->rate_pitch < 0.0f) || (p >= hi && c->rate_pitch > 0.0f))
    c->rate_pitch = 0.0f;
  *pitch = p;
  if (fp) {
    c->fp_yaw = Wrap360(c->fp_yaw + dyaw);
  } else {
    c->yaw = Wrap180(c->yaw + dyaw);
    float z = Clampf(Finite(c->zoom), kZoomMin, kZoomMax);
    for (; wheel > 0; wheel--)
      z *= kZoomStep;
    for (; wheel < 0; wheel++)
      z *= 1.0f / kZoomStep;
    c->zoom = Clampf(z, kZoomMin, kZoomMax);
  }

  // While the camera can't return (first person, or Camera returns off), idle
  // stops at 2 s, so the spring below always starts from rest.
  bool can_return = !fp && t->camera_returns;
  float idle0 = Clampf(Finite(c->idle), 0.0f, 1000.0f);
  c->idle = active ? 0.0f : Clampf(idle0 + dt, 0.0f, 1000.0f);
  if (!can_return && c->idle > kReturnDelay)
    c->idle = kReturnDelay;

  // Camera returns. The spring x'' = -2w x' - w^2 x released from rest at
  // the 2 s mark follows x0 (1 + w s) e^-ws, s the time since. Stepping it
  // by the ratio needs no stored velocity, so yaw, pitch and zoom go back
  // together on a straight line, without overshoot. (s is capped: past it the
  // step is about e^-w dt, which still converges.)
  if (can_return && !active && c->idle >= kReturnDelay) {
    float step = dt - (idle0 < kReturnDelay ? kReturnDelay - idle0 : 0.0f);
    float s0 = Clampf(idle0 - kReturnDelay, 0.0f, 10.0f);
    if (step > 0.0f) {
      float f = (1.0f + kReturnOmega * (s0 + step)) / (1.0f + kReturnOmega * s0) * ExpNeg(kReturnOmega * step);
      c->yaw = Wrap180(c->yaw) * f;
      c->pitch = kTpPitch + (c->pitch - kTpPitch) * f;
      c->zoom = 1.0f + (c->zoom - 1.0f) * f;
    }
    c->rate_yaw = c->rate_pitch = 0.0f;
    if (fabsf(c->yaw) < kSnapAngle && fabsf(c->pitch - kTpPitch) < kSnapAngle && fabsf(c->zoom - 1.0f) < kSnapZoom)
      ResetThirdPerson(c);
  }

  // The blend goes 0 to 1 in 0.3 s (eased in Cam3d_MakeView). The ends are
  // exact, whatever the rounding of the steps.
  float b = Clampf(Finite(c->blend), 0.0f, 1.0f) + (fp ? dt : -dt) * (1.0f / kBlendTime);
  c->blend = b > 0.9999f ? 1.0f : b < 0.0001f ? 0.0f : b;
}

bool Cam3d_IsDefaultView(const Cam3d *c) {
  return !c->first_person && c->blend <= 0.0f && fabsf(Wrap180(c->yaw)) < kSnapAngle &&
         fabsf(c->pitch - kTpPitch) < kSnapAngle && fabsf(c->zoom - 1.0f) < kSnapZoom;
}

bool Cam3d_IsTurned(const Cam3d *c) {
  return !c->first_person &&
         (fabsf(Wrap180(c->yaw)) > kTurnedAngle || fabsf(c->pitch - kTpPitch) > kTurnedAngle);
}

// One end of the blend (or a point between).
typedef struct View {
  float eye[3];
  float yaw, pitch;          // degrees
  float focal, stretch_y, znear;
} View;

// Third person. The eye is D px back from the target, and focal = D * zoom
// makes the target 1:1 (times zoom). At yaw 0 and pitch 45 a step south or
// up moves the picture focal / D / sqrt(2) px, which the sqrt(2) stretch
// makes 1 px: near the target the picture is (x, y - z), the 2D picture.
// The stretch fades out 25 degrees either side of 45. A wider picture shows
// more at the sides (Hor+).
static void ThirdPersonView(const Cam3d *c, const Cam3dPose *p, const Cam3dTuning *t, View *v) {
  float perspective = Clampf((float)t->perspective, 0.0f, 100.0f);
  float dist = 1600.0f / (1.0f + 4.0f * perspective * 0.01f);  // 533 at 50
  float yaw = Wrap180(c->yaw), pitch = Clampf(Finite(c->pitch), kTpPitchMin, kTpPitchMax);
  float zoom = Clampf(Finite(c->zoom), kZoomMin, kZoomMax);
  float ps = sinf(pitch * kDegToRad), pc = cosf(pitch * kDegToRad);
  float ys = sinf(yaw * kDegToRad), yc = cosf(yaw * kDegToRad);
  float fwd[3] = { pc * ys, -pc * yc, -ps };
  float target[3] = { p->target[0] + p->shake[0], p->target[1] + p->shake[1], p->target[2] };
  for (int i = 0; i < 3; i++)
    v->eye[i] = target[i] - fwd[i] * dist;
  v->yaw = yaw, v->pitch = pitch;
  v->focal = dist * zoom;
  float around45 = 1.0f - fabsf(pitch - kTpPitch) * (1.0f / 25.0f);
  v->stretch_y = 1.0f + (kSqrt2 - 1.0f) * (around45 > 0.0f ? around45 : 0.0f);
  v->znear = kNearTp;
}

// First person: the eye from the pose, the vertical field of view from the
// setting (Hor+: the width only adds to the sides).
static void FirstPersonView(const Cam3d *c, const Cam3dPose *p, const Cam3dTuning *t, View *v) {
  float half_fov = Clampf((float)t->fov, 50.0f, 90.0f) * 0.5f * kDegToRad;
  v->eye[0] = p->eye[0] + p->shake[0];
  v->eye[1] = p->eye[1] + p->shake[1];
  v->eye[2] = p->eye[2];
  v->yaw = Wrap360(c->fp_yaw);
  v->pitch = Clampf(Finite(c->fp_pitch), kFpPitchMin, kFpPitchMax);
  v->focal = (float)p->h * 0.5f * cosf(half_fov) / sinf(half_fov);
  v->stretch_y = 1.0f;
  v->znear = kNearFp;
}

static void SetBasis(R3dCamera *out, float yaw, float pitch) {
  float ps = sinf(pitch * kDegToRad), pc = cosf(pitch * kDegToRad);
  float ys = sinf(yaw * kDegToRad), yc = cosf(yaw * kDegToRad);
  out->fwd[0] = pc * ys, out->fwd[1] = -pc * yc, out->fwd[2] = -ps;
  out->right[0] = yc, out->right[1] = ys, out->right[2] = 0.0f;
  out->up[0] = ps * ys, out->up[1] = -ps * yc, out->up[2] = pc;
}

void Cam3d_MakeView(const Cam3d *c, const Cam3dPose *p, const Cam3dTuning *t, R3dCamera *out) {
  if (!t)
    t = &kDefaultTuning;
  // The blend eases eye, angles, focal, stretch and near plane together
  // (smoothstep over the 0.3 s). An end is only made when it shows, so the
  // unused anchor of the pose is never read.
  float b = SmoothStep01(Finite(c->blend));
  View v;
  if (b >= 1.0f) {
    FirstPersonView(c, p, t, &v);
  } else {
    ThirdPersonView(c, p, t, &v);
    if (b > 0.0f) {
      View f;
      FirstPersonView(c, p, t, &f);
      for (int i = 0; i < 3; i++)
        v.eye[i] += (f.eye[i] - v.eye[i]) * b;
      v.yaw += Wrap180(f.yaw - v.yaw) * b;  // (the short way round)
      v.pitch += (f.pitch - v.pitch) * b;
      v.focal += (f.focal - v.focal) * b;
      v.stretch_y += (f.stretch_y - v.stretch_y) * b;
      v.znear += (f.znear - v.znear) * b;
    }
  }
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < 3; i++)
    out->eye[i] = Finite(v.eye[i]);
  SetBasis(out, v.yaw, v.pitch);
  out->focal = v.focal;
  out->stretch_y = v.stretch_y;
  out->cx = (float)p->w * 0.5f, out->cy = (float)p->h * 0.5f;
  out->znear = v.znear, out->zfar = kFar;
  out->ortho = false;
}

// The parallel oblique camera: looking north and down at 45 degrees with no
// perspective, focal 1 and the sqrt(2) stretch, world (x, y, z) lands on
// screen (x, y - z) - (the picture's top left), exactly the 2D picture.
void Cam3d_MakeTestView(const Cam3dPose *p, R3dCamera *out) {
  memset(out, 0, sizeof(*out));
  out->eye[0] = p->target[0] + p->shake[0];
  out->eye[1] = p->target[1] + p->shake[1] + kTestBack;
  out->eye[2] = p->target[2] + kTestBack;
  out->right[0] = 1.0f;
  out->up[1] = -kHalfSqrt2, out->up[2] = kHalfSqrt2;
  out->fwd[1] = -kHalfSqrt2, out->fwd[2] = -kHalfSqrt2;
  out->focal = 1.0f;
  out->stretch_y = kSqrt2;
  out->cx = (float)p->w * 0.5f, out->cy = (float)p->h * 0.5f;
  out->znear = 1.0f, out->zfar = kFar + 2.0f * kTestBack;
  out->ortho = true;
}

// ---- Camera-relative movement ----

// The 8 directions in screen axes (y down), from right going clockwise, and
// their d-pad bits (0x10 up, 0x20 down, 0x40 left, 0x80 right).
static const float kSectorDir[8][2] = {
  { 1.0f, 0.0f }, { kHalfSqrt2, kHalfSqrt2 }, { 0.0f, 1.0f }, { -kHalfSqrt2, kHalfSqrt2 },
  { -1.0f, 0.0f }, { -kHalfSqrt2, -kHalfSqrt2 }, { 0.0f, -1.0f }, { kHalfSqrt2, -kHalfSqrt2 },
};
static const uint8 kSectorBits[8] = { 0x80, 0xa0, 0x20, 0x60, 0x40, 0x50, 0x10, 0x90 };

uint16 Cam3d_Remap(Cam3dRemap *r, const Cam3d *c, uint16 input, float move_x, float move_y) {
  if (!(input & 0xf0)) {
    Cam3d_RemapReset(r);
    return input;
  }
  float yaw;
  if (c->first_person && (!r->latched || r->latched_fp)) {
    // The live view. It is also latched, so leaving first person while
    // walking keeps walking that way until the d-pad is let go. (A walk
    // begun before entering first person keeps its way too: the view now
    // faces where Link faces, and the held direction would turn him round.)
    yaw = c->fp_yaw;
    r->latched_yaw = yaw, r->latched = true, r->latched_fp = true;
  } else {
    // A turning (or returning) camera never steers a walk in progress (nor
    // does first person entered again during it).
    if (!r->latched)
      r->latched_yaw = c->yaw, r->latched = true;
    r->latched_fp = false;
    yaw = r->latched_yaw;
  }
  return Cam3d_Rotate(r, input, move_x, move_y, yaw);
}

void Cam3d_RemapReset(Cam3dRemap *r) {
  memset(r, 0, sizeof(*r));
  r->sector = -1;
}

uint16 Cam3d_Rotate(Cam3dRemap *r, uint16 input, float move_x, float move_y, float yaw) {
  int dirs = input & 0xf0;
  if (!dirs) {
    r->sector = -1, r->last_dirs = 0;
    return input;
  }
  yaw = Wrap180(yaw);
  if (fabsf(yaw) < kIdentityYaw) {
    r->sector = -1, r->last_dirs = (uint16)dirs;
    return input;
  }
  // The direction wanted on the screen: the stick's, else the bits' (an
  // opposite pair cancels).
  float vx = Finite(move_x), vy = Finite(move_y);
  float vmax = fabsf(vx) > fabsf(vy) ? fabsf(vx) : fabsf(vy);
  if (vmax > 1e-3f) {
    vx /= vmax, vy /= vmax;
  } else {
    vx = (float)(((dirs >> 7) & 1) - ((dirs >> 6) & 1));
    vy = (float)(((dirs >> 5) & 1) - ((dirs >> 4) & 1));
    if (vx == 0.0f && vy == 0.0f)
      return input;
  }
  // Turned clockwise by the yaw: up becomes where the camera looks.
  float s = sinf(yaw * kDegToRad), co = cosf(yaw * kDegToRad);
  float wx = vx * co - vy * s, wy = vx * s + vy * co;
  float len = sqrtf(wx * wx + wy * wy);
  wx /= len, wy /= len;
  // The nearest of the 8 directions, but the one picked last time is kept
  // while the same keys are held and it is within 30 degrees (7.5 degrees
  // past the halfway line), so a direction never flickers at a boundary.
  int k = r->sector;
  if (!(k >= 0 && k < 8 && dirs == r->last_dirs && wx * kSectorDir[k][0] + wy * kSectorDir[k][1] >= kCos30)) {
    float best = -2.0f;
    k = 0;
    for (int i = 0; i < 8; i++) {
      float d = wx * kSectorDir[i][0] + wy * kSectorDir[i][1];
      if (d > best)
        best = d, k = i;
    }
  }
  r->sector = (int8)k, r->last_dirs = (uint16)dirs;
  return (uint16)((input & ~0xf0) | kSectorBits[k]);
}
