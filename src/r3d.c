// The 3D view's software rasterizer (see r3d.h). A frame is drawn in three
// steps:
// - Setup, on the runner over the quads (in chunks dealt round robin, see
//   SetupJob): each quad goes to camera space (most quads off the picture
//   stop there), is checked (NaN, absurd coordinates, back faces), is lit
//   and fogged at its corners, is clipped against the near plane and the
//   picture's four sides (1 px outside them) and is projected. It keeps the
//   screen gradients of 1/z, u/z and v/z (exactly planar on the screen)
//   for the spans.
// - Binning, here: the cards are sorted far to near, the kR3dBands row
//   bands are sized so that each has about the same work, and every quad
//   left goes in the list of each band it touches (opaque quads, then
//   decals, then see-through ones, each kind in the caller's order).
// - Bands, on the runner: a band clears its rows to the background, draws
//   its list and then the cards. A span steps 1/z, u/z and v/z (one divide
//   per pixel) and the light, darkness and fog taken along the quad's edges.
// Each pixel row belongs to one band and a band only reads what setup wrote,
// so the picture does not depend on how the runner splits the work.
//
// Conventions:
// - Pixels are sampled at their centers with the top-left fill rule: a pixel
//   whose center is exactly on a left or top edge is in, on a right or bottom
//   edge out. Two quads that share an edge never both draw a pixel of it,
//   and leave no gap.
// - The depth buffer holds 1 / depth (0: nothing there yet). An ortho camera
//   measures depth from kOrthoBack px behind its eye, so what stands up to
//   that far behind the eye still shows (R3d_Project measures it the same
//   way). Its far plane is not used.
// - A quad's texel coordinates are rebased to start at 1 (so truncation is
//   floor) and clamped to the quad's own range, so rounding at its edges
//   never reads past it (and a float never overflows an int).
// - A card's foot point (foot_x, foot_y) is a pixel corner of its picture:
//   it lands where the foot projects, and the card is drawn at the foot's
//   scale both ways (R3d_ScaleAt: like the 2D picture, no vertical stretch).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "r3d.h"

// The hot helpers are always inlined (FORCEINLINE is only a hint to gcc and
// clang), so each span loop is made for its own mix of features.
#if defined(__GNUC__) && !defined(__TINYC__)
#define R3D_INLINE static inline __attribute__((always_inline))
#else
#define R3D_INLINE static FORCEINLINE
#endif

enum {
  kMaxPolyVerts = 9,         // a convex quad clipped by 5 planes
  kClipCap = 16,             // what one clipping step may hold (more: a twisted quad, dropped)
  kMaxWorkQuads = 1 << 18,
  kMaxWorkCards = 1 << 16,
  kSetupChunk = 8,           // quads per item of the setup job
  kSetupLanes = 16,          // (see SetupJob)
};

static const float kOrthoBack = 4096.0f;
static const float kMaxCoord = 100000.0f;  // a quad or card with a coordinate beyond this is dropped
static const float kMinNear = 0.25f;       // the nearest a near plane may be
static const float kMaxLight = 4.0f;       // light per channel never goes above this
static const float kBelowOne = 0.99999976f;  // 1 - 2^-22

// The light per channel (x 256, face shade applied), how much of the
// frame's subtraction applies (0-256) and the fog (0-256): per corner, and
// across a quad along its edges (Gouraud). 1/z, u/z and v/z are exactly
// planar on the screen, so a quad keeps their gradients instead (Poly.g).
enum { kS_R, kS_G, kS_B, kS_Dark, kS_Fog, kNumS };
typedef struct SVert { float x, y, s[kNumS]; } SVert;

// A camera space vertex while clipping: x, y, z, u, v, then the kNumS shades.
enum { kNumC = 5 + kNumS };
typedef struct CVert { float c[kNumC]; } CVert;

// How a quad's spans are drawn.
enum {
  kSpan_Plain,               // perspective, textured, opaque, as painted (lut[texel])
  kSpan_Lit,                 // perspective, textured, opaque, with light, darkness or fog
  kSpan_General,             // anything else: ortho, flat, dithered, decals, half and add
};

// What a lit quad's spans have to do besides white light at most as painted.
enum {
  kVary_Dark = 1,            // darkness to subtract
  kVary_Fog = 2,             // fog
  kVary_Color = 4,           // colored light, or brighter than painted: each channel on its own
};

// One quad after setup.
typedef struct Poly {
  int16 y0, y1;              // the rows it covers
  int16 x0, x1;              // the columns it covers at most
  uint8 n;                   // vertices (0: not drawn)
  uint8 span;                // kSpan_*
  uint8 pass;                // 0 opaque, 1 decal, 2 half or add
  uint8 vary;                // kVary_*
  float ox, oy;              // the screen point the gradients start at
  float g[3][3];             // 1/z, u/z, v/z (ortho: z, u, v) there, then per px right and per px down
  float ulo, uhi, vlo, vhi;  // the rebased texel range
  uint32 ou, ov;             // added to a rebased texel coordinate: the rebase minus the texture origin
} Poly;

// A card after setup.
typedef struct PCard {
  float z;                   // the foot's depth (the sort key)
  float test;                // the 1/depth its pixels are tested at (2 px nearer)
  float left, top, inv_s;    // where its picture's corner lands, 1 / scale
  int x0, x1, y0, y1;        // the pixels it covers
  int index;
  int l[3];                  // light x 256
  int sub[3];                // what is subtracted (0-255)
  int fog;                   // 0-256
  int sub_from;              // texels >= this get the subtraction (256: none)
  bool plain;
} PCard;

typedef struct Cam {
  float eye[3], right[3], up[3], fwd[3];
  float focal, fy;           // fy: focal * stretch_y
  float cx, cy, znear, zfar; // zfar 0: none
  float back;                // kOrthoBack for ortho, else 0
  float side[4];             // the side planes for OutCode (set per frame): cx + 1, w + 1 - cx, cy + 1, h + 1 - cy
  bool ortho, ok;
} Cam;

typedef struct Prep {
  Cam cam;
  int w, h;
  float planes[5][4];        // near, left, right, top, bottom: a x + b y + c z + d >= 0 inside
  bool tex_ok[kR3dMaxTextures];
  float ambient[3], light_max, card_min;
  R3dLight lights[kR3dMaxLights];
  int num_lights;
  int sub[3];                // the frame's subtraction, 0-255
  bool any_sub, sub_lights, sub_cards;
  bool live_fog, dist_fog, any_fog;
  float live[4], live_f0, live_inv, dist_f0, dist_inv;  // (inv: 1 / (fog1 - fog0))
  uint32 fog_rgb;
  const uint32 *bg;
  int bg_pitch;
  bool see_through, xray;
  uint8 *pixels;
  size_t pitch;
  float *depth;              // NULL: only the background is drawn
  int depth_pitch;           // the frame's w (more than w when that is over kR3dMaxW)
  uint8 *cls;
  int cls_pitch;             // 0: no class buffer (the band's scratch row is used)
} Prep;

struct R3dWork {
  int max_quads, max_cards;
  Poly *polys;
  SVert *v4;                 // the first 4 vertices of each quad
  SVert *v5;                 // the other 5 (only clipped quads use them)
  int *bins;                 // kR3dBands lists of up to max_quads
  int bin_count[kR3dBands];
  PCard *cards;
  int *card_order, *card_tmp;
  int num_cards;
  uint8 scratch_cls[kR3dBands][kR3dMaxW];
  int row_cost[kR3dMaxH];
  int band_y[kR3dBands + 1]; // band b draws rows band_y[b] .. band_y[b + 1] - 1
  // This frame.
  Prep P;
  const R3dFrame *f;
  int num_quads;
};

// ---- Small helpers ----

R3D_INLINE float ClampF(float x, float lo, float hi) {
  // (NaN gives lo.)
  return x >= lo ? (x <= hi ? x : hi) : lo;
}

R3D_INLINE bool InRange(float x, float limit) {
  // (False for NaN and the infinities.)
  return x >= -limit && x <= limit;
}

// The first pixel whose center is at or right of x: ceil(x - 0.5). x is
// within a few pixels of the picture here. (Truncation rounds toward 0, so
// it steps up when it cut a fraction off a positive value.)
R3D_INLINE int CeilHalf(float x) {
  float v = x - 0.5f;
  int i = (int)v;
  return i + ((float)i < v);
}

// floor(x), for x well inside an int's range.
R3D_INLINE int32 FloorI(float x) {
  int32 i = (int32)x;
  return i - ((float)i > x);
}

