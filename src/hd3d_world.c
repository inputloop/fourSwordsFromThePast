// The 3D view: rooms and overworld areas as 3D scenes (see hd3d_internal.h).
//
// A scene is one dungeon room (512 x 512 px) or overworld area (512 or 1024
// px square) of one world. It is rebuilt from the game's own data, which it
// only ever reads (RAM and VRAM are never written):
// - Texels: one palette index per pixel (0 is see-through) in world space,
//   decoded from the RAM tilemaps (outdoors the map16 grid) and the BG tiles
//   in VRAM. Colors come later through the caller's LUT, so palette changes
//   and fades cost nothing here.
// - Cells (8 x 8 px) get a class from their collision attribute: ground,
//   pit, low or tall solid, empty or void.
// - A dungeon room's shell (the walls around the floor) becomes hinged
//   panels that stand up or lie flat (exactly as painted) depending on where
//   the camera is.
// - Everything else is a relief: a surface point (x, y, z) shows the picture
//   texel (x, y - z). Under the parallel oblique test camera (the screen
//   shows (x, y - z)) the picture is then exactly the original whatever
//   height each surface gets; wrong guesses only show once the camera turns.
//
// Emission rectangle: Hd3dScene_Update reads the reachable rectangle of the
// room or area (where the game's camera can go, plus what the screen shows
// now) and keeps it in the scene. The caller passes the live rectangle (the
// picture the 2D view shows) grown by kHd3dFogMargin in Hd3dEmit.rect; the
// scene clips it to its reachable rectangle, so hidden quadrants never show.
// A frozen scene keeps the reachable rectangle it last read. The decode
// rectangle of Hd3dScene_Update is that same rectangle plus kHd3dMaxRelief
// px north (relief and inpainting take texels from north of a surface);
// outdoors only cells in it are decoded, rooms are decoded whole. Call
// Hd3dScene_Textures before Hd3dScene_Emit: the lids on walls take the
// void's color from its LUT.
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "assets.h"
#include "instance.h"
#include "overworld.h"

enum {
  kMaxN = 128,               // cells per side (1024 px areas)
  kMaxCells = kMaxN * kMaxN,
  kChunkCells = 8,           // a culling chunk is 8 x 8 cells (64 px)
  kMaxLines = 1024,          // panel foot lines per scene
  kMaxPanelCells = 16384,
  kEmptyChar = 0x1ec,        // the game's empty BG tile
  kThinReach = 6,            // the thin test looks this far for ground
  kVoidTexels = 56,          // a solid cell with this many see-through or near-black texels is void
  kCornerReach = kHd3dBandMax,
};

// Cell classes.
enum { kCls_Ground, kCls_Pit, kCls_Low, kCls_Wall, kCls_Empty, kCls_Void };

// Cell flags (Layer.cf).
enum {
  kCf_Decoded = 1,           // its texels are up to date
  kCf_Anim = 2,              // it uses the animated BG tiles
  kCf_Prio = 4,              // drawn over sprites (the tile's priority bit)
  kCf_Blank = 8,             // every texel is see-through
  kCf_Holes = 0x20,          // some texels are see-through
  kCf_Empty = 0x40,          // the empty tile: nothing on this layer
  kCf_Thin = 0x80,           // a thin solid (a railing): a prop, never a wall
  kCf_Shell = 0x100,         // part of a room's shell (its walls)
  kCf_Panel = 0x200,         // drawn as part of a wall panel
  kCf_Overhead = 0x400,      // outdoors: walkable, but drawn over Links (canopies, eaves)
};

// What a picture cell becomes in the relief (Layer.pk).
// (Level: a prop without a foot, flat at the base; Flat: the void and shell
// cells no panel took, flat and without faces.)
enum { kPk_Hole, kPk_Ground, kPk_Level, kPk_Flat, kPk_Top, kPk_Face, kPk_PitWall, kPk_PitFloor, kPk_Panel };

// World cell flags (Layer.wf). Bits 4-6: how many 16 px steps north an
// inpainted cell takes its texels from.
enum { kWf_Set = 1, kWf_NoFace = 2, kWf_Empty = 4, kWf_Inpaint = 8 };

// Panel directions: where the wall lies from its floor (ties go in this order).
enum { kDir_N, kDir_W, kDir_E, kDir_S, kDir_None = 0xff };
static const int8 kDirX[4] = { 0, -1, 1, 0 }, kDirY[4] = { -1, 0, 0, 1 };

typedef struct PanelLine {   // the foot line of a wall: panels hinge on it
  uint8 layer, dir;
  uint8 bmax;                // its widest band (east, west and south walls stretch by it)
  int16 hinge;               // picture px from the scene origin: y (N, S) or x (W, E)
} PanelLine;

typedef struct PanelCell {
  uint16 cell;               // picture cell (row * n + column)
  uint16 line;
  uint8 d;                   // band distance: 1 next to the floor
  uint8 other;               // a corner: the direction of the other panel it can go with (kDir_None: not a corner)
} PanelCell;

typedef struct Layer {
  uint16 *word;              // tilemap word of each cell
  uint8 *attr;               // collision attribute
  uint16 *cf;                // kCf_*
  uint8 *cls;                // kCls_*
  uint8 *pk;                 // kPk_*
  int8 *wz;                  // world cells: the surface height (px)
  uint8 *wf;                 // world cells: kWf_*
  int16 *rec[2];             // panel cells of each picture cell (-1: none; corners have two)
  int8 *zlo, *zhi;           // per chunk: the lowest and highest relief (zlo > zhi: none)
  uint8 *tex;                // size x size texels
} Layer;

struct Hd3dScene {
  bool used;
  bool frozen;               // no longer its world's current scene: never read from the game again
  bool ready;                // the geometry is built
  bool drawn_flat;           // drawn before it was ready (it rises in)
  bool geom_dirty;
  bool attr_seen;            // src_attr holds a complete table
  bool indoors;
  uint8 bg1_role;
  uint8 num_layers;          // layers with texels
  int inst;
  int32 origin_x, origin_y, size;
  int n;                     // cells per side
  uint32 stamp, gen;
  float rise;
  int16 wall_height;
  int16 bg1_dx, bg1_dy;      // a parallax BG1's scroll against BG2
  int32 reach[4];            // the reachable rectangle (world px: x0, y0, x1, y1)
  uint32 void_rgb;           // the void's color as the caller's LUT has it (lids of east, west and south walls)
  int16 void_index;          // the void's texel: the room's filler, or 0 (see-through)
  uint32 hash_rest, hash_anim;
  uint16 anim_lo, anim_hi;   // chars of the animated range (anim_lo 0xffff: none)
  uint8 dark[16];            // BG colors that are near-black (bits)
  uint16 cgram[128];         // the BG colors when last read (for the dump)
  uint16 src_map[2][4096];   // the game's tilemaps as last read (outdoors: the map16 grid in [0])
  uint8 src_attr[2][4096];
  Layer layer[2];
  PanelLine *lines;
  int num_lines;
  PanelCell *pcells;
  int num_pcells;
  // The allocation (kept while the shape stays the same).
  void *mem;
  int32 mem_size;
  uint8 mem_layers;
  bool mem_panels;
};

static Hd3dScene g_scenes[kHd3dSlots];
static uint8 g_cur[kInstMax], g_prev[kInstMax];  // slot + 1 (0: none)
static uint32 g_prev_gen[kInstMax];
static uint32 g_clock, g_gens;
static uint8 g_class[2][256];                     // [indoors][attribute]: kCls_*
static bool g_tables_ready;
static uint64 g_spread[2][256];                   // a bitplane byte over 8 bytes (byte k: pixel k), drawn and mirrored

// Scratch for building (one scene at a time, main thread).
static int16 g_list[kMaxCells];
static uint8 g_mark[kMaxCells];
static uint8 g_own_d[kMaxCells];
static int16 g_own_line[kMaxCells];
static int16 g_strip_line[4][kMaxCells];          // the band strip a floor cell starts, per direction (-1: none)
static int16 g_line_of[2][4][kMaxN + 1];          // [layer][dir][hinge / 8]: the line (-1: none)
static uint8 g_dist[kMaxCells];                   // chessboard distance to ground
static int8 g_col_z[kMaxN];
static int16 g_col_row[kMaxN];

static void InitTables(void) {
  for (int in = 0; in < 2; in++) {
    for (int a = 0; a < 256; a++) {
      uint8 c = kCls_Wall;  // everything else solid: 01, 0B, 10-13, 18-1B, 26, 27, 43, 6C-6F, C0-CF, F0-FF
      if (a == 0x00 || (a >= 0x05 && a <= 0x0a) || (a >= 0x0c && a <= 0x0f) || (a >= 0x14 && a <= 0x17) ||
          (a >= 0x1c && a <= 0x1f) || (a >= 0x21 && a <= 0x25) || (a >= 0x30 && a <= 0x3f) || a == 0x40 ||
          a == 0x41 || a == 0x45 || (a >= 0x47 && a <= 0x4b) || (a >= 0x5e && a <= 0x62) || (a >= 0x64 && a <= 0x66) ||
          (a >= 0x68 && a <= 0x6b) || (a >= 0x80 && a <= 0xaf) || a == 0xbe || a == 0xbf || (a >= 0xd0 && a <= 0xef))
        c = kCls_Ground;  // (08 deep water is ground in v1; 1C only on a non-empty tile)
      else if (a == 0x20 || (a >= 0xb0 && a <= 0xbd))
        c = kCls_Pit;
      else if (a == 0x02 || a == 0x03 || a == 0x42 || a == 0x44 || a == 0x46 || (a >= 0x50 && a <= 0x5d) ||
               a == 0x63 || a == 0x67 || (a >= 0x70 && a <= 0x7f))
        c = kCls_Low;
      if (a == 0x04)
        c = in ? kCls_Low : kCls_Ground;  // outdoors: thick grass
      if (a >= 0x28 && a <= 0x2f)
        c = in ? kCls_Low : kCls_Ground;  // indoors the rims of walls, outdoors ledges
      if (a >= 0x4c && a <= 0x4f && !in)
        c = kCls_Ground;
      if (a == 0x0b && !in)
        c = kCls_Ground;  // outdoors deep water
      g_class[in][a] = c;
    }
  }
  for (int b = 0; b < 256; b++) {
    uint64 drawn = 0, mirrored = 0;
    for (int k = 0; k < 8; k++) {
      drawn |= (uint64)(b >> (7 - k) & 1) << (8 * k);
      mirrored |= (uint64)(b >> k & 1) << (8 * k);
    }
    g_spread[0][b] = drawn, g_spread[1][b] = mirrored;
  }
  g_tables_ready = true;
}

static int FloorDiv8(int32 v) {
  return v >= 0 ? (int)(v / 8) : -(int)((7 - v) / 8);
}

static int LayerBase(const Hd3dScene *s, int layer) {
  return layer && s->bg1_role == kHd3dBg1_LowerFloor ? kHd3dLowerFloor : 0;
}

static int GeometryLayers(const Hd3dScene *s) {
  return s->bg1_role == kHd3dBg1_LowerFloor ? 2 : 1;
}

// ---- Slots ----

