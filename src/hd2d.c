// (SDL first: on Windows its headers clash with variables.h's short names.)
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "hd2d.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "mp.h"
#include "snes/ppu.h"

Hd2dSettings g_hd2d;  // from the config (config.c has the defaults)

enum {
  kMaxW = kPpuXPixels,
  kMaxH = 240,
  kMaxLights = 48,
  kMaxViews = 4,
};

// Layer of each pixel and the Link pixels hidden behind background tiles
// (from the PPU). The blurred copies are made at lower resolution (they are
// smooth anyway) and read back with bilinear filtering: the out-of-focus
// picture at half, the bloom at a quarter. One channel at a time, 16-bit.
enum {
  kHalfW = kMaxW / 2 + 1, kHalfH = kMaxH / 2 + 1,
  kQuarterW = kMaxW / 4 + 1, kQuarterH = kMaxH / 4 + 1,
};
static uint8 g_layer[kMaxW * kMaxH];
static uint32 g_hidden[kMaxW * kMaxH];
static uint16 g_src[3][kMaxW * kMaxH];
static uint16 g_soft[3][kHalfW * kHalfH];      // the out-of-focus picture
static uint16 g_glow_half[3][kHalfW * kHalfH];
static uint16 g_glow[3][kQuarterW * kQuarterH];  // the bright parts, much blurred: the bloom
static uint16 g_tmp[3][kMaxW * kMaxH];  // blur scratch, one per channel (they run in parallel)
static uint32 g_colsum[3][kMaxW];
static float g_light[3][kMaxW * kMaxH];   // light reaching each pixel (lighting only)
static uint8 g_vignette[kMaxW * kMaxH];    // 255 = unchanged
static int g_vignette_key = -1;

typedef struct Light {
  float x, y, radius, power;
  float r, g, b;
} Light;

// What the pass needs to know about the world being drawn.
typedef struct Scene {
  int w, h;
  int num_players;
  float player_y[kMpMaxPlayers];
  bool dark;             // a dark dungeon room: the pass does the darkness itself
  float ambient;         // the light everywhere in a dark room
  bool far_bg2;          // BG2 is a far layer (a parallax room's lower floor)
  bool reveal;           // show hidden Links (only in normal play: scripted steps
                         // like walking out of a door hide Link on purpose)
  int num_lights;
  Light lights[kMaxLights];
} Scene;

static Scene g_scene;
static float g_focus_y[kMaxViews] = { -1, -1, -1, -1 };

// Box blur of radius r, horizontally then vertically (run twice it is close
// to a gaussian). Both passes go along rows, with running sums.
static void BoxBlur(uint16 *img, int w, int h, int r, int ch) {
  uint16 *tmp = g_tmp[ch];
  uint32 *colsum = g_colsum[ch];
  uint32 inv = 65536 / (2 * r + 1);
  for (int y = 0; y < h; y++) {
    uint16 *row = img + y * w, *out = tmp + y * w;
    uint32 sum = row[0] * (r + 1);
    for (int i = 1; i <= r; i++)
      sum += row[i < w ? i : w - 1];
    for (int x = 0; x < w; x++) {
      out[x] = (sum * inv + 32768) >> 16;
      int add = x + r + 1, sub = x - r;
      sum += row[add < w ? add : w - 1] - row[sub > 0 ? sub : 0];
    }
  }
  for (int x = 0; x < w; x++)
    colsum[x] = tmp[x] * (r + 1);
  for (int i = 1; i <= r; i++) {
    const uint16 *row = tmp + (i < h ? i : h - 1) * w;
    for (int x = 0; x < w; x++)
      colsum[x] += row[x];
  }
  for (int y = 0; y < h; y++) {
    uint16 *out = img + y * w;
    const uint16 *add = tmp + (y + r + 1 < h ? y + r + 1 : h - 1) * w;
    const uint16 *sub = tmp + (y - r > 0 ? y - r : 0) * w;
    for (int x = 0; x < w; x++) {
      out[x] = (colsum[x] * inv + 32768) >> 16;
      colsum[x] += add[x] - sub[x];
    }
  }
}

