#include "hd2d.h"
#include <math.h>
#include <string.h>
#include "zelda_rtl.h"
#include "snes/ppu.h"

Hd2dSettings g_hd2d = { false, 60, 40, 45, 50 };

enum {
  kMaxW = kPpuXPixels,
  kMaxH = 240,
};

// Layer of each pixel (from the PPU), and scratch images: the frame and two
// blurred copies, one channel at a time as 16-bit values.
static uint8 g_layer[kMaxW * kMaxH];
static uint16 g_src[3][kMaxW * kMaxH];
static uint16 g_soft[3][kMaxW * kMaxH];   // a little blurred: the out-of-focus picture
static uint16 g_glow[3][kMaxW * kMaxH];   // the bright parts, much blurred: the bloom
static uint16 g_tmp[kMaxW * kMaxH];

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

static void Apply(uint8 *pixels, size_t pitch, int w, int h) {
  // The frame, split into channels.
  for (int y = 0; y < h; y++) {
    const uint32 *row = (const uint32 *)(pixels + y * pitch);
    for (int x = 0; x < w; x++) {
      uint32 c = row[x];
      int i = y * w + x;
      g_src[0][i] = c >> 16 & 0xff, g_src[1][i] = c >> 8 & 0xff, g_src[2][i] = c & 0xff;
    }
  }
  float bloom = g_hd2d.bloom / 100.0f, dof = g_hd2d.depth_of_field / 100.0f;
  for (int ch = 0; ch < 3; ch++) {
    memcpy(g_soft[ch], g_src[ch], w * h * sizeof(uint16));
    BoxBlur(g_soft[ch], w, h, 2);
    BoxBlur(g_soft[ch], w, h, 3);
    // Bloom: only the brightest parts (not the HUD), spread wide.
    for (int i = 0; i < w * h; i++) {
      int v = g_src[ch][i] - 215;
      g_glow[ch][i] = v > 0 && g_layer[(i / w) * kMaxW + i % w] != 2 ? v * 4 : 0;
    }
    BoxBlur(g_glow[ch], w, h, 6);
    BoxBlur(g_glow[ch], w, h, 6);
  }
  // In focus: a band a little below the middle of the screen, where the
  // players usually are; it fades out of focus toward the top and bottom.
  float focus = h * 0.55f;
  for (int y = 0; y < h; y++) {
    uint32 *row = (uint32 *)(pixels + y * pitch);
    float t = fabsf(y - focus) / (h * 0.5f);
    float blur_row = SmoothStep(0.30f, 1.0f, t) * dof;
    for (int x = 0; x < w; x++) {
      int i = y * w + x;
      uint8 layer = g_layer[y * kMaxW + x];  // (the PPU writes it kMaxW wide)
      // The HUD (BG3) is left exactly as it is.
      if (layer == 2)
        continue;
      // Sprites (Links, enemies, items) stay sharp.
      float blur = (layer == 4 || layer == 6) ? 0 : blur_row;
      float c[3];
      for (int ch = 0; ch < 3; ch++)
        c[ch] = g_src[ch][i] + (g_soft[ch][i] - g_src[ch][i]) * blur + g_glow[ch][i] * 0.35f * bloom;
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

void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  if (!g_hd2d.enabled) {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
    return;
  }
  g_ppu_layer_out = g_layer;
  g_ppu_layer_pitch = kMaxW;
  ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
  g_ppu_layer_out = NULL;
  Ppu *ppu = g_zenv.ppu;
  int w = 256 + ppu->extraLeftRight * 2, h = (render_flags & kPpuRenderFlags_Height240) ? 240 : 224;
  // The layer buffer is one byte per pixel at 1x, so it lines up with the frame.
  if (ppu->mode == 7 && (render_flags & kPpuRenderFlags_4x4Mode7))
    ApplyLight(pixel_buffer, pitch, w * 4, h * 4);
  else if (w <= kMaxW && h <= kMaxH)
    Apply(pixel_buffer, pitch, w, h);
}