typedef struct SceneKey {
  bool indoors;
  uint8 role;
  int32 x, y, size;
} SceneKey;

static uint8 RoleOf(uint8 prop) {
  switch (prop) {
  case 6: return kHd3dBg1_LowerFloor;
  case 3: return kHd3dBg1_OnTop;
  case 4: return kHd3dBg1_Water;
  case 7: return kHd3dBg1_Light;
  case 1: case 5: return kHd3dBg1_Far;
  default: return kHd3dBg1_None;  // 0 none, 2 dark (Ganon and Agahnim: the caller draws those in 2D)
  }
}

static bool ReadKey(SceneKey *k) {
  k->indoors = player_is_indoors != 0;
  if (k->indoors) {
    k->x = dung_loade_bgoffs_h_copy & ~0x1ff;
    k->y = dung_loade_bgoffs_v_copy & ~0x1ff;
    k->size = 512;
    // In a dark room with the Lamp, BG1 holds the light cone's mask.
    k->role = hdr_dungeon_dark_with_lantern ? kHd3dBg1_None : RoleOf(dung_hdr_bg2_properties);
  } else {
    k->size = (overworld_offset_mask_y | 0xf) + 1;
    if (k->size != 512 && k->size != 1024)
      return false;
    k->x = (int32)overworld_offset_base_x << 3;
    k->y = overworld_offset_base_y;
    k->role = kHd3dBg1_None;
  }
  return true;
}

static bool KeyMatches(const Hd3dScene *s, int inst, const SceneKey *k) {
  return s->used && s->inst == inst && s->indoors == k->indoors && s->bg1_role == k->role &&
         s->origin_x == k->x && s->origin_y == k->y && s->size == k->size;
}

static void FreeScene(Hd3dScene *s) {
  free(s->mem);
  s->mem = NULL;
  s->used = false;
}

static void *Carve(uint8 **p, size_t bytes) {
  void *r = *p;
  *p += (bytes + 15) & ~(size_t)15;
  return r;
}

// Lays out the scene's arrays in one block (kept while its shape stays).
static bool AllocScene(Hd3dScene *s, int32 size, int layers, bool panels) {
  int n = size / 8, cells = n * n, chunks = (n / kChunkCells) * (n / kChunkCells);
  size_t per_layer = (size_t)cells * (2 + 1 + 2 + 1 + 1 + 1 + 1 + 4) + (size_t)chunks * 2 + (size_t)size * size +
                     16 * 12;  // (the arrays below, each rounded up to 16 bytes)
  size_t total = per_layer * layers +
                 (panels ? sizeof(PanelLine) * kMaxLines + sizeof(PanelCell) * kMaxPanelCells + 32 : 0);
  bool same = s->mem && s->mem_size == size && s->mem_layers == layers && s->mem_panels == panels;
  if (!same) {
    FreeScene(s);
    s->mem = calloc(1, total);
    if (!s->mem)
      return false;
    s->mem_size = size, s->mem_layers = (uint8)layers, s->mem_panels = panels;
  } else {
    memset(s->mem, 0, total);
  }
  uint8 *p = (uint8 *)s->mem;
  memset(s->layer, 0, sizeof(s->layer));
  for (int L = 0; L < layers; L++) {
    Layer *ly = &s->layer[L];
    ly->word = (uint16 *)Carve(&p, cells * 2);
    ly->attr = (uint8 *)Carve(&p, cells);
    ly->cf = (uint16 *)Carve(&p, cells * 2);
    ly->cls = (uint8 *)Carve(&p, cells);
    ly->pk = (uint8 *)Carve(&p, cells);
    ly->wz = (int8 *)Carve(&p, cells);
    ly->wf = (uint8 *)Carve(&p, cells);
    ly->rec[0] = (int16 *)Carve(&p, cells * 2);
    ly->rec[1] = (int16 *)Carve(&p, cells * 2);
    ly->zlo = (int8 *)Carve(&p, chunks);
    ly->zhi = (int8 *)Carve(&p, chunks);
    ly->tex = (uint8 *)Carve(&p, (size_t)size * size);
  }
  s->lines = panels ? (PanelLine *)Carve(&p, sizeof(PanelLine) * kMaxLines) : NULL;
  s->pcells = panels ? (PanelCell *)Carve(&p, sizeof(PanelCell) * kMaxPanelCells) : NULL;
  return true;
}

static void SetDungeonCell(Hd3dScene *s, int i);
static void SetOverworldCell(Hd3dScene *s, int i, uint16 m16, int sub);

static bool InitScene(Hd3dScene *s, int inst, const SceneKey *k) {
  uint8 role = k->role;
  int layers = k->indoors && (role == kHd3dBg1_LowerFloor || role == kHd3dBg1_Water || role == kHd3dBg1_Light ||
                              role == kHd3dBg1_Far) ? 2 : 1;
  if (!AllocScene(s, k->size, layers, k->indoors))
    return false;
  s->used = true;
  s->frozen = s->ready = s->drawn_flat = s->attr_seen = false;
  s->geom_dirty = true;
  s->inst = inst;
  s->indoors = k->indoors;
  s->bg1_role = role;
  s->num_layers = (uint8)layers;
  s->origin_x = k->x, s->origin_y = k->y, s->size = k->size;
  s->n = k->size / 8;
  s->gen = ++g_gens;
  s->rise = 0;
  s->wall_height = 48;
  s->bg1_dx = s->bg1_dy = 0;
  s->hash_rest = s->hash_anim = 0;
  s->anim_lo = 0xfffe, s->anim_hi = 0;  // (no range matches: everything is decoded first)
  memset(s->dark, 0, sizeof(s->dark));
  s->void_rgb = 0;
  s->num_lines = s->num_pcells = 0;
  int cells = s->n * s->n;
  for (int L = 0; L < layers; L++)
    for (int i = 0; i < cells; i++)
      s->layer[L].rec[0][i] = s->layer[L].rec[1][i] = -1;
  if (s->indoors) {
    memcpy(s->src_map[0], dung_bg2, sizeof(s->src_map[0]));
    memcpy(s->src_map[1], dung_bg1, sizeof(s->src_map[1]));
    for (int i = 0; i < cells; i++)
      SetDungeonCell(s, i);
  } else {
    memcpy(s->src_map[0], overworld_tileattr, sizeof(s->src_map[0]));
    int half = s->n / 2;
    for (int i = 0; i < half * half; i++) {
      int r16 = i / half, c16 = i % half;
      for (int sub = 0; sub < 4; sub++)
        SetOverworldCell(s, (2 * r16 + (sub >> 1)) * s->n + 2 * c16 + (sub & 1), s->src_map[0][r16 * 64 + c16], sub);
    }
  }
  return true;
}

static bool IsCurrentAnywhere(int slot) {
  for (int i = 0; i < kInstMax; i++)
    if (g_cur[i] == slot + 1)
      return true;
  return false;
}

// A slot for a new scene: an unused one, else the least recently used one
// that no world shows now.
static int PickSlot(void) {
  int best = -1;
  for (int pass = 0; pass < 2 && best < 0; pass++) {
    for (int i = 0; i < kHd3dSlots; i++) {
      if (!g_scenes[i].used)
        return i;
      if (pass == 0 && IsCurrentAnywhere(i))
        continue;
      if (best < 0 || g_scenes[i].stamp < g_scenes[best].stamp)
        best = i;
    }
  }
  return best;
}

static Hd3dScene *BindScene(int inst, const SceneKey *k, int *why) {
  int cur = g_cur[inst] - 1;
  if (cur >= 0 && (!g_scenes[cur].used || g_scenes[cur].inst != inst))
    cur = -1;  // (its slot went to another scene)
  if (cur >= 0 && KeyMatches(&g_scenes[cur], inst, k))
    return &g_scenes[cur];
  int found = -1;
  for (int i = 0; i < kHd3dSlots && found < 0; i++)
    if (KeyMatches(&g_scenes[i], inst, k))
      found = i;
  if (found < 0) {
    found = PickSlot();
    // Whatever showed the slot's old scene doesn't any more.
    for (int i = 0; i < kInstMax; i++) {
      if (g_cur[i] == found + 1)
        g_cur[i] = 0;
      if (g_prev[i] == found + 1)
        g_prev[i] = 0;
    }
    if (found == cur)
      cur = -1;
    if (!InitScene(&g_scenes[found], inst, k)) {
      *why = kHd3dWhy_Memory;
      return NULL;
    }
  }
  if (cur >= 0 && cur != found && g_scenes[cur].used) {
    g_scenes[cur].frozen = true;
    g_prev[inst] = (uint8)(cur + 1);
    g_prev_gen[inst] = g_scenes[cur].gen;
  }
  g_scenes[found].frozen = false;
  g_cur[inst] = (uint8)(found + 1);
  return &g_scenes[found];
}

// ---- Reading the world ----

static bool AttributesComplete(void) {
  // Room-to-room scrolls, stairs, falls and warps rebuild them 0x80 tiles a
  // frame (states 1-4 and 5, and 0 right after the new room's tilemaps came
  // in); a room loaded at once leaves 0. So 0 counts only in the dungeon's
  // submodules that never load a room.
  enum { kSettled = 1 << 0 | 1 << 1 | 1 << 3 | 1 << 4 | 1 << 5 | 1 << 8 | 1 << 9 | 1 << 0xa | 1 << 0x10 |
                    1 << 0x16 | 1 << 0x17 | 1 << 0x18 | 1 << 0x1a };
  uint8 st = overworld_map_state, sub = submodule_index;
  return st == 5 || (st == 0 && (main_module_index != 7 || (sub < 32 && (kSettled >> sub & 1))));
}

static void ReadReach(Hd3dScene *s) {
  int32 x0, y0, x1, y1;
  int32 cam_x = BG2HOFS_copy2, cam_y = BG2VOFS_copy2;
  if (s->indoors && main_module_index == 7 && (submodule_index == 1 || submodule_index == 2)) {
    // Scrolls: the bounds are only right at one end.
    x0 = s->origin_x, y0 = s->origin_y, x1 = x0 + s->size, y1 = y0 + s->size;
  } else if (s->indoors) {
    int qm = quadrant_fullsize_x >> 1 & 1, qy = quadrant_fullsize_y >> 1 & 1;
    x0 = room_bounds_x.v[qm], x1 = room_bounds_x.v[qm + 2] + 256;
    y0 = room_bounds_y.v[qy], y1 = room_bounds_y.v[qy + 2] + 1 + 240;
  } else {
    x0 = ow_scroll_vars0.xstart, x1 = ow_scroll_vars0.xend + 256;
    y0 = ow_scroll_vars0.ystart, y1 = ow_scroll_vars0.yend + 1 + 240;
  }
  // What the screen shows now (its middle 256 columns) is always reachable.
  if (cam_x < x0) x0 = cam_x;
  if (cam_x + 256 > x1) x1 = cam_x + 256;
  if (cam_y + 1 < y0) y0 = cam_y + 1;
  if (cam_y + 225 > y1) y1 = cam_y + 225;
  s->reach[0] = x0 > s->origin_x ? x0 : s->origin_x;
  s->reach[1] = y0 > s->origin_y ? y0 : s->origin_y;
  s->reach[2] = x1 < s->origin_x + s->size ? x1 : s->origin_x + s->size;
  s->reach[3] = y1 < s->origin_y + s->size ? y1 : s->origin_y + s->size;
}

