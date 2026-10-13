// Unit test of the 3D view's camera and camera-relative d-pad (src/cam3d.c).
// No ROM, SDL or game state:
//   cc -O2 -Werror tests/unit/cam3d_test.c src/cam3d.c src/r3d.c -lm -o cam3d_test && ./cam3d_test
// (Not -I src on Linux: src/features.h would hide glibc's own <features.h>,
// which every C library header includes. -iquote src is fine.)
// Projections are worked out here from the R3dCamera, as r3d.h defines them
// (sx = cx + focal * xc / zc, sy = cy - focal * stretch_y * yc / zc; ortho
// without the divide), so the test does not depend on the rasterizer. Where
// R3d_Project gives an answer (not the stub), it must agree.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../src/cam3d.h"

static int g_checks, g_fails;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { \
      if (++g_fails <= 60) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } \
  } while (0)

enum { kU = 0x10, kD = 0x20, kL = 0x40, kR = 0x80 };

// "U", "UR", "-" ... to d-pad bits.
static uint16 Keys(const char *s) {
  uint16 v = 0;
  for (; *s; s++)
    v |= *s == 'U' ? kU : *s == 'D' ? kD : *s == 'L' ? kL : *s == 'R' ? kR : 0;
  return v;
}

static const char *KeyName(uint16 v) {
  static char buf[4][8];
  static int n;
  char *p = buf[n++ & 3], *q = p;
  if (v & kU) *q++ = 'U';
  if (v & kD) *q++ = 'D';
  if (v & kL) *q++ = 'L';
  if (v & kR) *q++ = 'R';
  if (q == p) *q++ = '-';
  *q = 0;
  return p;
}

// The 8 directions clockwise from right, as Cam3d_Rotate numbers them.
static const uint16 kSector[8] = { kR, kD | kR, kD, kD | kL, kL, kU | kL, kU, kU | kR };

static uint16 Rot(Cam3dRemap *r, const char *keys, float yaw) {
  return Cam3d_Rotate(r, Keys(keys), 0, 0, yaw) & 0xf0;
}

static int g_r3d_checked, g_r3d_bad;

static bool Project(const R3dCamera *c, double x, double y, double z, double *sx, double *sy, double *zc) {
  double d[3] = { x - c->eye[0], y - c->eye[1], z - c->eye[2] };
  double xc = d[0] * c->right[0] + d[1] * c->right[1] + d[2] * c->right[2];
  double yc = d[0] * c->up[0] + d[1] * c->up[1] + d[2] * c->up[2];
  *zc = d[0] * c->fwd[0] + d[1] * c->fwd[1] + d[2] * c->fwd[2];
  if (c->ortho) {
    *sx = c->cx + c->focal * xc;
    *sy = c->cy - c->focal * c->stretch_y * yc;
  } else {
    *sx = c->cx + c->focal * xc / *zc;
    *sy = c->cy - c->focal * c->stretch_y * yc / *zc;
  }
  // The rasterizer's own projection must agree (the stub always says behind).
  float p[3] = { (float)x, (float)y, (float)z }, rx, ry, rdepth;
  if (*zc > 0 && R3d_Project(c, p, &rx, &ry, &rdepth)) {
    g_r3d_checked++;
    if (fabs(rx - *sx) > 0.02 + 1e-5 * fabs(*sx) || fabs(ry - *sy) > 0.02 + 1e-5 * fabs(*sy)) {
      if (g_r3d_bad++ < 5)
        printf("r3d disagrees: (%.1f %.1f %.1f) at (%.4f %.4f), here (%.4f %.4f)\n", x, y, z, rx, ry, *sx, *sy);
    }
  }
  return *zc > 0;
}

static double Dot(const float *a, const float *b) {
  return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
}

static bool Finite(double v) {
  return v > -1e30 && v < 1e30;
}

static bool CameraFinite(const R3dCamera *c) {
  for (int i = 0; i < 3; i++)
    if (!Finite(c->eye[i]) || !Finite(c->right[i]) || !Finite(c->up[i]) || !Finite(c->fwd[i]))
      return false;
  return Finite(c->focal) && Finite(c->stretch_y) && Finite(c->cx) && Finite(c->cy) && Finite(c->znear);
}

static void CheckBasis(const R3dCamera *c, const char *what) {
  CHECK(fabs(Dot(c->right, c->right) - 1) < 1e-5 && fabs(Dot(c->up, c->up) - 1) < 1e-5 &&
        fabs(Dot(c->fwd, c->fwd) - 1) < 1e-5, "%s: basis not unit", what);
  CHECK(fabs(Dot(c->right, c->up)) < 1e-5 && fabs(Dot(c->right, c->fwd)) < 1e-5 && fabs(Dot(c->up, c->fwd)) < 1e-5,
        "%s: basis not orthogonal", what);
}

static Cam3dTuning Tuning(bool returns) {
  Cam3dTuning t = { 50, 65, 100, 50, false, returns };
  return t;
}

// The pose hd3d.c makes: the target is the relief of the original screen's
// middle, at walk level tz.
static Cam3dPose Pose(float cam_x, float cam_y, int w, int h, float tz) {
  Cam3dPose p;
  memset(&p, 0, sizeof(p));
  p.target[0] = cam_x + w * 0.5f;
  p.target[1] = cam_y + 1 + h * 0.5f + tz;
  p.target[2] = tz;
  p.eye[0] = p.target[0], p.eye[1] = p.target[1] + 30, p.eye[2] = tz + 20;
  p.w = w, p.h = h;
  return p;
}

static void Run(Cam3d *c, const Cam3dInput *in, const Cam3dTuning *t, float seconds) {
  int n = (int)(seconds * 60 + 0.5f);
  for (int i = 0; i < n; i++)
    Cam3d_Update(c, in, t, 1.0f / 60);
}

