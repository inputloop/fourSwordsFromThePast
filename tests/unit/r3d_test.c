// Unit test of the 3D view's rasterizer (src/r3d.c). Needs no ROM and no SDL:
//   cc -O2 -Werror tests/unit/r3d_test.c src/r3d.c -lm -o r3d_test && ./r3d_test
// (Not -I src: src/features.h would hide the C library's <features.h>.)
// It checks:
// - 2000 random frames, many of them absurd (NaN, huge or degenerate quads,
//   the eye inside the geometry, pitch to +-89 degrees, cards behind the
//   camera, sizes 1x1 to 448x240), drawn into the middle of buffers full of
//   sentinels: nothing outside the target's w x h pixels (pitch 3 w + 7
//   pixels), its depth or its classes may change, and every target pixel
//   must be written.
// - The same frames split among parts in another order give the same bytes.
// - The oblique test camera shows a floor, a raised block, a pit, a shadow
//   and a card exactly as the flat picture (texel for pixel).
// - Shared edges: a mesh drawn additively covers every pixel exactly once.
// - Depth, decals, back faces, half, add, dither, light, subtraction, fog,
//   card order, x-ray and see-through.
// - R3d_Project round trips, R3d_ScaleAt and R3d_BoxVisible.
// "./r3d_test <seed>" runs other random frames; "./r3d_test bench [frames [view 0-5]]"
// times a 6k-quad room at 448x240 on one thread instead.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/r3d.h"

static const float kDeg = 0.017453292f;
static const float kSqrt2 = 1.41421356f;
static const float kHalfSqrt2 = 0.70710678f;

static int g_failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
      if (g_failures++ < 30) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } \
  } while (0)

// ---- Random numbers ----

static uint32 g_rng = 0x9e3779b9u;

static uint32 Rand(void) {
  g_rng ^= g_rng << 13, g_rng ^= g_rng >> 17, g_rng ^= g_rng << 5;
  return g_rng;
}

static int RandInt(int lo, int hi) {  // lo..hi
  return lo + (int)(Rand() % (uint32)(hi - lo + 1));
}

static float RandF(float lo, float hi) {
  return lo + (hi - lo) * (float)(Rand() >> 8) * (1.0f / 16777216.0f);
}