static bool IsAnimChar(const Hd3dScene *s, uint16 w) {
  uint16 ch = w & 0x3ff;
  return ch >= s->anim_lo && ch < s->anim_hi;
}

// The tilemap words of dungeon cell i, from src_map.
static void SetDungeonCell(Hd3dScene *s, int i) {
  uint16 w2 = s->src_map[0][i], w1 = s->src_map[1][i];
  bool e2 = (w2 & 0x3ff) == kEmptyChar, e1 = (w1 & 0x3ff) == kEmptyChar;
  Layer *ly = &s->layer[0];
  uint16 f = ly->cf[i] & ~(kCf_Decoded | kCf_Empty | kCf_Prio);
  ly->word[i] = w2;
  if (s->bg1_role == kHd3dBg1_OnTop) {
    // One picture: BG1 drawn over BG2.
    if (e2 && e1)
      f |= kCf_Empty;
    if ((!e2 && (w2 & 0x2000)) || (!e1 && (w1 & 0x2000)))
      f |= kCf_Prio;
  } else {
    if (e2)
      f |= kCf_Empty;
    if (w2 & 0x2000)
      f |= kCf_Prio;
  }
  ly->cf[i] = f;
  if (s->num_layers > 1) {
    ly = &s->layer[1];
    f = ly->cf[i] & ~(kCf_Decoded | kCf_Empty | kCf_Prio);
    ly->word[i] = w1;
    if (e1)
      f |= kCf_Empty;
    if (w1 & 0x2000)
      f |= kCf_Prio;
    ly->cf[i] = f;
  }
}

// Outdoors: the tilemap word and attribute of cell i (quarter sub of map16 m16).
static void SetOverworldCell(Hd3dScene *s, int i, uint16 m16, int sub) {
  Layer *ly = &s->layer[0];
  uint16 f = ly->cf[i] & ~(kCf_Decoded | kCf_Empty | kCf_Prio);
  size_t entries = kMap16ToMap8_SIZE / 2;
  const uint8 *attrs = GetMap8toTileAttr();
  if ((size_t)m16 * 4 + 3 >= entries) {
    ly->word[i] = 0, ly->attr[i] = 0;
    f |= kCf_Empty;
  } else {
    uint16 w = GetMap16toMap8Table()[m16 * 4 + sub];
    uint8 a = (size_t)(w & 0x1ff) < kMap8DataToTileAttr_SIZE ? attrs[w & 0x1ff] : 0;
    if (a >= 0x10 && a < 0x1c)
      a |= (w >> 14) & 1;
    ly->word[i] = w, ly->attr[i] = a;
    if (w & 0x2000)
      f |= kCf_Prio;
  }
  ly->cf[i] = f;
}

static void SyncTilemaps(Hd3dScene *s) {
  if (s->indoors) {
    for (int L = 0; L < 2; L++) {
      const uint16 *src = L ? dung_bg1 : dung_bg2;
      if (!memcmp(src, s->src_map[L], sizeof(s->src_map[L])))
        continue;
      bool used = L == 0 || s->num_layers > 1 || s->bg1_role == kHd3dBg1_OnTop;
      for (int i = 0; i < 4096; i++) {
        if (src[i] == s->src_map[L][i])
          continue;
        s->src_map[L][i] = src[i];
        if (used)
          SetDungeonCell(s, i);
      }
      if (used)
        s->geom_dirty = true;
    }
  } else {
    const uint16 *grid = overworld_tileattr;
    if (!memcmp(grid, s->src_map[0], sizeof(s->src_map[0])))
      return;
    int half = s->n / 2;
    for (int i = 0; i < 4096; i++) {
      if (grid[i] == s->src_map[0][i])
        continue;
      s->src_map[0][i] = grid[i];
      int r16 = i >> 6, c16 = i & 63;
      if (r16 >= half || c16 >= half)
        continue;
      for (int sub = 0; sub < 4; sub++)
        SetOverworldCell(s, (2 * r16 + (sub >> 1)) * s->n + 2 * c16 + (sub & 1), grid[i], sub);
      s->geom_dirty = true;
    }
  }
}

static void SyncAttributes(Hd3dScene *s) {
  for (int L = 0; L < 2; L++) {
    const uint8 *src = L ? dung_bg1_attr_table : dung_bg2_attr_table;
    if (s->attr_seen && !memcmp(src, s->src_attr[L], sizeof(s->src_attr[L])))
      continue;
    memcpy(s->src_attr[L], src, sizeof(s->src_attr[L]));
    s->geom_dirty = true;
  }
  s->attr_seen = true;
}