static void TestIdentity(void) {
  static const float kYaws[] = { 0, 0.4f, -0.4f, 0.49f, -0.49f, 360, -360, 359.7f, 720.2f };
  for (int y = 0; y < (int)(sizeof(kYaws) / sizeof(kYaws[0])); y++) {
    Cam3dRemap fresh, kept;
    Cam3d_RemapReset(&kept);
    int bad = 0;
    for (int in = 0; in < 65536; in++) {
      Cam3d_RemapReset(&fresh);
      if (Cam3d_Rotate(&fresh, (uint16)in, 0, 0, kYaws[y]) != in ||
          Cam3d_Rotate(&kept, (uint16)in, 0, 0, kYaws[y]) != in ||
          Cam3d_Rotate(&kept, (uint16)in, 0.3f, -0.95f, kYaws[y]) != in)
        bad++;
    }
    CHECK(bad == 0, "identity at yaw %.2f: %d inputs changed", kYaws[y], bad);
  }
  // Through Cam3d_Remap: a third-person camera at the original view (and
  // 0.4 degrees either side), and a first-person one looking north.
  static const float kNear0[] = { 0, 0.4f, -0.4f };
  for (int y = 0; y < 3; y++) {
    Cam3d tp, fp;
    Cam3d_Reset(&tp);
    Cam3d_Reset(&fp);
    Cam3d_SetFirstPerson(&fp, true, 0);
    tp.yaw = kNear0[y], fp.fp_yaw = kNear0[y] < 0 ? 360 + kNear0[y] : kNear0[y];
    Cam3dRemap r1, r2;
    Cam3d_RemapReset(&r1);
    Cam3d_RemapReset(&r2);
    int bad = 0;
    for (int in = 0; in < 65536; in++)
      bad += Cam3d_Remap(&r1, &tp, (uint16)in, 0, 0) != in || Cam3d_Remap(&r2, &fp, (uint16)in, 0, 0) != in;
    CHECK(bad == 0, "remap identity at yaw %.1f: %d changed", kNear0[y], bad);
  }
}

static void TestTables(void) {
  static const struct { float yaw; const char *in, *out; } kTable[] = {
    { 90, "U", "R" }, { 90, "R", "D" }, { 90, "D", "L" }, { 90, "L", "U" }, { 90, "UR", "DR" },
    { 45, "U", "UR" }, { -90, "U", "L" }, { 180, "U", "D" }, { 270, "U", "L" },
    { -180, "U", "D" }, { 450, "U", "R" }, { -45, "U", "UL" }, { 135, "U", "DR" },
  };
  for (int i = 0; i < (int)(sizeof(kTable) / sizeof(kTable[0])); i++) {
    Cam3dRemap r;
    Cam3d_RemapReset(&r);
    uint16 got = Rot(&r, kTable[i].in, kTable[i].yaw);
    CHECK(got == Keys(kTable[i].out), "yaw %.0f: %s gave %s, want %s", kTable[i].yaw, kTable[i].in, KeyName(got),
          kTable[i].out);
  }
  // Every direction at every multiple of 45 degrees moves round by as many sectors.
  for (int m = -8; m <= 8; m++) {
    for (int k = 0; k < 8; k++) {
      if (m % 8 == 0)
        continue;
      Cam3dRemap r;
      Cam3d_RemapReset(&r);
      uint16 got = Cam3d_Rotate(&r, kSector[k], 0, 0, 45.0f * m) & 0xf0;
      uint16 want = kSector[((k + m) % 8 + 8) % 8];
      CHECK(got == want, "yaw %d: %s gave %s, want %s", 45 * m, KeyName(kSector[k]), KeyName(got), KeyName(want));
    }
  }
}

static void TestHysteresis(void) {
  static const struct { float yaw; const char *out; } kSweep[] = {
    { 20, "U" }, { 28, "U" }, { 31, "UR" }, { 16, "UR" }, { 14, "U" },
  };
  Cam3dRemap r;
  Cam3d_RemapReset(&r);
  for (int i = 0; i < 5; i++) {
    uint16 got = Rot(&r, "U", kSweep[i].yaw);
    CHECK(got == Keys(kSweep[i].out), "hysteresis, U at yaw %.0f gave %s, want %s", kSweep[i].yaw, KeyName(got),
          kSweep[i].out);
  }
  // Other buttons do not matter; other directions start the pick over.
  Cam3d_RemapReset(&r);
  Rot(&r, "U", 20);
  CHECK(Rot(&r, "U", 28) == kU, "hysteresis kept");
  CHECK(Cam3d_Rotate(&r, kU | 0x8000, 0, 0, 28) == (kU | 0x8000), "hysteresis kept with B held");
  CHECK(Rot(&r, "R", 28) == (kD | kR), "new keys pick the nearest: R at 28 is DR");
  CHECK(Rot(&r, "UR", 28) == kR, "UR at 28 is R");
}

// A sweep 0 -> 360 -> 0 in 0.5 degree steps changes the output 16 times,
// whatever direction is held (the keys, or a stick between them).
static void TestSweep(void) {
  for (int k = 0; k < 8; k++) {
    Cam3dRemap r;
    Cam3d_RemapReset(&r);
    int flips = 0, prev = -1;
    for (int step = 0; step <= 1440; step++) {
      float yaw = step <= 720 ? step * 0.5f : (1440 - step) * 0.5f;
      int out = Cam3d_Rotate(&r, kSector[k], 0, 0, yaw) & 0xf0;
      flips += prev >= 0 && out != prev;
      prev = out;
    }
    CHECK(flips == 16, "sweep holding %s: %d flips, want 16", KeyName(kSector[k]), flips);
  }
  static const float kStick[] = { 0, 10, 20, -15 };
  for (int s = 0; s < 4; s++) {
    float a = kStick[s] * 0.017453292f, mx = sinf(a), my = -cosf(a);
    Cam3dRemap r;
    Cam3d_RemapReset(&r);
    int flips = 0, prev = -1;
    for (int step = 0; step <= 1440; step++) {
      float yaw = step <= 720 ? step * 0.5f : (1440 - step) * 0.5f;
      int out = Cam3d_Rotate(&r, kU, mx, my, yaw) & 0xf0;
      flips += prev >= 0 && out != prev;
      prev = out;
    }
    CHECK(flips == 16, "sweep with the stick %.0f degrees off up: %d flips, want 16", kStick[s], flips);
  }
}