static float FloatBits(uint32 bits) {
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

// Now and then something no caller should pass.
static float Weird(float normal) {
  switch (RandInt(0, 40)) {
  case 0: return FloatBits(0x7fc00000);   // NaN
  case 1: return FloatBits(0x7f800000);   // +Inf
  case 2: return FloatBits(0xff800000);   // -Inf
  case 3: return 1e30f;
  case 4: return -1e30f;
  case 5: return RandF(-30000.0f, 30000.0f);
  default: return normal;
  }
}

// ---- Cameras ----

// Looking along yaw (0 north, 90 east) and pitch (degrees below the horizon) from eye.
static void LookCamera(R3dCamera *c, const float eye[3], float yaw, float pitch, float focal, float stretch,
                       float cx, float cy, float znear, float zfar) {
  memset(c, 0, sizeof(*c));
  float hx = sinf(yaw * kDeg), hy = -cosf(yaw * kDeg), cp = cosf(pitch * kDeg), sp = sinf(pitch * kDeg);
  c->eye[0] = eye[0], c->eye[1] = eye[1], c->eye[2] = eye[2];
  c->fwd[0] = hx * cp, c->fwd[1] = hy * cp, c->fwd[2] = -sp;
  c->up[0] = hx * sp, c->up[1] = hy * sp, c->up[2] = cp;
  c->right[0] = -hy, c->right[1] = hx, c->right[2] = 0;
  c->focal = focal, c->stretch_y = stretch, c->cx = cx, c->cy = cy, c->znear = znear, c->zfar = zfar;
}

// The same, dist px back from target.
static void OrbitCamera(R3dCamera *c, const float target[3], float yaw, float pitch, float dist, float focal,
                        float stretch, int w, int h, float znear, float zfar) {
  float eye[3] = { 0, 0, 0 };
  LookCamera(c, eye, yaw, pitch, focal, stretch, (float)w * 0.5f, (float)h * 0.5f, znear, zfar);
  for (int i = 0; i < 3; i++)
    c->eye[i] = target[i] - c->fwd[i] * dist;
}

// The parallel oblique camera: world (x, y, z) lands on pixel (x - camx, y - z - camy).
static void ObliqueCamera(R3dCamera *c, float camx, float camy, int w, int h, float back) {
  memset(c, 0, sizeof(*c));
  c->eye[0] = camx + (float)w * 0.5f, c->eye[1] = camy + (float)h * 0.5f + back, c->eye[2] = back;
  c->right[0] = 1;
  c->up[1] = -kHalfSqrt2, c->up[2] = kHalfSqrt2;
  c->fwd[1] = -kHalfSqrt2, c->fwd[2] = -kHalfSqrt2;
  c->focal = 1, c->stretch_y = kSqrt2;
  c->cx = (float)w * 0.5f, c->cy = (float)h * 0.5f;
  c->znear = 1, c->zfar = 0;
  c->ortho = true;
}

// ---- Runners ----

static void SerialRunner(int count, R3dJob *job, void *ctx) {
  job(0, count, ctx);
}

static int g_split_parts = 2;
static bool g_split_reverse;

// The work in g_split_parts parts, maybe last part first (as threads might).
static void SplitRunner(int count, R3dJob *job, void *ctx) {
  int parts = g_split_parts;
  for (int k = 0; k < parts; k++) {
    int i = g_split_reverse ? parts - 1 - k : k;
    job(count * i / parts, count * (i + 1) / parts, ctx);
  }
}

// ---- Textures ----

typedef struct TestTex {
  uint8 *texels;
  uint32 lut[256];
} TestTex;

static void MakeTexture(TestTex *t, R3dTexture *out, int log2_w, int log2_h, int zero_percent) {
  int n = 1 << (log2_w + log2_h);
  t->texels = (uint8 *)malloc((size_t)n);
  for (int i = 0; i < n; i++)
    t->texels[i] = RandInt(0, 99) < zero_percent ? 0 : (uint8)RandInt(1, 255);
  for (int i = 0; i < 256; i++)
    t->lut[i] = Rand();  // (the top byte too: r3d must not let it through)
  memset(out, 0, sizeof(*out));
  out->texels = t->texels, out->lut = t->lut;
  out->log2_w = (uint8)log2_w, out->log2_h = (uint8)log2_h;
  out->subtract_from = 255;
}

static uint8 TexelAt(const R3dTexture *t, int u, int v) {
  uint32 x = ((uint32)u - (uint32)t->origin_x) & ((1u << t->log2_w) - 1);
  uint32 y = ((uint32)v - (uint32)t->origin_y) & ((1u << t->log2_h) - 1);
  uint32 i = (y << t->log2_w) + x;
  return t->texels[i];
}

// ---- Frames ----

static void DefaultFrame(R3dFrame *f, int w, int h) {
  memset(f, 0, sizeof(*f));
  f->w = w, f->h = h;
  f->ambient[0] = f->ambient[1] = f->ambient[2] = 1.0f;
  f->light_max = 1.25f;
}

typedef struct Target {
  uint32 *pixels;
  float *depth;
  uint8 *cls;
  int w, h;
  R3dTarget t;
} Target;

static void NewTarget(Target *T, int w, int h) {
  T->w = w, T->h = h;
  T->pixels = (uint32 *)calloc((size_t)w * h, 4);
  T->depth = (float *)calloc((size_t)w * h, sizeof(float));
  T->cls = (uint8 *)calloc((size_t)w * h, 1);
  T->t.pixels = (uint8 *)T->pixels, T->t.pitch = (size_t)w * 4;
  T->t.depth = T->depth, T->t.cls = T->cls, T->t.cls_pitch = w;
}

static void FreeTarget(Target *T) {
  free(T->pixels), free(T->depth), free(T->cls);
}

static void SetQuad(R3dQuad *q, const float p[4][3], const float uv[4][2], uint8 tex, uint8 flags, uint32 color) {
  memset(q, 0, sizeof(*q));
  for (int k = 0; k < 4; k++) {
    q->v[k].x = p[k][0], q->v[k].y = p[k][1], q->v[k].z = p[k][2];
    q->v[k].u = uv ? uv[k][0] : 0.0f, q->v[k].v = uv ? uv[k][1] : 0.0f;
  }
  q->tex = tex, q->flags = flags, q->color = color, q->shade = 255, q->cls = kR3dClass_World;
}

// A horizontal rectangle at height z, front up, textured by relief (u, v) = (x, y - z).
static void FloorQuad(R3dQuad *q, float x0, float y0, float x1, float y1, float z, uint8 tex, uint8 flags) {
  float p[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
  float uv[4][2] = { { x0, y0 - z }, { x1, y0 - z }, { x1, y1 - z }, { x0, y1 - z } };
  SetQuad(q, p, uv, tex, flags, 0);
}

// A wall facing south at y from z0 up to z1, textured by relief.
static void SouthFace(R3dQuad *q, float x0, float x1, float y, float z0, float z1, uint8 tex, uint8 flags) {
  float p[4][3] = { { x0, y, z1 }, { x1, y, z1 }, { x1, y, z0 }, { x0, y, z0 } };
  float uv[4][2] = { { x0, y - z1 }, { x1, y - z1 }, { x1, y - z0 }, { x0, y - z0 } };
  SetQuad(q, p, uv, tex, flags, 0);
}

// A wall facing east at x from z0 up to z1 (u one texel inside, west of x).
static void EastFace(R3dQuad *q, float x, float y0, float y1, float z0, float z1, uint8 tex) {
  float p[4][3] = { { x, y1, z1 }, { x, y0, z1 }, { x, y0, z0 }, { x, y1, z0 } };
  float u = x - 0.5f;
  float uv[4][2] = { { u, y1 - z1 }, { u, y0 - z1 }, { u, y0 - z0 }, { u, y1 - z0 } };
  SetQuad(q, p, uv, tex, 0, 0);
}

// ---- The fuzz ----

enum { kGuard = 64 };
static const uint32 kSentinel = 0xdeadbeefu;
static const uint8 kClsSentinel = 0xa5;

static void RandomCamera(R3dCamera *c, int w, int h, const float center[3]) {
  int mode = RandInt(0, 9);
  float yaw = RandF(-180.0f, 180.0f), pitch = RandF(-89.0f, 89.0f);
  float stretch = RandInt(0, 3) == 0 ? kSqrt2 : RandInt(0, 3) == 0 ? 0.0f : RandF(0.5f, 2.0f);
  float cx = RandInt(0, 3) ? (float)w * 0.5f : RandF(-2.0f * (float)w, 3.0f * (float)w);
  float cy = RandInt(0, 3) ? (float)h * 0.5f : RandF(-2.0f * (float)h, 3.0f * (float)h);
  static const float kNears[] = { 0.0f, 0.1f, 1.0f, 2.0f, 8.0f, 50.0f, -3.0f };
  float znear = kNears[RandInt(0, 6)];
  float zfar = RandInt(0, 2) ? 0.0f : RandF(1.0f, 5000.0f);
  if (mode <= 3) {
    // Third person around the scene.
    OrbitCamera(c, center, yaw, RandF(20.0f, 89.0f), RandF(50.0f, 2000.0f), RandF(50.0f, 2000.0f), stretch, w, h,
                znear, zfar);
    c->cx = cx, c->cy = cy;
  } else if (mode <= 6) {
    // An eye inside the geometry, looking anywhere.
    float eye[3] = { center[0] + RandF(-64.0f, 64.0f), center[1] + RandF(-64.0f, 64.0f),
                     center[2] + RandF(-20.0f, 40.0f) };
    LookCamera(c, eye, yaw, pitch, RandF(20.0f, 600.0f), stretch, cx, cy, znear, zfar);
  } else if (mode <= 8) {
    ObliqueCamera(c, center[0] - (float)w * 0.5f, center[1] - (float)h * 0.5f, w, h, RandF(-200.0f, 5000.0f));
    if (RandInt(0, 1))
      c->focal = RandF(0.25f, 4.0f), c->cx = cx, c->cy = cy;
  } else {
    float eye[3] = { RandF(-1e5f, 1e5f), RandF(-1e5f, 1e5f), RandF(-1e5f, 1e5f) };
    LookCamera(c, eye, yaw, pitch, RandF(0.01f, 1e6f), stretch, cx, cy, znear, zfar);
    c->ortho = RandInt(0, 1);
  }
  // Now and then a broken value anywhere.
  float *fields[] = { &c->eye[0], &c->eye[2], &c->right[0], &c->up[1], &c->fwd[2], &c->focal, &c->stretch_y,
                      &c->cx, &c->cy, &c->znear, &c->zfar };
  if (RandInt(0, 9) == 0)
    *fields[RandInt(0, 10)] = Weird(FloatBits(0x7fc00000));
}

static void RandomVertex(R3dVertex *v, const float center[3], float radius) {
  v->x = Weird(center[0] + RandF(-radius, radius));
  v->y = Weird(center[1] + RandF(-radius, radius));
  v->z = Weird(center[2] + RandF(-radius * 0.25f, radius * 0.25f));
  v->u = Weird(RandF(-2000.0f, 2000.0f));
  v->v = Weird(RandF(-2000.0f, 2000.0f));
}

static void RandomQuad(R3dQuad *q, const float center[3]) {
  memset(q, 0, sizeof(*q));
  int kind = RandInt(0, 9);
  if (kind <= 4) {
    // A cell of a room near the center: a top or a face.
    float x = center[0] + (float)(RandInt(-12, 12) * 8), y = center[1] + (float)(RandInt(-12, 12) * 8);
    float z = (float)(RandInt(-2, 6) * 8);
    if (RandInt(0, 1))
      FloorQuad(q, x, y, x + 8.0f * (float)RandInt(1, 4), y + 8.0f, z, (uint8)RandInt(0, 7), 0);
    else
      SouthFace(q, x, x + 8.0f, y, z - 8.0f * (float)RandInt(1, 4), z, (uint8)RandInt(0, 7), 0);
  } else {
    float radius = kind <= 6 ? 200.0f : kind == 7 ? 30000.0f : 20.0f;
    for (int k = 0; k < 4; k++)
      RandomVertex(&q->v[k], center, radius);
    if (kind == 9) {
      // Degenerate: corners repeated or in a line.
      int which = RandInt(0, 2);
      if (which == 0) {
        q->v[1] = q->v[2] = q->v[3] = q->v[0];
      } else if (which == 1) {
        for (int k = 1; k < 4; k++) {
          q->v[k].x = q->v[0].x + (float)k * 3.0f, q->v[k].y = q->v[0].y + (float)k, q->v[k].z = q->v[0].z;
        }
      } else {
        q->v[RandInt(1, 3)] = q->v[0];
      }
    }
  }
  q->color = Rand();
  q->tex = (uint8)RandInt(0, 7);
  q->flags = (uint8)(Rand() & 63);
  if (RandInt(0, 2))
    q->flags &= ~(kR3dQuad_Half | kR3dQuad_Add | kR3dQuad_Decal | kR3dQuad_Dither);
  q->shade = (uint8)(RandInt(0, 1) ? 255 : RandInt(0, 255));
  q->cls = (uint8)RandInt(0, 5);
}

static void RandomCard(R3dCard *c, const float center[3], const R3dCamera *cam) {
  memset(c, 0, sizeof(*c));
  int kind = RandInt(0, 5);
  for (int i = 0; i < 3; i++)
    c->foot[i] = center[i] + RandF(-150.0f, 150.0f);
  if (kind == 0) {
    // Behind the camera.
    for (int i = 0; i < 3; i++)
      c->foot[i] = cam->eye[i] - cam->fwd[i] * RandF(0.0f, 300.0f) + RandF(-5.0f, 5.0f);
  } else if (kind == 1) {
    for (int i = 0; i < 3; i++)
      c->foot[i] = Weird(RandF(-30000.0f, 30000.0f));
  } else if (kind == 2) {
    // Right at the eye.
    for (int i = 0; i < 3; i++)
      c->foot[i] = cam->eye[i] + cam->fwd[i] * RandF(-3.0f, 3.0f);
  }
  c->atlas_x = (int16)RandInt(-600, 600), c->atlas_y = (int16)RandInt(-600, 600);
  c->w = (int16)(RandInt(0, 9) == 0 ? RandInt(-5, 0) : RandInt(0, 9) == 0 ? 32767 : RandInt(1, 64));
  c->h = (int16)(RandInt(0, 9) == 0 ? RandInt(-5, 0) : RandInt(0, 9) == 0 ? 32767 : RandInt(1, 64));
  c->foot_x = (int16)RandInt(-40, 80), c->foot_y = (int16)RandInt(-40, 80);
  if (RandInt(0, 19) == 0)
    c->foot_x = 32767, c->foot_y = -32768;
  c->order = (uint16)RandInt(0, 3);
  c->tex = (uint8)RandInt(0, 7);
  c->flags = (uint8)RandInt(0, 3);
  c->xray_rgb = Rand();
}

typedef struct FuzzBuffers {
  uint32 *pix;               // (pitch_px x (h + 4)), the target at column w, row 2
  int pitch_px;
  float *depth;              // kGuard + w h + kGuard
  uint8 *cls;                // kGuard + cls_pitch h + kGuard
  int cls_pitch;
} FuzzBuffers;

static void FillBuffers(FuzzBuffers *B, int w, int h, uint32 fill) {
  for (int i = 0; i < B->pitch_px * (h + 4); i++)
    B->pix[i] = kSentinel;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      B->pix[(y + 2) * B->pitch_px + w + x] = fill;
  for (int i = 0; i < w * h + 2 * kGuard; i++)
    B->depth[i] = FloatBits(kSentinel);
  memset(B->cls, kClsSentinel, (size_t)B->cls_pitch * h + 2 * kGuard);
}

// Checks the sentinels, and that the drawn part (w x h, at most kR3dMaxW x
// kR3dMaxH) was written: no pixel keeps a top byte, every depth is a number.
static int CheckBuffers(const FuzzBuffers *B, int w, int h, bool cls_used, int iter) {
  int bad = 0, dw = w < kR3dMaxW ? w : kR3dMaxW, dh = h < kR3dMaxH ? h : kR3dMaxH;
  for (int y = 0; y < h + 4; y++) {
    for (int x = 0; x < B->pitch_px; x++) {
      uint32 v = B->pix[y * B->pitch_px + x];
      bool inside = y >= 2 && y < h + 2 && x >= w && x < 2 * w;
      if (!inside && v != kSentinel)
        bad++;
      if (inside && x - w < dw && y - 2 < dh && (v >> 24) != 0)
        bad++;
    }
  }
  for (int i = 0; i < w * h + 2 * kGuard; i++) {
    float d = B->depth[i];
    bool inside = i >= kGuard && i < kGuard + w * h;
    uint32 bits;
    memcpy(&bits, &d, 4);
    if (!inside && bits != kSentinel)
      bad++;
    if (inside && (i - kGuard) % w < dw && (i - kGuard) / w < dh && !(d >= 0.0f && d < 1e30f))
      bad++;
  }
  for (int i = 0; i < B->cls_pitch * h + 2 * kGuard; i++) {
    bool inside = i >= kGuard && i < kGuard + B->cls_pitch * h && (i - kGuard) % B->cls_pitch < w;
    if ((!inside || !cls_used) && B->cls[i] != kClsSentinel)
      bad++;
  }
  CHECK(bad == 0, "fuzz frame %d (%dx%d): %d sentinels changed or pixels not written", iter, w, h, bad);
  return bad;
}

static void TestFuzz(void) {
  R3dWork *works[2] = { R3d_NewWork(1000, 48), R3d_NewWork(0, 0) };
  CHECK(works[0] && works[1], "R3d_NewWork");
  static R3dQuad quads[1200];
  static R3dCard cards[64];
  static R3dLight lights[64];
  TestTex tt[kR3dMaxTextures];
  int total_quads = 0, total_cards = 0, frames = 2000;
  for (int iter = 0; iter < frames; iter++) {
    int w, h;
    switch (iter % 10) {
    case 0: w = 1, h = 1; break;
    case 1: w = kR3dMaxW, h = kR3dMaxH; break;
    case 2: w = RandInt(1, 3), h = RandInt(1, kR3dMaxH); break;
    case 3: w = RandInt(1, kR3dMaxW), h = RandInt(1, 3); break;
    case 4: w = 256, h = 224; break;
    default: w = RandInt(1, kR3dMaxW), h = RandInt(1, kR3dMaxH); break;
    }
    if (iter % 97 == 5)
      w = kR3dMaxW + RandInt(1, 40);  // (wider than allowed: only kR3dMaxW columns are drawn)
    R3dWork *work = works[iter % 13 == 0];
    // The scene.
    float center[3] = { RandF(-1000.0f, 5000.0f), RandF(-1000.0f, 5000.0f), RandF(-50.0f, 50.0f) };
    if (iter % 7 == 0)
      center[0] = RandF(-30000.0f, 30000.0f), center[1] = RandF(-30000.0f, 30000.0f);
    R3dFrame f;
    DefaultFrame(&f, w, h);
    RandomCamera(&f.cam, w, h, center);
    for (int i = 0; i < kR3dMaxTextures; i++) {
      MakeTexture(&tt[i], &f.tex[i], RandInt(0, 9), RandInt(0, 9), RandInt(0, 50));
      f.tex[i].origin_x = RandInt(0, 3) ? 0 : (int32)Rand();
      f.tex[i].origin_y = RandInt(0, 3) ? 0 : (int32)Rand();
      static const uint8 kFrom[] = { 0, 65, 255, 1, 200 };
      f.tex[i].subtract_from = kFrom[RandInt(0, 4)];
      if (RandInt(0, 19) == 0)
        f.tex[i].texels = NULL;
      if (RandInt(0, 19) == 0)
        f.tex[i].lut = NULL;
      if (RandInt(0, 29) == 0)
        f.tex[i].log2_w = 20;
    }
    int nq = RandInt(0, 3) == 0 ? RandInt(0, 20) : RandInt(0, 1200);
    for (int i = 0; i < nq; i++)
      RandomQuad(&quads[i], center);
    f.quads = quads, f.num_quads = nq;
    if (RandInt(0, 29) == 0)
      f.num_quads = -RandInt(1, 100);
    if (RandInt(0, 29) == 0)
      f.quads = NULL;
    int nc = RandInt(0, 64);
    for (int i = 0; i < nc; i++)
      RandomCard(&cards[i], center, &f.cam);
    f.cards = cards, f.num_cards = nc;
    if (RandInt(0, 29) == 0)
      f.cards = NULL;
    int nl = RandInt(0, 64);
    for (int i = 0; i < nl; i++) {
      R3dLight *L = &lights[i];
      L->x = Weird(center[0] + RandF(-200.0f, 200.0f));
      L->y = Weird(center[1] + RandF(-200.0f, 200.0f));
      L->z = Weird(center[2] + RandF(0.0f, 40.0f));
      L->radius = Weird(RandF(-10.0f, 300.0f));
      L->power = Weird(RandF(0.0f, 2.0f));
      L->r = RandF(0.0f, 1.5f), L->g = RandF(0.0f, 1.5f), L->b = Weird(RandF(0.0f, 1.5f));
    }
    f.lights = lights, f.num_lights = nl;
    for (int i = 0; i < 3; i++) {
      f.ambient[i] = Weird(RandF(0.0f, 1.5f));
      f.subtract[i] = Weird(RandInt(0, 1) ? 0.0f : RandF(-20.0f, 300.0f));
    }
    f.light_max = Weird(RandF(0.0f, 2.0f));
    f.card_min_light = Weird(RandF(0.0f, 1.0f));
    f.subtract_lights = RandInt(0, 1), f.subtract_cards = RandInt(0, 1);
    f.live_x0 = center[0] - RandF(0.0f, 300.0f), f.live_x1 = center[0] + RandF(0.0f, 300.0f);
    f.live_y0 = Weird(center[1] - RandF(0.0f, 300.0f)), f.live_y1 = center[1] + RandF(0.0f, 300.0f);
    f.live_fog0 = RandF(0.0f, 20.0f), f.live_fog1 = Weird(RandF(-10.0f, 100.0f));
    f.dist_fog0 = RandF(0.0f, 200.0f), f.dist_fog1 = RandInt(0, 1) ? 0.0f : Weird(RandF(0.0f, 400.0f));
    f.fog_rgb = Rand();
    f.see_through = RandInt(0, 1), f.xray = RandInt(0, 1);
    // The background.
    int bg_pitch = w + RandInt(-2, 8);
    uint32 *bg = (uint32 *)malloc((size_t)(bg_pitch > 0 ? bg_pitch : 1) * h * 4);
    for (int i = 0; i < (bg_pitch > 0 ? bg_pitch : 1) * h; i++)
      bg[i] = Rand();
    f.background = RandInt(0, 2) ? NULL : bg;
    f.background_pitch = bg_pitch;
    // The target, in the middle of sentinels.
    FuzzBuffers B;
    B.pitch_px = 3 * w + 7;
    B.pix = (uint32 *)malloc((size_t)B.pitch_px * (h + 4) * 4);
    B.depth = (float *)malloc(((size_t)w * h + 2 * kGuard) * sizeof(float));
    B.cls_pitch = w + RandInt(0, 9);
    B.cls = (uint8 *)malloc((size_t)B.cls_pitch * h + 2 * kGuard);
    R3dTarget t;
    t.pixels = (uint8 *)(B.pix + 2 * B.pitch_px + w);
    t.pitch = (size_t)B.pitch_px * 4;
    t.depth = B.depth + kGuard;
    t.cls = RandInt(0, 9) ? B.cls + kGuard : NULL;
    // (Now and then a pitch narrower than the drawn width: then the classes are not written.)
    int drawn_w = w < kR3dMaxW ? w : kR3dMaxW;
    t.cls_pitch = RandInt(0, 19) ? B.cls_pitch : drawn_w - 1;
    bool cls_used = t.cls && t.cls_pitch >= drawn_w;
    FillBuffers(&B, w, h, 0xff00ff00u);
    R3dStats stats;
    R3d_Draw(work, &f, &t, iter & 1 ? SerialRunner : NULL, &stats);
    total_quads += stats.quads_drawn, total_cards += stats.cards_drawn;
    if (CheckBuffers(&B, w, h, cls_used, iter) == 0) {
      // Again, split up: the same bytes.
      size_t np = (size_t)B.pitch_px * (h + 4), nd = (size_t)w * h + 2 * kGuard;
      size_t nc2 = (size_t)B.cls_pitch * h + 2 * kGuard;
      uint32 *pix0 = (uint32 *)malloc(np * 4);
      float *depth0 = (float *)malloc(nd * sizeof(float));
      uint8 *cls0 = (uint8 *)malloc(nc2);
      memcpy(pix0, B.pix, np * 4), memcpy(depth0, B.depth, nd * sizeof(float)), memcpy(cls0, B.cls, nc2);
      FillBuffers(&B, w, h, 0x12345678u);
      g_split_parts = 2 + iter % 4, g_split_reverse = (iter / 4) & 1;
      R3dStats stats2;
      R3d_Draw(work, &f, &t, SplitRunner, &stats2);
      CheckBuffers(&B, w, h, cls_used, iter);
      // (Columns past kR3dMaxW are not drawn: they keep what each fill put there.)
      for (int y = 0; y < h && w > kR3dMaxW; y++)
        memcpy(&B.pix[(y + 2) * B.pitch_px + w + kR3dMaxW], &pix0[(y + 2) * B.pitch_px + w + kR3dMaxW],
               (size_t)(w - kR3dMaxW) * 4);
      CHECK(memcmp(pix0, B.pix, np * 4) == 0, "fuzz frame %d: pixels differ when split %d ways", iter, g_split_parts);
      CHECK(memcmp(depth0, B.depth, nd * sizeof(float)) == 0, "fuzz frame %d: depth differs when split", iter);
      CHECK(memcmp(cls0, B.cls, nc2) == 0, "fuzz frame %d: classes differ when split", iter);
      CHECK(stats.quads_drawn == stats2.quads_drawn && stats.cards_drawn == stats2.cards_drawn,
            "fuzz frame %d: stats differ when split", iter);
      free(pix0), free(depth0), free(cls0);
    }
    free(B.pix), free(B.depth), free(B.cls), free(bg);
    for (int i = 0; i < kR3dMaxTextures; i++)
      free(tt[i].texels);
  }
  printf("fuzz: %d frames, %d quads and %d cards drawn in all\n", frames, total_quads, total_cards);
  CHECK(total_quads > 50000 && total_cards > 1000, "the fuzz drew too little (%d quads, %d cards)", total_quads,
        total_cards);
  R3d_FreeWork(works[0]);
  R3d_FreeWork(works[1]);
  R3d_FreeWork(NULL);
}

// ---- The oblique test ----

// The test camera shows a floor, a block raised by the relief rule, a pit,
// a shadow decal and a card exactly as the flat picture would.
static void TestOblique(int w, int h, R3dRunner *run) {
  TestTex tt, ta;
  R3dFrame f;
  DefaultFrame(&f, w, h);
  MakeTexture(&tt, &f.tex[0], 9, 9, 0);
  f.tex[0].origin_x = 1024, f.tex[0].origin_y = -512;
  MakeTexture(&ta, &f.tex[4], 6, 6, 40);
  f.tex[4].origin_x = 3, f.tex[4].origin_y = 5;
  int camx = 1234, camy = 777;
  ObliqueCamera(&f.cam, (float)camx, (float)camy, w, h, 300.0f);
  static R3dQuad quads[4096];
  int n = 0;
  // A raised block: picture rows [by, by + 32), columns [bx, bx + 40), 16 px high.
  int bx = camx + 40, by = camy + 64, bh = 16;
  // A pit: picture rows [py, py + 40), columns [px, px + 48), 16 px deep.
  int px = camx + 120, py = camy + 100, pd = 16;
  for (int y = camy - 16; y < camy + h + 16; y += 8) {
    for (int x = camx - 16; x < camx + w + 16; x += 8) {
      bool in_block = x >= bx && x < bx + 40 && y >= by && y < by + 32;
      bool in_pit = x >= px && x < px + 48 && y >= py && y < py + 40;
      if (!in_block && !in_pit)
        FloorQuad(&quads[n++], (float)x, (float)y, (float)x + 8.0f, (float)y + 8.0f, 0.0f, 0, 0);
    }
  }
  // The block: its top lands bh px south and up (the floor it uncovers is
  // inpainted with the top's own picture), its south face is the painted face.
  FloorQuad(&quads[n++], (float)bx, (float)by, (float)bx + 40.0f, (float)(by + bh), 0.0f, 0, 0);
  FloorQuad(&quads[n++], (float)bx, (float)(by + bh), (float)bx + 40.0f, (float)(by + 32), (float)bh, 0, 0);
  SouthFace(&quads[n++], (float)bx, (float)bx + 40.0f, (float)(by + 32), 0.0f, (float)bh, 0, 0);
  EastFace(&quads[n++], (float)bx + 40.0f, (float)(by + bh), (float)(by + 32), 0.0f, (float)bh, 0);
  // The pit: the painted inner wall, the floor pd px down (and inpainted under the south rim).
  SouthFace(&quads[n++], (float)px, (float)px + 48.0f, (float)py, (float)-pd, 0.0f, 0, 0);
  FloorQuad(&quads[n++], (float)px, (float)py, (float)px + 48.0f, (float)(py + 40), (float)-pd, 0, 0);
  // A shadow decal on the floor, textured from the atlas by relief.
  int sx = camx + 200 < camx + w - 20 ? camx + 200 : camx + 2, sy = camy + 30;
  float sp[4][3] = { { (float)sx, (float)sy, 0 }, { (float)sx + 12.0f, (float)sy, 0 },
                     { (float)sx + 12.0f, (float)sy + 6.0f, 0 }, { (float)sx, (float)sy + 6.0f, 0 } };
  float suv[4][2] = { { 0, 0 }, { 12, 0 }, { 12, 6 }, { 0, 6 } };
  SetQuad(&quads[n++], sp, suv, 4, kR3dQuad_Decal, 0);
  f.quads = quads, f.num_quads = n;
  // A card standing on the floor: its foot at picture (cx, cy).
  R3dCard card;
  memset(&card, 0, sizeof(card));
  int ccx = camx + 20, ccy = camy + 40;
  card.foot[0] = (float)ccx, card.foot[1] = (float)ccy, card.foot[2] = 0;
  card.atlas_x = 20, card.atlas_y = 10, card.w = 16, card.h = 24, card.foot_x = 8, card.foot_y = 22;
  card.tex = 4;
  f.cards = &card, f.num_cards = 1;
  Target T;
  NewTarget(&T, w, h);
  R3dWork *work = R3d_NewWork(4096, 4);
  R3dStats stats;
  R3d_Draw(work, &f, &T.t, run, &stats);
  int bad = 0, card_px = 0, shadow_px = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int wx = camx + x, wy = camy + y;
      uint8 t = TexelAt(&f.tex[0], wx, wy);
      uint32 want = tt.lut[t] & 0xffffff;
      if (wx >= sx && wx < sx + 12 && wy >= sy && wy < sy + 6) {
        uint8 s = TexelAt(&f.tex[4], wx - sx, wy - sy);
        if (s)
          want = ta.lut[s] & 0xffffff, shadow_px++;
      }
      int cxp = wx - (ccx - 8), cyp = wy - (ccy - 22);
      if (cxp >= 0 && cxp < 16 && cyp >= 0 && cyp < 24) {
        uint8 s = TexelAt(&f.tex[4], 20 + cxp, 10 + cyp);
        if (s)
          want = ta.lut[s] & 0xffffff, card_px++;
      }
      if (T.pixels[y * w + x] != want && bad++ < 5)
        printf("  oblique %dx%d: pixel (%d, %d) is %06x, want %06x\n", w, h, x, y, T.pixels[y * w + x], want);
    }
  }
  CHECK(bad == 0, "oblique %dx%d: %d pixels differ from the flat picture", w, h, bad);
  CHECK(card_px > 50 && shadow_px > 10, "oblique: the card (%d px) or shadow (%d px) is missing", card_px, shadow_px);
  // (The east face is edge-on: culled. So are floor cells off the picture.)
  CHECK(stats.quads_drawn < n && stats.quads_drawn >= (w / 8) * (h / 8) * 3 / 4 && stats.cards_drawn == 1,
        "oblique: %d quads (of %d), %d cards drawn", stats.quads_drawn, n, stats.cards_drawn);
  R3d_FreeWork(work);
  FreeTarget(&T);
  free(tt.texels), free(ta.texels);
}

