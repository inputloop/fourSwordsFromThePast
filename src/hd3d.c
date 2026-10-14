// The 3D view (beta): drawing a pane in 3D (see hd3d.h).
//
// A pane is drawn in two phases:
// - Phase A, with its world bound (in split screen under the audio lock).
//   It first decides whether the pane can be 3D at all (Why2D), before
//   anything is drawn, so a pane that falls back is drawn by the 2D path
//   exactly once. Then it draws the 2D picture once into the pane's own
//   buffer: the HUD and text boxes come from it, the game's drawing side
//   effects (irq_flag) happen exactly as in 2D, and the frame hook captures
//   the sprites. Last it reads everything else from the world: the scene,
//   the sprites, the lights and darkness, the colors, the outdoor overlay
//   and where the camera stands.
// - Phase B, with no game memory: the camera, the quads and cards, light,
//   darkness and fog, the rasterizer on the worker pool, then the overlay,
//   the HUD, the fades and the 2D-HD pass. In split screen it runs for every
//   pane once the lock is released (Hd3d_FinishPanes).
// Nothing here writes the game's memory or leaves a PPU field changed.
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hd3d_internal.h"
#include "hd2d.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "instance.h"
#include "mp.h"
#include "session.h"
#include "ui.h"

Hd3dSettings g_hd3d;

const char *const kHd3dWhyNames[kHd3dWhy_Count] = {
  "none", "off", "renderer", "scale", "mode", "blank", "mosaic", "window", "module", "room", "size", "memory",
};

enum {
  kPicW = kR3dMaxW,          // the pane buffers' rows (pixels)
  kPicH = kR3dMaxH,
  kPicPixels = kPicW * kPicH,
  // The pane's clock runs in 1/60 s ticks: the game frames its world ran
  // since the last picture, at least one per picture (so fades also finish
  // while the game is paused under the menu).
  kFadeInTicks = 12,         // 0.2 s from 2D into 3D
  kFadeOutTicks = 9,         // 0.15 s from 3D back to 2D
  kMaxStep = 60,
  kAtlasSlot = 4,
};

// Dungeon rooms always drawn in 2D: bosses drawn on a background layer.
static const uint16 kHd3dRooms2D[] = {
  0xa4,                      // Trinexx (its body is BG1)
  0xde,                      // Kholdstare (its shell is BG1)
};

// The submodules the 3D view draws: play, scrolls inside a dungeon, doors
// and in-room changes; text boxes and refills over play.
static const uint8 kDungeonSubs[] = { 0, 1, 2, 3, 4, 5, 8, 9, 0x0a, 0x10, 0x16, 0x17, 0x18, 0x1a };
static const uint8 kOverworldSubs[] = { 0, 9, 0x0a, 0x0b, 0x0c, 0x2d };
static const uint8 kMessageSubs[] = { 2, 4, 8, 9, 11 };

typedef struct Pane {
  // Kept from picture to picture.
  bool drawn;                // drawn at least once (the clock runs)
  bool is3d;                 // the last picture was 3D
  bool had_2d;               // tick_2d is set
  bool had_scene;            // info is the scene of the last 3D picture
  uint8 why;                 // kHd3dWhy_* of the last picture
  uint8 frame;               // its world's frame_counter at the last picture
  int inst;                  // the world it showed
  uint32 ticks;
  uint32 tick_2d, tick_3d;   // when it last showed a 2D / 3D picture
  float tz, eye[3];          // smoothed: the camera's height and the first-person eye
  bool last_ok;              // last holds the last 3D picture (last_w x last_h), for a fade out
  int last_w, last_h;
  bool cam_ok;               // cam: the camera of the last 3D picture (Hd3d_Project)
  R3dCamera cam;
  int quads, cards;
  // Buffers, allocated the first time the pane is 3D (pitch kPicW).
  uint32 *pic2d;             // this frame's 2D picture
  uint8 *layer2d;            // its layers (2: the HUD)
  uint32 *last;
  uint8 *overlay;            // the outdoor BG1 overlay as palette indices (allocated when one shows)
  Hd3dSpriteFrame *sf;
  // This frame, from phase A for phase B.
  bool pending;
  uint8 *px;
  size_t pitch;
  int w, h;
  Hd3dScene *scene, *prev;
  Hd3dSceneInfo info, prev_info;
  float rise, prev_rise;
  int32 live[4];             // the live rectangle (picture px: x0, y0, x1, y1)
  int shown_x0, shown_x1, shown_h;  // the same in the pane's columns and rows (the 2D picture blanks the rest)
  int32 rect[4], prev_rect[4];  // what each scene emits
  int32 reach[4];            // where this world's camera can go (rect never leaves it)
  Cam3d cam3d;
  Cam3dTuning tuning;
  Cam3dPose pose;
  float link[3];             // the pane player's Link (fade scenery)
  int fade_in;               // 0-256: how much of the 3D picture shows (256: no fade)
  uint32 bg_lut[256];        // cgram 0-127 with the brightness
  uint32 fog_tp, fog_fp;     // the fog in third and first person
  uint32 backdrop;           // cgram 0 as lit: the ground where a background tile is see-through
  float ambient, card_min;
  float subtract[3];
  bool subtract_lights, subtract_cards, see_through;
  int num_lights;
  R3dLight lights[kHd3dMaxLights];
  uint8 ov_mode;             // the outdoor overlay: 0 none, 1 above everything, 2 below (the background)
  bool ov_sub, ov_half, ov_obj;
  uint32 ov_fixed;           // the fixed color (where the overlay is see-through)
} Pane;

static Pane g_panes[kHd3dMaxPanes];
static bool g_deferred;
static bool g_test_camera;
static Hd3dSpriteFrame *g_capture;  // where the frame hook copies the sprites
// Scenes whose rise already moved in this batch of panes (two panes can show one world).
static Hd3dScene *g_stepped[kHd3dMaxPanes];
static int g_num_stepped;

// Shared by the panes (phase B runs one pane at a time).
static R3dWork *g_work;
static R3dQuad *g_quads;
static R3dCard *g_cards;
static float *g_depth;
static uint8 *g_cls, *g_blur, *g_atlas;
static uint32 *g_bg;
static uint32 g_sprite_lut[256];
static R3dFrame g_frame;

static float Clampf(float v, float lo, float hi) {
  return !(v >= lo) ? lo : v > hi ? hi : v;  // (NaN: lo)
}