// Only bits 0xF0 change, never to both of an opposite pair; the output is
// within 30 degrees of the turned direction.
static void TestBits(void) {
  int bad_bits = 0, bad_pair = 0, bad_dir = 0;
  uint32 seed = 12345;
  for (int y = 0; y < 64; y++) {
    seed = seed * 1103515245 + 12345;
    float yaw = y < 32 ? -360 + y * 22.7f : (float)(seed >> 8 & 0xffff) / 65536.0f * 1440 - 720;
    Cam3dRemap kept;
    Cam3d_RemapReset(&kept);
    for (int in = 0; in < 65536; in++) {
      Cam3dRemap fresh;
      Cam3d_RemapReset(&fresh);
      uint16 a = Cam3d_Rotate(&fresh, (uint16)in, 0, 0, yaw);
      uint16 b = Cam3d_Rotate(&kept, (uint16)in, 0, 0, yaw);
      for (int j = 0; j < 2; j++) {
        uint16 out = j ? b : a;
        bad_bits += (out & ~0xf0) != (in & ~0xf0) || ((in & 0xf0) == 0 && out != in);
        if (out != in)
          bad_pair += (out & (kU | kD)) == (kU | kD) || (out & (kL | kR)) == (kL | kR) || (out & 0xf0) == 0;
      }
      // The fresh pick is the nearest of the 8 directions to the turned one.
      int vx = ((in >> 7) & 1) - ((in >> 6) & 1), vy = ((in >> 5) & 1) - ((in >> 4) & 1);
      if ((vx || vy) && a != in) {
        double ps = sin(yaw * 0.017453292519943295), pc = cos(yaw * 0.017453292519943295);
        double wx = vx * pc - vy * ps, wy = vx * ps + vy * pc, len = sqrt(wx * wx + wy * wy);
        int ox = ((a >> 7) & 1) - ((a >> 6) & 1), oy = ((a >> 5) & 1) - ((a >> 4) & 1);
        double olen = sqrt((double)(ox * ox + oy * oy));
        bad_dir += (wx * ox + wy * oy) / (len * olen) < 0.92387;  // cos 22.5
      }
    }
  }
  CHECK(bad_bits == 0, "%d outputs changed bits other than 0xF0", bad_bits);
  CHECK(bad_pair == 0, "%d outputs hold an opposite pair (or no direction)", bad_pair);
  CHECK(bad_dir == 0, "%d fresh picks are not the nearest direction", bad_dir);
  // The same with the left stick's direction, held across turns (the stick
  // decides the direction, the bits only that one is held).
  {
    int bad = 0;
    Cam3dRemap kept;
    Cam3d_RemapReset(&kept);
    for (int i = 0; i < 200000; i++) {
      seed = seed * 1103515245 + 12345;
      uint16 in = (uint16)(seed >> 8);
      seed = seed * 1103515245 + 12345;
      float mx = (float)((int)(seed >> 8 & 0xffff) - 32768) / 32768.0f;
      seed = seed * 1103515245 + 12345;
      float my = (float)((int)(seed >> 8 & 0xffff) - 32768) / 32768.0f;
      seed = seed * 1103515245 + 12345;
      float yaw = (float)(seed >> 8 & 0xffff) / 65536.0f * 720 - 360;
      uint16 out = Cam3d_Rotate(&kept, in, mx, my, yaw);
      bad += (out & ~0xf0) != (in & ~0xf0) || ((in & 0xf0) == 0 && out != in);
      if (out != in)
        bad += (out & (kU | kD)) == (kU | kD) || (out & (kL | kR)) == (kL | kR) || (out & 0xf0) == 0;
    }
    CHECK(bad == 0, "stick: %d bad outputs", bad);
  }
  // Opposite pairs cancel: U+D alone is left alone, U+D+R walks as R does
  // (down at 90).
  Cam3dRemap r;
  Cam3d_RemapReset(&r);
  CHECK(Cam3d_Rotate(&r, kU | kD | 0x8000, 0, 0, 90) == (kU | kD | 0x8000), "UD left alone");
  uint16 udr = Cam3d_Rotate(&r, kU | kD | kR, 0, 0, 90);
  CHECK(udr == kD, "UDR at 90 gave %s", KeyName(udr));
  CHECK(Cam3d_Rotate(&r, kU | 0x8000 | 0x0100, 0, 0, 90) == (kR | 0x8000 | 0x0100), "UB at 90 keeps B and A");
  // The stick's own direction wins over the bits.
  Cam3d_RemapReset(&r);
  CHECK((Cam3d_Rotate(&r, kU, 1, 0, 90) & 0xf0) == kD, "stick right at 90 walks down");
  // Junk from outside is harmless.
  Cam3d_RemapReset(&r);
  uint16 o = Cam3d_Rotate(&r, kU, NAN, INFINITY, NAN);
  CHECK(o == kU, "NaN yaw and stick gave %s", KeyName(o));
  o = Cam3d_Rotate(&r, kU, 1e30f, -3e38f, 90);
  CHECK((o & 0xf0) && !((o & (kU | kD)) == (kU | kD)) && !((o & (kL | kR)) == (kL | kR)), "huge stick");
}

static void TestLatch(void) {
  Cam3d c;
  Cam3dRemap r;
  Cam3d_Reset(&c);
  Cam3d_RemapReset(&r);
  // Third person: the yaw when the walk began, until every direction is let go.
  c.yaw = 90;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kR, "latch: U at 90 walks R");
  c.yaw = 0;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kR, "latch: still R after the camera went back");
  CHECK((Cam3d_Remap(&r, &c, kU | kL, 0, 0) & 0xf0) == (kU | kR), "latch: UL at the latched 90 is UR");
  CHECK(Cam3d_Remap(&r, &c, 0x8000, 0, 0) == 0x8000, "latch: nothing held");
  CHECK(!r.latched && r.sector == -1, "latch: released");
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kU, "latch: a new walk at 0 is U");
  c.yaw = -90;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kU, "latch: held through a turn");
  // First person: the live yaw.
  Cam3d_RemapReset(&r);
  c.first_person = true, c.fp_yaw = 90;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kR, "fp: U at 90 is R");
  c.fp_yaw = 270;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kL, "fp: live, U at 270 is L");
  c.fp_yaw = 0.2f;
  CHECK(Cam3d_Remap(&r, &c, kU, 0, 0) == kU, "fp: identity under 0.5");
  c.fp_yaw = 359.8f;
  CHECK(Cam3d_Remap(&r, &c, kU, 0, 0) == kU, "fp: identity at 359.8");
  c.fp_yaw = 180;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kD, "fp: U at 180 is D");
  // Leaving first person mid-walk keeps that way until let go.
  c.first_person = false, c.yaw = 0;
  CHECK((Cam3d_Remap(&r, &c, kU, 0, 0) & 0xf0) == kD, "fp to tp mid-walk keeps D");
  Cam3d_Remap(&r, &c, 0, 0, 0);
  CHECK(Cam3d_Remap(&r, &c, kU, 0, 0) == kU, "then a new walk at 0 is U");
}