// ---- Shared edges ----

// A jittered mesh of quads drawn additively in 1/255 steps: every pixel it
// covers is covered exactly once, so a picture fully inside it is all 1.
static void TestWatertight(void) {
  int w = 448, h = 240;
  enum { kN = 96 };
  static float grid[kN + 1][kN + 1][2];
  static R3dQuad quads[kN * kN];
  Target T;
  NewTarget(&T, w, h);
  R3dWork *work = R3d_NewWork(kN * kN, 0);
  for (int pass = 0; pass < 12; pass++) {
    float cell = pass < 6 ? 8.0f : 3.0f;
    for (int j = 0; j <= kN; j++) {
      for (int i = 0; i <= kN; i++) {
        bool edge = i == 0 || j == 0 || i == kN || j == kN;
        grid[j][i][0] = (float)i * cell + (edge ? 0.0f : RandF(-0.3f, 0.3f) * cell);
        grid[j][i][1] = (float)j * cell + (edge ? 0.0f : RandF(-0.3f, 0.3f) * cell);
      }
    }
    int n = 0;
    for (int j = 0; j < kN; j++) {
      for (int i = 0; i < kN; i++) {
        float p[4][3] = { { grid[j][i][0], grid[j][i][1], 0 }, { grid[j][i + 1][0], grid[j][i + 1][1], 0 },
                          { grid[j + 1][i + 1][0], grid[j + 1][i + 1][1], 0 },
                          { grid[j + 1][i][0], grid[j + 1][i][1], 0 } };
        SetQuad(&quads[n++], p, NULL, 0, kR3dQuad_Flat | kR3dQuad_Add, 0x010101);
      }
    }
    R3dFrame f;
    DefaultFrame(&f, w, h);
    float mid = (float)kN * cell * 0.5f;
    float target[3] = { mid + RandF(-20.0f, 20.0f), mid + RandF(-20.0f, 20.0f), 0 };
    // Steep enough that the mesh fills the picture; the last ones put the eye
    // a few px over it, so the near plane cuts the quads at the bottom (with
    // the horizon in view, so some of the mesh lies beyond the near plane).
    bool low = pass % 6 >= 4;
    float pitch = low ? RandF(5.0f, 25.0f) : RandF(40.0f, 89.0f);
    float dist = low ? RandF(0.5f, 2.5f) : RandF(150.0f, 400.0f) * cell / 8.0f;
    float focal = low ? RandF(100.0f, 200.0f) : RandF(150.0f, 600.0f);
    OrbitCamera(&f.cam, target, RandF(-180.0f, 180.0f), pitch, dist, focal, RandInt(0, 1) ? kSqrt2 : 1.0f, w, h,
                low ? 2.0f : 8.0f, 0.0f);
    f.quads = quads, f.num_quads = n;
    R3dStats stats;
    R3d_Draw(work, &f, &T.t, NULL, &stats);
    // A pixel left out is a gap if its ray meets the mesh (1 px inside its border).
    int gaps = 0, more = 0, drawn = 0, cut = 0, expect = 0;
    const R3dCamera *c = &f.cam;
    float size = (float)kN * cell;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        uint32 v = T.pixels[y * w + x];
        more += v != 0 && v != 0x010101, drawn += v == 0x010101;
        if (v)
          continue;
        float a = ((float)x + 0.5f - c->cx) / c->focal, b = -((float)y + 0.5f - c->cy) / (c->focal * c->stretch_y);
        float dir[3];
        for (int i = 0; i < 3; i++)
          dir[i] = c->fwd[i] + a * c->right[i] + b * c->up[i];
        // (dir . fwd = 1, so tt is the hit's depth: nearer than the near plane is rightly left out.)
        float tt = dir[2] < 0 ? -c->eye[2] / dir[2] : 0.0f;
        float hx = c->eye[0] + tt * dir[0], hy = c->eye[1] + tt * dir[1];
        bool on_mesh = tt > 0 && hx > 1.0f && hx < size - 1.0f && hy > 1.0f && hy < size - 1.0f;
        cut += on_mesh && tt < c->znear - 0.5f;
        gaps += on_mesh && tt > c->znear + 0.5f;
      }
    }
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float a = ((float)x + 0.5f - c->cx) / c->focal, b = -((float)y + 0.5f - c->cy) / (c->focal * c->stretch_y);
        float dz = c->fwd[2] + a * c->right[2] + b * c->up[2];
        float tt = dz < 0 ? -c->eye[2] / dz : 0.0f;
        float hx = c->eye[0] + tt * (c->fwd[0] + a * c->right[0] + b * c->up[0]);
        float hy = c->eye[1] + tt * (c->fwd[1] + a * c->right[1] + b * c->up[1]);
        expect += tt > c->znear + 0.5f && hx > 1.0f && hx < size - 1.0f && hy > 1.0f && hy < size - 1.0f;
      }
    }
    CHECK(more == 0, "watertight pass %d: %d pixels drawn twice or more", pass, more);
    CHECK(gaps == 0, "watertight pass %d: %d pixels left out inside the mesh", pass, gaps);
    CHECK(expect > w * h / (low ? 256 : 2) && drawn >= expect, "watertight pass %d: %d pixels drawn, %d expected", pass,
          drawn, expect);
    CHECK(!low || cut > 0, "watertight pass %d: the near plane cut nothing", pass);
  }
  R3d_FreeWork(work);
  FreeTarget(&T);
}

