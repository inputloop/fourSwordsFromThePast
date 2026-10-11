#include "hd2d.h"
#include <math.h>
#include <string.h>
#include "zelda_rtl.h"
#include "variables.h"
#include "mp.h"
#include "snes/ppu.h"

Hd2dSettings g_hd2d = { false, 60, 40, 45, 50, 70 };

enum {
  kMaxW = kPpuXPixels,
  kMaxH = 240,
  kMaxLights = 48,
  kMaxViews = 4,
};

// Layer of each pixel and the Link pixels hidden behind background tiles
// (from the PPU), and scratch images: the frame and two blurred copies, one
// channel at a time as 16-bit values.
static uint8 g_layer[kMaxW * kMaxH];
static uint32 g_hidden[kMaxW * kMaxH];
static uint16 g_src[3][kMaxW * kMaxH];
static uint16 g_soft[3][kMaxW * kMaxH];   // a little blurred: the out-of-focus picture
static uint16 g_glow[3][kMaxW * kMaxH];   // the bright parts, much blurred: the bloom
static uint16 g_tmp[kMaxW * kMaxH];
static float g_light[3][kMaxW * kMaxH];   // light reaching each pixel (lighting only)

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
// to a gaussian).
static void BoxBlur(uint16 *img, int w, int h, int r) {
  int n = 2 * r + 1;
  for (int y = 0; y < h; y++) {
    uint16 *row = img + y * w, *out = g_tmp + y * w;
    int sum = 0;
    for (int i = -r; i <= r; i++)
      sum += row[i < 0 ? 0 : i >= w ? w - 1 : i];
    for (int x = 0; x < w; x++) {
      out[x] = sum / n;
      int add = x + r + 1, sub = x - r;
      sum += row[add >= w ? w - 1 : add] - row[sub < 0 ? 0 : sub];
    }
  }
  for (int x = 0; x < w; x++) {
    int sum = 0;
    for (int i = -r; i <= r; i++)
      sum += g_tmp[(i < 0 ? 0 : i >= h ? h - 1 : i) * w + x];
    for (int y = 0; y < h; y++) {
      img[y * w + x] = sum / n;
      int add = y + r + 1, sub = y - r;
      sum += g_tmp[(add >= h ? h - 1 : add) * w + x] - g_tmp[(sub < 0 ? 0 : sub) * w + x];
    }
  }
}

static float SmoothStep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

// Color grade and vignette for one pixel (channels 0-255 as floats).
static void GradeAndVignette(float *r, float *g, float *b, float vig) {
  float grade = g_hd2d.grade / 100.0f;
  float l = 0.299f * *r + 0.587f * *g + 0.114f * *b;
  float sat = 1 + 0.25f * grade, con = 1 + 0.15f * grade;
  *r = ((l + (*r - l) * sat) - 128) * con + 128;
  *g = ((l + (*g - l) * sat) - 128) * con + 128;
  *b = ((l + (*b - l) * sat) - 128) * con + 128;
  *r *= 1 + 0.06f * grade;  // warm light
  *b *= 1 - 0.06f * grade;
  *r *= vig, *g *= vig, *b *= vig;
}

static uint32 Pack(float r, float g, float b) {
  int ir = r < 0 ? 0 : r > 255 ? 255 : (int)r;
  int ig = g < 0 ? 0 : g > 255 ? 255 : (int)g;
  int ib = b < 0 ? 0 : b > 255 ? 255 : (int)b;
  return ir << 16 | ig << 8 | ib;
}