// 0 at a, 1 at b, smooth between (inv = 1 / (b - a)).
static float SmoothStep(float a, float inv, float x) {
  float t = ClampF((x - a) * inv, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

R3D_INLINE float Dot3(const float *a, const float *b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

R3D_INLINE void ToCamera(const Cam *c, const float p[3], float out[3]) {
  float d[3] = { p[0] - c->eye[0], p[1] - c->eye[1], p[2] - c->eye[2] };
  out[0] = Dot3(d, c->right);
  out[1] = Dot3(d, c->up);
  out[2] = Dot3(d, c->fwd) + c->back;
}

// A checked copy of the camera. ok is false when it can't be used at all.
static void PrepCamera(const R3dCamera *src, Cam *c) {
  memset(c, 0, sizeof(*c));
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    c->eye[i] = src->eye[i], c->right[i] = src->right[i], c->up[i] = src->up[i], c->fwd[i] = src->fwd[i];
    ok &= InRange(c->eye[i], 1e6f) && InRange(c->right[i], 2.0f) && InRange(c->up[i], 2.0f) &&
          InRange(c->fwd[i], 2.0f);
  }
  c->ortho = src->ortho;
  c->back = c->ortho ? kOrthoBack : 0.0f;
  // (A stretch left at 0 means none.)
  float stretch = src->stretch_y > 0.0f && src->stretch_y <= 100.0f ? src->stretch_y : 1.0f;
  c->focal = src->focal;
  c->fy = src->focal * stretch;
  c->cx = src->cx, c->cy = src->cy;
  ok &= c->focal > 0.0f && c->focal <= 1e5f && InRange(c->cx, 1e5f) && InRange(c->cy, 1e5f);
  c->znear = src->znear >= kMinNear ? (src->znear <= 1e5f ? src->znear : 1e5f) : kMinNear;
  c->zfar = !c->ortho && src->zfar > c->znear && src->zfar <= 1e7f ? src->zfar : 0.0f;
  c->ok = ok;
}

// The five clipping planes in camera space, 1 px (margin) outside the picture.
static void MakePlanes(const Cam *c, int w, int h, float margin, float planes[5][4]) {
  float p[5][4] = {
    { 0, 0, 1, -c->znear },
    { c->focal, 0, c->cx + margin, 0 },
    { -c->focal, 0, (float)w + margin - c->cx, 0 },
    { 0, -c->fy, c->cy + margin, 0 },
    { 0, c->fy, (float)h + margin - c->cy, 0 },
  };
  if (c->ortho) {
    // (Parallel: the sides don't move with depth.)
    for (int i = 1; i < 5; i++)
      p[i][3] = p[i][2], p[i][2] = 0;
  }
  memcpy(planes, p, sizeof(p));
}

R3D_INLINE float PlaneDist(const float *pl, const float *c) {
  return pl[0] * c[0] + pl[1] * c[1] + pl[2] * c[2] + pl[3];
}

// Which of the five planes (bit 0 near, then left, right, top, bottom) a
// camera space point is outside of: Prep.planes, tested the quick way.
R3D_INLINE int OutCode(const Cam *cam, const float *c) {
  float zz = cam->ortho ? 1.0f : c[2];
  float fx = cam->focal * c[0], fy = cam->fy * c[1];
  // (Written as "not inside" so NaN counts as outside.)
  int code = c[2] >= cam->znear ? 0 : 1;
  code |= fx + cam->side[0] * zz >= 0 ? 0 : 2;
  code |= cam->side[1] * zz - fx >= 0 ? 0 : 4;
  code |= cam->side[2] * zz - fy >= 0 ? 0 : 8;
  code |= fy + cam->side[3] * zz >= 0 ? 0 : 16;
  return code;
}

// ---- Light and fog ----

// The lights that can reach the box lo..hi.
static int LightsNear(const Prep *P, const float lo[3], const float hi[3], int *which) {
  int n = 0;
  for (int k = 0; k < P->num_lights; k++) {
    const R3dLight *L = &P->lights[k];
    float c[3] = { L->x, L->y, L->z }, d2 = 0;
    for (int i = 0; i < 3; i++) {
      float d = c[i] < lo[i] ? lo[i] - c[i] : c[i] > hi[i] ? c[i] - hi[i] : 0.0f;
      d2 += d * d;
    }
    if (d2 < L->radius * L->radius)
      which[n++] = k;
  }
  return n;
}

static float FogAt(const Prep *P, const float p[3]) {
  float fog = 0;
  if (P->live_fog) {
    // (How far the picture point lies outside the live rectangle.)
    float px = p[0], py = p[1] - p[2];
    float dx = P->live[0] - px > px - P->live[2] ? P->live[0] - px : px - P->live[2];
    float dy = P->live[1] - py > py - P->live[3] ? P->live[1] - py : py - P->live[3];
    dx = dx > 0 ? dx : 0, dy = dy > 0 ? dy : 0;
    // (Nearer than fog0 there is none: no square root needed.)
    float d2 = dx * dx + dy * dy;
    if (d2 > 0 && (P->live_f0 < 0 || d2 > P->live_f0 * P->live_f0))
      fog = SmoothStep(P->live_f0, P->live_inv, sqrtf(d2));
  }
  if (P->dist_fog) {
    const float *e = P->cam.eye;
    float dx = p[0] - e[0], dy = p[1] - e[1], dz = p[2] - e[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (P->dist_f0 < 0 || d2 > P->dist_f0 * P->dist_f0) {
      float f2 = SmoothStep(P->dist_f0, P->dist_inv, sqrtf(d2));
      fog = f2 > fog ? f2 : fog;
    }
  }
  return ClampF(fog, 0.0f, 1.0f);
}

// Light (x 256, times shade), dark (0-256: how much of the subtraction
// applies) and fog (0-256) at a world point, from the lights in which[].
static void ShadeAt(const Prep *P, const int *which, int n, const float p[3], float shade, float min_light,
                    float out[5]) {
  float sum[3] = { 0, 0, 0 }, hole = 0;
  for (int k = 0; k < n; k++) {
    const R3dLight *L = &P->lights[which[k]];
    float dx = p[0] - L->x, dy = p[1] - L->y, dz = p[2] - L->z;
    float d2 = dx * dx + dy * dy + dz * dz, r2 = L->radius * L->radius;
    if (d2 >= r2)
      continue;
    float q = 1.0f - d2 / r2;
    float s = q * sqrtf(q) * L->power;
    sum[0] += s * L->r, sum[1] += s * L->g, sum[2] += s * L->b;
    hole += s;
  }
  for (int i = 0; i < 3; i++) {
    float l = P->ambient[i] + sum[i];
    l = l < P->light_max ? l : P->light_max;
    l = l > min_light ? l : min_light;
    out[i] = ClampF(l * shade, 0.0f, kMaxLight) * 256.0f;
  }
  out[3] = P->sub_lights ? ClampF(1.0f - hole, 0.0f, 1.0f) * 256.0f : 256.0f;
  out[4] = P->any_fog ? FogAt(P, p) * 256.0f : 0.0f;
}

// ---- Setup ----

// Clips the polygon in[0..n) against one plane into out. Returns the new
// count, or -1 if it would not fit. A new point is always computed from the
// inside end of its edge toward the outside one, so two quads that share an
// edge get the same point.
static int ClipPlane(const CVert *in, int n, CVert *out, const float *pl) {
  float d[kClipCap];
  for (int i = 0; i < n; i++)
    d[i] = PlaneDist(pl, in[i].c);
  int m = 0;
  for (int i = 0; i < n; i++) {
    int j = i + 1 == n ? 0 : i + 1;
    bool in_i = d[i] >= 0, in_j = d[j] >= 0;
    if (in_i) {
      if (m >= kClipCap)
        return -1;
      out[m++] = in[i];
    }
    if (in_i != in_j) {
      if (m >= kClipCap)
        return -1;
      const CVert *a = in_i ? &in[i] : &in[j], *b = in_i ? &in[j] : &in[i];
      float da = in_i ? d[i] : d[j], db = in_i ? d[j] : d[i];
      float t = ClampF(da / (da - db), 0.0f, 1.0f);
      for (int k = 0; k < kNumC; k++)
        out[m].c[k] = a->c[k] + t * (b->c[k] - a->c[k]);
      m++;
    }
  }
  return m;
}

R3D_INLINE SVert *PolyVert(R3dWork *w, int q, int k) {
  return k < 4 ? &w->v4[(size_t)q * 4 + k] : &w->v5[(size_t)q * (kMaxPolyVerts - 4) + k - 4];
}

// The previous quad's corners and their shades, within one setup job.
// Neighbors share corners, and a corner's shades depend only on where it
// is, the face shade and the lights that reach it, so reusing them gives
// the same bits as working them out again (whatever the split).
typedef struct CornerCache {
  int n;
  float p[4][3], shade;
  float sh[4][kNumS];
} CornerCache;

static void SetupQuad(R3dWork *w, int qi, const Cam *cam, CornerCache *cache) {
  const Prep *P = &w->P;
  const R3dQuad *q = &w->f->quads[qi];
  Poly *poly = &w->polys[qi];
  poly->n = 0, poly->y0 = poly->y1 = 0;
  bool flat = (q->flags & kR3dQuad_Flat) != 0;
  if (!flat && (q->tex >= kR3dMaxTextures || !P->tex_ok[q->tex]))
    return;
  // To camera space. Dropped when all of it is outside one plane or beyond
  // the far plane (most quads off the picture go here).
  CVert buf[2][kClipCap];
  float wp[4][3];
  int out_and = 0x1f, out_or = 0, beyond = 0;
  for (int k = 0; k < 4; k++) {
    wp[k][0] = q->v[k].x, wp[k][1] = q->v[k].y, wp[k][2] = q->v[k].z;
    float *c = buf[0][k].c;
    ToCamera(cam, wp[k], c);
    int code = OutCode(cam, c);
    out_and &= code, out_or |= code;
    beyond += cam->zfar > 0 && c[2] > cam->zfar;
  }
  if (out_and || beyond == 4)
    return;
  // Absurd coordinates: NaN, the infinities, or far beyond any world.
  float umin = 0, umax = 0, vmin = 0, vmax = 0;
  for (int k = 0; k < 4; k++) {
    const R3dVertex *v = &q->v[k];
    if (!InRange(v->x, kMaxCoord) || !InRange(v->y, kMaxCoord) || !InRange(v->z, kMaxCoord) ||
        (!flat && (!InRange(v->u, kMaxCoord) || !InRange(v->v, kMaxCoord))))
      return;
    umin = k == 0 || v->u < umin ? v->u : umin, umax = k == 0 || v->u > umax ? v->u : umax;
    vmin = k == 0 || v->v < vmin ? v->v : vmin, vmax = k == 0 || v->v > vmax ? v->v : vmax;
  }
  // Back faces. (The diagonals' cross product is twice the area along the
  // normal: the same side as (v1 - v0) x (v2 - v0) for a planar convex quad,
  // and still right for a triangle given with a corner twice.)
  if (!(q->flags & kR3dQuad_TwoSided)) {
    float d1[3], d2[3], nrm[3], view[3];
    for (int i = 0; i < 3; i++) {
      d1[i] = wp[2][i] - wp[0][i], d2[i] = wp[3][i] - wp[1][i];
      view[i] = cam->ortho ? -cam->fwd[i] : cam->eye[i] - wp[0][i];
    }
    nrm[0] = d1[1] * d2[2] - d1[2] * d2[1];
    nrm[1] = d1[2] * d2[0] - d1[0] * d2[2];
    nrm[2] = d1[0] * d2[1] - d1[1] * d2[0];
    if (!(Dot3(nrm, view) > 0))
      return;
  }
  // Light, darkness and fog at the corners (worked out once when no light
  // reaches the quad and there is no fog: the same at every corner then).
  float shade = q->shade == 255 ? 1.0f : (float)q->shade * (1.0f / 255.0f);
  int sub_from = flat ? 0 : w->f->tex[q->tex].subtract_from == 255 ? 256 : w->f->tex[q->tex].subtract_from;
  bool sub = P->any_sub && sub_from < 256;
  int which[kR3dMaxLights], nl = 0;
  if (P->num_lights) {
    float lo[3], hi[3];
    for (int i = 0; i < 3; i++) {
      lo[i] = hi[i] = wp[0][i];
      for (int k = 1; k < 4; k++)
        lo[i] = wp[k][i] < lo[i] ? wp[k][i] : lo[i], hi[i] = wp[k][i] > hi[i] ? wp[k][i] : hi[i];
    }
    nl = LightsNear(P, lo, hi, which);
  }
  bool same = nl == 0 && !P->any_fog;
  float sh[4][kNumS];
  for (int k = 0; k < 4; k++) {
    if (k && same) {
      memcpy(sh[k], sh[0], sizeof(sh[0]));
      continue;
    }
    int hit = -1;
    for (int j = 0; j < cache->n && !same; j++) {
      if (cache->p[j][0] == wp[k][0] && cache->p[j][1] == wp[k][1] && cache->p[j][2] == wp[k][2] &&
          cache->shade == shade)
        hit = j;
    }
    if (hit >= 0)
      memcpy(sh[k], cache->sh[hit], sizeof(sh[0]));
    else
      ShadeAt(P, which, nl, wp[k], shade, 0.0f, sh[k]);
  }
  if (!same) {
    memcpy(cache->p, wp, sizeof(wp));
    memcpy(cache->sh, sh, sizeof(sh));
    cache->shade = shade, cache->n = 4;
  }
  // (Texel coordinates are rebased to start at 1, so truncation is floor.)
  int32 ub = flat ? 0 : FloorI(umin) - 1, vb = flat ? 0 : FloorI(vmin) - 1;
  float ubase = (float)ub, vbase = (float)vb;
  bool plain = true;
  int vary = 0;
  for (int k = 0; k < 4; k++) {
    float *c = buf[0][k].c;
    c[3] = flat ? 0.0f : q->v[k].u - ubase;
    c[4] = flat ? 0.0f : q->v[k].v - vbase;
    for (int i = 0; i < kNumS; i++)
      c[5 + i] = sh[k][i];
    plain &= sh[k][kS_R] == 256.0f && sh[k][kS_G] == 256.0f && sh[k][kS_B] == 256.0f;
    vary |= (sub && sh[k][kS_Dark] != 0.0f ? kVary_Dark : 0) | (sh[k][kS_Fog] != 0.0f ? kVary_Fog : 0);
    if (sh[k][kS_R] != sh[k][kS_G] || sh[k][kS_R] != sh[k][kS_B] || sh[k][kS_R] > 256.0f)
      vary |= kVary_Color;
  }
  // Clip, only against the planes some corner is outside of (the points a
  // clip adds lie between corners, so inside the other planes too).
  int n = 4, cur = 0;
  for (int pl = 0; pl < 5; pl++) {
    if (!(out_or >> pl & 1))
      continue;
    n = ClipPlane(buf[cur], n, buf[cur ^ 1], P->planes[pl]);
    cur ^= 1;
    if (n < 3)
      return;
  }
  if (n > kMaxPolyVerts)
    return;
  // Project.
  float sx[kMaxPolyVerts], sy[kMaxPolyVerts], za[kMaxPolyVerts], ua[kMaxPolyVerts], va[kMaxPolyVerts];
  float xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  float minz = kMinNear * 0.5f;
  for (int k = 0; k < n; k++) {
    const float *c = buf[cur][k].c;
    float z = c[2] >= minz ? c[2] : minz;
    if (cam->ortho) {
      sx[k] = cam->cx + cam->focal * c[0];
      sy[k] = cam->cy - cam->fy * c[1];
      za[k] = z, ua[k] = c[3], va[k] = c[4];
    } else {
      float iz = 1.0f / z;
      sx[k] = cam->cx + cam->focal * c[0] * iz;
      sy[k] = cam->cy - cam->fy * c[1] * iz;
      za[k] = iz, ua[k] = c[3] * iz, va[k] = c[4] * iz;
    }
    // (The clip keeps them 1 px around the picture; this only guards rounding.)
    sx[k] = ClampF(sx[k], -1.0f, (float)P->w + 1.0f);
    sy[k] = ClampF(sy[k], -1.0f, (float)P->h + 1.0f);
    SVert *sv = PolyVert(w, qi, k);
    sv->x = sx[k], sv->y = sy[k];
    for (int i = 0; i < kNumS; i++)
      sv->s[i] = c[5 + i];
    xmin = k == 0 || sx[k] < xmin ? sx[k] : xmin, xmax = k == 0 || sx[k] > xmax ? sx[k] : xmax;
    ymin = k == 0 || sy[k] < ymin ? sy[k] : ymin, ymax = k == 0 || sy[k] > ymax ? sy[k] : ymax;
  }
  int y0 = CeilHalf(ymin), y1 = CeilHalf(ymax), x0 = CeilHalf(xmin), x1 = CeilHalf(xmax);
  y0 = y0 < 0 ? 0 : y0, y1 = y1 > P->h ? P->h : y1;
  x0 = x0 < 0 ? 0 : x0, x1 = x1 > P->w ? P->w : x1;
  if (y0 >= y1 || x0 >= x1)
    return;
  // The gradients, from the widest triangle on the first corner (for a
  // planar quad any of them gives the same plane). No area: nothing to draw.
  int bi = 1, bj = 2;
  float best = 0;
  for (int i = 1; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      float det = (sx[i] - sx[0]) * (sy[j] - sy[0]) - (sx[j] - sx[0]) * (sy[i] - sy[0]);
      if (fabsf(det) > fabsf(best))
        best = det, bi = i, bj = j;
    }
  }
  if (!(fabsf(best) > 1e-6f))
    return;
  float dx1 = sx[bi] - sx[0], dy1 = sy[bi] - sy[0], dx2 = sx[bj] - sx[0], dy2 = sy[bj] - sy[0];
  float inv = 1.0f / best;
  const float *vals[3] = { za, ua, va };
  for (int a = 0; a < 3; a++) {
    const float *f = vals[a];
    float d1 = f[bi] - f[0], d2 = f[bj] - f[0];
    poly->g[a][0] = f[0];
    poly->g[a][1] = (d1 * dy2 - d2 * dy1) * inv;
    poly->g[a][2] = (d2 * dx1 - d1 * dx2) * inv;
  }
  poly->ox = sx[0], poly->oy = sy[0];
  int pass = q->flags & (kR3dQuad_Half | kR3dQuad_Add) ? 2 : q->flags & kR3dQuad_Decal ? 1 : 0;
  poly->y0 = (int16)y0, poly->y1 = (int16)y1;
  poly->x0 = (int16)x0, poly->x1 = (int16)x1;
  poly->n = (uint8)n;
  poly->pass = (uint8)pass;
  poly->vary = (uint8)vary;
  poly->span = cam->ortho || flat || pass || (q->flags & kR3dQuad_Dither) ? kSpan_General :
               plain && !(vary & (kVary_Dark | kVary_Fog)) ? kSpan_Plain : kSpan_Lit;
  if (flat) {
    poly->ou = poly->ov = 0;
    poly->ulo = poly->uhi = poly->vlo = poly->vhi = 0;
  } else {
    const R3dTexture *t = &w->f->tex[q->tex];
    poly->ou = (uint32)ub - (uint32)t->origin_x;
    poly->ov = (uint32)vb - (uint32)t->origin_y;
    // (Just under the far end: a texel coordinate right on it belongs to the next texel.)
    poly->ulo = umin - ubase, poly->vlo = vmin - vbase;
    poly->uhi = (umax - ubase) * kBelowOne, poly->vhi = (vmax - vbase) * kBelowOne;
    poly->uhi = poly->uhi > poly->ulo ? poly->uhi : poly->ulo;
    poly->vhi = poly->vhi > poly->vlo ? poly->vhi : poly->vlo;
  }
}

// The setup job's items are chunks of kSetupChunk quads, dealt round robin
// over kSetupLanes lanes: item j is chunk (j % lanes) * stride + j / lanes.
// So any run of items samples the whole list, and the threads get even
// shares even when the quads that need work (those on the picture) sit
// together in the caller's order.
static int SetupStride(const R3dWork *w) {
  int chunks = (w->num_quads + kSetupChunk - 1) / kSetupChunk;
  return (chunks + kSetupLanes - 1) / kSetupLanes;
}

static void SetupJob(int begin, int end, void *ctx) {
  R3dWork *w = (R3dWork *)ctx;
  // (A copy of the camera that no store through a float pointer can change.)
  Cam cam = w->P.cam;
  CornerCache cache;
  cache.n = 0;
  int stride = SetupStride(w);
  begin = begin < 0 ? 0 : begin, end = end > stride * kSetupLanes ? stride * kSetupLanes : end;
  for (int j = begin; j < end; j++) {
    int q0 = (j % kSetupLanes * stride + j / kSetupLanes) * kSetupChunk;
    int q1 = q0 + kSetupChunk < w->num_quads ? q0 + kSetupChunk : w->num_quads;
    for (int i = q0; i < q1; i++)
      SetupQuad(w, i, &cam, &cache);
  }
}

// ---- Bands ----

// What a span needs about its quad and the frame.
typedef struct Raster {
  const uint8 *texels;
  const uint32 *lut;
  uint32 mw, mh, ou, ov;
  int lw;
  float ulo, uhi, vlo, vhi;
  float d[3];                // 1/z, u/z, v/z (ortho: z, u, v) per px right
  uint32 color;              // flat
  int sub_from;              // texels >= this get the subtraction (256: none)
  int sub[3];
  uint32 fog_rgb;
  uint8 cls, pass;
  bool flat, dither, decal, add, ortho;
} Raster;

R3D_INLINE uint32 ShadePixel(uint32 c, int t, int lr, int lg, int lb, int sr, int sg, int sb, int fog,
                                     int sub_from, uint32 fog_rgb) {
  int r = (int)(c >> 16 & 0xff) * lr >> 8, g = (int)(c >> 8 & 0xff) * lg >> 8, b = (int)(c & 0xff) * lb >> 8;
  r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
  if (t >= sub_from) {
    r -= sr, g -= sg, b -= sb;
    r = r < 0 ? 0 : r, g = g < 0 ? 0 : g, b = b < 0 ? 0 : b;
  }
  if (fog) {
    int k = 256 - fog;
    r = (r * k + (int)(fog_rgb >> 16 & 0xff) * fog) >> 8;
    g = (g * k + (int)(fog_rgb >> 8 & 0xff) * fog) >> 8;
    b = (b * k + (int)(fog_rgb & 0xff) * fog) >> 8;
  }
  return (uint32)r << 16 | (uint32)g << 8 | (uint32)b;
}

R3D_INLINE uint32 BlendHalf(uint32 a, uint32 b) {
  return ((a & 0xfefefe) + (b & 0xfefefe)) >> 1;
}

R3D_INLINE uint32 BlendAdd(uint32 a, uint32 b) {
  uint32 r = (a >> 16 & 0xff) + (b >> 16 & 0xff), g = (a >> 8 & 0xff) + (b >> 8 & 0xff), bb = (a & 0xff) + (b & 0xff);
  return (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bb > 255 ? 255 : bb);
}

// a weighted wa/5 over b.
R3D_INLINE uint32 BlendFifths(uint32 a, uint32 b, uint32 wa) {
  uint32 wb = 5 - wa;
  uint32 r = ((a >> 16 & 0xff) * wa + (b >> 16 & 0xff) * wb) / 5;
  uint32 g = ((a >> 8 & 0xff) * wa + (b >> 8 & 0xff) * wb) / 5;
  uint32 bl = ((a & 0xff) * wa + (b & 0xff) * wb) / 5;
  return r << 16 | g << 8 | bl;
}

// The texel at rebased (u, v), or 0.
R3D_INLINE uint8 Texel(const Raster *R, float u, float v) {
  u = u > R->ulo ? u : R->ulo, u = u < R->uhi ? u : R->uhi;
  v = v > R->vlo ? v : R->vlo, v = v < R->vhi ? v : R->vhi;
  uint32 tx = ((uint32)(int32)u + R->ou) & R->mw;
  uint32 ty = ((uint32)(int32)v + R->ov) & R->mh;
  uint32 i = (ty << R->lw) + tx;
  return R->texels[i];
}

// The common span: perspective, textured, as painted, opaque.
static void SpanPlain(const Raster *R, int xs, int xe, float a0, float a1, float a2, uint32 *prow, float *zrow,
                      uint8 *crow) {
  float d0 = R->d[0], d1 = R->d[1], d2 = R->d[2];
  const uint32 *lut = R->lut;
  uint8 cls = R->cls;
  for (int x = xs; x < xe; x++, a0 += d0, a1 += d1, a2 += d2) {
    if (a0 > zrow[x]) {
      float z = 1.0f / a0;
      uint8 t = Texel(R, a1 * z, a2 * z);
      if (t) {
        zrow[x] = a0;
        prow[x] = lut[t] & 0xffffff;
        crow[x] = cls;
      }
    }
  }
}

// The shades across a span in 16.16: light per channel (x 256; all three
// the same when the light is white), what is subtracted per channel, fog.
typedef struct Shades { int L[3], S[3], F; } Shades;

// c lit, darkened and fogged (l: light x 256 per channel, sb: what is
// subtracted, f: fog 0-256, all 16.16): what ShadePixel gives, with white
// light and the fog done on two channels at a time (r and b, then g).
R3D_INLINE uint32 ShadeWith(uint32 c, int t, int lr, int lg, int lb, int sr, int sg, int sb, int f, bool color,
                            bool dark, bool fog, int sub_from, uint32 fog_rgb) {
  if (color) {
    int r = (int)(c >> 16 & 0xff) * (lr >> 16) >> 8;
    int g = (int)(c >> 8 & 0xff) * (lg >> 16) >> 8;
    int b = (int)(c & 0xff) * (lb >> 16) >> 8;
    // (One test for both ends: a light rounded to just under 0, or brighter than 255.)
    if ((uint32)r > 255)
      r = r < 0 ? 0 : 255;
    if ((uint32)g > 255)
      g = g < 0 ? 0 : 255;
    if ((uint32)b > 255)
      b = b < 0 ? 0 : 255;
    c = (uint32)r << 16 | (uint32)g << 8 | (uint32)b;
  } else {
    // (White and at most as painted: no channel can carry into the next.)
    int l = lr >> 16;
    uint32 lu = l < 0 ? 0 : (uint32)l;
    c = ((c & 0xff00ff) * lu >> 8 & 0xff00ff) | ((c & 0xff00) * lu >> 8 & 0xff00);
  }
  if (dark && t >= sub_from) {
    sr >>= 16, sg >>= 16, sb >>= 16;
    int r = (int)(c >> 16) - (sr < 0 ? 0 : sr), g = (int)(c >> 8 & 0xff) - (sg < 0 ? 0 : sg);
    int b = (int)(c & 0xff) - (sb < 0 ? 0 : sb);
    r = r < 0 ? 0 : r, g = g < 0 ? 0 : g, b = b < 0 ? 0 : b;
    c = (uint32)r << 16 | (uint32)g << 8 | (uint32)b;
  }
  if (fog) {
    f >>= 16;
    uint32 fu = f < 0 ? 0 : f > 256 ? 256 : (uint32)f, k = 256 - fu;
    c = (((c & 0xff00ff) * k + (fog_rgb & 0xff00ff) * fu) >> 8 & 0xff00ff) |
        (((c & 0xff00) * k + (fog_rgb & 0xff00) * fu) >> 8 & 0xff00);
  }
  return c;
}

// Perspective, textured, opaque, lit (made for each mix of colored light,
// darkness and fog; the shades are stepped in scalars so they stay in registers).
R3D_INLINE void SpanLitWith(const Raster *R, int xs, int xe, float a0, float a1, float a2, const Shades *at,
                            const Shades *step, uint32 *prow, float *zrow, uint8 *crow, bool color, bool dark,
                            bool fog) {
  float d0 = R->d[0], d1 = R->d[1], d2 = R->d[2];
  int lr = at->L[0], lg = at->L[1], lb = at->L[2], sr = at->S[0], sg = at->S[1], sb = at->S[2], f = at->F;
  int dlr = step->L[0], dlg = color ? step->L[1] : 0, dlb = color ? step->L[2] : 0;
  int dsr = dark ? step->S[0] : 0, dsg = dark ? step->S[1] : 0, dsb = dark ? step->S[2] : 0, df = fog ? step->F : 0;
  const uint32 *lut = R->lut;
  uint8 cls = R->cls;
  int sub_from = R->sub_from;
  uint32 fog_rgb = R->fog_rgb;
  for (int x = xs; x < xe; x++, a0 += d0, a1 += d1, a2 += d2, lr += dlr, lg += dlg, lb += dlb, sr += dsr, sg += dsg,
       sb += dsb, f += df) {
    if (a0 > zrow[x]) {
      float z = 1.0f / a0;
      uint8 t = Texel(R, a1 * z, a2 * z);
      if (t) {
        zrow[x] = a0;
        prow[x] = ShadeWith(lut[t], t, lr, lg, lb, sr, sg, sb, f, color, dark, fog, sub_from, fog_rgb);
        crow[x] = cls;
      }
    }
  }
}

static void SpanLit(const Raster *R, int vary, int xs, int xe, float a0, float a1, float a2, const Shades *at,
                    const Shades *step, uint32 *prow, float *zrow, uint8 *crow) {
  switch (vary & (kVary_Color | kVary_Dark | kVary_Fog)) {
  case 0: SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, false, false, false); break;
  case kVary_Dark: SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, false, true, false); break;
  case kVary_Fog: SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, false, false, true); break;
  case kVary_Dark | kVary_Fog:
    SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, false, true, true);
    break;
  case kVary_Color: SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, true, false, false); break;
  case kVary_Color | kVary_Dark:
    SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, true, true, false);
    break;
  case kVary_Color | kVary_Fog:
    SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, true, false, true);
    break;
  default: SpanLitWith(R, xs, xe, a0, a1, a2, at, step, prow, zrow, crow, true, true, true); break;
  }
}