static void TestDefaultView(void) {
  static const int kSizes[][2] = { { 256, 224 }, { 448, 240 }, { 398, 224 }, { 256, 240 } };
  static const float kCams[][2] = { { 0, 0 }, { 3798, 1536 }, { 2304, 8464 }, { 7900, 7700 } };
  static const uint8 kPersp[] = { 0, 25, 50, 75, 100 };
  for (int s = 0; s < 4; s++) for (int k = 0; k < 4; k++) for (int z = 0; z < 2; z++) for (int q = 0; q < 5; q++) {
    int w = kSizes[s][0], h = kSizes[s][1];
    float tz = z ? -16.0f : 0.0f;
    Cam3dPose p = Pose(kCams[k][0], kCams[k][1], w, h, tz);
    Cam3dTuning t = Tuning(true);
    t.perspective = kPersp[q];
    Cam3d c;
    Cam3d_Reset(&c);
    R3dCamera rc;
    Cam3d_MakeView(&c, &p, &t, &rc);
    CheckBasis(&rc, "default view");
    CHECK(!rc.ortho && fabs(rc.stretch_y - sqrt(2.0)) < 1e-5, "default view: stretch %f", rc.stretch_y);
    double D = 1600.0 / (1 + 4 * kPersp[q] / 100.0);
    CHECK(fabs(rc.focal - D) < 1e-3 * D, "focal %f, want D = %f", rc.focal, D);
    // The eye: D back along the view, south of and above the target.
    double ex = rc.eye[0] - p.target[0], ey = rc.eye[1] - p.target[1], ez = rc.eye[2] - p.target[2];
    CHECK(fabs(ex) < 1e-3 && fabs(ey - D * 0.70710678) < 0.05 && fabs(ez - D * 0.70710678) < 0.05,
          "eye offset (%f %f %f), D %f", ex, ey, ez, D);
    CHECK(rc.znear > 0 && rc.zfar > D * 4, "near %f far %f", rc.znear, rc.zfar);
    double sx0, sy0, zc;
    bool front = Project(&rc, p.target[0], p.target[1], p.target[2], &sx0, &sy0, &zc);
    CHECK(front && fabs(sx0 - w * 0.5) < 0.01 && fabs(sy0 - h * 0.5) < 0.01, "target at (%f %f), want the middle",
          sx0, sy0);
    // Ground and height map 1:1 at the target: one step east, south or up
    // moves the picture one pixel right, down or up (within 0.01 px).
    static const int kSteps[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int i = 0; i < 6; i++) {
      double sx, sy;
      Project(&rc, p.target[0] + kSteps[i][0], p.target[1] + kSteps[i][1], p.target[2] + kSteps[i][2], &sx, &sy, &zc);
      double want_x = kSteps[i][0], want_y = kSteps[i][1] - kSteps[i][2];
      CHECK(fabs(sx - sx0 - want_x) < 0.01 && fabs(sy - sy0 - want_y) < 0.01,
            "%dx%d persp %d: step (%d %d %d) moved (%.4f %.4f), want (%.0f %.0f)", w, h, kPersp[q],
            kSteps[i][0], kSteps[i][1], kSteps[i][2], sx - sx0, sy - sy0, want_x, want_y);
    }
    // A picture point at the middle shows the same thing whatever its height
    // (the relief rule): (x, y + z, z) lands where (x, y, 0) does, to first order.
    double sa, sb, ya, yb;
    Project(&rc, p.target[0] + 0.5, p.target[1] + 0.5, p.target[2], &sa, &ya, &zc);
    Project(&rc, p.target[0] + 0.5, p.target[1] + 0.5 + 0.25, p.target[2] + 0.25, &sb, &yb, &zc);
    CHECK(fabs(sa - sb) < 0.01 && fabs(ya - yb) < 0.01, "relief at the middle");
  }
  // Zoom 2 doubles the scale at the target and keeps it in the middle.
  Cam3d c;
  Cam3d_Reset(&c);
  c.zoom = 2;
  Cam3dPose p = Pose(1000, 2000, 256, 224, 0);
  Cam3dTuning t = Tuning(false);
  R3dCamera rc;
  Cam3d_MakeView(&c, &p, &t, &rc);
  double sx0, sy0, sx, sy, zc;
  Project(&rc, p.target[0], p.target[1], p.target[2], &sx0, &sy0, &zc);
  Project(&rc, p.target[0] + 1, p.target[1], p.target[2], &sx, &sy, &zc);
  CHECK(fabs(sx0 - 128) < 0.01 && fabs(sy0 - 112) < 0.01 && fabs(sx - sx0 - 2) < 0.01, "zoom 2");
  // The shake moves the camera with the picture: shaken by (3, -2), the point
  // 3 east and 2 north of the target is in the middle.
  Cam3d_Reset(&c);
  p.shake[0] = 3, p.shake[1] = -2;
  Cam3d_MakeView(&c, &p, &t, &rc);
  Project(&rc, p.target[0] + 3, p.target[1] - 2, p.target[2], &sx, &sy, &zc);
  CHECK(fabs(sx - 128) < 0.01 && fabs(sy - 112) < 0.01, "shake: (%f %f)", sx, sy);
  // Turned to yaw 90 (looking east) the target stays in the middle; north
  // is then on the left.
  p.shake[0] = p.shake[1] = 0;
  c.yaw = 90;
  Cam3d_MakeView(&c, &p, &t, &rc);
  CheckBasis(&rc, "yaw 90");
  CHECK(rc.fwd[0] > 0.5f && fabs(rc.fwd[1]) < 1e-5, "yaw 90 looks east");
  Project(&rc, p.target[0], p.target[1], p.target[2], &sx0, &sy0, &zc);
  Project(&rc, p.target[0], p.target[1] - 10, p.target[2], &sx, &sy, &zc);
  CHECK(fabs(sx0 - 128) < 0.01 && fabs(sy0 - 112) < 0.01 && sx < sx0 - 9, "yaw 90: north on the left");
  // The stretch fades from sqrt 2 at 45 to 1 from 70.
  c.yaw = 0, c.pitch = 30;
  Cam3d_MakeView(&c, &p, &t, &rc);
  CHECK(fabs(rc.stretch_y - (1 + (sqrt(2.0) - 1) * 0.4)) < 1e-4, "stretch at 30: %f", rc.stretch_y);
  CheckBasis(&rc, "pitch 30");
  c.pitch = 75;
  Cam3d_MakeView(&c, &p, &t, &rc);
  CHECK(fabs(rc.stretch_y - 1) < 1e-6, "stretch at 75: %f", rc.stretch_y);
  // Wider pictures show more at the sides (Hor+): same focal, cx in the middle.
  R3dCamera narrow, wide;
  Cam3d_Reset(&c);
  Cam3dPose pn = Pose(0, 0, 256, 224, 0), pw = Pose(-96, 0, 448, 224, 0);
  Cam3d_MakeView(&c, &pn, &t, &narrow);
  Cam3d_MakeView(&c, &pw, &t, &wide);
  CHECK(narrow.focal == wide.focal && wide.cx == 224 && narrow.cx == 128, "Hor+ third person");
}