// ---- Semantics ----

// A top-down perspective camera over (0, 0): one world px is one screen px at z = 0.
static void TopDown(R3dFrame *f, int w, int h) {
  DefaultFrame(f, w, h);
  float eye[3] = { 0, 0, 200 };
  LookCamera(&f->cam, eye, 0, 90.0f, 200.0f, 1.0f, (float)w * 0.5f, (float)h * 0.5f, 1.0f, 0.0f);
}

static uint32 Center(const Target *T) {
  return T->pixels[(T->h / 2) * T->w + T->w / 2];
}

static void TestSemantics(void) {
  int w = 64, h = 64;
  Target T;
  NewTarget(&T, w, h);
  R3dWork *work = R3d_NewWork(16, 8);
  R3dFrame f;
  R3dQuad q[4];
  R3dStats st;
  // The nearer of two quads wins, whichever comes first.
  for (int order = 0; order < 2; order++) {
    TopDown(&f, w, h);
    FloorQuad(&q[order], -20, -20, 20, 20, 0, 0, kR3dQuad_Flat);
    q[order].color = 0x0000ff;
    FloorQuad(&q[1 - order], -10, -10, 10, 10, 10, 0, kR3dQuad_Flat);
    q[1 - order].color = 0xff0000;
    f.quads = q, f.num_quads = 2;
    R3d_Draw(work, &f, &T.t, NULL, &st);
    CHECK(Center(&T) == 0xff0000 && T.pixels[14 * w + 14] == 0x0000ff, "depth order %d: %06x %06x", order, Center(&T),
          T.pixels[14 * w + 14]);
    CHECK(T.cls[(h / 2) * w + w / 2] == kR3dClass_World && T.cls[0] == kR3dClass_Far, "classes");
  }
  // A decal shows on the floor it lies on, not under it. Back faces are culled unless two-sided.
  TopDown(&f, w, h);
  FloorQuad(&q[0], -20, -20, 20, 20, 0, 0, kR3dQuad_Flat);
  q[0].color = 0x0000ff;
  FloorQuad(&q[1], -5, -5, 5, 5, 0, 0, kR3dQuad_Flat | kR3dQuad_Decal);
  q[1].color = 0x00ff00;
  FloorQuad(&q[2], 10, 10, 15, 15, -3, 0, kR3dQuad_Flat | kR3dQuad_Decal);
  q[2].color = 0xffffff;
  f.quads = q, f.num_quads = 3;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x00ff00 && T.pixels[(h / 2 + 12) * w + w / 2 + 12] == 0x0000ff, "decal: %06x %06x", Center(&T),
        T.pixels[(h / 2 + 12) * w + w / 2 + 12]);
  float back[4][3] = { { -5, -5, 20 }, { -5, 5, 20 }, { 5, 5, 20 }, { 5, -5, 20 } };
  SetQuad(&q[1], back, NULL, 0, kR3dQuad_Flat, 0xff0000);
  f.num_quads = 2;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x0000ff && st.quads_drawn == 1, "a back face was drawn");
  q[1].flags |= kR3dQuad_TwoSided;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0xff0000 && st.quads_drawn == 2, "a two-sided quad was not drawn");
  // Half and add blend over what is behind, and don't hide what comes later; dither draws every other pixel.
  FloorQuad(&q[0], -20, -20, 20, 20, 0, 0, kR3dQuad_Flat);
  q[0].color = 0x808080;
  FloorQuad(&q[1], -20, -20, 20, 20, 5, 0, kR3dQuad_Flat | kR3dQuad_Half);
  q[1].color = 0x000040;
  f.num_quads = 2;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x404060, "half: %06x", Center(&T));
  q[1].flags = kR3dQuad_Flat | kR3dQuad_Add, q[1].color = 0x90a000;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0xffff80, "add: %06x", Center(&T));
  q[1].flags = kR3dQuad_Flat | kR3dQuad_Dither, q[1].color = 0xffffff;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  int lit = 0;
  for (int y = 24; y < 40; y++) {
    for (int x = 24; x < 40; x++)
      lit += (T.pixels[y * w + x] == 0xffffff) == !((x + y) & 1);
  }
  CHECK(lit == 256, "dither: %d of 256 pixels right", lit);
  // Light: ambient, the cap, a light (per vertex: four quads meet under it), face shade.
  f.num_quads = 1;
  q[0].color = 0xc08040;
  f.ambient[0] = 0.5f, f.ambient[1] = 1.0f, f.ambient[2] = 2.0f;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x608050, "ambient and cap: %06x", Center(&T));
  R3dQuad lq[4];
  for (int i = 0; i < 4; i++) {
    float x0 = i & 1 ? 0.0f : -60.0f, y0 = i & 2 ? 0.0f : -60.0f;
    FloorQuad(&lq[i], x0, y0, x0 + 60.0f, y0 + 60.0f, 0, 1, kR3dQuad_Flat);
    lq[i].color = 0xc08040;
  }
  f.quads = lq, f.num_quads = 4;
  f.ambient[0] = f.ambient[1] = f.ambient[2] = 0.25f;
  R3dLight light = { 0, 0, 10, 100, 1, 1, 1, 1 };
  f.lights = &light, f.num_lights = 1;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  uint32 c0 = Center(&T), c1 = T.pixels[2 * w + 2];
  CHECK((c0 >> 16) > 0xe0 && (c0 >> 16) <= 0xf0 && (c1 >> 16) < (c0 >> 16) - 0x20 && (c1 >> 16) >= 0x30,
        "light: %06x %06x", c0, c1);
  // The darkness with a hole where the Lamp is.
  for (int i = 0; i < 4; i++) {
    float x0 = i & 1 ? 0.0f : -60.0f, y0 = i & 2 ? 0.0f : -60.0f;
    FloorQuad(&lq[i], x0, y0, x0 + 60.0f, y0 + 60.0f, 0, 2, 0);
  }
  TestTex td;
  MakeTexture(&td, &f.tex[2], 4, 4, 0);
  for (int i = 0; i < 256; i++)
    td.lut[i] = 0x808080;
  f.tex[2].subtract_from = 0;
  f.ambient[0] = f.ambient[1] = f.ambient[2] = 1.0f;
  f.subtract[0] = f.subtract[1] = f.subtract[2] = 255;
  R3dLight lamp = { 0, 0, 0, 30, 2, 0, 0, 0 };
  f.lights = &lamp, f.num_lights = 1, f.subtract_lights = true;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK((Center(&T) >> 16) >= 0x78 && T.pixels[0] == 0, "subtract hole: %06x %06x", Center(&T), T.pixels[0]);
  f.lights = NULL, f.num_lights = 0, f.subtract_lights = false;
  f.subtract[0] = f.subtract[1] = f.subtract[2] = 0;
  free(td.texels);
  f.quads = q, f.num_quads = 1;
  q[0].shade = 128;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x604020, "shade: %06x", Center(&T));
  q[0].shade = 255;
  // The game's darkness: subtracted, except in a light's hole and below subtract_from.
  TestTex tt;
  MakeTexture(&tt, &f.tex[1], 4, 4, 0);
  for (int i = 0; i < 256; i++)
    tt.lut[i] = 0x808080;
  memset(tt.texels, 100, 256);
  FloorQuad(&q[0], -20, -20, 20, 20, 0, 1, 0);
  f.subtract[0] = f.subtract[1] = f.subtract[2] = 255;
  f.tex[1].subtract_from = 0;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0, "subtract: %06x", Center(&T));
  f.tex[1].subtract_from = 101;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x808080, "subtract_from: %06x", Center(&T));
  f.subtract[0] = f.subtract[1] = f.subtract[2] = 0;
  // Fog: by distance from the eye, and outside the live rectangle.
  f.fog_rgb = 0x204060;
  f.dist_fog0 = 10, f.dist_fog1 = 100;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x204060, "distance fog: %06x", Center(&T));
  f.dist_fog0 = f.dist_fog1 = 0;
  f.live_x0 = -100, f.live_x1 = 100, f.live_y0 = -100, f.live_y1 = 100, f.live_fog0 = 8, f.live_fog1 = 72;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x808080, "live fog inside: %06x", Center(&T));
  f.live_x1 = -200;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x204060, "live fog outside: %06x", Center(&T));
  f.live_fog0 = f.live_fog1 = 0;
  // Cards: lower order on top at the same foot; hidden by a wall unless x-ray or see-through.
  TestTex ta;
  MakeTexture(&ta, &f.tex[4], 5, 5, 0);
  for (int i = 0; i < 1024; i++)
    ta.texels[i] = i < 512 ? 1 : 2;
  ta.lut[1] = 0xff0000, ta.lut[2] = 0x00ff00;
  R3dCard cards[2];
  memset(cards, 0, sizeof(cards));
  for (int i = 0; i < 2; i++) {
    cards[i].foot[0] = 0, cards[i].foot[1] = 0, cards[i].foot[2] = 0;
    cards[i].w = 8, cards[i].h = 8, cards[i].foot_x = 4, cards[i].foot_y = 4, cards[i].tex = 4;
    cards[i].atlas_y = (int16)(i * 16);
  }
  cards[0].order = 1, cards[1].order = 0;
  f.cards = NULL, f.num_cards = 0;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  float floor_depth = T.depth[(h / 2) * w + w / 2];
  f.cards = cards, f.num_cards = 2;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(T.depth[(h / 2) * w + w / 2] == floor_depth, "a card wrote the depth buffer");
  CHECK(Center(&T) == 0x00ff00 && T.cls[(h / 2) * w + w / 2] == kR3dClass_Sprite && st.cards_drawn == 2,
        "card order: %06x", Center(&T));
  cards[0].order = 0, cards[1].order = 1;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0xff0000, "card order: %06x", Center(&T));
  f.num_cards = 1;
  FloorQuad(&q[1], -20, -20, 20, 20, 30, 0, kR3dQuad_Flat);
  q[1].color = 0x0000ff;
  f.num_quads = 2;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x0000ff, "hidden card: %06x", Center(&T));
  cards[0].flags = kR3dCard_XRay | kR3dCard_Link, cards[0].xray_rgb = 0xff00ff;
  f.xray = true;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x6600ff, "x-ray: %06x", Center(&T));
  f.xray = false, f.see_through = true;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x990066, "see-through: %06x", Center(&T));
  // Light at the foot, at least card_min_light.
  f.see_through = false, f.num_quads = 1;
  f.ambient[0] = f.ambient[1] = f.ambient[2] = 0.25f;
  f.card_min_light = 0.5f;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(Center(&T) == 0x7f0000, "card light: %06x", Center(&T));
  // Misaligned pixels or pitch: nothing is written.
  memset(T.pixels, 0x11, (size_t)w * h * 4);
  T.t.pitch = (size_t)w * 4 - 2;
  R3d_Draw(work, &f, &T.t, NULL, &st);
  CHECK(T.pixels[0] == 0x11111111u && T.pixels[w * h - 1] == 0x11111111u, "a misaligned pitch was written");
  T.t.pitch = (size_t)w * 4;
  free(tt.texels), free(ta.texels);
  R3d_FreeWork(work);
  FreeTarget(&T);
}