// Everything else: ortho, flat, dithered, decals, half and add.
static void SpanGeneral(const Raster *R, int y, int xs, int xe, float a0, float a1, float a2, const Shades *at,
                        const Shades *step, uint32 *prow, float *zrow, uint8 *crow) {
  float d0 = R->d[0], d1 = R->d[1], d2 = R->d[2];
  int lr = at->L[0], lg = at->L[1], lb = at->L[2], sr = at->S[0], sg = at->S[1], sb = at->S[2], f = at->F;
  for (int x = xs; x < xe; x++, a0 += d0, a1 += d1, a2 += d2, lr += step->L[0], lg += step->L[1], lb += step->L[2],
       sr += step->S[0], sg += step->S[1], sb += step->S[2], f += step->F) {
    if (R->dither && ((x + y) & 1))
      continue;
    // (Ortho: a0 is the depth itself, and u, v are affine.)
    if (!(a0 > 0))
      continue;
    float depth, u, v;
    if (R->ortho) {
      depth = 1.0f / a0, u = a1, v = a2;
    } else {
      float z = 1.0f / a0;
      depth = a0, u = a1 * z, v = a2 * z;
    }
    // (A decal is tested 1 px nearer: 1 / (z - 1) is about d + d^2 for d = 1 / z.)
    if (!((R->decal ? depth + depth * depth : depth) > zrow[x]))
      continue;
    uint32 c;
    int t;
    if (R->flat) {
      c = R->color, t = 256;
    } else {
      t = Texel(R, u, v);
      if (!t)
        continue;
      c = R->lut[t];
    }
    c = ShadeWith(c, t, lr, lg, lb, sr, sg, sb, f, true, true, true, R->sub_from, R->fog_rgb);
    if (R->pass == 2) {
      prow[x] = R->add ? BlendAdd(prow[x], c) : BlendHalf(prow[x], c);
    } else {
      if (R->pass == 0)
        zrow[x] = depth;
      prow[x] = c;
    }
    crow[x] = R->cls;
  }
}