// Halves an image (2x2 averages; the last row/column repeats if odd).
static void Downsample(const uint16 *src, int w, int h, uint16 *dst, int dw, int dh) {
  for (int y = 0; y < dh; y++) {
    const uint16 *r0 = src + (2 * y) * w, *r1 = src + (2 * y + 1 < h ? 2 * y + 1 : h - 1) * w;
    for (int x = 0; x < dw; x++) {
      int x0 = 2 * x, x1 = 2 * x + 1 < w ? 2 * x + 1 : w - 1;
      dst[y * dw + x] = (r0[x0] + r0[x1] + r1[x0] + r1[x1] + 2) >> 2;
    }
  }
}

// Bilinear sampling of an image 2^shift times smaller: where each output
// column/row reads from, and how much of the second sample it takes (0-256).
typedef struct Taps { uint16 i0, i1, f; } Taps;
static Taps g_taps_x[2][kMaxW], g_taps_y[2][kMaxH];

static void MakeTaps(Taps *t, int n, int shift, int small_n) {
  int scale = 1 << shift;
  for (int i = 0; i < n; i++) {
    int pos = (2 * i + 1) * 128 / scale - 128;  // in 1/256 of a small pixel
    if (pos < 0)
      pos = 0;
    int i0 = pos >> 8, f = pos & 255;
    if (i0 >= small_n - 1)
      i0 = small_n - 1, f = 0;
    t[i].i0 = i0, t[i].i1 = i0 + 1 < small_n ? i0 + 1 : i0, t[i].f = f;
  }
}

// Bilinear upsampling, done in two steps: each row of the small image is
// first widened to the full width (all rows, once), then every output row
// blends the two widened rows around it.
static uint16 g_wide_soft[3][kHalfH * kMaxW], g_wide_glow[3][kQuarterH * kMaxW];

static void WidenRows(const uint16 *src, int sw, int sh, const Taps *tx, int w, uint16 *dst) {
  for (int y = 0; y < sh; y++) {
    const uint16 *row = src + y * sw;
    uint16 *out = dst + y * w;
    for (int x = 0; x < w; x++)
      out[x] = ((row[tx[x].i0] << 8) + (row[tx[x].i1] - row[tx[x].i0]) * tx[x].f) >> 8;
  }
}

// A few worker threads: each stage of the pass is split by rows or by color
// channel. (Display only: nothing here touches the game.)
enum { kMaxWorkers = 3 };
typedef void JobFunc(int begin, int end);
static struct {
  int workers;  // -1: not started
  SDL_Thread *thread[kMaxWorkers];
  SDL_sem *start[kMaxWorkers], *done;
  JobFunc *func;
  int count;
} g_pool = { -1 };

static int PoolWorker(void *arg) {
  int id = (int)(intptr_t)arg;
  for (;;) {
    SDL_SemWait(g_pool.start[id]);
    int parts = g_pool.workers + 1;
    g_pool.func(g_pool.count * (id + 1) / parts, g_pool.count * (id + 2) / parts);
    SDL_SemPost(g_pool.done);
  }
  return 0;
}

static void ParallelFor(int count, JobFunc *func) {
  if (g_pool.workers < 0) {
    g_pool.workers = 0;
    int want = SDL_GetCPUCount() - 1;
    want = want > kMaxWorkers ? kMaxWorkers : want;
    if (want > 0 && (g_pool.done = SDL_CreateSemaphore(0)) != NULL) {
      for (int i = 0; i < want; i++) {
        g_pool.start[i] = SDL_CreateSemaphore(0);
        g_pool.thread[i] = g_pool.start[i] ? SDL_CreateThread(PoolWorker, "hd2d", (void *)(intptr_t)i) : NULL;
        if (!g_pool.thread[i])
          break;
        g_pool.workers = i + 1;
      }
    }
  }
  int workers = count < 2 ? 0 : g_pool.workers < count - 1 ? g_pool.workers : count - 1;
  if (workers == 0) {
    func(0, count);
    return;
  }
  // (The workers split the work as if all of them ran: the ones not needed
  // get an empty range.)
  g_pool.func = func, g_pool.count = count;
  for (int i = 0; i < g_pool.workers; i++)
    SDL_SemPost(g_pool.start[i]);
  func(0, count / (g_pool.workers + 1));
  for (int i = 0; i < g_pool.workers; i++)
    SDL_SemWait(g_pool.done);
}

static float SmoothStep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