static float SmoothStep(float a, float b, float x) {
  float t = Clampf((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// e^-a for a >= 0 (no expf: halve until small, a short series, square back).
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

static uint32 ScaleRgb(uint32 rgb, int num, int den) {
  uint32 r = (rgb >> 16 & 0xff) * num / den, g = (rgb >> 8 & 0xff) * num / den, b = (rgb & 0xff) * num / den;
  return r << 16 | g << 8 | b;
}

static uint32 LerpRgb(uint32 a, uint32 b, float t) {
  int k = (int)(Clampf(t, 0.0f, 1.0f) * 256.0f);
  uint32 r = ((a >> 16 & 0xff) * (256 - k) + (b >> 16 & 0xff) * k) >> 8;
  uint32 g = ((a >> 8 & 0xff) * (256 - k) + (b >> 8 & 0xff) * k) >> 8;
  uint32 bl = ((a & 0xff) * (256 - k) + (b & 0xff) * k) >> 8;
  return r << 16 | g << 8 | bl;
}

// A BGR555 color as the PPU shows it at that brightness (never its
// brightnessMult cache: that is only filled when it draws).
static uint32 Rgb555(uint16 c, int brightness) {
  int r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
  r = ((r << 3) | (r >> 2)) * brightness / 15;
  g = ((g << 3) | (g >> 2)) * brightness / 15;
  b = ((b << 3) | (b >> 2)) * brightness / 15;
  return (uint32)(r << 16 | g << 8 | b);
}

static bool InList(uint8 v, const uint8 *list, int n) {
  for (int i = 0; i < n; i++)
    if (list[i] == v)
      return true;
  return false;
}

static Pane *PaneOf(int view) {
  return view >= 0 && view < kHd3dMaxPanes ? &g_panes[view] : NULL;
}

// ---- Why a pane stays 2D ----

static bool ModuleShows3D(void) {
  uint8 m = main_module_index, s = submodule_index;
  if (m == 7)
    return InList(s, kDungeonSubs, (int)countof(kDungeonSubs));
  if (m == 9 || m == 11)
    return InList(s, kOverworldSubs, (int)countof(kOverworldSubs));
  if (m == 14) {
    uint8 under = saved_module_for_menu;
    return (under == 7 || under == 9 || under == 11) && InList(s, kMessageSubs, (int)countof(kMessageSubs));
  }
  return false;
}

static bool RoomIs2D(void) {
  if (!player_is_indoors)
    return false;
  // A moving floor or water, a boss moving BG1, or Ganon's and Agahnim's rooms.
  if (dung_hdr_collision >= 3 || dung_hdr_collision_2_mirror || dung_hdr_bg2_properties == 2)
    return true;
  for (int i = 0; i < (int)countof(kHd3dRooms2D); i++)
    if (dungeon_room_index == kHd3dRooms2D[i])
      return true;
  return false;
}

// The cheap tests, in order. (The scene and memory come after.)
static int Why2D(Ppu *ppu, uint32 flags, int w, int h, const uint8 *px, size_t pitch) {
  if (!g_hd3d.enabled)
    return kHd3dWhy_Off;
  if (!(flags & kPpuRenderFlags_NewRenderer))
    return kHd3dWhy_Renderer;  // (only it reports each pixel's layer)
  if (PpuGetCurrentRenderScale(ppu, flags) != 1)
    return kHd3dWhy_Scale;
  if (ppu->mode != 1)
    return kHd3dWhy_Mode;
  if (ppu->forcedBlank)
    return kHd3dWhy_Blank;
  if ((ppu->mosaicEnabled & 0x13) && ppu->mosaicSize > 1)
    return kHd3dWhy_Mosaic;
  // HDMAEN = 0x80 with ch6/ch7 on WH0 (0x26) is set in all ordinary play
  // (dungeon.c:8003; seen at runtime in modules 7 and 9 with TMW = 0), and
  // the M7 channels stay set after the world map. Window fallbacks test the
  // TMW/TSW enable bits, never HDMA channel targets.
  if ((ppu->screenWindowed[0] & 0x13) || ((ppu->screenWindowed[1] & 0x13) && ppu->screenEnabled[1]))
    return kHd3dWhy_Window;
  if (!ModuleShows3D())
    return kHd3dWhy_Module;
  if (RoomIs2D())
    return kHd3dWhy_Room;
  // (Rows are written as 32-bit pixels, aligned.)
  if (w > kR3dMaxW || h > kR3dMaxH || ((uintptr_t)px & 3) || (pitch & 3) || pitch < (size_t)w * 4)
    return kHd3dWhy_Size;
  return kHd3dWhy_None;
}

// ---- Memory ----

static bool AllocShared(void) {
  if (!g_work)
    g_work = R3d_NewWork(kHd3dMaxQuads, kHd3dMaxCards);
  if (!g_quads)
    g_quads = (R3dQuad *)malloc(sizeof(R3dQuad) * kHd3dMaxQuads);
  if (!g_cards)
    g_cards = (R3dCard *)malloc(sizeof(R3dCard) * kHd3dMaxCards);
  if (!g_depth)
    g_depth = (float *)malloc(sizeof(float) * kPicPixels);
  if (!g_cls)
    g_cls = (uint8 *)malloc(kPicPixels);
  if (!g_blur)
    g_blur = (uint8 *)malloc(kPicPixels);
  if (!g_atlas)
    g_atlas = (uint8 *)calloc(kHd3dAtlasW * kHd3dAtlasH, 1);
  if (!g_bg)
    g_bg = (uint32 *)malloc(sizeof(uint32) * kPicPixels);
  return g_work && g_quads && g_cards && g_depth && g_cls && g_blur && g_atlas && g_bg;
}

static bool AllocPane(Pane *p, bool overlay) {
  if (!p->pic2d)
    p->pic2d = (uint32 *)malloc(sizeof(uint32) * kPicPixels);
  if (!p->layer2d)
    p->layer2d = (uint8 *)malloc(kPicPixels);
  if (!p->last)
    p->last = (uint32 *)malloc(sizeof(uint32) * kPicPixels);
  if (!p->sf)
    p->sf = (Hd3dSpriteFrame *)calloc(1, sizeof(Hd3dSpriteFrame));
  if (overlay && !p->overlay)
    p->overlay = (uint8 *)malloc(kPicPixels);
  return p->pic2d && p->layer2d && p->last && p->sf && (!overlay || p->overlay);
}

// ---- Phase A: reading the world ----

// Advances the pane's clock: returns the ticks since its last picture.
static int Tick(Pane *p, bool *same_world) {
  int inst = Inst_Current();
  uint8 frame = frame_counter;
  int step = 1;
  *same_world = p->drawn && p->inst == inst;
  if (*same_world) {
    int d = (uint8)(frame - p->frame);
    step = d < 1 ? 1 : d > kMaxStep ? kMaxStep : d;
  }
  p->drawn = true;
  p->inst = inst, p->frame = frame;
  p->ticks += (uint32)step;
  return step;
}

// The bound world's room or area (world px).
static void AreaRect(int32 r[4]) {
  int32 x, y, size;
  if (player_is_indoors) {
    x = dung_loade_bgoffs_h_copy & ~0x1ff, y = dung_loade_bgoffs_v_copy & ~0x1ff, size = 512;
  } else {
    x = (int32)overworld_offset_base_x << 3, y = overworld_offset_base_y;
    size = (overworld_offset_mask_y | 0xf) + 1;
  }
  r[0] = x, r[1] = y, r[2] = x + size, r[3] = y + size;
}

static void Intersect(int32 r[4], const int32 o[4]) {
  r[0] = r[0] > o[0] ? r[0] : o[0];
  r[1] = r[1] > o[1] ? r[1] : o[1];
  r[2] = r[2] < o[2] ? r[2] : o[2];
  r[3] = r[3] < o[3] ? r[3] : o[3];
}

static void Grow(int32 r[4], int by) {
  r[0] -= by, r[1] -= by, r[2] += by, r[3] += by;
}

// Where the game's camera can go in this room or area, plus what the screen
// shows now: the 3D view never shows quadrants the game keeps hidden.
static void ReachRect(int32 r[4], const int32 live[4], int h) {
  int32 area[4];
  AreaRect(area);
  if (player_is_indoors) {
    if (main_module_index == 7 && (submodule_index == 1 || submodule_index == 2)) {
      // Scrolls: the bounds are right only at one end.
      memcpy(r, area, sizeof(area));
      return;
    }
    int qm = quadrant_fullsize_x >> 1 & 1, qy = quadrant_fullsize_y >> 1 & 1;
    r[0] = room_bounds_x.v[qm], r[2] = room_bounds_x.v[qm + 2] + 256;
    r[1] = room_bounds_y.v[qy], r[3] = room_bounds_y.v[qy + 2] + h;
  } else {
    r[0] = ow_scroll_vars0.xstart, r[2] = ow_scroll_vars0.xend + 256;
    r[1] = ow_scroll_vars0.ystart, r[3] = ow_scroll_vars0.yend + h;
  }
  r[0] = r[0] < live[0] ? r[0] : live[0];
  r[1] = r[1] < live[1] ? r[1] : live[1];
  r[2] = r[2] > live[2] ? r[2] : live[2];
  r[3] = r[3] > live[3] ? r[3] : live[3];
  Intersect(r, area);
}

// The player whose eyes pane `local` looks through, in this world.
static int PanePlayer(int local) {
  int p = local >= 0 && local < kHd3dMaxLocal ? Ui_WatchedPlayer(local) : -1;
  if (p < 0 || p >= kMpMaxPlayers || !g_mp.p[p].joined)
    p = g_mp.leader;
  return p;
}

static void FrameHook(struct Ppu *ppu) {
  if (g_capture)
    Hd3dSprites_Capture(ppu, g_capture);
}

// The outdoor BG1 overlay (fog, rain, canopy, clouds) exactly where the 2D
// picture has it: it is anchored to the screen, not the world.
static void DecodeOverlay(const Ppu *ppu, uint8 *out, int w, int h) {
  const BgLayer *bg = &ppu->bgLayer[0];
  int e = ppu->extraLeftRight;
  for (int r = 0; r < h; r++) {
    uint8 *dst = out + r * kPicW;
    int y = bg->vScroll + r + 1;
    int sc = bg->tilemapAdr + (((y >> 3) & 0x1f) << 5);
    if ((y & 0x100) && bg->tilemapHigher)
      sc += bg->tilemapWider ? 0x800 : 0x400;
    for (int d = 0; d < w;) {
      int x = (bg->hScroll + d - e) & 0x1ff;
      int half = (x >> 8 & 1) && bg->tilemapWider ? 0x400 : 0;
      uint16 tile = ppu->vram[(sc + half + ((x >> 3) & 0x1f)) & 0x7fff];
      int row = tile & 0x8000 ? 7 - (y & 7) : y & 7;
      int a = bg->tileAdr + (tile & 0x3ff) * 16 + row;
      uint32 bits = ppu->vram[a & 0x7fff] | (uint32)ppu->vram[(a + 8) & 0x7fff] << 16;
      uint8 pal = (uint8)((tile >> 6) & 0x70);
      int c0 = x & 7, n = IntMin(8 - c0, w - d);
      for (int k = 0; k < n; k++) {
        int s = tile & 0x4000 ? c0 + k : 7 - (c0 + k);
        int v = (bits >> s & 1) | (bits >> (s + 7) & 2) | (bits >> (s + 14) & 4) | (bits >> (s + 21) & 8);
        dst[d + k] = v ? (uint8)(pal | v) : 0;
      }
      d += n;
    }
  }
}

// The lights, the darkness and the fog (spec 3.8).
static void ReadLight(Pane *p, Ppu *ppu, int player_z) {
  float lit = 0.0f;
  bool dark = Hd2d_IsDarkRoom(ppu, &lit);
  bool by_hd2d = g_hd2d.enabled && g_hd2d.lighting > 0;
  p->ambient = by_hd2d && dark ? 0.12f + 0.88f * lit : 1.0f;
  p->card_min = by_hd2d && dark ? 0.75f : 0.0f;
  p->subtract[0] = p->subtract[1] = p->subtract[2] = 0.0f;
  p->subtract_lights = p->subtract_cards = false;
  p->num_lights = 0;
  if (by_hd2d || dark) {
    Hd2dLight in[kHd3dMaxLights];
    int n = Hd2d_GatherLights(in, kHd3dMaxLights, dark);
    float scale = by_hd2d && !dark ? 0.45f * g_hd2d.lighting / 100.0f : 1.0f;
    for (int i = 0; i < n && p->num_lights < kHd3dMaxLights; i++) {
      const Hd2dLight *s = &in[i];
      // The game's own darkness: only the Lamp opens a hole in it.
      if (!by_hd2d && s->kind != 0)
        continue;
      R3dLight *l = &p->lights[p->num_lights++];
      float z = s->kind == 1 ? 0.0f : (float)player_z;  // (torches stand on the upper floor)
      l->x = s->x, l->y = s->y + z, l->z = z + (s->kind == 2 ? 8.0f : 16.0f);
      l->radius = s->radius, l->power = s->power * scale;
      if (by_hd2d)
        l->r = s->r, l->g = s->g, l->b = s->b;
      else
        l->r = l->g = l->b = 0.0f;  // (it adds no light, it only takes the darkness away)
    }
  }
  if (dark && !by_hd2d) {
    // The game's own color math: a dark room without the Lamp is black.
    p->subtract[0] = ppu->fixedColorR * 8.0f * ppu->brightness / 15.0f;
    p->subtract[1] = ppu->fixedColorG * 8.0f * ppu->brightness / 15.0f;
    p->subtract[2] = ppu->fixedColorB * 8.0f * ppu->brightness / 15.0f;
    p->subtract_lights = true;
    p->subtract_cards = p->sf->obj_math;
  }
  // Around a room the picture shows its filler (black in most rooms), not the backdrop.
  uint32 around = p->info.indoors && p->info.void_index ? p->bg_lut[p->info.void_index & 255] : p->bg_lut[0];
  uint32 c0 = around;
  if (dark && by_hd2d) {
    c0 = ScaleRgb(c0, (int)(p->ambient * 256.0f), 256);
  } else if (dark) {
    int r = (int)(c0 >> 16 & 0xff) - (int)p->subtract[0], g = (int)(c0 >> 8 & 0xff) - (int)p->subtract[1];
    int b = (int)(c0 & 0xff) - (int)p->subtract[2];
    c0 = (uint32)(IntMax(r, 0) << 16 | IntMax(g, 0) << 8 | IntMax(b, 0));
  }
  p->backdrop = c0;
  int br = ppu->brightness & 15;
  if (player_is_indoors) {
    p->fog_tp = dark ? 0 : around;
    p->fog_fp = dark ? 0 : ScaleRgb(0x08080c, br, 15);
  } else {
    p->fog_tp = ScaleRgb(0x788296, br, 15);
    p->fog_fp = ScaleRgb(overworld_screen_index & 0x40 ? 0x887898 : 0xb8d0e8, br, 15);
  }
}

// The outdoor overlay (spec 3.8): on the subscreen with color math either on
// BG2 (fog, rain, canopy: over everything) or on the backdrop only (Death
// Mountain's clouds: under everything).
static uint8 OverlayMode(const Ppu *ppu) {
  if (player_is_indoors || !(ppu->screenEnabled[1] & 1) || !ppu->addSubscreen || ppu->preventMathMode == 3)
    return 0;
  return ppu->mathEnabled & 0x02 ? 1 : ppu->mathEnabled & 0x20 ? 2 : 0;
}

static void ReadOverlay(Pane *p, const Ppu *ppu, uint8 mode) {
  p->ov_mode = mode;
  if (!mode)
    return;
  DecodeOverlay(ppu, p->overlay, p->w, p->h);
  p->ov_sub = ppu->subtractColor;
  p->ov_half = ppu->halfColor;
  p->ov_obj = (ppu->mathEnabled & 0x10) != 0;
  uint16 fixed = (uint16)(ppu->fixedColorR | ppu->fixedColorG << 5 | ppu->fixedColorB << 10);
  p->ov_fixed = Rgb555(fixed, ppu->brightness & 15);
}

static void SmoothTo(float *v, float to, float dt, float tau, bool snap) {
  *v = snap ? to : *v + (to - *v) * (1.0f - ExpNeg(dt / tau));
}

// Where the camera is anchored (spec 5): the target is the relief of the 2D
// screen's middle at the pane player's floor; the first-person eye is over
// that player's Link.
static void ReadPose(Pane *p, int player, float dt, bool snap) {
  Ppu *ppu = g_zenv.ppu;
  int e = ppu->extraLeftRight;
  int cam_x = (int)BG2HOFS_copy2 - e, cam_y = (int)BG2VOFS_copy2;
  bool lower = player_is_indoors && *Mp_PlayerRam(player, 0xEE) != 0;  // link_is_on_lower_level
  // (Outdoors the ground has terraces: the height under the Link's feet.)
  int g = Hd3dScene_StandZ(p->scene, lower, Mp_PlayerX(player) + 8, Mp_PlayerY(player) + 20);
  SmoothTo(&p->tz, (float)g, dt, 0.15f, snap);
  Cam3dPose *pose = &p->pose;
  pose->w = p->w, pose->h = p->h;
  float cx = (float)cam_x + 0.5f * (float)p->w, cy = (float)cam_y + 1.0f + 0.5f * (float)p->h;
  pose->target[0] = cx, pose->target[1] = cy + p->tz, pose->target[2] = p->tz;
  pose->shake[0] = (float)(int16)bg1_x_offset, pose->shake[1] = (float)(int16)bg1_y_offset;
  // (The other Links are drawn moved by their netplay smoothing; the leader is not.)
  int dx = player == g_mp.leader ? 0 : g_mp_draw_offset[player][0];
  int dy = player == g_mp.leader ? 0 : g_mp_draw_offset[player][1];
  uint16 z = WORD(*Mp_PlayerRam(player, 0x24));  // link_z_coord (0xffff on the ground)
  int lz = (int16)z >= 0 || (z & 0xff) < 0xf0 ? z & 0xff : 0;
  float bx = (float)(Mp_PlayerX(player) + 8 + dx), by = (float)(Mp_PlayerY(player) + 16 + dy);
  float eye[3];
  eye[0] = bx, eye[1] = by + (float)g, eye[2] = (float)(g + lz + 20);
  float jx = eye[0] - p->eye[0], jy = eye[1] - p->eye[1];
  bool jump = snap || jx * jx + jy * jy > 48.0f * 48.0f;  // (a warp or a new room: no gliding)
  for (int i = 0; i < 3; i++)
    SmoothTo(&p->eye[i], eye[i], dt, 0.04f, jump);
  memcpy(pose->eye, p->eye, sizeof(pose->eye));
  p->link[0] = bx, p->link[1] = by + (float)g, p->link[2] = (float)g;
}

static bool SameScene(const Hd3dSceneInfo *a, const Hd3dSceneInfo *b) {
  return a->indoors == b->indoors && a->bg1_role == b->bg1_role && a->origin_x == b->origin_x &&
         a->origin_y == b->origin_y && a->size == b->size;
}

// The ancillae that belong to player's sword: the arc of a swing, the spin
// attack's sparkles, the sparkles while it charges and at its tip.
static uint16 WeaponAncillae(int player) {
  uint16 bits = 0;
  for (int k = 0; k < 10; k++) {
    uint8 t = ancilla_type[k];
    if (t != 0x0d && t != 0x26 && t != 0x2a && t != 0x2b && t != 0x3b && t != 0x3c)
      continue;
    int owner = Mp_Active() ? g_mp.ancilla_owner[k] : g_mp.leader;
    if (owner >= kMpMaxPlayers || !g_mp.p[owner].joined)
      owner = g_mp.leader;  // (as Mp_PreAncilla)
    if (owner == player)
      bits |= (uint16)(1 << k);
  }
  return bits;
}

static void ReadScene(Pane *p, Hd3dScene *scene, float dt) {
  p->scene = scene;
  Hd3dScene_GetInfo(scene, &p->info);
  bool stepped = false;
  for (int i = 0; i < g_num_stepped; i++)
    stepped |= g_stepped[i] == scene;
  p->rise = Hd3dScene_StepRise(scene, stepped ? 0.0f : dt, true);
  if (g_deferred && !stepped && g_num_stepped < kHd3dMaxPanes)
    g_stepped[g_num_stepped++] = scene;
  // Room to room: the old room stays where it was, frozen, until the scroll ends.
  p->prev = NULL;
  if (main_module_index == 7 && submodule_index == 2) {
    Hd3dScene *prev = Hd3dScene_Previous();
    if (prev && prev != scene) {
      p->prev = prev;
      Hd3dScene_GetInfo(prev, &p->prev_info);
      p->prev_rise = Hd3dScene_StepRise(prev, 0.0f, true);
    }
  }
}

// Phase A for a pane that will be 3D: the 2D pass, then the reads.
static void ReadWorld(Pane *p, uint32 flags, int step, bool same_world, Hd3dScene *scene, int local) {
  Ppu *ppu = g_zenv.ppu;
  int h = p->h;
  uint8 ov_mode = OverlayMode(ppu);
  // The 2D picture, exactly once (the HUD, irq_flag, the sprites).
  g_ppu_layer_out = p->layer2d;
  g_ppu_hidden_out = NULL;
  g_ppu_layer_pitch = kPicW;
  g_capture = p->sf;
  g_ppu_frame_hook = FrameHook;
  ZeldaDrawPpuFrame((uint8 *)p->pic2d, kPicW * 4, flags);
  g_ppu_frame_hook = NULL;
  g_capture = NULL;
  g_ppu_layer_out = NULL;

  // What the 2D picture really shows (its side columns and bottom rows can be blank).
  int hofs = BG2HOFS_copy2, vofs = BG2VOFS_copy2;
  int shown_h = IntMin(h, 224 + ppu->extraBottomCur);
  p->live[0] = hofs - ppu->extraLeftCur, p->live[2] = hofs + 256 + ppu->extraRightCur;
  p->live[1] = vofs + 1, p->live[3] = vofs + 1 + shown_h;
  p->shown_x0 = ppu->extraLeftRight - ppu->extraLeftCur;
  p->shown_x1 = ppu->extraLeftRight + 256 + ppu->extraRightCur;
  p->shown_h = shown_h;
  int32 reach[4];
  ReachRect(reach, p->live, h);
  memcpy(p->rect, p->live, sizeof(p->rect));
  Grow(p->rect, kHd3dFogMargin);
  memcpy(p->prev_rect, p->rect, sizeof(p->prev_rect));  // (a frozen scene clips it to its own)
  Intersect(p->rect, reach);
  memcpy(p->reach, reach, sizeof(p->reach));

  float dt = (float)step * (1.0f / 60.0f);
  Hd3dSceneInfo before = p->info;
  ReadScene(p, scene, dt);
  Hd3dSprites_ReadWorld(p->sf);
  int player = PanePlayer(local);
  bool test = g_test_camera;
  // First person back in 3D in another room after a stretch in 2D (stairs to
  // another floor, a building's door, a fall): the view turns to where the
  // Link faces, as on entering first person, unless it already looks about
  // that way. (Not between overworld areas: a sideways walk keeps its view.)
  if (p->had_scene && !p->is3d && !test && !SameScene(&before, &p->info) && (before.indoors || p->info.indoors))
    Hd3dInput_FaceLink(local, *Mp_PlayerRam(player, 0x2F));  // link_direction_facing
  p->had_scene = true;
  p->cam3d = g_hd3d_cam[local];
  Hd3dInput_Tuning(&p->tuning);
  bool snap = !same_world || !p->is3d;
  ReadPose(p, player, dt, snap);
  p->sf->hide_player = (int8)(!test && p->cam3d.blend > 0.5f ? player : -1);
  p->sf->xray_player = (int8)(!test && Cam3d_IsTurned(&p->cam3d) ? player : -1);
  p->sf->weapon_ancillae = p->sf->hide_player >= 0 ? WeaponAncillae(player) : 0;
  Hd3dLinkPose_Read(p->sf, test ? NULL : &p->cam3d);  // (the Links face the way this camera sees them)
  int br = ppu->brightness & 15;
  memset(p->bg_lut, 0, sizeof(p->bg_lut));
  for (int i = 0; i < 128; i++)
    p->bg_lut[i] = Rgb555(ppu->cgram[i], br);
  ReadLight(p, ppu, (int)p->tz);
  ReadOverlay(p, ppu, ov_mode);
  uint8 m = main_module_index;
  if (m == 14)
    m = saved_module_for_menu;
  p->see_through = !test && g_hd2d.enabled && g_hd2d.see_through && (m == 7 || m == 9 || m == 11) &&
                   submodule_index == 0;
  // Into 3D from a 2D picture: a short crossfade.
  uint32 since_2d = p->ticks - p->tick_2d;
  p->fade_in = test || !p->had_2d || since_2d >= kFadeInTicks ? 256 : (int)(since_2d * 256 / kFadeInTicks);
}

// ---- Phase B: drawing (no game memory) ----

// The color math of the PPU with the overlay pixel (index 0: see-through,
// then the fixed color, never halved), on 8-bit channels.
static uint32 OverlayMath(const Pane *p, uint32 c, uint8 idx) {
  uint32 o = idx ? p->bg_lut[idx] : p->ov_fixed;
  bool half = idx && p->ov_half;
  int r = (int)(c >> 16 & 0xff), g = (int)(c >> 8 & 0xff), b = (int)(c & 0xff);
  int r2 = (int)(o >> 16 & 0xff), g2 = (int)(o >> 8 & 0xff), b2 = (int)(o & 0xff);
  if (p->ov_sub) {
    r = r > r2 ? r - r2 : 0, g = g > g2 ? g - g2 : 0, b = b > b2 ? b - b2 : 0;
  } else {
    r += r2, g += g2, b += b2;
  }
  if (half)
    r >>= 1, g >>= 1, b >>= 1;
  r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
  return (uint32)(r << 16 | g << 8 | b);
}

typedef struct BackgroundJob {
  const Pane *p;
  const R3dFrame *f;
  float horizon, top;        // the sky: a little darker at the top, down to the horizon row
  uint32 ground;             // the ground's color near the camera
} BackgroundJob;

// What shows where nothing is drawn. Below the horizon it is the ground: the
// backdrop color (the game's own ground wherever a background tile is
// see-through, as outdoors at Skull Woods), fogged as the ground there would
// be. Above it, the fog, or in first person the sky.
static void BackgroundRows(int y0, int y1, void *ctx) {
  const BackgroundJob *j = (const BackgroundJob *)ctx;
  const Pane *p = j->p;
  const R3dFrame *f = j->f;
  const R3dCamera *c = &f->cam;
  float inv_live = 1.0f / (f->live_fog1 - f->live_fog0);
  bool dist = f->dist_fog1 > f->dist_fog0 && f->dist_fog1 > 0.0f;
  for (int y = y0; y < y1; y++) {
    uint32 *row = g_bg + y * kPicW;
    float k = (float)y < j->horizon ? j->top + (1.0f - j->top) * (float)y / j->horizon : 1.0f;
    uint32 sky = ScaleRgb(f->fog_rgb, (int)(k * 256.0f), 256);
    float vy = -((float)y + 0.5f - c->cy) / (c->focal * c->stretch_y);
    float ry[3];
    for (int i = 0; i < 3; i++)
      ry[i] = c->fwd[i] + c->up[i] * vy;
    for (int x = 0; x < p->w; x++) {
      float vx = ((float)x + 0.5f - c->cx) / c->focal;
      float dx = ry[0] + c->right[0] * vx, dy = ry[1] + c->right[1] * vx, dz = ry[2] + c->right[2] * vx;
      float t = dz < -1e-6f ? -c->eye[2] / dz : -1.0f;  // (where the ray meets z = 0)
      if (!(t > 0.0f && t < 1e6f)) {
        row[x] = sky;
        continue;
      }
      float gx = c->eye[0] + t * dx, gy = c->eye[1] + t * dy;
      float ox = f->live_x0 - gx > gx - f->live_x1 ? f->live_x0 - gx : gx - f->live_x1;
      float oy = f->live_y0 - gy > gy - f->live_y1 ? f->live_y0 - gy : gy - f->live_y1;
      ox = ox > 0.0f ? ox : 0.0f, oy = oy > 0.0f ? oy : 0.0f;
      float fog = ox + oy > 0.0f ? Clampf((sqrtf(ox * ox + oy * oy) - f->live_fog0) * inv_live, 0.0f, 1.0f) : 0.0f;
      if (dist) {
        float d = t * sqrtf(dx * dx + dy * dy + dz * dz);
        float fd = Clampf((d - f->dist_fog0) / (f->dist_fog1 - f->dist_fog0), 0.0f, 1.0f);
        fog = fd > fog ? fd : fog;
      }
      row[x] = LerpRgb(j->ground, f->fog_rgb, fog * fog * (3.0f - 2.0f * fog));
    }
  }
}

static void MakeBackground(const Pane *p, const R3dFrame *f, float b) {
  const R3dCamera *cam = &f->cam;
  BackgroundJob j;
  j.p = p, j.f = f;
  float hx = cam->fwd[0], hy = cam->fwd[1], len = sqrtf(hx * hx + hy * hy);
  j.horizon = (float)p->h;
  if (len > 1e-4f) {
    hx /= len, hy /= len;
    float yc = hx * cam->up[0] + hy * cam->up[1], zc = hx * cam->fwd[0] + hy * cam->fwd[1];
    if (zc > 0.05f)
      j.horizon = cam->cy - cam->focal * cam->stretch_y * yc / zc;
  }
  j.horizon = Clampf(j.horizon, 1.0f, (float)p->h);
  j.top = 1.0f - 0.15f * Clampf(b, 0.0f, 1.0f);
  // (Indoors in first person no ground goes on past the room: it is the fog.)
  j.ground = p->info.indoors ? LerpRgb(p->backdrop, f->fog_rgb, Clampf(b, 0.0f, 1.0f)) : p->backdrop;
  Hd2d_ParallelFor(p->h, BackgroundRows, &j);
}

// Death Mountain's clouds under everything, as the PPU mixes them into the backdrop.
static void MakeBelowOverlay(const Pane *p) {
  for (int y = 0; y < p->h; y++) {
    uint32 *row = g_bg + y * kPicW;
    const uint8 *ov = p->overlay + y * kPicW;
    for (int x = 0; x < p->w; x++)
      row[x] = OverlayMath(p, p->bg_lut[0], ov[x]);
  }
}

// The test camera's background: the backdrop (with the clouds under it)
// where the 2D picture shows the world, black in the columns and rows it blanks.
static void MakeTestBackground(const Pane *p) {
  if (p->ov_mode == 2)
    MakeBelowOverlay(p);
  for (int y = 0; y < p->h; y++) {
    uint32 *row = g_bg + y * kPicW;
    for (int x = 0; x < p->w; x++) {
      if (y >= p->shown_h || x < p->shown_x0 || x >= p->shown_x1)
        row[x] = 0;
      else if (p->ov_mode != 2)
        row[x] = p->bg_lut[0];
    }
  }
}

typedef struct BlurJob {
  const float *depth;
  int w;
  float focus_inv, focus, k;
} BlurJob;

// Depth of field from the real depth: in focus around the camera's target.
static void BlurRows(int y0, int y1, void *ctx) {
  const BlurJob *j = (const BlurJob *)ctx;
  for (int y = y0; y < y1; y++) {
    const float *d = j->depth + y * j->w;
    uint8 *out = g_blur + y * j->w;
    for (int x = 0; x < j->w; x++) {
      float z = d[x] > 0.0f ? 1.0f / d[x] : 1e9f;  // (the depth buffer holds 1 / depth; 0: nothing)
      out[x] = (uint8)(j->k * SmoothStep(0.08f, 0.35f, fabsf(z - j->focus) * j->focus_inv));
    }
  }
}

static void FinishPane(Pane *p, int view) {
  bool test = g_test_camera;
  int w = p->w, h = p->h;
  const Cam3d *c = &p->cam3d;
  float b = test ? 0.0f : SmoothStep(0.0f, 1.0f, c->blend);
  R3dFrame *f = &g_frame;
  memset(f, 0, sizeof(*f));
  if (test)
    Cam3d_MakeTestView(&p->pose, &f->cam);
  else
    Cam3d_MakeView(c, &p->pose, &p->tuning, &f->cam);
  f->w = w, f->h = h;

  // The scenes. (In split screen a later pane's world can take the slot of
  // a scene: then it is left out for this picture.)
  Hd3dSceneInfo now;
  Hd3dScene_GetInfo(p->scene, &now);
  Hd3dScene *scene = SameScene(&now, &p->info) ? p->scene : NULL;
  Hd3dScene *prev = NULL;
  if (p->prev) {
    Hd3dScene_GetInfo(p->prev, &now);
    prev = SameScene(&now, &p->prev_info) ? p->prev : NULL;
  }
  Hd3dScene_Textures(scene, p->bg_lut, &f->tex[0], &f->tex[1]);
  if (prev)
    Hd3dScene_Textures(prev, p->bg_lut, &f->tex[2], &f->tex[3]);
  R3dTexture *at = &f->tex[kAtlasSlot];
  at->texels = g_atlas, at->lut = g_sprite_lut;
  at->origin_x = at->origin_y = 0;
  at->log2_w = 9, at->log2_h = 8;  // kHd3dAtlasW x kHd3dAtlasH
  at->subtract_from = 65;          // the game's darkness reaches OBJ palettes 4-7

  // How far past the original screen the scene shows: a little at the
  // classic view (the game runs nothing further out), more once the camera
  // is turned or in first person, where the room's or area's own walls and
  // ground are worth seeing although no enemy moves out there. (Before, tall
  // things just past the screen stood as grey blocks of mist next to a
  // turned camera, and the far walls of a room were lost in first person.)
  float yaw = c->yaw - 360.0f * floorf((c->yaw + 180.0f) / 360.0f);  // (-180..180)
  float turn = fabsf(yaw), tilt = fabsf(c->pitch - 45.0f);
  float wide = test ? 0.0f : Clampf((((turn > tilt ? turn : tilt) - 4.0f) / 16.0f), 0.0f, 1.0f);
  wide = b > wide ? b : wide;
  int margin = kHd3dFogMargin + (int)(wide * (float)(kHd3dWideMargin - kHd3dFogMargin));
  memcpy(p->rect, p->live, sizeof(p->rect));
  Grow(p->rect, margin);
  Intersect(p->rect, p->reach);

  Hd3dEmit e;
  memset(&e, 0, sizeof(e));
  e.cam = &f->cam, e.w = w, e.h = h;
  // The way toward the eye, level.
  float vx = -f->cam.fwd[0], vy = -f->cam.fwd[1], vl = sqrtf(vx * vx + vy * vy);
  e.view_dir[0] = vl > 1e-4f ? vx / vl : 0.0f, e.view_dir[1] = vl > 1e-4f ? vy / vl : 1.0f;
  e.fp_blend = b;
  e.test_camera = test;
  e.rise = p->rise;
  memcpy(e.rect, p->rect, sizeof(e.rect));
  e.tex_bg2 = 0, e.tex_bg1 = 1;
  e.fade = !test && g_hd3d.fade_scenery && Cam3d_IsTurned(c);
  memcpy(e.fade_a, p->link, sizeof(e.fade_a));
  memcpy(e.fade_b, f->cam.eye, sizeof(e.fade_b));
  int nq = scene ? Hd3dScene_Emit(scene, &e, g_quads, kHd3dMaxQuads) : 0;
  nq = IntMax(0, IntMin(nq, kHd3dMaxQuads));
  if (prev) {
    e.rise = p->prev_rise;
    memcpy(e.rect, p->prev_rect, sizeof(e.rect));
    e.tex_bg2 = 2, e.tex_bg1 = 3;
    e.fade = false;
    int n = Hd3dScene_Emit(prev, &e, g_quads + nq, kHd3dMaxQuads - nq);
    nq += IntMax(0, IntMin(n, kHd3dMaxQuads - nq));
  }
  int nd = 0;
  int nc = Hd3dSprites_Build(p->sf, scene, &f->cam, kAtlasSlot, g_atlas, g_sprite_lut, g_cards, kHd3dMaxCards,
                             g_quads + nq, IntMin(kHd3dMaxDecals, kHd3dMaxQuads - nq), &nd);
  nq += IntMax(0, IntMin(nd, IntMin(kHd3dMaxDecals, kHd3dMaxQuads - nq)));
  f->quads = g_quads, f->num_quads = nq;
  f->cards = g_cards, f->num_cards = IntMax(0, IntMin(nc, kHd3dMaxCards));

  // Light, darkness and fog.
  f->ambient[0] = f->ambient[1] = f->ambient[2] = p->ambient;
  f->lights = p->lights, f->num_lights = p->num_lights;
  f->light_max = 1.25f;
  f->card_min_light = p->card_min;
  memcpy(f->subtract, p->subtract, sizeof(f->subtract));
  f->subtract_lights = p->subtract_lights;
  f->subtract_cards = p->subtract_cards;
  uint32 fog = LerpRgb(p->fog_tp, p->fog_fp, b);
  if (test) {
    // (The oblique test camera shows exactly the 2D picture: the backdrop, no fog.)
    f->fog_rgb = p->bg_lut[0];
    MakeTestBackground(p);
    f->background = g_bg, f->background_pitch = kPicW;
  } else {
    f->fog_rgb = fog;
    f->live_x0 = (float)p->live[0], f->live_y0 = (float)p->live[1];
    f->live_x1 = (float)p->live[2], f->live_y1 = (float)p->live[3];
    f->live_fog0 = 8.0f + 88.0f * wide, f->live_fog1 = (float)margin;
    if (b > 0.01f)
      f->dist_fog0 = 160.0f / b, f->dist_fog1 = 320.0f / b;  // (first person: it comes in with the blend)
    if (p->ov_mode == 2 && b <= 0.0f)
      MakeBelowOverlay(p);
    else
      MakeBackground(p, f, b);
    f->background = g_bg, f->background_pitch = kPicW;
  }
  f->see_through = p->see_through;
  f->xray = p->sf->xray_player >= 0;

  R3dTarget t;
  t.pixels = p->px, t.pitch = p->pitch;
  t.depth = g_depth;
  t.cls = g_cls, t.cls_pitch = kPicW;
  R3dStats st = { 0, 0 };
  R3d_Draw(g_work, f, &t, Hd2d_ParallelFor, &st);

  // The outdoor overlay over everything (the HUD comes after).
  if (p->ov_mode == 1) {
    for (int y = 0; y < h; y++) {
      uint32 *row = (uint32 *)(p->px + y * p->pitch);
      const uint8 *ov = p->overlay + y * kPicW, *cls = g_cls + y * kPicW;
      for (int x = 0; x < w; x++)
        if (cls[x] != kR3dClass_Sprite || p->ov_obj)
          row[x] = OverlayMath(p, row[x], ov[x]);
    }
  }
  // The HUD and text boxes exactly as the 2D picture has them.
  for (int y = 0; y < h; y++) {
    uint32 *row = (uint32 *)(p->px + y * p->pitch);
    const uint32 *src = p->pic2d + y * kPicW;
    const uint8 *layer = p->layer2d + y * kPicW;
    uint8 *cls = g_cls + y * kPicW;
    for (int x = 0; x < w; x++)
      if (layer[x] == 2)
        row[x] = src[x], cls[x] = kR3dClass_Hud;
  }
  // Coming from 2D: the 2D picture fades out over the first frames.
  if (p->fade_in < 256) {
    uint32 a = (uint32)IntMax(p->fade_in, 0);
    for (int y = 0; y < h; y++) {
      uint32 *row = (uint32 *)(p->px + y * p->pitch);
      const uint32 *src = p->pic2d + y * kPicW;
      for (int x = 0; x < w; x++) {
        uint32 n = row[x], o = src[x];
        uint32 rb = ((n & 0xff00ff) * a + (o & 0xff00ff) * (256 - a)) >> 8 & 0xff00ff;
        uint32 g = ((n & 0xff00) * a + (o & 0xff00) * (256 - a)) >> 8 & 0xff00;
        row[x] = rb | g;
      }
    }
  }
  // The 2D-HD look, with depth of field from the real depth.
  if (g_hd2d.enabled) {
    float focus = 96.0f;
    if (b < 1.0f) {
      float sx, sy, d;
      R3d_Project(&f->cam, p->pose.target, &sx, &sy, &d);
      focus = d > 1.0f ? d + (96.0f - d) * b : 96.0f;
    }
    BlurJob j;
    j.depth = g_depth, j.w = w;
    j.focus = focus, j.focus_inv = 1.0f / focus;
    j.k = 255.0f * g_hd2d.depth_of_field / 100.0f;
    Hd2d_ParallelFor(h, BlurRows, &j);
    Hd2dPostIn in;
    in.cls = g_cls, in.cls_pitch = kPicW;
    in.blur = g_blur;
    in.lit = true;
    in.cheap = Hd3dInput_PaneCount() >= 3;
    Hd2d_Post(p->px, p->pitch, w, h, view, &in);
  }
  // Kept for a fade back to 2D, and for the overlay's projections.
  p->last_ok = !test;
  if (p->last_ok) {
    for (int y = 0; y < h; y++)
      memcpy(p->last + y * kPicW, p->px + y * p->pitch, (size_t)w * 4);
    p->last_w = w, p->last_h = h;
  }
  p->cam = f->cam;
  p->cam_ok = true;
  p->quads = st.quads_drawn, p->cards = st.cards_drawn;
}

// ---- Drawing ----

bool Hd3d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  g_hd3d_tagging = g_hd3d.enabled;
  Pane *p = PaneOf(view);
  if (!p)
    return false;
  bool same_world;
  int step = Tick(p, &same_world);
  p->pending = false;
  Ppu *ppu = g_zenv.ppu;
  int w = 256 + 2 * ppu->extraLeftRight, h = render_flags & kPpuRenderFlags_Height240 ? 240 : 224;
  int why = Why2D(ppu, render_flags, w, h, pixel_buffer, pitch);
  if (why == kHd3dWhy_None && !(AllocShared() && AllocPane(p, OverlayMode(ppu) != 0)))
    why = kHd3dWhy_Memory;
  Hd3dScene *scene = NULL;
  if (why == kHd3dWhy_None) {
    // What the scene must have decoded: everything this picture could show
    // (the full width: the 2D pass hasn't said yet which side columns it
    // blanks), the fog margin, and above it the rows tall surfaces show.
    int32 shown[4], reach[4], decode[4];
    shown[0] = BG2HOFS_copy2 - ppu->extraLeftRight, shown[1] = BG2VOFS_copy2 + 1;
    shown[2] = shown[0] + w, shown[3] = shown[1] + h;
    ReachRect(reach, shown, h);
    memcpy(decode, shown, sizeof(decode));
    Grow(decode, kHd3dWideMargin);
    Intersect(decode, reach);
    decode[1] -= kHd3dMaxRelief;
    why = kHd3dWhy_Room;
    scene = Hd3dScene_Update(decode, &why);
    if (!scene && why == kHd3dWhy_None)
      why = kHd3dWhy_Room;
    if (scene)
      why = kHd3dWhy_None;
  }
  p->why = (uint8)why;
  if (why != kHd3dWhy_None)
    return false;

  p->px = pixel_buffer, p->pitch = pitch, p->w = w, p->h = h;
  ReadWorld(p, render_flags, step, same_world, scene, Hd3dInput_PaneLocal(view));
  p->is3d = true;
  p->tick_3d = p->ticks;
  if (g_deferred) {
    p->pending = true;
    return true;
  }
  FinishPane(p, view);
  return true;
}

void Hd3d_After2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  Pane *p = PaneOf(view);
  if (!p)
    return;
  bool was3d = p->is3d;
  p->is3d = false;
  p->cam_ok = false;
  p->had_2d = true;
  p->tick_2d = p->ticks;
  // Just out of 3D: the last 3D picture fades away over the 2D one.
  uint32 since = p->ticks - p->tick_3d;
  Ppu *ppu = g_zenv.ppu;
  int w = 256 + 2 * ppu->extraLeftRight, h = render_flags & kPpuRenderFlags_Height240 ? 240 : 224;
  if (!p->last_ok || since >= kFadeOutTicks || g_test_camera || PpuGetCurrentRenderScale(ppu, render_flags) != 1 ||
      w != p->last_w || h != p->last_h || ((uintptr_t)pixel_buffer & 3) || (pitch & 3) || pitch < (size_t)w * 4) {
    if (!was3d && since >= kFadeOutTicks)
      p->last_ok = false;
    return;
  }
  uint32 a = 256 - since * 256 / kFadeOutTicks;  // (how much of the 3D picture still shows)
  for (int y = 0; y < h; y++) {
    uint32 *row = (uint32 *)(pixel_buffer + y * pitch);
    const uint32 *src = p->last + y * kPicW;
    for (int x = 0; x < w; x++) {
      uint32 n = row[x], o = src[x];
      uint32 rb = ((o & 0xff00ff) * a + (n & 0xff00ff) * (256 - a)) >> 8 & 0xff00ff;
      uint32 g = ((o & 0xff00) * a + (n & 0xff00) * (256 - a)) >> 8 & 0xff00;
      row[x] = rb | g;
    }
  }
}

void Hd3d_BeginPanes(bool deferred) {
  g_deferred = deferred;
  g_num_stepped = 0;
  for (int v = 0; v < kHd3dMaxPanes; v++)
    g_panes[v].pending = false;
}

void Hd3d_FinishPanes(void) {
  for (int v = 0; v < kHd3dMaxPanes; v++) {
    Pane *p = &g_panes[v];
    if (p->pending) {
      p->pending = false;
      FinishPane(p, v);
    }
  }
  g_deferred = false;
  g_num_stepped = 0;
}

bool Hd3d_PaneIs3D(int view) {
  const Pane *p = PaneOf(view);
  return p && p->is3d && g_hd3d.enabled;
}

bool Hd3d_LocalSees3D(int local) {
  if (!g_hd3d.enabled)
    return false;
  int n = IntMin(Hd3dInput_PaneCount(), kHd3dMaxPanes);
  for (int v = 0; v < n; v++)
    if (Hd3dInput_PaneLocal(v) == local && g_panes[v].is3d)
      return true;
  return false;
}

// ---- Overlay (ui.c) ----

bool Hd3d_Project(int view, int x, int y, int above, int *sx, int *sy, bool *behind) {
  *behind = false;
  const Pane *p = PaneOf(view);
  if (!p || !p->is3d || !p->cam_ok || !g_hd3d.enabled)
    return false;
  // (The ground is taken at the upper floor's level; outdoors at its terrace,
  // where the cards stand. Ui_Draw runs after Hd3d_FinishPanes, so the
  // pane's scene is still the one it drew.)
  int g = Hd3dScene_StandZ(p->scene, 0, x, y);
  float pt[3], fx, fy, depth;
  pt[0] = (float)x, pt[1] = (float)(y + g), pt[2] = (float)(g + above);
  bool front = R3d_Project(&p->cam, pt, &fx, &fy, &depth);
  fx = Clampf(fx, -1e6f, 1e6f), fy = Clampf(fy, -1e6f, 1e6f);
  *sx = (int)floorf(fx), *sy = (int)floorf(fy);
  *behind = !front;
  return front;
}

float Hd3d_ScaleAt(int view, int x, int y) {
  const Pane *p = PaneOf(view);
  if (!p || !p->is3d || !p->cam_ok || !g_hd3d.enabled)
    return 1.0f;
  int g = Hd3dScene_StandZ(p->scene, 0, x, y);
  float pt[3], fx, fy, depth;
  pt[0] = (float)x, pt[1] = (float)(y + g), pt[2] = (float)g;
  if (!R3d_Project(&p->cam, pt, &fx, &fy, &depth))
    return 1.0f;
  return R3d_ScaleAt(&p->cam, depth);
}

// ---- (Tests) ----

void Hd3d_SetTestCamera(bool on) {
  g_test_camera = on;
}

bool Hd3d_TestCameraOn(void) {
  return g_test_camera;
}

const char *Hd3d_LastWhy2D(int view) {
  const Pane *p = PaneOf(view);
  if (!p || !p->drawn)
    return kHd3dWhyNames[kHd3dWhy_Off];
  return kHd3dWhyNames[p->is3d ? kHd3dWhy_None : p->why < kHd3dWhy_Count ? p->why : kHd3dWhy_Off];
}

void Hd3d_GetStats(int view, int *quads, int *cards) {
  const Pane *p = PaneOf(view);
  bool ok = p && p->is3d;
  *quads = ok ? p->quads : 0;
  *cards = ok ? p->cards : 0;
}

void Hd3d_DumpScene(const char *ppm_path) {
  int32 area[4];
  AreaRect(area);
  int why = kHd3dWhy_Room;
  Hd3dScene *s = Hd3dScene_Update(area, &why);
  if (s)
    Hd3dScene_Dump(s, ppm_path);
}