typedef struct Edge {
  float y0, y1, x0, dxdy;    // top y, bottom y, x at the top, x per px down
  float s0[kNumS], dsdy[kNumS];  // the shades at the top, per px down
} Edge;

// The shades at a span's first pixel and per pixel, from its two edges
// (only those that vary: green and blue for colored light, darkness, fog).
static void RowShades(const Raster *R, int vary, const Edge *el, const Edge *er, float py, float xl, float xr, int xs,
                      int xe, Shades *at, Shades *step) {
  float tl = py - el->y0, tr = py - er->y0;
  float inv = 1.0f / (xr - xl), off = (float)xs + 0.5f - xl;
  bool steps = xe - xs > 1;
  int need = 1 << kS_R | (vary & kVary_Color ? 1 << kS_G | 1 << kS_B : 0) | (vary & kVary_Dark ? 1 << kS_Dark : 0) |
             (vary & kVary_Fog ? 1 << kS_Fog : 0);
  float s[kNumS] = { 0, 0, 0, 0, 0 }, d[kNumS] = { 0, 0, 0, 0, 0 };
  for (int i = 0; i < kNumS; i++) {
    if (!(need >> i & 1))
      continue;
    // (xs + 0.5 lies in [xl, xr), so off / (xr - xl) is in [0, 1).)
    float al = el->s0[i] + tl * el->dsdy[i], ar = er->s0[i] + tr * er->dsdy[i];
    float di = (ar - al) * inv;
    s[i] = al + off * di, d[i] = steps ? di : 0.0f;
  }
  for (int i = 0; i < 3; i++) {
    if (need >> (kS_R + i) & 1)
      at->L[i] = (int)(s[kS_R + i] * 65536.0f), step->L[i] = (int)(d[kS_R + i] * 65536.0f);
    else
      at->L[i] = at->L[0], step->L[i] = step->L[0];
    if (vary & kVary_Dark)
      at->S[i] = (int)(s[kS_Dark] * (float)R->sub[i] * 256.0f),
      step->S[i] = (int)(d[kS_Dark] * (float)R->sub[i] * 256.0f);
    else
      at->S[i] = step->S[i] = 0;
  }
  at->F = (int)(s[kS_Fog] * 65536.0f), step->F = (int)(d[kS_Fog] * 65536.0f);
}