// ---- Projection ----

static void TestProject(void) {
  int fails = 0;
  for (int iter = 0; iter < 20000; iter++) {
    R3dCamera c;
    float target[3] = { RandF(0.0f, 4096.0f), RandF(0.0f, 4096.0f), RandF(-16.0f, 16.0f) };
    int w = RandInt(64, 448), h = RandInt(64, 240);
    if (iter % 5 == 4) {
      ObliqueCamera(&c, target[0], target[1], w, h, RandF(0.0f, 500.0f));
      c.focal = RandF(0.5f, 3.0f);
    } else {
      OrbitCamera(&c, target, RandF(-180.0f, 180.0f), RandF(-89.0f, 89.0f), RandF(20.0f, 900.0f), RandF(100.0f, 900.0f),
                  RandF(1.0f, 1.5f), w, h, RandF(1.0f, 8.0f), 0.0f);
    }
    float p[3] = { target[0] + RandF(-300.0f, 300.0f), target[1] + RandF(-300.0f, 300.0f),
                   target[2] + RandF(-64.0f, 64.0f) };
    float sx, sy, depth;
    bool front = R3d_Project(&c, p, &sx, &sy, &depth);
    float d[3] = { p[0] - c.eye[0], p[1] - c.eye[1], p[2] - c.eye[2] };
    float xc = d[0] * c.right[0] + d[1] * c.right[1] + d[2] * c.right[2];
    float yc = d[0] * c.up[0] + d[1] * c.up[1] + d[2] * c.up[2];
    float zc = d[0] * c.fwd[0] + d[1] * c.fwd[1] + d[2] * c.fwd[2];
    if (c.ortho) {
      // (Depth is measured from 4096 px behind the eye.)
      float q[3];
      float rx = (sx - c.cx) / c.focal, ry = -(sy - c.cy) / (c.focal * c.stretch_y), rz = depth - 4096.0f;
      for (int i = 0; i < 3; i++)
        q[i] = c.eye[i] + rx * c.right[i] + ry * c.up[i] + rz * c.fwd[i];
      float err = fabsf(q[0] - p[0]) + fabsf(q[1] - p[1]) + fabsf(q[2] - p[2]);
      fails += !front || err > 0.02f;
      continue;
    }
    if (zc >= c.znear) {
      float rx = (sx - c.cx) * depth / c.focal, ry = -(sy - c.cy) * depth / (c.focal * c.stretch_y);
      float q[3];
      for (int i = 0; i < 3; i++)
        q[i] = c.eye[i] + rx * c.right[i] + ry * c.up[i] + depth * c.fwd[i];
      float err = fabsf(q[0] - p[0]) + fabsf(q[1] - p[1]) + fabsf(q[2] - p[2]);
      bool near_picture = sx > -1e5f && sx < 1e5f && sy > -1e5f && sy < 1e5f;
      fails += !front || fabsf(depth - zc) > 0.01f || (err > 0.02f && near_picture);
      // One world px along right is R3d_ScaleAt screen px. (p + right is
      // rounded to about 0.002 px here, which moves the point that much
      // across and in depth.)
      float p2[3] = { p[0] + c.right[0], p[1] + c.right[1], p[2] + c.right[2] }, sx2, sy2, depth2;
      R3d_Project(&c, p2, &sx2, &sy2, &depth2);
      float scale = R3d_ScaleAt(&c, depth);
      if (fabsf(sx) < 4096.0f)
        fails += fabsf((sx2 - sx) - scale) > 0.002f * (scale + fabsf(sx - c.cx) / depth) + 0.002f;
      // A small box around a point on the picture is visible; a box behind the eye is not.
      float lo[3] = { p[0] - 1, p[1] - 1, p[2] - 1 }, hi[3] = { p[0] + 1, p[1] + 1, p[2] + 1 };
      if (sx > 2 && sx < (float)w - 2 && sy > 2 && sy < (float)h - 2)
        fails += !R3d_BoxVisible(&c, w, h, lo, hi);
    } else if (zc < -1.0f) {
      // Behind: false, and the point leans its way.
      fails += front;
      fails += (xc > 1.0f && sx <= c.cx) || (xc < -1.0f && sx >= c.cx);
      fails += (yc > 1.0f && sy >= c.cy) || (yc < -1.0f && sy <= c.cy);
      float lo[3], hi[3];
      for (int i = 0; i < 3; i++)
        lo[i] = c.eye[i] - c.fwd[i] * 50.0f - 5.0f, hi[i] = c.eye[i] - c.fwd[i] * 50.0f + 5.0f;
      fails += R3d_BoxVisible(&c, w, h, lo, hi);
    }
  }
  CHECK(fails == 0, "R3d_Project / R3d_ScaleAt / R3d_BoxVisible: %d failures", fails);
  R3dCamera bad;
  memset(&bad, 0, sizeof(bad));
  float p[3] = { 1, 2, 3 }, sx, sy, depth;
  CHECK(!R3d_Project(&bad, p, &sx, &sy, &depth) && sx == 0 && sy == 0, "a zero camera projects");
  bad.focal = FloatBits(0x7fc00000);
  CHECK(!R3d_Project(&bad, p, &sx, &sy, &depth), "a NaN camera projects");
}