static uint32 HashWords(const uint16 *p, int from, int to) {
  uint32 h = 2166136261u;
  for (int i = from; i < to; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

static void Undecode(Hd3dScene *s, uint16 need) {
  int cells = s->n * s->n;
  for (int L = 0; L < s->num_layers; L++) {
    uint16 *cf = s->layer[L].cf;
    for (int i = 0; i < cells; i++)
      if ((cf[i] & need) == need)
        cf[i] &= ~kCf_Decoded;
  }
}

// BG tiles: an animated range (water, lava: copied every frame, changed
// every few) and the rest.
static void SyncVram(Hd3dScene *s) {
  const uint16 *vram = g_zenv.ppu->vram;
  int adr = animated_tile_vram_addr, lo = 0, hi = 0;
  if (adr >= 0x2000 && adr <= 0x4000 - 0x400)
    lo = adr, hi = adr + 0x400;
  uint32 rest = lo ? HashWords(vram, 0x2000, lo) * 31u + HashWords(vram, hi, 0x4000) : HashWords(vram, 0x2000, 0x4000);
  uint32 anim = lo ? HashWords(vram, lo, hi) : 0;
  uint16 alo = lo ? (uint16)((lo - 0x2000) >> 4) : 0xffff, ahi = lo ? (uint16)((hi - 0x2000) >> 4) : 0;
  if (rest != s->hash_rest || alo != s->anim_lo) {
    Undecode(s, 0);
    s->geom_dirty = true;  // (see-through pits, void walls)
  } else if (anim != s->hash_anim) {
    Undecode(s, kCf_Anim);
  }
  s->hash_rest = rest, s->hash_anim = anim, s->anim_lo = alo, s->anim_hi = ahi;
}

static int CountVoidish(const Hd3dScene *s, const uint8 *t) {
  int k = 0;
  for (int y = 0; y < 8; y++, t += s->size)
    for (int x = 0; x < 8; x++)
      k += !t[x] || (s->dark[t[x] >> 3 & 15] >> (t[x] & 7) & 1);
  return k;
}

static uint8 *CellTexels(const Hd3dScene *s, int L, int i) {
  return s->layer[L].tex + (size_t)(i / s->n) * 8 * s->size + (i % s->n) * 8;
}

// Near-black BG colors decide which walls are void.
static void SyncPalette(Hd3dScene *s) {
  const uint16 *cg = g_zenv.ppu->cgram;
  uint8 dark[16];
  memset(dark, 0, sizeof(dark));
  for (int i = 1; i < 128; i++) {
    int c = cg[i];
    if ((c & 31) + (c >> 5 & 31) + (c >> 10 & 31) <= 3)
      dark[i >> 3] |= (uint8)(1 << (i & 7));
  }
  memcpy(s->cgram, cg, sizeof(s->cgram));
  if (!memcmp(dark, s->dark, sizeof(dark)))
    return;
  memcpy(s->dark, dark, sizeof(dark));
  if (s->indoors)
    s->geom_dirty = true;
}

// The 8 texels of row py of tilemap word w (0: see-through), as the PPU draws them.
static void TileRow(const uint16 *vram, uint16 w, int py, uint8 *out) {
  int row = (w & 0x8000) ? 7 - py : py;
  int a = 0x2000 + (w & 0x3ff) * 16 + row;
  uint32 p0 = vram[a & 0x7fff], p1 = vram[(a + 8) & 0x7fff];
  if (!(p0 | p1)) {
    memset(out, 0, 8);
    return;
  }
  const uint64 *sp = g_spread[(w >> 14) & 1];
  uint64 v = sp[p0 & 255] | sp[p0 >> 8 & 255] << 1 | sp[p1 & 255] << 2 | sp[p1 >> 8 & 255] << 3;
  uint64 nz = (v | v >> 1 | v >> 2 | v >> 3) & 0x0101010101010101ull;
  v |= nz * (uint64)(((w >> 10) & 7) << 4);  // (the palette, only on texels that aren't see-through)
  for (int px = 0; px < 8; px++)
    out[px] = (uint8)(v >> (8 * px));
}

// kCf_Blank and kCf_Holes of a tile, straight from VRAM (for cells not decoded).
static uint16 CharCoverage(const uint16 *vram, uint16 w) {
  int a = 0x2000 + (w & 0x3ff) * 16, any = 0, all = 0xff;
  for (int k = 0; k < 8; k++) {
    uint16 p0 = vram[(a + k) & 0x7fff], p1 = vram[(a + 8 + k) & 0x7fff];
    int row = (p0 | p0 >> 8 | p1 | p1 >> 8) & 0xff;
    any |= row, all &= row;
  }
  return (uint16)((any ? 0 : kCf_Blank) | (all == 0xff ? 0 : kCf_Holes));
}

static void DecodeCell(Hd3dScene *s, int L, int i) {
  const uint16 *vram = g_zenv.ppu->vram;
  Layer *ly = &s->layer[L];
  uint8 *dst = CellTexels(s, L, i);
  uint16 w = ly->word[i], f = ly->cf[i];
  bool on_top = L == 0 && s->bg1_role == kHd3dBg1_OnTop;
  uint16 w1 = on_top ? s->src_map[1][i] : 0;
  bool e0 = s->indoors ? (w & 0x3ff) == kEmptyChar : (f & kCf_Empty) != 0;
  bool e1 = !on_top || (w1 & 0x3ff) == kEmptyChar;
  bool anim = (!e0 && IsAnimChar(s, w)) || (!e1 && IsAnimChar(s, w1));
  int see = 0;
  for (int py = 0; py < 8; py++) {
    uint8 *row = dst + py * s->size, b[8];
    if (e0)
      memset(row, 0, 8);
    else
      TileRow(vram, w, py, row);
    if (!e1) {
      // BG1 over BG2 the way the PPU orders them: BG1 wins unless it is a
      // plain tile over a BG2 priority one.
      TileRow(vram, w1, py, b);
      bool bg2_wins = !(w1 & 0x2000) && !e0 && (w & 0x2000);
      for (int px = 0; px < 8; px++)
        if (b[px] && !(bg2_wins && row[px]))
          row[px] = b[px];
    }
    for (int px = 0; px < 8; px++)
      see += !row[px];
  }
  uint16 nf = (f & ~(kCf_Anim | kCf_Blank | kCf_Holes)) | kCf_Decoded;
  if (anim)
    nf |= kCf_Anim;
  if (see == 64)
    nf |= kCf_Blank;
  if (see)
    nf |= kCf_Holes;
  ly->cf[i] = nf;
}

static void DecodeRect(Hd3dScene *s, const int32 *rect) {
  int n = s->n, c0 = 0, r0 = 0, c1 = n, r1 = n;
  if (rect && !s->indoors) {
    // (Rooms are decoded whole: the void test needs every texel.)
    c0 = IntMax(FloorDiv8(rect[0] - s->origin_x), 0), c1 = IntMin(FloorDiv8(rect[2] - s->origin_x + 7), n);
    r0 = IntMax(FloorDiv8(rect[1] - s->origin_y), 0), r1 = IntMin(FloorDiv8(rect[3] - s->origin_y + 7), n);
  }
  for (int L = 0; L < s->num_layers; L++) {
    const uint16 *cf = s->layer[L].cf;
    for (int r = r0; r < r1; r++)
      for (int c = c0; c < c1; c++)
        if (!(cf[r * n + c] & kCf_Decoded))
          DecodeCell(s, L, r * n + c);
  }
}

// ---- Geometry: classes ----

static bool IsSolid(uint8 cls) {
  return cls == kCls_Low || cls == kCls_Wall;
}

static int CountIndex(const Hd3dScene *s, const uint8 *t, int idx) {
  int k = 0;
  for (int y = 0; y < 8; y++, t += s->size)
    for (int x = 0; x < 8; x++)
      k += t[x] == idx;
  return k;
}

// The one color the outside of a room is painted with, if it is (the
// Sanctuary's green, the castle's dotted purple), from the cells on the edge
// of the room. 0: none (see-through or black outside).
static int FindFiller(const Hd3dScene *s, int L) {
  const Layer *ly = &s->layer[L];
  int n = s->n, votes[128], best = 0;
  uint8 count[128];
  memset(votes, 0, sizeof(votes));
  for (int i = 0; i < n * n; i++) {
    int r = i / n, c = i % n;
    if ((r != 0 && r != n - 1 && c != 0 && c != n - 1) || (ly->cf[i] & (kCf_Empty | kCf_Blank)) ||
        !(ly->cf[i] & kCf_Decoded))
      continue;
    const uint8 *t = CellTexels(s, L, i);
    memset(count, 0, sizeof(count));
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        count[t[y * s->size + x] & 127]++;
    for (int k = 1; k < 128; k++)
      if (count[k] >= kVoidTexels)
        votes[k]++;
  }
  for (int k = 1; k < 128; k++)
    if (votes[k] > votes[best])
      best = k;
  return votes[best] >= 8 ? best : 0;
}

static void Classify(Hd3dScene *s, int L) {
  Layer *ly = &s->layer[L];
  int cells = s->n * s->n;
  const uint8 *tab = g_class[s->indoors];
  const uint16 *vram = g_zenv.ppu->vram;
  int filler = s->indoors ? FindFiller(s, L) : 0;
  // A Normal room's lower floor under the upper one is never seen: void.
  const uint8 *upper = L == 1 && s->bg1_role == kHd3dBg1_LowerFloor ? s->layer[0].cls : NULL;
  if (L == 0)
    s->void_index = (int16)filler;
  if (s->indoors)
    memcpy(ly->attr, s->src_attr[L], 4096);
  for (int i = 0; i < cells; i++) {
    uint16 f = ly->cf[i] & ~(kCf_Thin | kCf_Shell | kCf_Panel | kCf_Overhead);
    if (!(f & kCf_Decoded) && !(f & kCf_Empty)) {
      f &= ~(kCf_Blank | kCf_Holes);
      f |= CharCoverage(vram, ly->word[i]);
    }
    uint8 c;
    if (f & kCf_Empty) {
      c = kCls_Empty;
    } else {
      c = tab[ly->attr[i]];
      if (c == kCls_Pit && (f & kCf_Blank)) {
        c = kCls_Empty;  // a see-through pit: what is below shows
      } else if (c == kCls_Ground && !s->indoors && (f & kCf_Prio)) {
        c = kCls_Wall;   // over Link's head: canopy rims, eaves, bridge decks
        f |= kCf_Overhead;
      } else if (IsSolid(c) && s->indoors && (f & kCf_Decoded)) {
        const uint8 *t = CellTexels(s, L, i);
        if (CountVoidish(s, t) >= kVoidTexels || (filler && CountIndex(s, t, filler) >= kVoidTexels))
          c = kCls_Void;
      }
      if (upper && upper[i] != kCls_Empty)
        c = kCls_Void;
    }
    ly->cls[i] = c;
    ly->cf[i] = f;
  }
}

// ---- Geometry: dungeon shells as hinged wall panels ----

// How far ground is from cell (r, c) that way (99: not within kThinReach, or void first).
static int RayToGround(const Layer *ly, int n, int r, int c, int dx, int dy) {
  for (int k = 1; k <= kThinReach; k++) {
    int rr = r + dy * k, cc = c + dx * k;
    if (rr < 0 || rr >= n || cc < 0 || cc >= n)
      return 99;
    uint8 cl = ly->cls[rr * n + cc];
    if (cl == kCls_Ground)
      return k;
    if (cl == kCls_Void || cl == kCls_Empty)
      return 99;
  }
  return 99;
}

static int FindLine(Hd3dScene *s, int L, int dir, int hinge) {
  int h8 = hinge >> 3;
  if (h8 < 0 || h8 > kMaxN)
    return -1;
  int16 *slot = &g_line_of[L][dir][h8];
  if (*slot >= 0)
    return *slot;
  if (s->num_lines >= kMaxLines)
    return -1;
  PanelLine *ln = &s->lines[s->num_lines];
  ln->layer = (uint8)L, ln->dir = (uint8)dir, ln->bmax = 1, ln->hinge = (int16)hinge;
  *slot = (int16)s->num_lines;
  return s->num_lines++;
}

static void AddPanelCell(Hd3dScene *s, int L, int cell, int line, int d, uint8 other) {
  Layer *ly = &s->layer[L];
  int slot = ly->rec[0][cell] < 0 ? 0 : ly->rec[1][cell] < 0 ? 1 : -1;
  if (slot < 0 || s->num_pcells >= kMaxPanelCells)
    return;
  PanelCell *p = &s->pcells[s->num_pcells];
  p->cell = (uint16)cell, p->line = (uint16)line, p->d = (uint8)d, p->other = other;
  ly->rec[slot][cell] = (int16)s->num_pcells++;
  ly->cf[cell] |= kCf_Panel;
  if (d > s->lines[line].bmax)
    s->lines[line].bmax = (uint8)d;
}

static bool LintelCandidate(const Layer *ly, int i) {
  return ly->cls[i] == kCls_Ground && (ly->cf[i] & kCf_Prio) && ly->rec[0][i] < 0;
}

// The (line, d) two cells share, on a line of one of the two directions (-1: none).
static int SharedBand(const Hd3dScene *s, const Layer *ly, int i, int j, int dir_a, int dir_b, int *d) {
  for (int a = 0; a < 2; a++) {
    int ra = ly->rec[a][i];
    if (ra < 0)
      continue;
    const PanelCell *pa = &s->pcells[ra];
    int dir = s->lines[pa->line].dir;
    if (dir != dir_a && dir != dir_b)
      continue;
    for (int b = 0; b < 2; b++) {
      int rb = ly->rec[b][j];
      if (rb >= 0 && s->pcells[rb].line == pa->line && s->pcells[rb].d == pa->d) {
        *d = pa->d;
        return pa->line;
      }
    }
  }
  return -1;
}

// Door arches: a run of walkable priority cells between two cells of the same
// band joins that panel (so it stands over the doorway, as it covers Link in 2D).
static void AddLintels(Hd3dScene *s, int L) {
  Layer *ly = &s->layer[L];
  int n = s->n;
  for (int vertical = 0; vertical < 2; vertical++) {
    for (int u = 0; u < n; u++) {
      for (int v = 0; v < n;) {
        int i = vertical ? v * n + u : u * n + v;
        if (!LintelCandidate(ly, i)) {
          v++;
          continue;
        }
        int v0 = v;
        while (v < n && LintelCandidate(ly, vertical ? v * n + u : u * n + v))
          v++;
        if (v0 == 0 || v >= n || v - v0 > 8)
          continue;
        int before = vertical ? (v0 - 1) * n + u : u * n + v0 - 1, after = vertical ? v * n + u : u * n + v;
        int d, line = vertical ? SharedBand(s, ly, before, after, kDir_W, kDir_E, &d)
                               : SharedBand(s, ly, before, after, kDir_N, kDir_S, &d);
        if (line < 0)
          continue;
        for (int k = v0; k < v; k++)
          AddPanelCell(s, L, vertical ? k * n + u : u * n + k, line, d, kDir_None);
      }
    }
  }
}

static void BuildShell(Hd3dScene *s, int L, int *hist_n, int *hist_all) {
  Layer *ly = &s->layer[L];
  int n = s->n, cells = n * n;
  // Thin solids (railings) are props.
  for (int i = 0; i < cells; i++) {
    if (!IsSolid(ly->cls[i]))
      continue;
    int r = i / n, c = i % n;
    int dn = RayToGround(ly, n, r, c, 0, -1), ds = RayToGround(ly, n, r, c, 0, 1);
    int dw = RayToGround(ly, n, r, c, -1, 0), de = RayToGround(ly, n, r, c, 1, 0);
    if (IntMin(de + dw - 1, dn + ds - 1) <= 2)
      ly->cf[i] |= kCf_Thin;
  }
  // The shell: solid pieces (8-connected, without the thin cells) that touch
  // the edge of the room or the void.
  memset(g_mark, 0, cells);
  for (int seed = 0; seed < cells; seed++) {
    if (g_mark[seed] || !IsSolid(ly->cls[seed]) || (ly->cf[seed] & kCf_Thin))
      continue;
    int head = 0, tail = 0;
    bool shell = false;
    g_list[tail++] = (int16)seed;
    g_mark[seed] = 1;
    while (head < tail) {
      int i = g_list[head++], r = i / n, c = i % n;
      if (r == 0 || c == 0 || r == n - 1 || c == n - 1)
        shell = true;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          int rr = r + dy, cc = c + dx, j = rr * n + cc;
          if (rr < 0 || rr >= n || cc < 0 || cc >= n || j == i)
            continue;
          if (ly->cls[j] == kCls_Void)
            shell = true;
          if (!g_mark[j] && IsSolid(ly->cls[j]) && !(ly->cf[j] & kCf_Thin)) {
            g_mark[j] = 1;
            g_list[tail++] = (int16)j;
          }
        }
      }
    }
    if (shell)
      for (int k = 0; k < tail; k++)
        ly->cf[g_list[k]] |= kCf_Shell;
  }
  // Band strips: from each floor cell along a shell, the shell cells outward
  // (up to kHd3dBandMax, until the void). Each cell goes with its nearest floor.
  memset(g_own_d, 0xff, cells);
  for (int dir = 0; dir < 4; dir++)
    for (int i = 0; i < cells; i++)
      g_strip_line[dir][i] = -1;
  for (int i = 0; i < cells; i++) {
    if (ly->cls[i] != kCls_Ground)
      continue;
    int r = i / n, c = i % n;
    for (int dir = 0; dir < 4; dir++) {
      int b = 0;
      while (b < kHd3dBandMax) {
        int rr = r + kDirY[dir] * (b + 1), cc = c + kDirX[dir] * (b + 1);
        if (rr < 0 || rr >= n || cc < 0 || cc >= n || !(ly->cf[rr * n + cc] & kCf_Shell))
          break;
        b++;
      }
      if (!b)
        continue;
      int hinge = dir == kDir_N ? 8 * r : dir == kDir_S ? 8 * (r + 1) : dir == kDir_W ? 8 * c : 8 * (c + 1);
      int line = FindLine(s, L, dir, hinge);
      if (line < 0)
        continue;
      if (b > s->lines[line].bmax)
        s->lines[line].bmax = (uint8)b;
      g_strip_line[dir][i] = (int16)line;
      hist_all[b]++;
      if (dir == kDir_N)
        hist_n[b]++;
      for (int k = 1; k <= b; k++) {
        int j = (r + kDirY[dir] * k) * n + c + kDirX[dir] * k;
        if (k < g_own_d[j] || (k == g_own_d[j] && dir < s->lines[g_own_line[j]].dir))
          g_own_d[j] = (uint8)k, g_own_line[j] = (int16)line;
      }
    }
  }
  for (int i = 0; i < cells; i++)
    if (g_own_d[i] != 0xff)
      AddPanelCell(s, L, i, g_own_line[i], g_own_d[i], kDir_None);
  // Corners: shell cells whose nearest floor is diagonal belong to both
  // walls that meet there (the camera decides which one each frame).
  for (int i = 0; i < cells; i++)
    g_dist[i] = ly->cls[i] == kCls_Ground ? 0 : 255;
  for (int pass = 0; pass < 2; pass++) {
    for (int k = 0; k < cells; k++) {
      int i = pass ? cells - 1 - k : k, r = i / n, c = i % n, best = g_dist[i];
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int rr = r + (pass ? -dy : dy), cc = c + dx;
          if ((dy == 0 && dx == 0) || rr < 0 || rr >= n || cc < 0 || cc >= n)
            continue;
          // (only neighbours already visited in this pass's order)
          int j = rr * n + cc;
          if ((pass == 0 && j < i) || (pass == 1 && j > i))
            best = IntMin(best, g_dist[j] + 1);
        }
      g_dist[i] = (uint8)IntMin(best, 255);
    }
  }
  for (int i = 0; i < cells; i++) {
    if (!(ly->cf[i] & kCf_Shell) || (ly->cf[i] & kCf_Panel) || g_dist[i] > kCornerReach)
      continue;
    // The nearest floor cell diagonally (rings of growing chessboard
    // distance, from the distance to the nearest floor of any kind).
    int r = i / n, c = i % n, bs = 99, by = -1, bx = -1, bfr = 0, bfc = 0;
    for (int m = IntMax(g_dist[i], 1); m <= kCornerReach && bs == 99; m++) {
      for (int fr = -m; fr <= m; fr++) {
        int rr = r + fr, step = (fr == -m || fr == m) ? 1 : 2 * m;
        if (fr == 0 || rr < 0 || rr >= n)
          continue;
        for (int fc = -m; fc <= m; fc += step) {
          int cc = c + fc, f = rr * n + cc;
          if (fc == 0 || cc < 0 || cc >= n || ly->cls[f] != kCls_Ground)
            continue;
          int ly_line = g_strip_line[fr > 0 ? kDir_N : kDir_S][f], lx_line = g_strip_line[fc > 0 ? kDir_W : kDir_E][f];
          int sum = abs(fr) + abs(fc);
          if ((ly_line >= 0 || lx_line >= 0) && sum < bs)
            bs = sum, by = ly_line, bx = lx_line, bfr = fr, bfc = fc;
        }
      }
    }
    if (by < 0 && bx < 0)
      continue;
    uint8 dir_y = bfr > 0 ? kDir_N : kDir_S, dir_x = bfc > 0 ? kDir_W : kDir_E;
    if (by >= 0)
      AddPanelCell(s, L, i, by, abs(bfr), bx >= 0 ? dir_x : kDir_None);
    if (bx >= 0)
      AddPanelCell(s, L, i, bx, abs(bfc), by >= 0 ? dir_y : kDir_None);
  }
  AddLintels(s, L);
}