static void TestTestView(void) {
  static const int kSizes[][2] = { { 256, 224 }, { 448, 240 } };
  uint32 seed = 99;
  for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) {
    float cam_x = k * 3000.0f + 17, cam_y = k * 4000.0f + 5;
    Cam3dPose p = Pose(cam_x, cam_y, kSizes[s][0], kSizes[s][1], k == 1 ? -16.0f : 0.0f);
    p.shake[0] = (float)(k - 1) * 2, p.shake[1] = (float)k;
    R3dCamera rc;
    Cam3d_MakeTestView(&p, &rc);
    CheckBasis(&rc, "test view");
    CHECK(rc.ortho && rc.znear > 0, "test view is ortho");
    int bad = 0, behind = 0;
    for (int i = 0; i < 20000; i++) {
      seed = seed * 1103515245 + 12345;
      double x = cam_x - 200 + (seed >> 8 & 1023) * 0.83;
      seed = seed * 1103515245 + 12345;
      double y = cam_y - 200 + (seed >> 8 & 1023) * 0.71;
      seed = seed * 1103515245 + 12345;
      double z = -96 + (seed >> 8 & 255) * 0.75;
      double sx, sy, zc;
      if (!Project(&rc, x, y, z, &sx, &sy, &zc) || zc < rc.znear || zc > rc.zfar)
        behind++;
      // (x, y - z) minus the picture's top left (the shaken camera; picture
      // row 0 shows world row cam_y + 1).
      double want_x = x - (cam_x + p.shake[0]), want_y = (y - z) - (cam_y + 1 + p.shake[1]);
      bad += fabs(sx - want_x) > 0.01 || fabs(sy - want_y) > 0.01;
    }
    CHECK(bad == 0 && behind == 0, "test view: %d points off (x, y - z), %d outside near/far", bad, behind);
  }
}

static void TestStick(void) {
  Cam3dTuning t = Tuning(false);
  Cam3dInput in;
  Cam3d c;
  // Inside the deadzone nothing moves, and it counts as idle.
  static const float kDead[][2] = { { 0.17f, 0 }, { 0.12f, 0.12f }, { 0, -0.179f }, { 0.18f, 0 } };
  for (int i = 0; i < 4; i++) {
    Cam3d_Reset(&c);
    memset(&in, 0, sizeof(in));
    in.stick_x = kDead[i][0], in.stick_y = kDead[i][1];
    Run(&c, &in, &t, 2);
    CHECK(c.yaw == 0 && c.pitch == 45 && c.idle > 1.9f, "deadzone (%.3f %.3f): yaw %f pitch %f idle %f", kDead[i][0],
          kDead[i][1], c.yaw, c.pitch, c.idle);
  }
  // Full right: 150 degrees a second once the 50 ms easing has caught up
  // (it lags 7.5 degrees), the rest comes after the stick is let go.
  Cam3d_Reset(&c);
  memset(&in, 0, sizeof(in));
  in.stick_x = 1;
  Run(&c, &in, &t, 0.5f);
  CHECK(fabsf(c.yaw - 67.5f) < 1.5f && fabsf(c.rate_yaw - 150) < 1, "full stick 0.5 s: yaw %f rate %f", c.yaw,
        c.rate_yaw);
  in.stick_x = 0;
  Run(&c, &in, &t, 1);
  CHECK(fabsf(c.yaw - 75) < 0.5f && fabsf(c.rate_yaw) < 0.01f, "after letting go: yaw %f", c.yaw);
  Run(&c, &in, &t, 3);
  CHECK(fabsf(c.yaw - 75) < 0.5f, "camera returns off: still %f", c.yaw);
  // The response: t * sqrt(t) between 0.18 and 0.95, full past 0.95.
  static const float kMag[] = { 0.3f, 0.5f, 0.8f, 0.95f, 1.0f };
  for (int i = 0; i < 5; i++) {
    // A diagonal: the deadzone is radial, the yaw rate takes the x part.
    Cam3d_Reset(&c);
    in.stick_x = kMag[i] * 0.6f, in.stick_y = kMag[i] * 0.8f;
    Run(&c, &in, &t, 0.5f);
    double tt = (kMag[i] - 0.18) / 0.77;
    if (tt > 1) tt = 1;
    double want = tt * sqrt(tt) * 0.6 * 150;
    CHECK(fabs(c.rate_yaw - want) < 0.01 * want + 0.01, "stick %.2f: rate %f, want %f", kMag[i], c.rate_yaw, want);
  }
  // Stick speed scales the rates; first person turns faster.
  Cam3d_Reset(&c);
  in.stick_x = 1, in.stick_y = 0;
  t.stick_speed = 200;
  Run(&c, &in, &t, 0.5f);
  CHECK(fabsf(c.rate_yaw - 300) < 1, "stick speed 200: rate %f", c.rate_yaw);
  t.stick_speed = 100;
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 0);
  Run(&c, &in, &t, 0.5f);
  CHECK(fabsf(c.rate_yaw - 180) < 1 && c.fp_yaw > 60 && c.yaw == 0, "first person: rate %f fp_yaw %f yaw %f",
        c.rate_yaw, c.fp_yaw, c.yaw);
  // Stick up looks up (the pitch goes down to its limit); invert flips it.
  Cam3d_Reset(&c);
  in.stick_x = 0, in.stick_y = -1;
  Run(&c, &in, &t, 0.1f);
  CHECK(c.pitch < 45 && c.pitch > 35, "stick up 0.1 s: pitch %f", c.pitch);
  Run(&c, &in, &t, 2);
  CHECK(c.pitch == 30, "stick up stops at 30: %f", c.pitch);
  in.stick_y = 1;
  Run(&c, &in, &t, 0.1f);
  CHECK(c.pitch > 33, "back down at once from the limit: %f", c.pitch);
  t.invert_y = true;
  Cam3d_Reset(&c);
  in.stick_y = -1;
  Run(&c, &in, &t, 2);
  CHECK(c.pitch == 75, "inverted stick up stops at 75: %f", c.pitch);
  t.invert_y = false;
  // First person pitch limits.
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 0);
  in.stick_y = -1;
  Run(&c, &in, &t, 3);
  CHECK(c.fp_pitch == -60, "first person looks up to -60: %f", c.fp_pitch);
  in.stick_y = 1;
  Run(&c, &in, &t, 3);
  CHECK(c.fp_pitch == 45, "first person looks down to 45: %f", c.fp_pitch);
  CHECK(c.pitch == 45, "the third-person pitch is untouched: %f", c.pitch);
}