static FORCEINLINE int Clamp255(int v) {
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

// The vignette for every pixel (made again when the size or strength changes).
static void MakeVignette(int w, int h) {
  int key = w << 20 | h << 8 | g_hd2d.vignette;
  if (key == g_vignette_key)
    return;
  g_vignette_key = key;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / h - 0.5f;
      float d = (dx * dx + dy * dy) * 2;  // 0 in the middle, 1 in the corners
      g_vignette[y * w + x] = (uint8)(255 * (1 - g_hd2d.vignette / 100.0f * 0.45f * d * d) + 0.5f);
    }
  }
}

// Color grade (contrast, saturation, warmth) and vignette, in fixed point.
typedef struct Grade { int sat, con, warm_r, warm_b; } Grade;

static Grade MakeGrade() {
  int g = g_hd2d.grade;  // 0-100
  Grade gr = { 256 + 64 * g / 100, 256 + 38 * g / 100, 256 + 15 * g / 100, 256 - 15 * g / 100 };
  return gr;
}

static FORCEINLINE uint32 GradeAndPack(const Grade *gr, int r, int g, int b, int vig) {
  int l = (77 * r + 150 * g + 29 * b) >> 8;
  r = (((((l << 8) + (r - l) * gr->sat) >> 8) - 128) * gr->con >> 8) + 128;
  g = (((((l << 8) + (g - l) * gr->sat) >> 8) - 128) * gr->con >> 8) + 128;
  b = (((((l << 8) + (b - l) * gr->sat) >> 8) - 128) * gr->con >> 8) + 128;
  vig += vig >> 7;  // 0-255 -> 0-256
  r = r * gr->warm_r * vig >> 16, g = g * vig >> 8, b = b * gr->warm_b * vig >> 16;
  return Clamp255(r) << 16 | Clamp255(g) << 8 | Clamp255(b);
}

static void AddLight(Scene *sc, float x, float y, float radius, float power, float r, float g, float b) {
  if (sc->num_lights >= kMaxLights || x < -radius || y < -radius || x > sc->w + radius || y > sc->h + radius)
    return;
  Light *l = &sc->lights[sc->num_lights++];
  l->x = x, l->y = y, l->radius = radius, l->power = power, l->r = r, l->g = g, l->b = b;
}

static bool IsFireAncilla(uint8 type) {
  switch (type) {
  case 0x02:  // fire rod shot
  case 0x2f:  // lamp flame
  case 0x32:  // blast wall fireball
  case 0x33:  // blast wall explosion
  case 0x34:  // skull woods fire
  case 0x3a:  // big bomb explosion
    return true;
  }
  return false;
}