static float Vignette(int x, int y, int w, int h) {
  float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / h - 0.5f;
  float d = (dx * dx + dy * dy) * 2;  // 0 in the middle, 1 in the corners
  return 1 - g_hd2d.vignette / 100.0f * 0.45f * d * d;
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
  sc->reveal = (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
  int cam_x = BG2HOFS_copy2 - ppu->extraLeftRight, cam_y = BG2VOFS_copy2;
  // A dark room: the game darkens it by subtracting a layer of darkness
  // (BG1 on the subscreen), with a hole for the Lamp. The pass lights the
  // room itself instead.
  sc->dark = player_is_indoors && g_hd2d.lighting && (dung_want_lights_out | dung_want_lights_out_copy) &&
             ppu->addSubscreen && ppu->subtractColor && ppu->screenEnabled[1] == 1;
  if (sc->dark) {
    float lit = (31 - ppu->fixedColorR) / 31.0f;  // 0 with no torch lit, 1 with all
    sc->ambient = 0.03f + 0.97f * lit;
  }
  bool lamp = sc->dark && link_item_torch != 0;
  float flicker = 0.93f + 0.07f * sinf(frame_counter * 0.37f);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (Mp_Active() ? !Mp_IsAlive(p) : p != 0)
      continue;
    float x = (uint16)(Mp_PlayerX(p) - cam_x) + 8.0f, y = (int16)(Mp_PlayerY(p) - cam_y) + 14.0f;
    if (x < -64 || x > w + 64)
      continue;
    sc->player_y[sc->num_players++] = y;
    if (lamp) {
      // The Lamp lights a circle around the Link, a little ahead of it.
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
  float add = sc->dark ? 1.0f : 0.3f * strength;
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

static void Apply(uint8 *pixels, size_t pitch, int w, int h, int view) {
  const Scene *sc = &g_scene;
  // The frame, split into channels, with hidden Links showing through.
  for (int y = 0; y < h; y++) {
    const uint32 *row = (const uint32 *)(pixels + y * pitch);
    for (int x = 0; x < w; x++) {
      uint32 c = row[x], hid = g_hidden[y * kMaxW + x];
      int i = y * w + x;
      int r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;
      if (hid && sc->reveal) {
        r = (r * 2 + (hid >> 16 & 0xff) * 3) / 5;
        g = (g * 2 + (hid >> 8 & 0xff) * 3) / 5;
        b = (b * 2 + (hid & 0xff) * 3) / 5;
      }
      g_src[0][i] = r, g_src[1][i] = g, g_src[2][i] = b;
    }
  }
  float bloom = g_hd2d.bloom / 100.0f, dof = g_hd2d.depth_of_field / 100.0f;
  for (int ch = 0; ch < 3; ch++) {
    memcpy(g_soft[ch], g_src[ch], w * h * sizeof(uint16));
    BoxBlur(g_soft[ch], w, h, 2);
    BoxBlur(g_soft[ch], w, h, 3);
    // Bloom: only the brightest parts (not the HUD), spread wide.
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int i = y * w + x, v = g_src[ch][i] - 215;
        g_glow[ch][i] = v > 0 && g_layer[y * kMaxW + x] != 2 ? v * 4 : 0;
      }
    }
    BoxBlur(g_glow[ch], w, h, 6);
    BoxBlur(g_glow[ch], w, h, 6);
  }
  bool lighting = sc->dark || (g_hd2d.lighting && sc->num_lights);
  if (lighting)
    ComputeLight(sc, w, h);
  // In focus: a band around the players; it fades out of focus above and below.
  float top, bottom;
  FocusBand(sc, view, h, &top, &bottom);
  for (int y = 0; y < h; y++) {
    uint32 *row = (uint32 *)(pixels + y * pitch);
    float d = y < top ? top - y : y > bottom ? y - bottom : 0;
    float blur_row = SmoothStep(0.15f, 0.65f, d / h) * dof;
    for (int x = 0; x < w; x++) {
      int i = y * w + x;
      uint8 layer = g_layer[y * kMaxW + x];  // (the PPU writes it kMaxW wide)
      // The HUD (BG3) is left exactly as it is.
      if (layer == 2)
        continue;
      // Sprites (Links, enemies, items) stay sharp; far layers are softer and hazy.
      bool far = layer == 5 || (layer == 1 && sc->far_bg2);
      float blur = (layer == 4 || layer == 6) ? 0 : far ? blur_row * 0.5f + 0.6f * dof : blur_row;
      float c[3];
      for (int ch = 0; ch < 3; ch++) {
        c[ch] = g_src[ch][i] + (g_soft[ch][i] - g_src[ch][i]) * blur + g_glow[ch][i] * 0.35f * bloom;
        if (lighting)
          c[ch] *= g_light[ch][i] < 1.25f ? g_light[ch][i] : 1.25f;
      }
      if (far) {
        static const float kHaze[3] = { 120, 130, 150 };
        for (int ch = 0; ch < 3; ch++)
          c[ch] += (kHaze[ch] * (sc->dark ? sc->ambient : 1) - c[ch]) * 0.18f * dof;
      }
      GradeAndVignette(&c[0], &c[1], &c[2], Vignette(x, y, w, h));
      row[x] = Pack(c[0], c[1], c[2]);
    }
  }
}

// The HD world map (4x mode 7): just the grade and vignette.
static void ApplyLight(uint8 *pixels, size_t pitch, int w, int h) {
  for (int y = 0; y < h; y++) {
    uint32 *row = (uint32 *)(pixels + y * pitch);
    for (int x = 0; x < w; x++) {
      uint32 c = row[x];
      float r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;
      GradeAndVignette(&r, &g, &b, Vignette(x, y, w, h));
      row[x] = Pack(r, g, b);
    }
  }
}

void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  if (!g_hd2d.enabled) {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
    return;
  }
  Ppu *ppu = g_zenv.ppu;
  int w = 256 + ppu->extraLeftRight * 2, h = (render_flags & kPpuRenderFlags_Height240) ? 240 : 224;
  bool mode7 = ppu->mode == 7 && (render_flags & kPpuRenderFlags_4x4Mode7);
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

bool Hd2d_LitDarkRoom(void) {
  return g_scene.dark;
}