static int MedianBand(const int *hist) {
  int total = 0, acc = 0;
  for (int b = 1; b <= kHd3dBandMax; b++)
    total += hist[b];
  if (!total)
    return 0;
  for (int b = 1; b <= kHd3dBandMax; b++) {
    acc += hist[b];
    if (2 * acc >= total)
      return b;
  }
  return kHd3dBandMax;
}

// ---- Geometry: the relief ----

static bool ReliefSolid(const Layer *ly, int i) {
  return IsSolid(ly->cls[i]) && !(ly->cf[i] & (kCf_Shell | kCf_Panel));
}

static bool OverheadGround(const Layer *ly, int i) {
  return ly->cls[i] == kCls_Ground && (ly->cf[i] & kCf_Prio) && !(ly->cf[i] & kCf_Panel);
}

static bool HasSurface(uint8 pk) {
  return pk == kPk_Ground || pk == kPk_Level || pk == kPk_Flat || pk == kPk_Top || pk == kPk_PitFloor;
}

// Per picture column, north to south: props (solid runs) get a top and a
// face, pits a painted inner wall and a floor below, everything else lies at
// the layer's base. A surface from picture cell (c, r) at height z lands on
// world cell (c, r + z / 8); world cells nothing lands on take the height
// north of them (inpainted).
static void BuildRelief(Hd3dScene *s, int L) {
  Layer *ly = &s->layer[L];
  int n = s->n, base = LayerBase(s, L), off = base / 8;
  memset(ly->wz, 0, (size_t)n * n);
  memset(ly->wf, 0, (size_t)n * n);
  for (int c = 0; c < n; c++) {
    for (int r = 0; r < n;) {
      int i = r * n + c;
      uint8 cl = ly->cls[i];
      uint16 f = ly->cf[i];
      if (f & kCf_Panel) {
        ly->pk[i] = kPk_Panel, r++;
      } else if (cl == kCls_Empty) {
        ly->pk[i] = kPk_Hole, r++;
      } else if (cl == kCls_Void || (f & kCf_Shell)) {
        // The void, and shell cells no panel took: flat, as painted.
        ly->pk[i] = kPk_Flat, g_col_z[r] = (int8)base, g_col_row[r] = (int16)(r + off), r++;
      } else if (ReliefSolid(ly, i)) {
        int a = r, b;
        while (r < n && ReliefSolid(ly, r * n + c))
          r++;
        b = r;
        // Canopy over a trunk: up to 2 overhead cells right above join it.
        for (int k = 0; k < 2 && a > 0 && OverheadGround(ly, (a - 1) * n + c); k++)
          a--;
        int foot = b * n + c;
        if (b >= n || ly->cls[foot] != kCls_Ground || (ly->cf[foot] & kCf_Panel)) {
          for (int k = a; k < b; k++)  // no foot: flat
            ly->pk[k * n + c] = kPk_Level, g_col_z[k] = (int8)base, g_col_row[k] = (int16)(k + off);
          continue;
        }
        int len = b - a, cap = 1;
        for (int k = a; k < b; k++)
          if (ly->cls[k * n + c] != kCls_Low || (ly->cf[k * n + c] & kCf_Prio))
            cap = 3;
        int h = len == 1 ? 1 : IntMin(len - 1, cap);
        if (len == 1) {
          ly->pk[a * n + c] = kPk_Top, g_col_z[a] = (int8)(base + 8), g_col_row[a] = (int16)(a + off);
          continue;
        }
        for (int k = a; k < b - h; k++)
          ly->pk[k * n + c] = kPk_Top, g_col_z[k] = (int8)(base + 8 * h), g_col_row[k] = (int16)(k + off + h);
        for (int k = b - h; k < b; k++)
          ly->pk[k * n + c] = kPk_Face;
      } else if (cl == kCls_Pit) {
        int a = r;
        while (r < n && ly->cls[r * n + c] == kCls_Pit && !(ly->cf[r * n + c] & kCf_Panel))
          r++;
        // The first rows are its painted inner wall, shown by the face of
        // the surface right north of it (no such surface: a flat pit).
        uint8 rim = a > 0 ? ly->pk[(a - 1) * n + c] : kPk_Hole;
        bool sink = rim == kPk_Ground || rim == kPk_Level || (rim == kPk_PitFloor && g_col_z[a - 1] == base);
        int len = r - a, wall = sink ? IntMin(kHd3dPitDepth / 8, len - 1) : 0;
        for (int k = a; k < a + wall; k++)
          ly->pk[k * n + c] = kPk_PitWall;
        for (int k = a + wall; k < r; k++)
          ly->pk[k * n + c] = kPk_PitFloor, g_col_z[k] = (int8)(base - 8 * wall),
          g_col_row[k] = (int16)(k + off - wall);
      } else {
        ly->pk[i] = kPk_Ground, g_col_z[r] = (int8)base, g_col_row[r] = (int16)(r + off), r++;
      }
    }
    // The surfaces land; the higher one wins.
    for (int r = 0; r < n; r++) {
      uint8 pk = ly->pk[r * n + c];
      int wr = g_col_row[r];
      if (!HasSurface(pk) || wr < 0 || wr >= n)
        continue;
      int j = wr * n + c;
      if (!(ly->wf[j] & kWf_Set) || g_col_z[r] > ly->wz[j])
        ly->wz[j] = g_col_z[r], ly->wf[j] = (uint8)(kWf_Set | (pk == kPk_Flat ? kWf_NoFace : 0));
    }
    // Holes, and inpainting what was uncovered.
    int above = base;
    bool have = false;
    for (int r = 0; r < n; r++) {
      int j = r * n + c;
      if (ly->wf[j] & kWf_Set) {
        above = ly->wz[j], have = true;
        continue;
      }
      int pr = r - off;
      uint8 pk = pr >= 0 && pr < n ? ly->pk[pr * n + c] : kPk_Hole;
      // (A door arch stands with its wall; the doorway floor under it is inpainted.)
      bool arch = pk == kPk_Panel && ly->cls[pr * n + c] == kCls_Ground;
      if (pk == kPk_Hole || (pk == kPk_Panel && !arch)) {
        if (pr >= 0 && pr < n && ly->cls[pr * n + c] == kCls_Empty)
          ly->wf[j] = kWf_Empty;
        have = false;
        continue;
      }
      int z = have ? above : base, src = r - z / 8, k = 1;
      // Texels from plain ground (or a pit's floor) 16 px steps north, which
      // keeps the floor patterns lined up. But where the surface in front of
      // it has see-through texels, its own (the same ones, so the picture
      // still shows what is behind them).
      if (src < 0 || src >= n || (ly->cf[src * n + c] & kCf_Holes))
        k = 5;
      for (; k <= 4; k++) {
        int sr = src - 2 * k;
        if (sr >= 0 && sr < n && (ly->pk[sr * n + c] == kPk_Ground || ly->pk[sr * n + c] == kPk_PitFloor))
          break;
      }
      if (k > 4)
        k = 0;
      ly->wz[j] = (int8)z, ly->wf[j] = (uint8)(kWf_Set | kWf_Inpaint | k << 4);
      above = z, have = true;
    }
  }
}