// Reads where the players and the light sources are in the world being drawn.
static void GatherScene(Scene *sc, Ppu *ppu, int w, int h) {
  memset(sc, 0, sizeof(*sc));
  sc->w = w, sc->h = h;
  sc->reveal = g_hd2d.see_through && (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
  int cam_x = BG2HOFS_copy2 - ppu->extraLeftRight, cam_y = BG2VOFS_copy2;
  // A dark room: the game darkens it by subtracting a layer of darkness
  // (BG1 on the subscreen), with a hole for the Lamp; without the Lamp, or
  // once a torch burns, by subtracting the fixed color alone (nothing on the
  // subscreen). The pass lights the room itself instead.
  sc->dark = player_is_indoors && g_hd2d.lighting && (dung_want_lights_out | dung_want_lights_out_copy) &&
             ppu->addSubscreen && ppu->subtractColor && ((ppu->screenEnabled[1] & 1) || !ppu->screenEnabled[1]);
  if (sc->dark) {
    float lit = (31 - ppu->fixedColorR) / 31.0f;  // 0 with no torch lit, 1 with all
    // (Never pitch black: without the Lamp the room stays a dim room.)
    sc->ambient = 0.12f + 0.88f * lit;
  }
  bool have_lamp = link_item_torch != 0;
  float flicker = 0.93f + 0.07f * sinf(frame_counter * 0.37f);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Mp_IsAlive(p))  // (the players in this world: alone it need not be P1)
      continue;
    float x = (int16)(Mp_PlayerX(p) - cam_x) + 8.0f, y = (int16)(Mp_PlayerY(p) - cam_y) + 14.0f;
    if (x < -64 || x > w + 64 || y < -64 || y > h + 64)
      continue;
    sc->player_y[sc->num_players++] = y;
    // The Lamp lights a circle around the Link, a little ahead of it: in a
    // dark room whenever the team has it, elsewhere while it is this
    // Link's selected item (11).
    if (have_lamp && (sc->dark || *Mp_PlayerRam(p, 0x202) == 11)) {
      static const int8 kAhead[4][2] = { {0, -12}, {0, 12}, {-12, 0}, {12, 0} };
      int dir = (*Mp_PlayerRam(p, 0x2F) >> 1) & 3;
      AddLight(sc, x + kAhead[dir][0], y + kAhead[dir][1], 92, 1.3f * flicker, 1.0f, 0.9f, 0.7f);
    }
  }
  for (int k = 0; k < 10; k++) {
    if (IsFireAncilla(ancilla_type[k])) {
      float x = (uint16)((ancilla_x_lo[k] | ancilla_x_hi[k] << 8) - cam_x) + 8.0f;
      float y = (int16)((ancilla_y_lo[k] | ancilla_y_hi[k] << 8) - cam_y) + 8.0f;
      AddLight(sc, x, y, 56, 0.9f * flicker, 1.0f, 0.7f, 0.4f);
    }
  }
  if (player_is_indoors) {
    // Lit torches: the room's entry in the torch list (room, torches..., 0xffff)
    // says how many there are.
    int count = 0;
    for (int i = 0; i < 144;) {
      int j = i + 1;
      while (j < 144 && dung_torch_data[j] != 0xffff)
        j++;
      if (dung_torch_data[i] == dungeon_room_index) {
        count = j - i - 1;
        break;
      }
      i = j + 1;
    }
    int start = dung_index_of_torches_start >> 1;
    int base_x = (dungeon_room_index & 0xf) << 9, base_y = (dungeon_room_index & 0xf0) << 5;
    for (int j = 0; j < count && start + j < 0x20; j++) {
      uint16 pos = dung_object_tilemap_pos[start + j];
      if (!(pos & 0x8000))
        continue;
      int tile = (pos & 0x3fff) >> 1;
      float x = (uint16)(base_x + (tile & 63) * 8 + 8 - cam_x);
      float y = (int16)(base_y + (tile >> 6) * 8 + 8 - cam_y);
      AddLight(sc, x, y, 96, 0.95f * flicker, 1.0f, 0.78f, 0.5f);
    }
    sc->far_bg2 = dung_hdr_bg2_properties == 1 && (ppu->screenEnabled[0] & 3) == 3;
  }
}

// How much light each pixel gets: 1 is unchanged. In a dark room it's the
// ambient light plus the lights; elsewhere the lights only add a warm glow.
static void ComputeLight(const Scene *sc, int w, int h) {
  float strength = g_hd2d.lighting / 100.0f;
  float base = sc->dark ? sc->ambient : 1.0f;
  float add = sc->dark ? 1.0f : 0.45f * strength;
  for (int i = 0; i < w * h; i++)
    g_light[0][i] = g_light[1][i] = g_light[2][i] = base;
  for (int n = 0; n < sc->num_lights; n++) {
    const Light *l = &sc->lights[n];
    int x0 = (int)(l->x - l->radius), x1 = (int)(l->x + l->radius);
    int y0 = (int)(l->y - l->radius), y1 = (int)(l->y + l->radius);
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > w - 1 ? w - 1 : x1, y1 = y1 > h - 1 ? h - 1 : y1;
    float inv = 1.0f / (l->radius * l->radius);
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        float dx = x - l->x, dy = y - l->y;
        float f = 1 - (dx * dx + dy * dy) * inv;
        if (f <= 0)
          continue;
        f = f * sqrtf(f) * l->power * add;
        int i = y * w + x;
        g_light[0][i] += f * l->r, g_light[1][i] += f * l->g, g_light[2][i] += f * l->b;
      }
    }
  }
}