static void DrawPoly(R3dWork *w, int qi, int by0, int by1, uint8 *scratch) {
  const Prep *P = &w->P;
  const Poly *p = &w->polys[qi];
  int ya = p->y0 > by0 ? p->y0 : by0, yb = p->y1 < by1 ? p->y1 : by1;
  if (ya >= yb)
    return;
  const R3dQuad *q = &w->f->quads[qi];
  int n = p->n, ns = p->span == kSpan_Plain ? 0 : kNumS;
  // The edges that reach this band's rows.
  float ytop = (float)ya + 0.5f, ybot = (float)yb - 0.5f;
  Edge e[kMaxPolyVerts];
  int ne = 0;
  for (int k = 0; k < n; k++) {
    const SVert *a = PolyVert(w, qi, k), *b = PolyVert(w, qi, k + 1 == n ? 0 : k + 1);
    if (a->y == b->y)
      continue;
    if (a->y > b->y) {
      const SVert *t = a;
      a = b, b = t;
    }
    if (b->y <= ytop || a->y > ybot)
      continue;
    Edge *E = &e[ne++];
    float inv = 1.0f / (b->y - a->y);
    E->y0 = a->y, E->y1 = b->y, E->x0 = a->x, E->dxdy = (b->x - a->x) * inv;
    for (int i = 0; i < ns; i++)
      E->s0[i] = a->s[i], E->dsdy[i] = (b->s[i] - a->s[i]) * inv;
  }
  if (ne < 2)
    return;
  Raster R;
  R.flat = (q->flags & kR3dQuad_Flat) != 0;
  if (!R.flat) {
    const R3dTexture *t = &w->f->tex[q->tex];
    R.texels = t->texels, R.lut = t->lut, R.lw = t->log2_w;
    R.mw = (1u << t->log2_w) - 1, R.mh = (1u << t->log2_h) - 1;
    R.sub_from = t->subtract_from == 255 ? 256 : t->subtract_from;
  } else {
    R.texels = NULL, R.lut = NULL, R.lw = 0, R.mw = R.mh = 0, R.sub_from = 0;
  }
  R.ou = p->ou, R.ov = p->ov;
  R.ulo = p->ulo, R.uhi = p->uhi, R.vlo = p->vlo, R.vhi = p->vhi;
  R.d[0] = p->g[0][1], R.d[1] = p->g[1][1], R.d[2] = p->g[2][1];
  R.color = q->color & 0xffffff;
  for (int i = 0; i < 3; i++)
    R.sub[i] = P->sub[i];
  R.fog_rgb = P->fog_rgb;
  R.cls = q->cls;
  R.pass = p->pass;
  R.dither = (q->flags & kR3dQuad_Dither) != 0;
  R.decal = (q->flags & kR3dQuad_Decal) != 0;
  R.add = (q->flags & kR3dQuad_Add) != 0;
  R.ortho = P->cam.ortho;
  int wpx = P->w;
  for (int y = ya; y < yb; y++) {
    float py = (float)y + 0.5f;
    int l = -1, r = -1;
    float xl = 0, xr = 0;
    for (int k = 0; k < ne; k++) {
      if (py >= e[k].y0 && py < e[k].y1) {
        float x = e[k].x0 + (py - e[k].y0) * e[k].dxdy;
        if (l < 0 || x < xl)
          l = k, xl = x;
        if (r < 0 || x > xr)
          r = k, xr = x;
      }
    }
    if (l < 0)
      continue;
    int xs = CeilHalf(xl), xe = CeilHalf(xr);
    xs = xs < 0 ? 0 : xs, xe = xe > wpx ? wpx : xe;
    if (xs >= xe)
      continue;
    // 1/z, u/z and v/z at the first pixel's center.
    float fx = (float)xs + 0.5f - p->ox, fy = py - p->oy;
    float a0 = p->g[0][0] + p->g[0][1] * fx + p->g[0][2] * fy;
    float a1 = p->g[1][0] + p->g[1][1] * fx + p->g[1][2] * fy;
    float a2 = p->g[2][0] + p->g[2][1] * fx + p->g[2][2] * fy;
    uint32 *prow = (uint32 *)(P->pixels + (size_t)y * P->pitch);
    float *zrow = P->depth + (size_t)y * P->depth_pitch;
    uint8 *crow = P->cls_pitch ? P->cls + (size_t)y * P->cls_pitch : scratch;
    if (p->span == kSpan_Plain) {
      SpanPlain(&R, xs, xe, a0, a1, a2, prow, zrow, crow);
      continue;
    }
    // (A general span lights each channel on its own.)
    Shades at, step;
    RowShades(&R, p->span == kSpan_Lit ? p->vary : p->vary | kVary_Color, &e[l], &e[r], py, xl, xr, xs, xe, &at, &step);
    if (p->span == kSpan_Lit)
      SpanLit(&R, p->vary, xs, xe, a0, a1, a2, &at, &step, prow, zrow, crow);
    else
      SpanGeneral(&R, y, xs, xe, a0, a1, a2, &at, &step, prow, zrow, crow);
  }
}