static void ChunkBounds(Hd3dScene *s, int L) {
  Layer *ly = &s->layer[L];
  int n = s->n, nc = n / kChunkCells;
  bool slab = L == 0 && s->bg1_role == kHd3dBg1_LowerFloor;
  for (int cy = 0; cy < nc; cy++) {
    for (int cx = 0; cx < nc; cx++) {
      int lo = 127, hi = -128;
      for (int r = IntMax(cy * kChunkCells - 1, 0); r < IntMin(cy * kChunkCells + kChunkCells + 1, n); r++)
        for (int c = IntMax(cx * kChunkCells - 1, 0); c < IntMin(cx * kChunkCells + kChunkCells + 1, n); c++)
          if (ly->wf[r * n + c] & kWf_Set) {
            lo = IntMin(lo, ly->wz[r * n + c]);
            hi = IntMax(hi, ly->wz[r * n + c]);
          }
      if (slab && lo <= hi)
        lo = IntMin(lo, kHd3dLowerFloor);
      ly->zlo[cy * nc + cx] = (int8)lo, ly->zhi[cy * nc + cx] = (int8)hi;
    }
  }
}

static void BuildGeometry(Hd3dScene *s) {
  int layers = GeometryLayers(s), hist_n[kHd3dBandMax + 1], hist_all[kHd3dBandMax + 1];
  memset(hist_n, 0, sizeof(hist_n));
  memset(hist_all, 0, sizeof(hist_all));
  s->num_lines = s->num_pcells = 0;
  for (int L = 0; L < layers; L++) {
    Layer *ly = &s->layer[L];
    for (int i = 0; i < s->n * s->n; i++)
      ly->rec[0][i] = ly->rec[1][i] = -1;
    Classify(s, L);
    if (s->indoors) {
      for (int dir = 0; dir < 4; dir++)
        for (int h = 0; h <= kMaxN; h++)
          g_line_of[L][dir][h] = -1;
      BuildShell(s, L, hist_n, hist_all);
    }
  }
  // East, west and south walls are as high as the north ones are wide.
  int b = MedianBand(hist_n);
  if (!b)
    b = MedianBand(hist_all);
  s->wall_height = (int16)(b ? 8 * IntMin(IntMax(b, 3), kHd3dBandMax) : 48);
  for (int L = 0; L < layers; L++) {
    BuildRelief(s, L);
    ChunkBounds(s, L);
  }
  s->ready = true;
  s->geom_dirty = false;
}

// ---- Public: phase A ----

Hd3dScene *Hd3dScene_Update(const int32 decode_rect[4], int *why) {
  int dummy;
  if (!why)
    why = &dummy;
  *why = kHd3dWhy_Room;
  if (!g_tables_ready)
    InitTables();
  int inst = Inst_Current();
  SceneKey key;
  if (inst < 0 || inst >= kInstMax || !g_zenv.ppu || !ReadKey(&key))
    return NULL;
  Hd3dScene *s = BindScene(inst, &key, why);
  if (!s)
    return NULL;
  s->stamp = ++g_clock;
  ReadReach(s);
  if (s->bg1_role == kHd3dBg1_Far)
    s->bg1_dx = (int16)(BG1HOFS_copy - BG2HOFS_copy), s->bg1_dy = (int16)(BG1VOFS_copy - BG2VOFS_copy);
  bool complete = !s->indoors || AttributesComplete();
  SyncTilemaps(s);
  if (s->indoors && complete)
    SyncAttributes(s);
  SyncVram(s);
  SyncPalette(s);
  DecodeRect(s, decode_rect);
  if (complete && s->geom_dirty && (!s->indoors || s->attr_seen))
    BuildGeometry(s);
  *why = kHd3dWhy_None;
  return s;
}

Hd3dScene *Hd3dScene_Previous(void) {
  int inst = Inst_Current();
  if (inst < 0 || inst >= kInstMax || !g_prev[inst])
    return NULL;
  Hd3dScene *s = &g_scenes[g_prev[inst] - 1];
  if (!s->used || !s->frozen || s->gen != g_prev_gen[inst] || s->inst != inst)
    return NULL;
  s->stamp = ++g_clock;
  return s;
}

void Hd3dScene_GetInfo(const Hd3dScene *s, Hd3dSceneInfo *out) {
  memset(out, 0, sizeof(*out));
  if (!s)
    return;
  out->indoors = s->indoors;
  out->ready = s->ready;
  out->bg1_role = s->bg1_role;
  out->origin_x = s->origin_x, out->origin_y = s->origin_y, out->size = s->size;
  out->wall_height = s->wall_height;
  out->void_index = s->void_index;
}

int Hd3dScene_WalkZ(const Hd3dScene *s, int layer) {
  return s && layer ? LayerBase(s, 1) : 0;
}

float Hd3dScene_StepRise(Hd3dScene *s, float dt, bool drawn) {
  if (!s)
    return 1.0f;
  if (!s->ready) {
    if (drawn)
      s->drawn_flat = true;
    s->rise = 0;
  } else if (!s->drawn_flat) {
    s->rise = 1.0f;
  } else if (drawn) {
    s->rise += (dt > 0 ? dt : 0) * (1.0f / 0.3f);
    if (s->rise >= 1.0f)
      s->rise = 1.0f, s->drawn_flat = false;
  }
  return s->rise;
}

// ---- Public: phase B (no game memory) ----

void Hd3dScene_Textures(Hd3dScene *s, const uint32 *lut, R3dTexture *bg2, R3dTexture *bg1) {
  memset(bg2, 0, sizeof(*bg2));
  memset(bg1, 0, sizeof(*bg1));
  if (!s)
    return;
  uint8 lg = s->size == 1024 ? 10 : 9;
  bg2->texels = s->layer[0].tex;
  bg2->lut = lut;
  bg2->origin_x = s->origin_x, bg2->origin_y = s->origin_y;
  bg2->log2_w = bg2->log2_h = lg;
  bg2->subtract_from = 0;
  *bg1 = *bg2;
  if (s->num_layers > 1) {
    bg1->texels = s->layer[1].tex;
    if (s->bg1_role == kHd3dBg1_Far)
      bg1->origin_x -= s->bg1_dx, bg1->origin_y -= s->bg1_dy;
  }
  s->void_rgb = lut ? lut[s->void_index & 255] : 0;
}

typedef struct EmitCtx {
  Hd3dScene *s;
  const Hd3dEmit *e;
  R3dQuad *out;
  int max, num;
  int c0, r0, c1, r1;        // the emission rectangle, in world cells
  float rise;
  float k[4];                // the fold of each wall direction
  float cphi[4], sphi[4];    // cos and sin of its angle (90 degrees times k)
  int8 chunk_vis[2][kMaxN / kChunkCells * (kMaxN / kChunkCells)];  // panels: 0 unknown, 1 seen, -1 culled
} EmitCtx;

static R3dQuad *NewQuad(EmitCtx *x, uint8 tex, uint8 shade, uint8 cls) {
  if (x->num >= x->max)
    return NULL;
  R3dQuad *q = &x->out[x->num++];
  q->color = 0, q->tex = tex, q->flags = 0, q->shade = shade, q->cls = cls;
  return q;
}

static void SetVertex(R3dVertex *v, float px, float py, float pz, float u, float tv) {
  v->x = px, v->y = py, v->z = pz, v->u = u, v->v = tv;
}

// Relief vertices rise in: (x, y, z) -> (x, y - (1 - rise)(z - B), B + rise (z - B)) keeps the picture
// (and, for rise > 0, the side a quad faces).
static void ApplyRise(const EmitCtx *x, R3dQuad *q, int base) {
  if (x->rise >= 1.0f)
    return;
  for (int i = 0; i < 4; i++) {
    float rel = q->v[i].z - (float)base;
    q->v[i].y -= (1.0f - x->rise) * rel;
    q->v[i].z = (float)base + x->rise * rel;
  }
}

// Makes the quad's front face (nx, ny, nz).
static void Orient(R3dQuad *q, float nx, float ny, float nz) {
  float ax = q->v[1].x - q->v[0].x, ay = q->v[1].y - q->v[0].y, az = q->v[1].z - q->v[0].z;
  float bx = q->v[2].x - q->v[0].x, by = q->v[2].y - q->v[0].y, bz = q->v[2].z - q->v[0].z;
  float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
  if (cx * nx + cy * ny + cz * nz < 0) {
    R3dVertex t = q->v[1];
    q->v[1] = q->v[3], q->v[3] = t;
  }
}

// Scenery near the line from the camera to the player's Link is dithered.
static void Fade(const EmitCtx *x, R3dQuad *q) {
  const Hd3dEmit *e = x->e;
  if (!e->fade)
    return;
  float zt = q->v[0].z;
  for (int i = 1; i < 4; i++)
    zt = q->v[i].z > zt ? q->v[i].z : zt;
  if (zt <= e->fade_a[2] + 12.0f)
    return;
  float px = (q->v[0].x + q->v[1].x + q->v[2].x + q->v[3].x) * 0.25f;
  float py = (q->v[0].y + q->v[1].y + q->v[2].y + q->v[3].y) * 0.25f;
  float dx = e->fade_b[0] - e->fade_a[0], dy = e->fade_b[1] - e->fade_a[1];
  float len2 = dx * dx + dy * dy, t = 0;
  if (len2 > 1e-6f) {
    t = ((px - e->fade_a[0]) * dx + (py - e->fade_a[1]) * dy) / len2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
  }
  float ex = e->fade_a[0] + dx * t - px, ey = e->fade_a[1] + dy * t - py;
  if (ex * ex + ey * ey < 28.0f * 28.0f)
    q->flags |= kR3dQuad_Dither;
}

static bool BoxVisible(const EmitCtx *x, float x0, float y0, float z0, float x1, float y1, float z1) {
  if (!x->e->cam)
    return true;
  float lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
  return R3d_BoxVisible(x->e->cam, x->e->w, x->e->h, lo, hi);
}

static bool SourceDecoded(const Layer *ly, int n, int r, int c) {
  return r >= 0 && r < n && (ly->cf[r * n + c] & kCf_Decoded);
}

static void EmitTop(EmitCtx *x, int L, int c, int r, int len, int z, int ink) {
  Hd3dScene *s = x->s;
  R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, 255, kR3dClass_World);
  if (!q)
    return;
  float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f * (float)len;
  float y0 = (float)(s->origin_y + 8 * r), y1 = y0 + 8.0f, zf = (float)z, dv = (float)(z + 16 * ink);
  SetVertex(&q->v[0], x0, y0, zf, x0, y0 - dv);
  SetVertex(&q->v[1], x1, y0, zf, x1, y0 - dv);
  SetVertex(&q->v[2], x1, y1, zf, x1, y1 - dv);
  SetVertex(&q->v[3], x0, y1, zf, x0, y1 - dv);
  ApplyRise(x, q, LayerBase(s, L));
  Fade(x, q);
}