// Where the in-focus band is: around the players of this view, eased so it
// doesn't jump.
static void FocusBand(const Scene *sc, int view, int h, float *top, float *bottom) {
  float lo = h * 0.55f, hi = lo;
  if (sc->num_players) {
    lo = hi = sc->player_y[0];
    for (int i = 1; i < sc->num_players; i++) {
      lo = sc->player_y[i] < lo ? sc->player_y[i] : lo;
      hi = sc->player_y[i] > hi ? sc->player_y[i] : hi;
    }
    lo = lo < 0 ? 0 : lo > h ? h : lo;
    hi = hi < 0 ? 0 : hi > h ? h : hi;
  }
  float mid = (lo + hi) * 0.5f;
  float *f = &g_focus_y[view & (kMaxViews - 1)];
  *f = *f < 0 ? mid : *f + (mid - *f) * 0.12f;
  float half = (hi - lo) * 0.5f;
  *top = *f - half, *bottom = *f + half;
}

// What the stages of Apply share.
static struct {
  uint8 *pixels;
  size_t pitch;
  int w, h, hw, hh, qw, qh;
  bool any_glow, lighting;
  uint8 row_glow[kHalfH];
  float top, bottom, dof;
  int glow_k, haze_k, haze[3];
  Grade grade;
  float m7_k;  // the world map's vignette strength
} A;

// The frame, split into channels, with hidden Links showing through.
static void StageSplit(int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    const uint32 *row = (const uint32 *)(A.pixels + y * A.pitch);
    const uint32 *hid = g_hidden + y * kMaxW;
    for (int x = 0; x < A.w; x++) {
      uint32 c = row[x];
      int i = y * A.w + x;
      int r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;
      if (hid[x] && g_scene.reveal) {
        r = (r * 2 + (hid[x] >> 16 & 0xff) * 3) / 5;
        g = (g * 2 + (hid[x] >> 8 & 0xff) * 3) / 5;
        b = (b * 2 + (hid[x] & 0xff) * 3) / 5;
      }
      g_src[0][i] = r, g_src[1][i] = g, g_src[2][i] = b;
    }
  }
}

// The out-of-focus picture, at half resolution.
static void StageSoft(int c0, int c1) {
  for (int ch = c0; ch < c1; ch++) {
    Downsample(g_src[ch], A.w, A.h, g_soft[ch], A.hw, A.hh);
    BoxBlur(g_soft[ch], A.hw, A.hh, 1, ch);
    BoxBlur(g_soft[ch], A.hw, A.hh, 1, ch);
  }
}

// Bloom: only the brightest parts (not the HUD), at half resolution first.
static void StageGlowThreshold(int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    int any = 0;
    for (int x = 0; x < A.hw; x++) {
      int sum[3] = { 0, 0, 0 };
      for (int k = 0; k < 4; k++) {
        int sx = 2 * x + (k & 1), sy = 2 * y + (k >> 1), si = sy * A.w + sx;
        if (g_layer[sy * kMaxW + sx] == 2 || g_layer[sy * kMaxW + sx] == 0xff)
          continue;
        for (int ch = 0; ch < 3; ch++) {
          int v = g_src[ch][si] - 215;
          sum[ch] += v > 0 ? v : 0;
        }
      }
      for (int ch = 0; ch < 3; ch++)
        g_glow_half[ch][y * A.hw + x] = sum[ch];  // (4 pixels' worth: the bloom's strength)
      any |= sum[0] | sum[1] | sum[2];
    }
    A.row_glow[y] = any != 0;
  }
}

// ... then spread wide at a quarter.
static void StageGlowBlur(int c0, int c1) {
  for (int ch = c0; ch < c1; ch++) {
    Downsample(g_glow_half[ch], A.hw, A.hh, g_glow[ch], A.qw, A.qh);
    BoxBlur(g_glow[ch], A.qw, A.qh, 1, ch);
    BoxBlur(g_glow[ch], A.qw, A.qh, 2, ch);
  }
}

static void StageWiden(int c0, int c1) {
  for (int ch = c0; ch < c1; ch++) {
    WidenRows(g_soft[ch], A.hw, A.hh, g_taps_x[0], A.w, g_wide_soft[ch]);
    if (A.any_glow)
      WidenRows(g_glow[ch], A.qw, A.qh, g_taps_x[1], A.w, g_wide_glow[ch]);
  }
}