static void TestMouseKeysWheel(void) {
  Cam3dTuning t = Tuning(false);
  Cam3dInput in;
  Cam3d c;
  Cam3d_Reset(&c);
  memset(&in, 0, sizeof(in));
  in.mouse_dx = 10;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.yaw - 3) < 1e-4f, "mouse 10 px at speed 50: yaw %f", c.yaw);
  t.mouse_speed = 100;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.yaw - 9) < 1e-4f, "mouse 10 px at speed 100: yaw %f", c.yaw);
  in.mouse_dx = 0, in.mouse_dy = 10;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.pitch - 51) < 1e-4f, "mouse down looks down: pitch %f", c.pitch);
  t.invert_y = true;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.pitch - 45) < 1e-4f, "inverted: pitch %f", c.pitch);
  t.invert_y = false, t.mouse_speed = 0;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.pitch - 45) < 1e-4f, "mouse speed 0: pitch %f", c.pitch);
  t.mouse_speed = 50;
  // Mouse look in first person.
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 6);
  in.mouse_dx = -400, in.mouse_dy = 0;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.fp_yaw - 330) < 1e-3f, "fp mouse: fp_yaw %f, want 90 - 120 = 330", c.fp_yaw);
  // Keys: 120 degrees a second turning, 90 tilting (+ looks up).
  Cam3d_Reset(&c);
  memset(&in, 0, sizeof(in));
  in.key_turn = 1;
  Run(&c, &in, &t, 1);
  CHECK(fabsf(c.yaw - 120) < 0.1f, "key turn 1 s: yaw %f", c.yaw);
  Run(&c, &in, &t, 1);
  CHECK(fabsf(c.yaw + 120) < 0.1f, "key turn 2 s wraps to -120: yaw %f", c.yaw);
  in.key_turn = 0, in.key_tilt = 1;
  Run(&c, &in, &t, 0.1f);
  CHECK(fabsf(c.pitch - 36) < 0.1f, "key tilt up 0.1 s: pitch %f", c.pitch);
  // Wheel: x1.1 a step, 0.75 to 2; nothing in first person.
  Cam3d_Reset(&c);
  memset(&in, 0, sizeof(in));
  in.wheel = 1;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.zoom - 1.1f) < 1e-5f, "wheel +1: zoom %f", c.zoom);
  in.wheel = -2;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(fabsf(c.zoom - 1 / 1.1f) < 1e-5f, "wheel -2: zoom %f", c.zoom);
  in.wheel = 30;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(c.zoom == 2, "wheel clamps at 2: %f", c.zoom);
  in.wheel = -1000;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(c.zoom == 0.75f, "wheel clamps at 0.75: %f", c.zoom);
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 0);
  in.wheel = 3;
  Cam3d_Update(&c, &in, &t, 1.0f / 60);
  CHECK(c.zoom == 1, "wheel does nothing in first person: %f", c.zoom);
}

static void TestSpringBack(void) {
  Cam3dTuning t = Tuning(true);
  Cam3dInput none;
  memset(&none, 0, sizeof(none));
  static const float kStart[][3] = { { 60, 70, 1.6f }, { -170, 30, 0.75f }, { 179, 75, 2 }, { 0.3f, 45.2f, 1.006f } };
  for (int s = 0; s < 4; s++) {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = kStart[s][0], c.pitch = kStart[s][1], c.zoom = kStart[s][2];
    CHECK(!Cam3d_IsDefaultView(&c), "start %d is not the default view", s);
    Run(&c, &none, &t, 1.98f);
    CHECK(c.yaw == kStart[s][0] && c.pitch == kStart[s][1] && c.zoom == kStart[s][2], "start %d: moved before 2 s", s);
    // Then straight back without overshoot, all three together, and snapped.
    float yaw0 = kStart[s][0], pitch0 = kStart[s][1], zoom0 = kStart[s][2];
    int snapped_at = -1, overshoot = 0;
    for (int i = 0; i < 300 && snapped_at < 0; i++) {
      float y = c.yaw, p = c.pitch, z = c.zoom;
      Cam3d_Update(&c, &none, &t, 1.0f / 60);
      overshoot += c.yaw * yaw0 < 0 || (c.pitch - 45) * (pitch0 - 45) < 0 || (c.zoom - 1) * (zoom0 - 1) < 0;
      overshoot += fabsf(c.yaw) > fabsf(y) || fabsf(c.pitch - 45) > fabsf(p - 45) || fabsf(c.zoom - 1) > fabsf(z - 1);
      if (c.yaw == 0 && c.pitch == 45 && c.zoom == 1)
        snapped_at = i;
    }
    CHECK(overshoot == 0, "start %d: overshoot or not monotone", s);
    CHECK(snapped_at >= 0 && snapped_at < 2 * 60, "start %d: snapped after %d frames", s, snapped_at);
    CHECK(Cam3d_IsDefaultView(&c) && !Cam3d_IsTurned(&c), "start %d: default view after returning", s);
  }
  // The spring is half way back about 0.28 s in (critically damped, w = 6).
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = 100;
    Run(&c, &none, &t, 2.0f + 0.2797f);
    CHECK(fabsf(c.yaw - 50) < 2, "half way at 0.28 s: %f", c.yaw);
  }
  // Off: it stays.
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = 60, c.pitch = 60;
    Cam3dTuning off = Tuning(false);
    Run(&c, &none, &off, 10);
    CHECK(c.yaw == 60 && c.pitch == 60, "camera returns off: %f %f", c.yaw, c.pitch);
  }
  // Any camera input starts the 2 s over.
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = 60;
    Run(&c, &none, &t, 1.5f);
    Cam3dInput nudge = none;
    nudge.mouse_dx = 1;
    Cam3d_Update(&c, &nudge, &t, 1.0f / 60);
    CHECK(c.idle == 0 && fabsf(c.yaw - 60.3f) < 1e-4f, "input resets idle");
    Run(&c, &none, &t, 1.9f);
    CHECK(fabsf(c.yaw - 60.3f) < 1e-4f, "still waiting 1.9 s after the input: %f", c.yaw);
    Run(&c, &none, &t, 2);
    CHECK(c.yaw == 0, "then back: %f", c.yaw);
    // A stick inside the deadzone is not input.
    c.yaw = 40, c.idle = 0;
    Cam3dInput drift = none;
    drift.stick_x = 0.1f;
    Run(&c, &drift, &t, 4);
    CHECK(c.yaw == 0, "a resting stick does not hold the camera: %f", c.yaw);
  }
  // Camera returns switched on long after the last input: the spring still
  // starts from rest (no jump), and is back within 2 s.
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = 75, c.pitch = 60;
    Cam3dTuning off = Tuning(false);
    Run(&c, &none, &off, 5);
    CHECK(c.yaw == 75 && c.idle <= 2.0f, "returns off: yaw %f idle %f", c.yaw, c.idle);
    Cam3d_Update(&c, &none, &t, 1.0f / 60);
    CHECK(c.yaw < 75 && c.yaw > 74.3f, "returns on: a gentle start, yaw %f after one frame", c.yaw);
    Run(&c, &none, &t, 2);
    CHECK(c.yaw == 0 && c.pitch == 45, "returns on: back after 2 s (%f %f)", c.yaw, c.pitch);
  }
  // Long idle (the camera set from outside): still eases back.
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.idle = 5000, c.yaw = 30, c.zoom = 0.8f;
    Run(&c, &none, &t, 3);
    CHECK(c.yaw == 0 && c.zoom == 1, "after a long idle: %f %f", c.yaw, c.zoom);
  }
  // Not in first person (the third-person view waits; leaving resets it).
  {
    Cam3d c;
    Cam3d_Reset(&c);
    c.yaw = 50;
    Cam3d_SetFirstPerson(&c, true, 0);
    Run(&c, &none, &t, 5);
    CHECK(c.yaw == 50 && c.blend == 1, "first person: the third-person yaw waits (%f)", c.yaw);
  }
}