// A vertical face of world cell (c, r) toward its neighbour that way, from zlo up to zhi.
static void EmitFace(EmitCtx *x, int L, int c, int r, int dir, int zlo, int zhi) {
  static const uint8 kShade[4] = { 176, 216, 216, 255 };
  Hd3dScene *s = x->s;
  R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, kShade[dir], kR3dClass_World);
  if (!q)
    return;
  float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f, y0 = (float)(s->origin_y + 8 * r), y1 = y0 + 8.0f;
  float zl = (float)zlo, zh = (float)zhi;
  if (dir == kDir_S || dir == kDir_N) {
    float y = dir == kDir_S ? y1 : y0;
    SetVertex(&q->v[0], x0, y, zh, x0, y - zh);
    SetVertex(&q->v[1], x1, y, zh, x1, y - zh);
    SetVertex(&q->v[2], x1, y, zl, x1, y - zl);
    SetVertex(&q->v[3], x0, y, zl, x0, y - zl);
  } else {
    // (u from a column a few texels inside the higher cell: the edge column
    // is usually the art's dark outline, which made side faces near black)
    float px = dir == kDir_E ? x1 : x0, u = dir == kDir_E ? x1 - 3.5f : x0 + 3.5f;
    SetVertex(&q->v[0], px, y0, zh, u, y0 - zh);
    SetVertex(&q->v[1], px, y1, zh, u, y1 - zh);
    SetVertex(&q->v[2], px, y1, zl, u, y1 - zl);
    SetVertex(&q->v[3], px, y0, zl, u, y0 - zl);
  }
  Orient(q, (float)kDirX[dir], (float)kDirY[dir], 0);
  ApplyRise(x, q, LayerBase(s, L));
  Fade(x, q);
}

static void EmitRelief(EmitCtx *x, int L) {
  Hd3dScene *s = x->s;
  Layer *ly = &s->layer[L];
  int n = s->n, nc = n / kChunkCells, base = LayerBase(s, L);
  bool slab = L == 0 && s->bg1_role == kHd3dBg1_LowerFloor;
  bool faces = x->rise > 0.01f;
  for (int cy = x->r0 / kChunkCells; cy * kChunkCells < x->r1; cy++) {
    for (int cx = x->c0 / kChunkCells; cx * kChunkCells < x->c1; cx++) {
      int ch = cy * nc + cx, zlo = ly->zlo[ch], zhi = ly->zhi[ch];
      if (zlo > zhi)
        continue;
      float rise = x->rise, up = (1.0f - rise) * (float)IntMax(zhi - base, 0);
      float down = (1.0f - rise) * (float)IntMax(base - zlo, 0);
      float bx = (float)(s->origin_x + 64 * cx), by = (float)(s->origin_y + 64 * cy);
      if (!BoxVisible(x, bx, by - up, (float)base + rise * (float)(zlo - base), bx + 64.0f, by + 64.0f + down,
                      (float)base + rise * (float)(zhi - base)))
        continue;
      int rb = IntMin(cy * kChunkCells + kChunkCells, x->r1), cb = IntMin(cx * kChunkCells + kChunkCells, x->c1);
      for (int r = IntMax(cy * kChunkCells, x->r0); r < rb; r++) {
        for (int c = IntMax(cx * kChunkCells, x->c0); c < cb;) {
          int j = r * n + c;
          uint8 f = ly->wf[j];
          int z = ly->wz[j], ink = f >> 4 & 7;
          if (!(f & kWf_Set)) {
            c++;
            continue;
          }
          // (A top whose texels aren't there, outside the area or not
          // decoded, is left out; its faces still show their own.)
          int len = 1;
          if (SourceDecoded(ly, n, r - z / 8 - 2 * ink, c)) {
            if (!(f & kWf_Inpaint))
              while (len < 4 && c + len < cb && (ly->wf[j + len] & (kWf_Set | kWf_Inpaint)) == kWf_Set &&
                     ly->wz[j + len] == z && SourceDecoded(ly, n, r - z / 8, c + len))
                len++;
            EmitTop(x, L, c, r, len, z, ink);
          }
          for (int k = 0; k < len && faces; k++) {
            int jj = j + k, cc = c + k;
            if (ly->wf[jj] & kWf_NoFace)
              continue;
            for (int dir = 0; dir < 4; dir++) {
              int rr = r + kDirY[dir], c2 = cc + kDirX[dir];
              if (rr < 0 || rr >= n || c2 < 0 || c2 >= n)
                continue;
              int nb = rr * n + c2, zl;
              uint8 nf = ly->wf[nb];
              if (nf & kWf_Set) {
                if ((nf & kWf_NoFace) || ly->wz[nb] >= z)
                  continue;
                zl = ly->wz[nb];
              } else if (slab && (nf & kWf_Empty) && z > kHd3dLowerFloor) {
                zl = kHd3dLowerFloor;  // the upper floor reads as a slab over the lower one
              } else {
                continue;
              }
              EmitFace(x, L, cc, r, dir, zl, z);
            }
          }
          c += len;
        }
      }
    }
  }
}

// Before the geometry is ready: every layer flat at its base (exactly the picture).
static void EmitFlat(EmitCtx *x, int L) {
  Hd3dScene *s = x->s;
  Layer *ly = &s->layer[L];
  int n = s->n, base = LayerBase(s, L), off = base / 8;
  for (int r = x->r0; r < x->r1; r++) {
    int pr = r - off;
    if (pr < 0 || pr >= n)
      continue;
    for (int c = x->c0; c < x->c1;) {
      uint16 f = ly->cf[pr * n + c];
      if (!(f & kCf_Decoded) || (f & (kCf_Empty | kCf_Blank))) {
        c++;
        continue;
      }
      int len = 1;
      while (len < 4 && c + len < x->c1 &&
             (ly->cf[pr * n + c + len] & (kCf_Decoded | kCf_Empty | kCf_Blank)) == kCf_Decoded)
        len++;
      EmitTop(x, L, c, r, len, base, 0);
      c += len;
    }
  }
}

static bool PanelWins(const EmitCtx *x, int dir, int other) {
  return x->k[dir] > x->k[other] || (x->k[dir] == x->k[other] && dir < other);
}

static bool PanelChunkVisible(EmitCtx *x, int L, int pr, int pc) {
  Hd3dScene *s = x->s;
  int nc = s->n / kChunkCells, ch = (pr / kChunkCells) * nc + pc / kChunkCells;
  int8 *v = &x->chunk_vis[L][ch];
  if (!*v) {
    // Folded cells stay within 96 px of where they are painted, and 64 px above the floor.
    float bx = (float)(s->origin_x + 64 * (pc / kChunkCells));
    float by = (float)(s->origin_y + 64 * (pr / kChunkCells) + LayerBase(s, L));
    float zb = (float)LayerBase(s, L);
    *v = BoxVisible(x, bx - 96.0f, by - 96.0f, zb, bx + 160.0f, by + 160.0f, zb + 72.0f) ? 1 : -1;
  }
  return *v > 0;
}

// One cell of a wall panel: a point at band distance t (px) along the foot
// is at hinge + R(t) (cos phi n + sin phi z), R(t) = t lerp(1, H / 8B, k).
static void EmitPanelCell(EmitCtx *x, int L, const PanelCell *p, int lid) {
  Hd3dScene *s = x->s;
  const PanelLine *ln = &s->lines[p->line];
  int dir = ln->dir, n = s->n, base = LayerBase(s, L);
  float k = x->k[dir], cp = x->cphi[dir], sp = x->sphi[dir];
  float scale = dir == kDir_N ? 1.0f : 1.0f + ((float)s->wall_height / (8.0f * (float)ln->bmax) - 1.0f) * k;
  float nx = (float)kDirX[dir], ny = (float)kDirY[dir];
  int pr = p->cell / n, pc = p->cell % n;
  bool along_x = dir == kDir_N || dir == kDir_S;
  float a0 = 8.0f * (float)(along_x ? pc : pr), a1 = a0 + 8.0f;
  float t[2] = { 8.0f * (float)(p->d - 1), 8.0f * (float)p->d };
  float ox = (float)s->origin_x, oy = (float)s->origin_y, hinge = (float)ln->hinge, zb = (float)base;
  static const uint8 kShadeOf[4] = { 255, 216, 216, 176 };
  uint8 shade = (uint8)(255.0f + ((float)kShadeOf[dir] - 255.0f) * k + 0.5f);
  R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, shade, kR3dClass_World);
  if (!q)
    return;
  for (int v = 0; v < 4; v++) {
    float a = (v == 1 || v == 2) ? a1 : a0, tt = t[v >> 1], rr = tt * scale;
    // hinge point, the folded point, and its texel (where it is painted)
    float hx = along_x ? a : hinge, hy = along_x ? hinge : a;
    SetVertex(&q->v[v], ox + hx + nx * rr * cp, oy + hy + ny * rr * cp + zb, zb + rr * sp,
              ox + hx + nx * tt, oy + hy + ny * tt);
  }
  Orient(q, -nx * sp, -ny * sp, cp);
  Fade(x, q);
  if (lid < 0 || k <= 0)
    return;
  // A lid over the wall top, out over the void past the band (lid cells).
  // North walls show the picture there (at the default view exactly what
  // the original shows above them); the others the void's color.
  float rt = t[1] * scale, inner = rt * cp, outer = 8.0f * (float)(p->d + lid), zt = zb + rt * sp;
  if (inner >= outer - 0.01f)
    return;
  q = NewQuad(x, x->e->tex_bg2, 255, kR3dClass_World);
  if (!q)
    return;
  if (dir != kDir_N)
    q->flags = kR3dQuad_Flat, q->color = s->void_rgb;
  for (int v = 0; v < 4; v++) {
    float a = (v == 1 || v == 2) ? a1 : a0, dist = v >> 1 ? outer : inner;
    float hx = along_x ? a : hinge, hy = along_x ? hinge : a;
    float px = ox + hx + nx * dist, py = oy + hy + ny * dist + zb;
    SetVertex(&q->v[v], px, py, zt, px, py - zt);
  }
  Orient(q, 0, 0, 1);
  Fade(x, q);
}

// How many cells of void a wall's lid covers past the top of a band
// column (0-2), -1: no lid (only upper floors' walls get one, and only
// where the void hides nothing: a see-through void over BG1 has none).
static int LidCells(const Hd3dScene *s, const Layer *ly, const PanelCell *p) {
  int dir = s->lines[p->line].dir, n = s->n, k;
  if (s->lines[p->line].layer != 0)
    return -1;
  if (dir != kDir_N && !s->void_index && (s->bg1_role == kHd3dBg1_Far || s->bg1_role == kHd3dBg1_LowerFloor))
    return -1;
  for (k = 0; k < 2; k++) {
    int r = p->cell / n + kDirY[dir] * (k + 1), c = p->cell % n + kDirX[dir] * (k + 1);
    if (r < 0 || r >= n || c < 0 || c >= n || ly->pk[r * n + c] != kPk_Flat)
      break;
  }
  return k;
}

static bool IsColumnTop(const Hd3dScene *s, const Layer *ly, const PanelCell *p) {
  int dir = s->lines[p->line].dir, n = s->n;
  int r = p->cell / n + kDirY[dir], c = p->cell % n + kDirX[dir];
  if (r < 0 || r >= n || c < 0 || c >= n)
    return true;
  for (int k = 0; k < 2; k++) {
    int rec = ly->rec[k][r * n + c];
    if (rec >= 0 && s->pcells[rec].line == p->line)
      return false;
  }
  return true;
}