// Everything together, row by row.
static void StageCompose(int y0, int y1) {
  const Scene *sc = &g_scene;
  int w = A.w;
  for (int y = y0; y < y1; y++) {
    uint32 *row = (uint32 *)(A.pixels + y * A.pitch);
    // In focus: a band around the players; it fades out of focus above and below.
    float d = y < A.top ? A.top - y : y > A.bottom ? y - A.bottom : 0;
    int blur_row = (int)(SmoothStep(0.15f, 0.65f, d / A.h) * A.dof * 256);
    int blur_far = (blur_row >> 1) + (int)(0.6f * A.dof * 256);
    const Taps *ty0 = &g_taps_y[0][y], *ty1 = &g_taps_y[1][y];
    const uint16 *soft0[3], *soft1[3], *glow0[3], *glow1[3];
    for (int ch = 0; ch < 3; ch++) {
      soft0[ch] = g_wide_soft[ch] + ty0->i0 * w, soft1[ch] = g_wide_soft[ch] + ty0->i1 * w;
      glow0[ch] = g_wide_glow[ch] + ty1->i0 * w, glow1[ch] = g_wide_glow[ch] + ty1->i1 * w;
    }
    int fs = ty0->f, fg = ty1->f;
    const uint8 *layers = g_layer + y * kMaxW, *vig = g_vignette + y * w;
    for (int x = 0; x < w; x++) {
      uint8 layer = layers[x];
      // The HUD (BG3) is left exactly as it is, and so are the blanked
      // columns and rows outside the picture (0xff).
      if (layer == 2 || layer == 0xff)
        continue;
      int i = y * w + x;
      // Sprites (Links, enemies, items) stay sharp; far layers are softer and hazy.
      bool far = layer == 5 || (layer == 1 && sc->far_bg2);
      int blur = (layer == 4 || layer == 6) ? 0 : far ? blur_far : blur_row;
      int c[3] = { g_src[0][i], g_src[1][i], g_src[2][i] };
      if (blur) {
        for (int ch = 0; ch < 3; ch++) {
          int t = ((soft0[ch][x] << 8) + (soft1[ch][x] - soft0[ch][x]) * fs) >> 8;
          c[ch] += (t - c[ch]) * blur >> 8;
        }
      }
      if (A.any_glow) {
        for (int ch = 0; ch < 3; ch++) {
          int t = ((glow0[ch][x] << 8) + (glow1[ch][x] - glow0[ch][x]) * fg) >> 8;
          c[ch] += t * A.glow_k >> 8;
        }
      }
      if (A.lighting) {
        // Links, enemies and items stay plainly visible in the dark (the
        // game's darkness never covered them either).
        float floor = sc->dark && (layer == 4 || layer == 6) ? 0.75f : 0.0f;
        for (int ch = 0; ch < 3; ch++) {
          float l = g_light[ch][i];
          l = l < floor ? floor : l;
          c[ch] = (int)(c[ch] * (l < 1.25f ? l : 1.25f));
        }
      }
      if (far) {
        for (int ch = 0; ch < 3; ch++)
          c[ch] += (A.haze[ch] - c[ch]) * A.haze_k >> 8;
      }
      row[x] = GradeAndPack(&A.grade, c[0], c[1], c[2], vig[x]);
    }
  }
}

static void Apply(uint8 *pixels, size_t pitch, int w, int h, int view) {
  const Scene *sc = &g_scene;
  A.pixels = pixels, A.pitch = pitch, A.w = w, A.h = h;
  A.hw = w / 2, A.hh = h / 2, A.qw = A.hw / 2, A.qh = A.hh / 2;
  float bloom = g_hd2d.bloom / 100.0f;
  A.dof = g_hd2d.depth_of_field / 100.0f;
  ParallelFor(h, StageSplit);
  ParallelFor(3, StageSoft);
  A.any_glow = false;
  if (bloom > 0) {
    ParallelFor(A.hh, StageGlowThreshold);
    for (int y = 0; y < A.hh; y++)
      A.any_glow |= A.row_glow[y];
    if (A.any_glow)
      ParallelFor(3, StageGlowBlur);
  }
  A.lighting = sc->dark || (g_hd2d.lighting && sc->num_lights);
  if (A.lighting)
    ComputeLight(sc, w, h);
  MakeVignette(w, h);
  MakeTaps(g_taps_x[0], w, 1, A.hw), MakeTaps(g_taps_y[0], h, 1, A.hh);
  MakeTaps(g_taps_x[1], w, 2, A.qw), MakeTaps(g_taps_y[1], h, 2, A.qh);
  ParallelFor(3, StageWiden);
  A.grade = MakeGrade();
  A.glow_k = (int)(0.35f * bloom * 256);
  A.haze_k = (int)(0.18f * A.dof * 256);
  static const int kHaze[3] = { 120, 130, 150 };
  for (int ch = 0; ch < 3; ch++)
    A.haze[ch] = sc->dark ? (int)(kHaze[ch] * sc->ambient) : kHaze[ch];
  FocusBand(sc, view, h, &A.top, &A.bottom);
  ParallelFor(h, StageCompose);
}