static void TestFirstPerson(void) {
  static const struct { int facing; float yaw; } kFacing[] = { { 0, 0 }, { 2, 180 }, { 4, 270 }, { 6, 90 } };
  Cam3dTuning t = Tuning(false);
  Cam3dInput none;
  memset(&none, 0, sizeof(none));
  for (int i = 0; i < 4; i++) {
    Cam3d c;
    Cam3d_Reset(&c);
    Cam3d_SetFirstPerson(&c, true, kFacing[i].facing);
    CHECK(c.first_person && c.fp_yaw == kFacing[i].yaw && c.fp_pitch == 8, "facing %d: fp_yaw %f pitch %f",
          kFacing[i].facing, c.fp_yaw, c.fp_pitch);
    CHECK(!Cam3d_IsDefaultView(&c) && !Cam3d_IsTurned(&c), "facing %d: first person is neither default nor turned",
          kFacing[i].facing);
  }
  // Entering again does not re-aim.
  Cam3d c;
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 6);
  c.fp_yaw = 123;
  Cam3d_SetFirstPerson(&c, true, 2);
  CHECK(c.fp_yaw == 123, "entering twice keeps the view");
  // The blend: 0.3 s each way.
  Cam3d_Reset(&c);
  Cam3d_SetFirstPerson(&c, true, 0);
  Run(&c, &none, &t, 0.15f);
  CHECK(fabsf(c.blend - 0.5f) < 0.01f, "blend half way at 0.15 s: %f", c.blend);
  Run(&c, &none, &t, 0.15f);
  CHECK(c.blend == 1, "blend done at 0.3 s: %f", c.blend);
  // The first-person camera: the pose's eye, the field of view, no stretch.
  Cam3dPose p = Pose(2000, 3000, 256, 224, 0);
  p.eye[0] = 2100, p.eye[1] = 3120, p.eye[2] = 20;
  R3dCamera rc;
  c.fp_yaw = 90, c.fp_pitch = 8;
  Cam3d_MakeView(&c, &p, &t, &rc);
  CheckBasis(&rc, "first person");
  CHECK(rc.eye[0] == 2100 && rc.eye[1] == 3120 && rc.eye[2] == 20, "first-person eye (%f %f %f)", rc.eye[0], rc.eye[1],
        rc.eye[2]);
  double half = 65 * 0.5 * 0.017453292519943295, focal = 112 * cos(half) / sin(half);
  CHECK(fabs(rc.focal - focal) < 0.01 && rc.stretch_y == 1 && rc.znear == 2 && !rc.ortho, "fp focal %f, want %f",
        rc.focal, focal);
  CHECK(fabs(rc.fwd[0] - cos(8 * 0.017453292519943295)) < 1e-5 && fabs(rc.fwd[1]) < 1e-5 && rc.fwd[2] < 0,
        "fp yaw 90 pitch 8 looks east and a little down");
  // Field of view 90 at h 240: focal 120. Hor+: the width does not change it.
  t.fov = 90;
  p.w = 448, p.h = 240;
  Cam3d_MakeView(&c, &p, &t, &rc);
  CHECK(fabs(rc.focal - 120) < 0.01 && rc.cx == 224 && rc.cy == 120, "fov 90: focal %f", rc.focal);
  t.fov = 65;
  // A point straight ahead is in the middle.
  double sx, sy, zc;
  Project(&rc, p.eye[0] + rc.fwd[0] * 50, p.eye[1] + rc.fwd[1] * 50, p.eye[2] + rc.fwd[2] * 50, &sx, &sy, &zc);
  CHECK(fabs(sx - 224) < 0.01 && fabs(sy - 120) < 0.01 && fabs(zc - 50) < 0.01, "fp: ahead is the middle");
  // Hor+: a narrower picture of the same height keeps the focal; the shake
  // moves the eye.
  {
    R3dCamera narrow, wide;
    Cam3dPose pn = p;
    pn.w = 256;
    pn.shake[0] = 2, pn.shake[1] = -3;
    Cam3d_MakeView(&c, &p, &t, &wide);
    Cam3d_MakeView(&c, &pn, &t, &narrow);
    CHECK(narrow.focal == wide.focal && narrow.cx == 128 && wide.cx == 224 && narrow.cy == 120, "Hor+ first person");
    CHECK(narrow.eye[0] == p.eye[0] + 2 && narrow.eye[1] == p.eye[1] - 3 && narrow.eye[2] == p.eye[2],
          "first-person shake (%f %f %f)", narrow.eye[0], narrow.eye[1], narrow.eye[2]);
  }
  // The blend has no jump at either end.
  {
    Cam3d e = c;
    e.yaw = 30, e.pitch = 50, e.zoom = 1.3f;
    R3dCamera end, near_end;
    for (int side = 0; side < 2; side++) {
      e.blend = (float)side;
      Cam3d_MakeView(&e, &p, &t, &end);
      e.blend = side ? 0.999f : 0.001f;
      Cam3d_MakeView(&e, &p, &t, &near_end);
      double d = 0;
      for (int i = 0; i < 3; i++)
        d += fabs(end.eye[i] - near_end.eye[i]) + fabs(end.fwd[i] - near_end.fwd[i]) * 100;
      d += fabs(end.focal - near_end.focal) + fabs(end.stretch_y - near_end.stretch_y) * 100;
      CHECK(d < 0.05, "blend end %d: a jump of %f", side, d);
    }
  }
  // Half way through the blend the camera is between the two and sane.
  Cam3d mid = c;
  mid.blend = 0.5f;
  mid.yaw = 0;
  Cam3d_MakeView(&mid, &p, &t, &rc);
  CheckBasis(&rc, "blend");
  CHECK(CameraFinite(&rc) && rc.focal > 120 && rc.focal < 533.4 && rc.stretch_y > 1 && rc.stretch_y < 1.4143,
        "blend: focal %f stretch %f", rc.focal, rc.stretch_y);
  // Yaw 0 to fp yaw 270 goes the short way (through 315).
  mid.fp_yaw = 270, mid.fp_pitch = 45;
  Cam3d_MakeView(&mid, &p, &t, &rc);
  CHECK(rc.fwd[0] < -0.2f && rc.fwd[1] < -0.2f, "blend 0 to 270 passes north-west (%f %f)", rc.fwd[0], rc.fwd[1]);
  // Leaving: back to the third-person view as it was.
  Cam3d_Reset(&c);
  c.yaw = 50;
  Cam3d_SetFirstPerson(&c, true, 0);
  Run(&c, &none, &t, 1);
  Cam3d_SetFirstPerson(&c, false, 0);
  CHECK(!c.first_person && c.blend == 1, "leaving starts the blend");
  Run(&c, &none, &t, 0.3f);
  CHECK(c.blend == 0 && c.yaw == 50, "left: blend %f yaw %f", c.blend, c.yaw);
  // With Camera returns, leaving eases to the original view.
  Cam3dTuning tr = Tuning(true);
  Cam3d_Reset(&c);
  c.yaw = 50, c.zoom = 1.5f;
  Cam3d_SetFirstPerson(&c, true, 0);
  Run(&c, &none, &tr, 1);
  Cam3d_SetFirstPerson(&c, false, 0);
  Cam3d_Update(&c, &none, &tr, 1.0f / 60);
  CHECK(c.yaw == 0 && c.pitch == 45 && c.zoom == 1 && c.blend > 0.9f, "returns: the view eases to the original");
  Run(&c, &none, &tr, 0.3f);
  CHECK(Cam3d_IsDefaultView(&c), "returns: the original view after 0.3 s");
}