static void EmitPanels(EmitCtx *x, int L) {
  Hd3dScene *s = x->s;
  Layer *ly = &s->layer[L];
  int n = s->n, off = LayerBase(s, L) / 8;
  for (int i = 0; i < s->num_pcells && x->num < x->max; i++) {
    const PanelCell *p = &s->pcells[i];
    const PanelLine *ln = &s->lines[p->line];
    if (ln->layer != L || (p->other != kDir_None && !PanelWins(x, ln->dir, p->other)))
      continue;
    int pr = p->cell / n, pc = p->cell % n;
    if (pr + off < x->r0 || pr + off >= x->r1 || pc < x->c0 || pc >= x->c1 || !(ly->cf[p->cell] & kCf_Decoded))
      continue;
    if (!PanelChunkVisible(x, L, pr, pc))
      continue;
    EmitPanelCell(x, L, p, IsColumnTop(s, ly, p) ? LidCells(s, ly, p) : -1);
  }
}

// A parallax room's BG1: a flat plane far below, one quad per 8-cell run of
// picture rows (placed so that it shows the same picture point).
static void EmitFar(EmitCtx *x) {
  Hd3dScene *s = x->s;
  float z = (float)kHd3dFarPlane;
  for (int r = x->r0; r < x->r1; r++) {
    for (int c = x->c0; c < x->c1;) {
      int end = IntMin((c / 8 + 1) * 8, x->c1);
      float x0 = (float)(s->origin_x + 8 * c), x1 = (float)(s->origin_x + 8 * end), y0 = (float)(s->origin_y + 8 * r);
      if ((c % 8 == 0 || c == x->c0) && !BoxVisible(x, x0, y0 + z, z, x1, y0 + 8.0f + z, z)) {
        c = end;
        continue;
      }
      R3dQuad *q = NewQuad(x, x->e->tex_bg1, 255, kR3dClass_Far);
      if (!q)
        return;
      SetVertex(&q->v[0], x0, y0 + z, z, x0, y0);
      SetVertex(&q->v[1], x1, y0 + z, z, x1, y0);
      SetVertex(&q->v[2], x1, y0 + 8.0f + z, z, x1, y0 + 8.0f);
      SetVertex(&q->v[3], x0, y0 + 8.0f + z, z, x0, y0 + 8.0f);
      c = end;
    }
  }
}

// Translucent water (50%) or additive light: BG1 as a plane just over the floor.
static void EmitPlanes(EmitCtx *x) {
  Hd3dScene *s = x->s;
  Layer *ly = &s->layer[1];
  int n = s->n;
  uint8 flags = s->bg1_role == kHd3dBg1_Water ? kR3dQuad_Half : kR3dQuad_Add;
  for (int r = x->r0; r < x->r1; r++) {
    for (int c = x->c0; c < x->c1;) {
      uint16 f = ly->cf[r * n + c];
      if ((f & (kCf_Decoded | kCf_Empty | kCf_Blank)) != kCf_Decoded) {
        c++;
        continue;
      }
      int len = 1;
      while (len < 8 && c + len < x->c1 &&
             (ly->cf[r * n + c + len] & (kCf_Decoded | kCf_Empty | kCf_Blank)) == kCf_Decoded)

        len++;
      R3dQuad *q = NewQuad(x, x->e->tex_bg1, 255, kR3dClass_World);
      if (!q)
        return;
      q->flags = flags;
      float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f * (float)len, y0 = (float)(s->origin_y + 8 * r);
      SetVertex(&q->v[0], x0, y0 + 0.5f, 0.5f, x0, y0);
      SetVertex(&q->v[1], x1, y0 + 0.5f, 0.5f, x1, y0);
      SetVertex(&q->v[2], x1, y0 + 8.5f, 0.5f, x1, y0 + 8.0f);
      SetVertex(&q->v[3], x0, y0 + 8.5f, 0.5f, x0, y0 + 8.0f);
      c += len;
    }
  }
}

static float Smoothstep(float e0, float e1, float v) {
  float t = (v - e0) / (e1 - e0);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3.0f - 2.0f * t);
}

int Hd3dScene_Emit(Hd3dScene *s, const Hd3dEmit *e, R3dQuad *out, int max) {
  EmitCtx x;
  if (!s || !e || !out || max <= 0 || !s->used)
    return 0;
  memset(&x, 0, sizeof(x));
  x.s = s, x.e = e, x.out = out, x.max = max;
  x.rise = s->ready ? (e->rise < 0 ? 0 : e->rise > 1 ? 1 : e->rise) : 0;
  int32 x0 = IntMax(e->rect[0], s->reach[0]), y0 = IntMax(e->rect[1], s->reach[1]);
  int32 x1 = IntMin(e->rect[2], s->reach[2]), y1 = IntMin(e->rect[3], s->reach[3]);
  x.c0 = IntMax(FloorDiv8(x0 - s->origin_x), 0), x.c1 = IntMin(FloorDiv8(x1 - s->origin_x + 7), s->n);
  x.r0 = IntMax(FloorDiv8(y0 - s->origin_y), 0), x.r1 = IntMin(FloorDiv8(y1 - s->origin_y + 7), s->n);
  if (x.c0 >= x.c1 || x.r0 >= x.r1)
    return 0;
  float fp = e->fp_blend < 0 ? 0 : e->fp_blend > 1 ? 1 : e->fp_blend;
  for (int dir = 0; dir < 4; dir++) {
    // m: the wall's inner normal (into the room); c: toward the eye.
    float dot = -(float)kDirX[dir] * e->view_dir[0] - (float)kDirY[dir] * e->view_dir[1], k;
    if (e->test_camera) {
      k = dot >= 0.5f ? 1.0f : 0.0f;
    } else {
      k = Smoothstep(0.0f, 0.35f, dot);
      k += (1.0f - k) * fp;
    }
    x.k[dir] = k * x.rise;
    x.cphi[dir] = cosf(x.k[dir] * 1.5707963f), x.sphi[dir] = sinf(x.k[dir] * 1.5707963f);
  }

  int layers = GeometryLayers(s);
  for (int L = 0; L < layers; L++) {
    if (!s->ready) {
      EmitFlat(&x, L);
    } else {
      EmitRelief(&x, L);
      if (s->indoors)
        EmitPanels(&x, L);
    }
  }
  if (s->bg1_role == kHd3dBg1_Far && s->num_layers > 1)
    EmitFar(&x);
  if ((s->bg1_role == kHd3dBg1_Water || s->bg1_role == kHd3dBg1_Light) && s->num_layers > 1)
    EmitPlanes(&x);
  return x.num;
}

// ---- Debug ----

// Three pictures a layer, side by side: classes and panels, world heights,
// and the texels (with the BG colors of the last read).
void Hd3dScene_Dump(const Hd3dScene *s, const char *ppm_path) {
  if (!s || !s->used || !ppm_path)
    return;
  int n = s->n, cs = n <= 64 ? 8 : 4, pw = n * cs, layers = s->num_layers;
  int w = pw * 3, h = pw * layers;
  uint8 *img = (uint8 *)calloc((size_t)w * h, 3);
  if (!img)
    return;
  static const uint8 kClsRgb[6][3] = {
    { 96, 96, 96 }, { 20, 30, 120 }, { 200, 180, 40 }, { 220, 110, 30 }, { 0, 0, 0 }, { 50, 0, 60 },
  };
  static const uint8 kDirRgb[4][3] = { { 230, 40, 40 }, { 40, 200, 60 }, { 60, 90, 240 }, { 220, 40, 220 } };
  for (int L = 0; L < layers; L++) {
    const Layer *ly = &s->layer[L];
    bool geom = s->ready && L < GeometryLayers(s);
    for (int i = 0; i < n * n; i++) {
      int r = i / n, c = i % n;
      uint8 col[3], hz[3];
      memcpy(col, kClsRgb[s->ready ? ly->cls[i] : (ly->cf[i] & kCf_Empty ? kCls_Empty : kCls_Ground)], 3);
      if (geom && (ly->cf[i] & kCf_Thin))
        col[0] = 255, col[1] = 230, col[2] = 120;
      else if (geom && (ly->cf[i] & kCf_Shell) && !(ly->cf[i] & kCf_Panel))
        col[0] = 120, col[1] = 60, col[2] = 40;
      if (geom && ly->rec[0][i] >= 0) {
        const PanelCell *p = &s->pcells[ly->rec[0][i]];
        int dir = s->lines[p->line].dir, lum = 255 - 20 * (p->d - 1);
        for (int k = 0; k < 3; k++)
          col[k] = (uint8)(kDirRgb[dir][k] * lum / 255);
        if (p->other != kDir_None)
          col[0] = col[0] / 2, col[1] = (uint8)(160 + p->d * 10), col[2] = (uint8)(160 + p->d * 10);  // corners: cyan
        if (ly->cls[i] == kCls_Ground)
          col[0] = col[1] = col[2] = 255;  // lintels
      }
      if (geom && (ly->wf[i] & kWf_Set)) {
        int z = ly->wz[i], g = 128 + z * 3;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        hz[0] = hz[1] = hz[2] = (uint8)g;
        if (z > LayerBase(s, L))
          hz[0] = (uint8)(g / 2), hz[2] = (uint8)(g / 2);
        if (z < LayerBase(s, L))
          hz[0] = hz[1] = (uint8)(g / 2);
        if (ly->wf[i] & kWf_Inpaint)
          hz[0] = 255;
        if (ly->wf[i] & kWf_NoFace)
          hz[0] = hz[1] = hz[2] = (uint8)(g / 3);
      } else {
        hz[0] = hz[1] = 0, hz[2] = (ly->wf[i] & kWf_Empty) ? 60 : 0;
      }
      for (int y = 0; y < cs; y++) {
        for (int xx = 0; xx < cs; xx++) {
          uint8 *px = img + ((size_t)(L * pw + r * cs + y) * w + c * cs + xx) * 3;
          bool edge = cs >= 8 && (y == 0 || xx == 0);
          for (int k = 0; k < 3; k++) {
            px[k] = edge ? (uint8)(col[k] * 3 / 4) : col[k];
            px[pw * 3 + k] = edge ? (uint8)(hz[k] * 3 / 4) : hz[k];
          }
          int tx = (c * 8) + xx * 8 / cs, ty = (r * 8) + y * 8 / cs;
          uint8 t = ly->tex[(size_t)ty * s->size + tx];
          uint16 cg = t < 128 ? s->cgram[t] : 0;
          uint8 rgb[3] = { (uint8)((cg & 31) << 3), (uint8)((cg >> 5 & 31) << 3), (uint8)((cg >> 10 & 31) << 3) };
          if (!(ly->cf[i] & kCf_Decoded))
            rgb[0] = 255, rgb[1] = 0, rgb[2] = 255;
          memcpy(px + pw * 6, rgb, 3);
        }
      }
    }
  }
  FILE *f = fopen(ppm_path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(img, 3, (size_t)w * h, f);
    fclose(f);
  }
  free(img);
}