static void DrawCard(R3dWork *w, const PCard *c, int by0, int by1, uint8 *scratch) {
  const Prep *P = &w->P;
  int ya = c->y0 > by0 ? c->y0 : by0, yb = c->y1 < by1 ? c->y1 : by1;
  if (ya >= yb)
    return;
  const R3dCard *src = &w->f->cards[c->index];
  const R3dTexture *tx = &w->f->tex[src->tex];
  uint32 mw = (1u << tx->log2_w) - 1, mh = (1u << tx->log2_h) - 1;
  bool xray = P->xray && (src->flags & kR3dCard_XRay), see = P->see_through && (src->flags & kR3dCard_Link);
  float cw = (float)src->w, ch = (float)src->h;
  uint32 xray_rgb = src->xray_rgb & 0xffffff;
  for (int y = ya; y < yb; y++) {
    float fj = ((float)y + 0.5f - c->top) * c->inv_s;
    int j = fj < 0 ? 0 : fj >= ch ? src->h - 1 : (int)fj;
    uint32 tv = ((uint32)(src->atlas_y + j) - (uint32)tx->origin_y) & mh;
    uint32 row_start = tv << tx->log2_w;
    const uint8 *trow = tx->texels + row_start;
    uint32 *prow = (uint32 *)(P->pixels + (size_t)y * P->pitch);
    float *zrow = P->depth + (size_t)y * P->depth_pitch;
    uint8 *crow = P->cls_pitch ? P->cls + (size_t)y * P->cls_pitch : scratch;
    for (int x = c->x0; x < c->x1; x++) {
      float fi = ((float)x + 0.5f - c->left) * c->inv_s;
      int i = fi < 0 ? 0 : fi >= cw ? src->w - 1 : (int)fi;
      uint8 t = trow[((uint32)(src->atlas_x + i) - (uint32)tx->origin_x) & mw];
      if (!t)
        continue;
      bool front = c->test > zrow[x];
      if (!front && !xray && !see)
        continue;
      uint32 col = tx->lut[t] & 0xffffff;
      if (!c->plain)
        col = ShadePixel(col, t, c->l[0], c->l[1], c->l[2], c->sub[0], c->sub[1], c->sub[2], c->fog, c->sub_from,
                         P->fog_rgb);
      if (front)
        prow[x] = col;
      else if (xray)
        prow[x] = BlendFifths(xray_rgb, prow[x], 2);
      else
        prow[x] = BlendFifths(col, prow[x], 3);
      crow[x] = kR3dClass_Sprite;
    }
  }
}

static void ClearRows(R3dWork *w, int y0, int y1) {
  const Prep *P = &w->P;
  int wpx = P->w;
  for (int y = y0; y < y1; y++) {
    uint32 *prow = (uint32 *)(P->pixels + (size_t)y * P->pitch);
    if (P->bg) {
      const uint32 *src = P->bg + (size_t)y * P->bg_pitch;
      for (int x = 0; x < wpx; x++)
        prow[x] = src[x] & 0xffffff;
    } else {
      uint32 fog_rgb = P->fog_rgb;
      for (int x = 0; x < wpx; x++)
        prow[x] = fog_rgb;
    }
    if (P->depth)
      memset(P->depth + (size_t)y * P->depth_pitch, 0, (size_t)wpx * sizeof(float));
    if (P->cls_pitch)
      memset(P->cls + (size_t)y * P->cls_pitch, kR3dClass_Far, (size_t)wpx);
  }
}

static void BandRows(const R3dWork *w, int b, int *y0, int *y1) {
  *y0 = w->band_y[b], *y1 = w->band_y[b + 1];
}