// ---- The benchmark ----

static double g_job_time[2];

static double Seconds(void) {
  return (double)clock() / CLOCKS_PER_SEC;
}

// Times the setup pass (many parts) apart from the band pass (kR3dBands parts).
static void TimingRunner(int count, R3dJob *job, void *ctx) {
  double t0 = Seconds();
  job(0, count, ctx);
  g_job_time[count == kR3dBands] += Seconds() - t0;
}

static void Bench(int frames, int only) {
  int w = 448, h = 240;
  // A 64 x 64 cell room like the speed probes': tops, walls around it and
  // pillars, each wall cell with a south and an east face (about 6.2k quads).
  static R3dQuad quads[8192];
  int n = 0;
  for (int cy = 0; cy < 64; cy++) {
    for (int cx = 0; cx < 64; cx++) {
      float x = (float)cx * 8.0f, y = (float)cy * 8.0f;
      bool wall = cx < 4 || cx >= 60 || cy < 4 || cy >= 60 || ((cx % 13) == 0 && (cy % 7) < 3);
      float z = wall ? 24.0f : 0.0f;
      FloorQuad(&quads[n++], x, y, x + 8.0f, y + 8.0f, z, 0, 0);
      if (wall) {
        SouthFace(&quads[n], x, x + 8.0f, y + 8.0f, 0.0f, z, 0, 0);
        quads[n++].shade = 176;
        EastFace(&quads[n], x + 8.0f, y, y + 8.0f, 0.0f, z, 0);
        quads[n++].shade = 216;
      }
    }
  }
  TestTex tt;
  R3dFrame f;
  DefaultFrame(&f, w, h);
  MakeTexture(&tt, &f.tex[0], 9, 9, 10);
  f.quads = quads, f.num_quads = n;
  static R3dLight lights[8];
  for (int i = 0; i < 8; i++) {
    lights[i].x = 64.0f + (float)(i % 4) * 128.0f, lights[i].y = 128.0f + (float)(i / 4) * 256.0f, lights[i].z = 16;
    lights[i].radius = 96, lights[i].power = 0.6f, lights[i].r = 1, lights[i].g = 0.8f, lights[i].b = 0.6f;
  }
  Target T;
  NewTarget(&T, w, h);
  R3dWork *work = R3d_NewWork(16384, 160);
  float target[3] = { 256, 256, 0 };
  printf("bench: %d quads at %dx%d, one thread\n", n, w, h);
  for (int view = 0; view < 6; view++) {
    if (only >= 0 && view != only)
      continue;
    bool lit = view >= 3;
    int v = view % 3;
    if (v == 0) {
      OrbitCamera(&f.cam, target, 0, 45.0f, 533.0f, 533.0f, kSqrt2, w, h, 8.0f, 4000.0f);
    } else if (v == 1) {
      OrbitCamera(&f.cam, target, 40.0f, 40.0f, 533.0f, 533.0f, 1.0f + (kSqrt2 - 1.0f) * 0.8f, w, h, 8.0f, 4000.0f);
    } else {
      float eye[3] = { 256, 400, 20 };
      LookCamera(&f.cam, eye, 0, 8.0f, 188.0f, 1.0f, (float)w * 0.5f, (float)h * 0.5f, 2.0f, 4000.0f);
    }
    // Lit: dim with lights, the mist outside the live area, and distance fog in first person.
    f.lights = lit ? lights : NULL, f.num_lights = lit ? 8 : 0;
    f.ambient[0] = f.ambient[1] = f.ambient[2] = lit ? 0.6f : 1.0f;
    f.live_x0 = 32, f.live_y0 = 136, f.live_x1 = 480, f.live_y1 = 376;
    f.live_fog0 = 8, f.live_fog1 = lit ? 72.0f : 0.0f;
    f.dist_fog0 = 160, f.dist_fog1 = lit && v == 2 ? 320.0f : 0.0f;
    f.fog_rgb = 0x405060;
    R3dStats st;
    for (int i = 0; i < 20 && i < frames; i++)
      R3d_Draw(work, &f, &T.t, NULL, &st);
    g_job_time[0] = g_job_time[1] = 0;
    double t0 = Seconds();
    for (int i = 0; i < frames; i++)
      R3d_Draw(work, &f, &T.t, TimingRunner, &st);
    double ms = (Seconds() - t0) * 1000.0 / frames;
    int covered = 0;
    for (int i = 0; i < w * h; i++)
      covered += T.depth[i] != 0;
    static const char *const kNames[3] = { "third person, yaw 0", "third person, turned 40", "first person" };
    printf("  %-24s %-5s %6.3f ms/frame (setup %.3f, bands %.3f), %d quads drawn, %d%% covered\n", kNames[v],
           lit ? "lit" : "plain", ms, g_job_time[0] * 1000.0 / frames, g_job_time[1] * 1000.0 / frames, st.quads_drawn,
           covered * 100 / (w * h));
  }
  R3d_FreeWork(work);
  FreeTarget(&T);
  free(tt.texels);
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "bench") == 0) {
    int frames = argc > 2 ? atoi(argv[2]) : 400;
    Bench(frames > 0 ? frames : 1, argc > 3 ? atoi(argv[3]) : -1);
    return 0;
  }
  if (argc > 1)
    g_rng = (uint32)strtoul(argv[1], NULL, 0) | 1;
  TestSemantics();
  TestOblique(256, 224, NULL);
  TestOblique(448, 240, SerialRunner);
  g_split_parts = 3, g_split_reverse = true;
  TestOblique(301, 199, SplitRunner);
  TestWatertight();
  TestProject();
  TestFuzz();
  if (g_failures) {
    printf("r3d_test: %d checks failed\n", g_failures);
    return 1;
  }
  printf("r3d_test: all passed\n");
  return 0;
}