static void TestTurnedAndDefault(void) {
  Cam3d c;
  Cam3d_Reset(&c);
  CHECK(Cam3d_IsDefaultView(&c) && !Cam3d_IsTurned(&c), "reset is the default view");
  c.yaw = 0.4f;
  CHECK(Cam3d_IsDefaultView(&c), "0.4 degrees is default");
  c.yaw = -0.6f;
  CHECK(!Cam3d_IsDefaultView(&c) && !Cam3d_IsTurned(&c), "0.6 degrees is neither");
  c.yaw = 10.5f;
  CHECK(Cam3d_IsTurned(&c), "10.5 degrees is turned");
  c.yaw = 359;
  CHECK(!Cam3d_IsTurned(&c), "359 degrees is 1 degree");
  c.yaw = 0, c.pitch = 56;
  CHECK(Cam3d_IsTurned(&c), "tilted 11 degrees is turned");
  c.pitch = 45, c.zoom = 1.2f;
  CHECK(!Cam3d_IsDefaultView(&c) && !Cam3d_IsTurned(&c), "zoomed is neither");
  c.zoom = 1, c.blend = 0.1f;
  CHECK(!Cam3d_IsDefaultView(&c), "blending is not the default view");
}

static void TestRobust(void) {
  Cam3dTuning t = Tuning(true);
  Cam3dInput junk;
  memset(&junk, 0, sizeof(junk));
  junk.stick_x = NAN, junk.stick_y = INFINITY, junk.mouse_dx = NAN, junk.mouse_dy = -INFINITY;
  junk.key_turn = NAN, junk.key_tilt = 1e30f, junk.wheel = 0x7fffffff;
  Cam3d c;
  Cam3d_Reset(&c);
  Cam3d_Update(&c, &junk, &t, NAN);
  Cam3d_Update(&c, &junk, &t, -1);
  Cam3d_Update(&c, &junk, &t, 1e30f);
  Cam3d_SetFirstPerson(&c, true, 255);
  Cam3d_Update(&c, &junk, &t, 0.05f);
  Cam3d_Update(&c, NULL, NULL, 0.05f);  // (no input, default tuning)
  bool ok = Finite(c.yaw) && Finite(c.pitch) && Finite(c.zoom) && Finite(c.fp_yaw) && Finite(c.fp_pitch) &&
            Finite(c.blend) && Finite(c.idle) && Finite(c.rate_yaw) && Finite(c.rate_pitch);
  CHECK(ok, "junk input left a non-finite camera");
  // A camera holding junk still makes a finite view (with a sane basis).
  Cam3d bad;
  Cam3d_Reset(&bad);
  bad.yaw = NAN, bad.pitch = INFINITY, bad.zoom = NAN, bad.blend = NAN;
  Cam3dPose p = Pose(100, 100, 256, 224, 0);
  R3dCamera rc;
  Cam3d_MakeView(&bad, &p, &t, &rc);
  CHECK(CameraFinite(&rc), "junk camera made a non-finite view");
  CheckBasis(&rc, "junk camera");
  bad.blend = 1, bad.fp_yaw = -INFINITY, bad.fp_pitch = NAN;
  Cam3d_MakeView(&bad, &p, &t, &rc);
  CHECK(CameraFinite(&rc), "junk first-person camera made a non-finite view");
  // Out-of-range settings are clamped.
  Cam3dTuning wild = { 255, 255, 255, 255, true, true };
  Cam3d_Reset(&c);
  Cam3d_MakeView(&c, &p, &wild, &rc);
  CHECK(CameraFinite(&rc) && fabs(rc.focal - 320) < 0.01, "perspective 255 acts as 100: focal %f", rc.focal);
  Cam3d_MakeView(&c, &p, NULL, &rc);
  CHECK(CameraFinite(&rc) && fabs(rc.focal - 1600 / 3.0) < 0.01, "no tuning: the defaults");
}

int main(void) {
  TestIdentity();
  TestTables();
  TestHysteresis();
  TestSweep();
  TestBits();
  TestLatch();
  TestDefaultView();
  TestTestView();
  TestStick();
  TestMouseKeysWheel();
  TestSpringBack();
  TestFirstPerson();
  TestTurnedAndDefault();
  TestRobust();
  CHECK(g_r3d_bad == 0, "R3d_Project disagrees on %d of %d points", g_r3d_bad, g_r3d_checked);
  printf("cam3d: %d checks, %d failed (R3d_Project cross-checked at %d points%s)\n", g_checks, g_fails,
         g_r3d_checked, g_r3d_checked ? "" : ": r3d.c is a stub");
  return g_fails != 0;
}