// Where the bands start: chosen each frame (here, before they run) so that
// they have about the same work, by the widths of the quads and cards on
// each row. The rows near the top often hold far more quads than the rest,
// and the runner gives each thread a run of bands.
static void PlanBands(R3dWork *w) {
  const Prep *P = &w->P;
  // (Each quad and card adds its width from its first row on and takes it
  // off after its last, then a running sum gives each row's cost.)
  int h = P->h, *cost = w->row_cost;
  memset(cost, 0, sizeof(w->row_cost));
  for (int i = 0; i < w->num_quads; i++) {
    const Poly *p = &w->polys[i];
    if (p->n) {
      cost[p->y0] += p->x1 - p->x0 + 16;
      if (p->y1 < h)
        cost[p->y1] -= p->x1 - p->x0 + 16;
    }
  }
  for (int i = 0; i < w->num_cards; i++) {
    const PCard *c = &w->cards[i];
    cost[c->y0] += c->x1 - c->x0;
    if (c->y1 < h)
      cost[c->y1] -= c->x1 - c->x0;
  }
  int64 total = 0, acc = 0;
  for (int y = 0, run = 0; y < h; y++) {
    run += cost[y];
    cost[y] = run + P->w / 4 + 16;  // (and the clear, and a row's own cost)
    total += cost[y];
  }
  int y = 0;
  w->band_y[0] = 0;
  for (int b = 1; b < kR3dBands; b++) {
    int64 goal = total * b / kR3dBands;
    while (y < h && acc + cost[y] / 2 < goal)
      acc += cost[y++];
    w->band_y[b] = y;
  }
  w->band_y[kR3dBands] = h;
}

static void BandJob(int begin, int end, void *ctx) {
  R3dWork *w = (R3dWork *)ctx;
  begin = begin < 0 ? 0 : begin, end = end > kR3dBands ? kR3dBands : end;
  for (int b = begin; b < end; b++) {
    int y0, y1;
    BandRows(w, b, &y0, &y1);
    if (y0 >= y1)
      continue;
    ClearRows(w, y0, y1);
    if (!w->P.depth)
      continue;
    uint8 *scratch = w->scratch_cls[b];
    const int *list = w->bins + (size_t)b * w->max_quads;
    for (int i = 0; i < w->bin_count[b]; i++)
      DrawPoly(w, list[i], y0, y1, scratch);
    for (int i = 0; i < w->num_cards; i++)
      DrawCard(w, &w->cards[w->card_order[i]], y0, y1, scratch);
  }
}

// ---- Binning and cards ----

static int Bin(R3dWork *w) {
  int band_y0[kR3dBands], band_y1[kR3dBands], drawn = 0;
  for (int b = 0; b < kR3dBands; b++)
    BandRows(w, b, &band_y0[b], &band_y1[b]), w->bin_count[b] = 0;
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < w->num_quads; i++) {
      const Poly *p = &w->polys[i];
      if (!p->n || p->pass != pass)
        continue;
      drawn++;
      for (int b = 0; b < kR3dBands; b++) {
        if (p->y0 < band_y1[b] && p->y1 > band_y0[b])
          w->bins[(size_t)b * w->max_quads + w->bin_count[b]++] = i;
      }
    }
  }
  return drawn;
}

// a is drawn before b: farther, or as far with a higher order (lower draws
// on top), or a later card.
static bool CardBefore(const PCard *a, const PCard *b, const R3dCard *ca, const R3dCard *cb) {
  if (a->z != b->z)
    return a->z > b->z;
  if (ca->order != cb->order)
    return ca->order > cb->order;
  return a->index > b->index;
}

static void SortCards(R3dWork *w) {
  int n = w->num_cards, *a = w->card_order, *tmp = w->card_tmp;
  for (int i = 0; i < n; i++)
    a[i] = i;
  // Bottom-up merge sort (the order is total, so the result is unique).
  for (int width = 1; width < n; width *= 2) {
    for (int lo = 0; lo < n; lo += 2 * width) {
      int mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
      int i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        const PCard *pi = &w->cards[a[i]], *pj = &w->cards[a[j]];
        bool take_j = CardBefore(pj, pi, &w->f->cards[pj->index], &w->f->cards[pi->index]);
        tmp[k++] = take_j ? a[j++] : a[i++];
      }
      while (i < mid)
        tmp[k++] = a[i++];
      while (j < hi)
        tmp[k++] = a[j++];
    }
    memcpy(a, tmp, (size_t)n * sizeof(int));
  }
}

static int PrepCards(R3dWork *w) {
  const Prep *P = &w->P;
  const R3dFrame *f = w->f;
  const Cam *cam = &P->cam;
  int n = f->cards && P->depth ? f->num_cards : 0;
  n = n < 0 ? 0 : n > w->max_cards ? w->max_cards : n;
  w->num_cards = 0;
  for (int i = 0; i < n; i++) {
    const R3dCard *src = &f->cards[i];
    if (src->tex >= kR3dMaxTextures || !P->tex_ok[src->tex] || src->w <= 0 || src->h <= 0)
      continue;
    if (!InRange(src->foot[0], kMaxCoord) || !InRange(src->foot[1], kMaxCoord) || !InRange(src->foot[2], kMaxCoord))
      continue;
    float c[3];
    ToCamera(cam, src->foot, c);
    if (!(c[2] >= cam->znear) || (cam->zfar > 0 && c[2] > cam->zfar))
      continue;
    // (A front card has its own scale, passes every depth test and sorts last.)
    bool front = (src->flags & kR3dCard_Front) && src->scale > 0.0f;
    float s = front ? src->scale : cam->ortho ? cam->focal : cam->focal / c[2];
    float fx = cam->ortho ? cam->cx + cam->focal * c[0] : cam->cx + cam->focal / c[2] * c[0];
    float fy = cam->ortho ? cam->cy - cam->fy * c[1] : cam->cy - cam->fy * c[1] / c[2];
    if (!InRange(fx, 1e7f) || !InRange(fy, 1e7f) || !(s > 1e-4f && s < 1e4f))
      continue;
    PCard *pc = &w->cards[w->num_cards];
    pc->index = i;
    pc->z = front ? 0.0f : c[2];
    float zt = c[2] - 2.0f;
    zt = zt > kMinNear * 0.5f ? zt : kMinNear * 0.5f;
    pc->test = front ? 1e30f : 1.0f / zt;
    pc->left = fx - (float)src->foot_x * s, pc->top = fy - (float)src->foot_y * s;
    pc->inv_s = 1.0f / s;
    float right = pc->left + (float)src->w * s, bottom = pc->top + (float)src->h * s;
    float wf = (float)P->w + 2.0f, hf = (float)P->h + 2.0f;
    pc->x0 = CeilHalf(ClampF(pc->left, -2.0f, wf)), pc->x1 = CeilHalf(ClampF(right, -2.0f, wf));
    pc->y0 = CeilHalf(ClampF(pc->top, -2.0f, hf)), pc->y1 = CeilHalf(ClampF(bottom, -2.0f, hf));
    pc->x0 = pc->x0 < 0 ? 0 : pc->x0, pc->x1 = pc->x1 > P->w ? P->w : pc->x1;
    pc->y0 = pc->y0 < 0 ? 0 : pc->y0, pc->y1 = pc->y1 > P->h ? P->h : pc->y1;
    if (pc->x0 >= pc->x1 || pc->y0 >= pc->y1)
      continue;
    // Light, subtraction and fog at the foot.
    int which[kR3dMaxLights];
    int nl = LightsNear(P, src->foot, src->foot, which);
    float sh[5];
    ShadeAt(P, which, nl, src->foot, 1.0f, P->card_min, sh);
    uint8 sub_from = f->tex[src->tex].subtract_from;
    bool sub = P->any_sub && P->sub_cards && sub_from != 255;
    for (int k = 0; k < 3; k++) {
      pc->l[k] = (int)sh[k];
      pc->sub[k] = sub ? (int)((float)P->sub[k] * sh[3] * (1.0f / 256.0f)) : 0;
    }
    pc->fog = (int)sh[4];
    pc->sub_from = sub ? sub_from : 256;
    pc->plain = pc->l[0] == 256 && pc->l[1] == 256 && pc->l[2] == 256 && pc->fog == 0 &&
                (pc->sub[0] | pc->sub[1] | pc->sub[2]) == 0;
    w->num_cards++;
  }
  SortCards(w);
  return w->num_cards;
}

// ---- The frame ----