// The HD world map (4x mode 7): just the grade and vignette, on all cores.
static float g_m7_col[kMaxW * 4];  // (dx * dx) * 2 per column

static void StageMode7Rows(int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    uint32 *row = (uint32 *)(A.pixels + y * A.pitch);
    float dy = (y + 0.5f) / A.h - 0.5f, dy2 = dy * dy * 2;
    for (int x = 0; x < A.w; x++) {
      uint32 c = row[x];
      float d = g_m7_col[x] + dy2;
      row[x] = GradeAndPack(&A.grade, c >> 16 & 0xff, c >> 8 & 0xff, c & 0xff, (int)(255 * (1 - A.m7_k * d * d)));
    }
  }
}

static void ApplyLight(uint8 *pixels, size_t pitch, int w, int h) {
  if (w > (int)countof(g_m7_col))
    return;
  A.pixels = pixels, A.pitch = pitch, A.w = w, A.h = h;
  A.grade = MakeGrade();
  A.m7_k = g_hd2d.vignette / 100.0f * 0.45f;
  for (int x = 0; x < w; x++) {
    float dx = (x + 0.5f) / w - 0.5f;
    g_m7_col[x] = dx * dx * 2;
  }
  ParallelFor(h, StageMode7Rows);
}

int g_draw_extra_left_right = -1;

void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  // (The window's buffer is as wide as the setting says.)
  if (g_draw_extra_left_right >= 0)
    g_zenv.ppu->extraLeftRight = g_draw_extra_left_right;
  if (!g_hd2d.enabled) {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
    return;
  }
  Ppu *ppu = g_zenv.ppu;
  int w = 256 + ppu->extraLeftRight * 2, h = (render_flags & kPpuRenderFlags_Height240) ? 240 : 224;
  // The HD world map: drawn at 4x exactly when the PPU says so (the buffer
  // is only that big then: not while the screen is still blanked, nor with
  // the old renderer).
  bool mode7 = PpuGetCurrentRenderScale(ppu, render_flags) == 4;
  if (!mode7)
    GatherScene(&g_scene, ppu, w, h);
  // In a dark room the PPU draws the room fully lit (no darkness layer) and
  // the pass darkens it.
  uint8 fixed[3] = { ppu->fixedColorR, ppu->fixedColorG, ppu->fixedColorB }, sub = ppu->screenEnabled[1];
  if (!mode7 && g_scene.dark)
    ppu->fixedColorR = ppu->fixedColorG = ppu->fixedColorB = 0, ppu->screenEnabled[1] = 0;
  g_ppu_layer_out = g_layer;
  g_ppu_hidden_out = g_hidden;
  g_ppu_layer_pitch = kMaxW;
  ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
  g_ppu_layer_out = NULL;
  g_ppu_hidden_out = NULL;
  ppu->fixedColorR = fixed[0], ppu->fixedColorG = fixed[1], ppu->fixedColorB = fixed[2], ppu->screenEnabled[1] = sub;
  if (mode7)
    ApplyLight(pixel_buffer, pitch, w * 4, h * 4);
  else if (w <= kMaxW && h <= kMaxH)
    Apply(pixel_buffer, pitch, w, h, view);
}

int Hd2d_HiddenPixels(void) {
  int n = 0;
  if (g_scene.reveal)
    for (int i = 0; i < kMaxW * kMaxH; i++)
      n += g_hidden[i] != 0;
  return n;
}

int Hd2d_NumLights(void) {
  return g_scene.num_lights;
}

bool Hd2d_LitDarkRoom(void) {
  return g_scene.dark;
}