static void PrepFrame(R3dWork *w, const R3dFrame *f, const R3dTarget *t, int wpx, int hpx) {
  Prep *P = &w->P;
  memset(P, 0, sizeof(*P));
  P->w = wpx, P->h = hpx;
  PrepCamera(&f->cam, &P->cam);
  MakePlanes(&P->cam, wpx, hpx, 1.0f, P->planes);
  P->cam.side[0] = P->cam.cx + 1.0f, P->cam.side[1] = (float)wpx + 1.0f - P->cam.cx;
  P->cam.side[2] = P->cam.cy + 1.0f, P->cam.side[3] = (float)hpx + 1.0f - P->cam.cy;
  for (int i = 0; i < kR3dMaxTextures; i++) {
    const R3dTexture *tx = &f->tex[i];
    P->tex_ok[i] = tx->texels && tx->lut && tx->log2_w <= 15 && tx->log2_h <= 15;
  }
  for (int i = 0; i < 3; i++) {
    P->ambient[i] = ClampF(f->ambient[i], 0.0f, kMaxLight);
    P->sub[i] = (int)ClampF(f->subtract[i], 0.0f, 255.0f);
    P->any_sub |= P->sub[i] != 0;
  }
  // (A cap left at 0 means none.)
  P->light_max = f->light_max > 0.0f ? ClampF(f->light_max, 0.0f, kMaxLight) : kMaxLight;
  P->card_min = ClampF(f->card_min_light, 0.0f, kMaxLight);
  int nl = f->lights ? f->num_lights : 0;
  nl = nl < 0 ? 0 : nl > kR3dMaxLights ? kR3dMaxLights : nl;
  for (int i = 0; i < nl; i++) {
    const R3dLight *L = &f->lights[i];
    if (!InRange(L->x, 1e6f) || !InRange(L->y, 1e6f) || !InRange(L->z, 1e6f) ||
        !(L->radius > 0.0f && L->radius < 1e5f) ||
        !InRange(L->power, 100.0f) || !InRange(L->r, 100.0f) || !InRange(L->g, 100.0f) || !InRange(L->b, 100.0f))
      continue;
    P->lights[P->num_lights++] = *L;
  }
  P->sub_lights = f->subtract_lights, P->sub_cards = f->subtract_cards;
  P->live_fog = InRange(f->live_x0, 1e6f) && InRange(f->live_y0, 1e6f) && InRange(f->live_x1, 1e6f) &&
                InRange(f->live_y1, 1e6f) && InRange(f->live_fog0, 1e6f) && InRange(f->live_fog1, 1e6f) &&
                f->live_fog1 > f->live_fog0;
  P->live[0] = f->live_x0, P->live[1] = f->live_y0, P->live[2] = f->live_x1, P->live[3] = f->live_y1;
  P->live_f0 = f->live_fog0;
  P->live_inv = P->live_fog ? 1.0f / (f->live_fog1 - f->live_fog0) : 0.0f;
  P->dist_fog = InRange(f->dist_fog0, 1e7f) && InRange(f->dist_fog1, 1e7f) && f->dist_fog1 > 0.0f &&
                f->dist_fog1 > f->dist_fog0;
  P->dist_f0 = f->dist_fog0;
  P->dist_inv = P->dist_fog ? 1.0f / (f->dist_fog1 - f->dist_fog0) : 0.0f;
  P->any_fog = P->live_fog || P->dist_fog;
  P->fog_rgb = f->fog_rgb & 0xffffff;
  P->bg = f->background && f->background_pitch >= wpx ? f->background : NULL;
  P->bg_pitch = f->background_pitch;
  P->see_through = f->see_through, P->xray = f->xray;
  P->pixels = t->pixels;
  P->pitch = t->pitch;
  P->depth = t->depth;
  P->depth_pitch = f->w;
  P->cls = t->cls;
  P->cls_pitch = t->cls && t->cls_pitch >= wpx ? t->cls_pitch : 0;
}

static void Run(R3dRunner *run, int count, R3dJob *job, void *ctx) {
  if (run)
    run(count, job, ctx);
  else
    job(0, count, ctx);
}

R3dWork *R3d_NewWork(int max_quads, int max_cards) {
  max_quads = max_quads < 0 ? 0 : max_quads > kMaxWorkQuads ? kMaxWorkQuads : max_quads;
  max_cards = max_cards < 0 ? 0 : max_cards > kMaxWorkCards ? kMaxWorkCards : max_cards;
  R3dWork *w = (R3dWork *)calloc(1, sizeof(R3dWork));
  if (!w)
    return NULL;
  size_t nq = max_quads ? (size_t)max_quads : 1, nc = max_cards ? (size_t)max_cards : 1;
  w->max_quads = max_quads, w->max_cards = max_cards;
  w->polys = (Poly *)malloc(nq * sizeof(Poly));
  w->v4 = (SVert *)malloc(nq * 4 * sizeof(SVert));
  w->v5 = (SVert *)malloc(nq * (kMaxPolyVerts - 4) * sizeof(SVert));
  w->bins = (int *)malloc(nq * kR3dBands * sizeof(int));
  w->cards = (PCard *)malloc(nc * sizeof(PCard));
  w->card_order = (int *)malloc(nc * sizeof(int));
  w->card_tmp = (int *)malloc(nc * sizeof(int));
  if (!w->polys || !w->v4 || !w->v5 || !w->bins || !w->cards || !w->card_order || !w->card_tmp) {
    R3d_FreeWork(w);
    return NULL;
  }
  return w;
}

void R3d_FreeWork(R3dWork *work) {
  if (!work)
    return;
  free(work->polys);
  free(work->v4);
  free(work->v5);
  free(work->bins);
  free(work->cards);
  free(work->card_order);
  free(work->card_tmp);
  free(work);
}

void R3d_Draw(R3dWork *work, const R3dFrame *f, const R3dTarget *t, R3dRunner *run, R3dStats *stats) {
  if (stats)
    stats->quads_drawn = stats->cards_drawn = 0;
  // (Rows are written as 32-bit pixels, so the picture must be aligned for them.)
  if (!work || !f || !t || !t->pixels || ((uintptr_t)t->pixels & 3) || (t->pitch & 3))
    return;
  int wpx = f->w < kR3dMaxW ? f->w : kR3dMaxW, hpx = f->h < kR3dMaxH ? f->h : kR3dMaxH;
  if (wpx <= 0 || hpx <= 0)
    return;
  work->f = f;
  PrepFrame(work, f, t, wpx, hpx);
  bool draw = work->P.cam.ok && work->P.depth;
  int nq = draw && f->quads ? f->num_quads : 0;
  work->num_quads = nq < 0 ? 0 : nq > work->max_quads ? work->max_quads : nq;
  if (work->num_quads)
    Run(run, SetupStride(work) * kSetupLanes, SetupJob, work);
  int cards = draw ? PrepCards(work) : (work->num_cards = 0);
  PlanBands(work);
  int quads = Bin(work);
  Run(run, kR3dBands, BandJob, work);
  if (stats)
    stats->quads_drawn = quads, stats->cards_drawn = cards;
}

// ---- Projection helpers ----

bool R3d_Project(const R3dCamera *cam, const float p[3], float *sx, float *sy, float *depth) {
  Cam c;
  PrepCamera(cam, &c);
  float v[3];
  ToCamera(&c, p, v);
  if (!c.ok || !InRange(v[0], 1e7f) || !InRange(v[1], 1e7f) || !InRange(v[2], 1e7f)) {
    *sx = c.ok ? c.cx : 0.0f, *sy = c.ok ? c.cy : 0.0f, *depth = 0;
    return false;
  }
  *depth = v[2];
  if (c.ortho) {
    *sx = c.cx + c.focal * v[0], *sy = c.cy - c.fy * v[1];
  } else {
    // (Behind the eye this divides by -depth: the plain formula's point
    // mirrored through cx, cy, so it points the way the point is.)
    float z = fabsf(v[2]);
    z = z > 0.01f ? z : 0.01f;
    *sx = c.cx + c.focal * v[0] / z, *sy = c.cy - c.fy * v[1] / z;
  }
  *sx = ClampF(*sx, -1e6f, 1e6f), *sy = ClampF(*sy, -1e6f, 1e6f);
  return v[2] >= c.znear;
}

float R3d_ScaleAt(const R3dCamera *cam, float depth) {
  if (cam->ortho)
    return cam->focal;
  float d = depth > 0.01f ? depth : 0.01f;
  return ClampF(cam->focal / d, 0.0f, 1e6f);
}

bool R3d_BoxVisible(const R3dCamera *cam, int w, int h, const float lo[3], const float hi[3]) {
  Cam c;
  PrepCamera(cam, &c);
  if (!c.ok)
    return true;
  float planes[5][4];
  MakePlanes(&c, w, h, 0.0f, planes);
  int out_and = 0x1f, beyond = 0;
  for (int k = 0; k < 8; k++) {
    float p[3] = { k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2] }, v[3];
    ToCamera(&c, p, v);
    if (!InRange(v[0], 1e7f) || !InRange(v[1], 1e7f) || !InRange(v[2], 1e7f))
      return true;
    int code = 0;
    for (int pl = 0; pl < 5; pl++)
      code |= (PlaneDist(planes[pl], v) < 0) << pl;
    out_and &= code;
    beyond += c.zfar > 0 && v[2] > c.zfar;
  }
  return !out_and && beyond < 8;
}
