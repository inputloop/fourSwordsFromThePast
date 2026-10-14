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
//   the camera is. In third person a wall that lies flat lies on a low body
//   (a dollhouse cutaway: the walls between the camera and the room are cut
//   down, their painted band on top), and a wall is one solid block: its
//   ends and the steps between its columns are closed.
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
  kOwSkirt = 16,             // outdoors: world rows kept past the area's north and south edges
  kMaxProps = 512,           // small props drawn as standing pictures, per scene
  kPropMaxCells = 4,         // a prop that fits in 4 x 4 cells (pots, statues, chests) is one
  kFpWallHeight = 64,        // in first person room walls are at least this tall (the eye is at 20)
  kCutWallHeight = 16,       // third person: a wall between the camera and the room is cut down to this
  kBandStep = 0x80,          // (PanelCell.b) the band ends at another floor: a step, not a room wall
  kBandLip = 0x40,           // (PanelCell.b) the upper floor's painted edge over a lower floor: it lies flat
  kBandKinds = kBandStep | kBandLip,
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
  kCf_Prop = 0x800,          // indoors: part of a small prop drawn as a standing picture (PropCard)
};

// What a picture cell becomes in the relief (Layer.pk).
// (Level: a prop without a foot, flat at the base; Flat: the void and shell
// cells no panel took, flat and without faces; Prop: under a standing
// picture, floor at the base.)
enum { kPk_Hole, kPk_Ground, kPk_Level, kPk_Flat, kPk_Top, kPk_Face, kPk_PitWall, kPk_PitFloor, kPk_Panel, kPk_Prop };

// World cell flags (Layer.wf). Bits 4-6: how many 16 px steps north an
// inpainted cell takes its texels from (indoors, south with kWf_South;
// outdoors 5-7 mean south). kWf_Flat (outdoors): an inpainted cell seen
// through see-through texels: the backdrop's color.
enum { kWf_Set = 1, kWf_NoFace = 2, kWf_Empty = 4, kWf_Inpaint = 8, kWf_South = 0x80, kWf_Flat = 0x100 };

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
  uint8 b;                   // the band of its column (how many cells the wall is painted with there), | kBandStep or kBandLip
  uint8 other;               // a corner: the direction of the other panel it can go with (kDir_None: not a corner)
  int8 s0, s1;               // the hinge's offset (px) at its two ends along the foot (diagonal walls)
} PanelCell;

// A small liftable prop indoors (a pot, a skull, a bush): its whole
// picture stands up on its front line and turns to face the camera. At the
// default view it stands where its painted face is, so it shows exactly the
// picture (the relief rule); from the side it is the same picture, not a
// box with smeared sides.
typedef struct PropCard {
  uint8 layer;
  uint8 c0, r0, w, h;        // picture cells (rows r0 .. r0 + h - 1)
} PropCard;

typedef struct Layer {
  uint16 *word;              // tilemap word of each cell
  uint8 *attr;               // collision attribute
  uint16 *cf;                // kCf_*
  uint8 *cls;                // kCls_*
  uint8 *pk;                 // kPk_*
  int8 *wz;                  // world cells: the surface height (px)
  uint16 *wf;                // world cells: kWf_*
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
  uint32 backdrop_rgb;       // outdoors: the backdrop's color, as the caller's LUT has it
  int16 void_index;          // the void's texel: the room's filler, or 0 (see-through)
  uint32 hash_rest, hash_anim;
  uint16 anim_lo, anim_hi;   // chars of the animated range (anim_lo 0xffff: none)
  uint8 dark[16];            // BG colors that are near-black (bits)
  uint16 cgram[128];         // the BG colors when last read (for the dump)
  uint16 src_map[2][4096];   // the game's tilemaps as last read (outdoors: the map16 grid in [0])
  uint8 src_attr[2][4096];
  Layer layer[2];
  // Outdoors (layer 0, see "Geometry: outdoors"). Picture cells: the level
  // of the ground (terraces) and, on the faces below a ledge, the level of
  // their top (0: not a face). World cells: a slab floating over the ground
  // (canopy edges) from slab_lo up to slab_hi (slab_hi <= slab_lo: none).
  int8 ow_level[kMaxCells], ow_face[kMaxCells];
  uint8 ow_fdir[kMaxCells];   // a face: 1 + which way it drops (0: not a face)
  int8 slab_lo[kMaxCells], slab_hi[kMaxCells];
  uint8 ow_front[kMaxCells];  // world cells: how far south the block at least this high goes (cells; 0x80: ground)
  int16 ow_zmax;             // the highest surface
  // Outdoors: world rows past the south edge (and north of the north one)
  // where surfaces of the edge's raised (lowered) picture rows land.
  int8 ow_skirt[2][kMaxN][kOwSkirt];
  PanelLine *lines;
  int num_lines;
  PanelCell *pcells;
  int num_pcells;
  PropCard *props;
  int num_props;
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
static uint8 g_own_b[kMaxCells];
static int16 g_own_line[kMaxCells];
static int16 g_strip_line[4][kMaxCells];          // the band strip a floor cell starts, per direction (-1: none)
static uint8 g_strip_len[4][kMaxCells];           // and its length
static int16 g_line_of[2][4][kMaxN + 1];          // [layer][dir][hinge / 8]: the line (-1: none)
static uint8 g_dist[kMaxCells];                   // chessboard distance to ground
static int8 g_col_z[kMaxN];
static int16 g_col_row[kMaxN];
// (Outdoors.)
static int16 g_region[kMaxCells];                 // terrace region of a picture cell (-1: a ledge)
static uint8 g_ow[kMaxCells];                     // kOw_* of a picture cell
static int16 g_obj[kMaxCells];                    // the object (connected solid cells) of a picture cell
static int8 g_open[kMaxN];                        // world rows of a column: open below this height (-128: not)
static int8 g_head[kMaxN];                        // and where its slab's underside goes, room permitting (a Link's height up)
static uint8 g_wground[kMaxN];                    // world rows of a column: the ground's own surface (not a prop's)
static int16 g_col_h[kMaxN];                      // world rows of a column: the highest surface

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
  size_t per_layer = (size_t)cells * (2 + 1 + 2 + 1 + 1 + 1 + 2 + 4) + (size_t)chunks * 2 + (size_t)size * size +
                     16 * 12;  // (the arrays below, each rounded up to 16 bytes)
  size_t total = per_layer * layers + (panels ? sizeof(PanelLine) * kMaxLines + sizeof(PanelCell) * kMaxPanelCells +
                                                    sizeof(PropCard) * kMaxProps + 48 : 0);
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
    ly->wf = (uint16 *)Carve(&p, cells * 2);
    ly->rec[0] = (int16 *)Carve(&p, cells * 2);
    ly->rec[1] = (int16 *)Carve(&p, cells * 2);
    ly->zlo = (int8 *)Carve(&p, chunks);
    ly->zhi = (int8 *)Carve(&p, chunks);
    ly->tex = (uint8 *)Carve(&p, (size_t)size * size);
  }
  s->lines = panels ? (PanelLine *)Carve(&p, sizeof(PanelLine) * kMaxLines) : NULL;
  s->pcells = panels ? (PanelCell *)Carve(&p, sizeof(PanelCell) * kMaxPanelCells) : NULL;
  s->props = panels ? (PropCard *)Carve(&p, sizeof(PropCard) * kMaxProps) : NULL;
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
  s->num_lines = s->num_pcells = s->num_props = 0;
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
    uint16 f = ly->cf[i] & ~(kCf_Thin | kCf_Shell | kCf_Panel | kCf_Overhead | kCf_Prop);
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
      } else if (c == kCls_Ground && !s->indoors && (f & kCf_Prio) && !(ly->attr[i] >= 0x28 && ly->attr[i] <= 0x2f)) {
        c = kCls_Wall;   // over Link's head: canopy rims, eaves, bridge decks (a ledge stays a ledge)
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

static void AddPanelCell(Hd3dScene *s, int L, int cell, int line, int d, int b, uint8 other) {
  Layer *ly = &s->layer[L];
  int slot = ly->rec[0][cell] < 0 ? 0 : ly->rec[1][cell] < 0 ? 1 : -1;
  if (slot < 0 || s->num_pcells >= kMaxPanelCells)
    return;
  PanelCell *p = &s->pcells[s->num_pcells];
  p->cell = (uint16)cell, p->line = (uint16)line, p->d = (uint8)d, p->other = other, p->s0 = p->s1 = 0;
  p->b = (uint8)(IntMax(b & ~kBandKinds, d) | (b & kBandKinds));
  ly->rec[slot][cell] = (int16)s->num_pcells++;
  ly->cf[cell] |= kCf_Panel;
  if (d > s->lines[line].bmax)
    s->lines[line].bmax = (uint8)d;
}

static bool LintelCandidate(const Layer *ly, int i) {
  return ly->cls[i] == kCls_Ground && (ly->cf[i] & kCf_Prio) && ly->rec[0][i] < 0;
}

// The (line, d) two cells share, on a line of one of the two directions (-1: none), and the band there.
static int SharedBand(const Hd3dScene *s, const Layer *ly, int i, int j, int dir_a, int dir_b, int *d, int *band) {
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
        *d = pa->d, *band = pa->b;
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
        int d, band, line = vertical ? SharedBand(s, ly, before, after, kDir_W, kDir_E, &d, &band)
                                     : SharedBand(s, ly, before, after, kDir_N, kDir_S, &d, &band);
        if (line < 0)
          continue;
        for (int k = v0; k < v; k++)
          AddPanelCell(s, L, vertical ? k * n + u : u * n + k, line, d, band, kDir_None);
      }
    }
  }
}

// What ends the band of b shell cells from floor cell (r, c) that way, in a
// Normal room (two floors):
// - the upper floor (BG2), for a wall of the lower floor (BG1): a step up to
//   it (kBandStep), such as the sides of the castle's sunken walkways;
// - the lower floor, seen where the upper one is empty, for a wall of the
//   upper floor: the upper floor's painted edge (kBandLip), which stays flat.
// Anything else (the void, the room's edge, floor of the same level past a
// thin wall) makes a room wall (0).
static int BandEnd(const Hd3dScene *s, int L, int r, int c, int dir, int b) {
  const Layer *ly = &s->layer[L];
  int n = s->n, rr = r + kDirY[dir] * (b + 1), cc = c + kDirX[dir] * (b + 1);
  if (s->bg1_role != kHd3dBg1_LowerFloor || b >= kHd3dBandMax || rr < 0 || rr >= n || cc < 0 || cc >= n)
    return 0;
  int i = rr * n + cc;
  if (L == 0 && ly->cls[i] == kCls_Empty && (s->src_map[1][i] & 0x3ff) != kEmptyChar)
    return kBandLip;
  uint8 up = s->layer[0].cls[i];
  return L == 1 && up != kCls_Empty && up != kCls_Void ? kBandStep : 0;
}

// Whether the wall of direction dir has a band strip from the floor cell at
// (fl, along) (fl: its floor row for north and south walls, else column).
static bool StripAt(const Hd3dScene *s, int dir, int fl, int along) {
  int n = s->n;
  if (fl < 0 || fl >= n || along < 0 || along >= n)
    return false;
  return g_strip_line[dir][dir == kDir_N || dir == kDir_S ? fl * n + along : along * n + fl] >= 0;
}

// Walls at a diagonal: where the floor's edge steps one cell from one column
// of a wall to the next, the two columns meet half way (4 px), so in first
// person the wall is one slanted face, not a staircase of strips with gaps
// between them. (Only there: the third-person views keep the painted steps.)
static void SlantPanels(Hd3dScene *s, int first) {
  int n = s->n;
  for (int i = first; i < s->num_pcells; i++) {
    PanelCell *p = &s->pcells[i];
    const PanelLine *ln = &s->lines[p->line];
    int dir = ln->dir, along = dir == kDir_N || dir == kDir_S ? p->cell % n : p->cell / n;
    int fl = dir == kDir_N || dir == kDir_W ? ln->hinge / 8 : ln->hinge / 8 - 1;
    if (!StripAt(s, dir, fl, along))
      continue;  // (corners and door arches stay straight)
    for (int end = 0; end < 2; end++) {
      int nb = along + (end ? 1 : -1), off = 0;
      if (!StripAt(s, dir, fl, nb))
        off = StripAt(s, dir, fl + 1, nb) ? 4 : StripAt(s, dir, fl - 1, nb) ? -4 : 0;
      if (end)
        p->s1 = (int8)off;
      else
        p->s0 = (int8)off;
    }
  }
}

// The band of each column is as many cells as the column really has: a
// thin wall between two floors is shared by its two faces (each takes the
// cells nearer to it), so each face stands with half of it, as tall as a
// whole wall.
static void ColumnBands(Hd3dScene *s, int L, int first) {
  const Layer *ly = &s->layer[L];
  int n = s->n;
  for (int i = first; i < s->num_pcells; i++) {
    const PanelCell *top = &s->pcells[i];
    int dir = s->lines[top->line].dir, r = top->cell / n + kDirY[dir], c = top->cell % n + kDirX[dir];
    bool above = false;  // (another cell of this column further out)
    for (int k = 0; k < 2 && r >= 0 && r < n && c >= 0 && c < n; k++) {
      int rec = ly->rec[k][r * n + c];
      above |= rec >= 0 && s->pcells[rec].line == top->line && s->pcells[rec].d > top->d;
    }
    if (above)
      continue;
    for (int d = 0; d < top->d; d++) {
      int rr = top->cell / n - d * kDirY[dir], cc = top->cell % n - d * kDirX[dir], j = rr * n + cc;
      if (rr < 0 || rr >= n || cc < 0 || cc >= n)
        break;
      for (int k = 0; k < 2; k++) {
        int rec = ly->rec[k][j];
        if (rec < 0 || s->pcells[rec].line != top->line)
          continue;
        PanelCell *p = &s->pcells[rec];
        if ((p->b & ~kBandKinds) > top->d)
          p->b = (uint8)(top->d | (p->b & kBandKinds));
      }
    }
  }
}

static void BuildShell(Hd3dScene *s, int L, int *hist_n, int *hist_all) {
  Layer *ly = &s->layer[L];
  int n = s->n, cells = n * n, first = s->num_pcells;
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
      g_strip_line[dir][i] = (int16)line, g_strip_len[dir][i] = (uint8)(b | BandEnd(s, L, r, c, dir, b));
      if (!(g_strip_len[dir][i] & kBandKinds)) {  // (only room walls set the room's wall height)
        hist_all[b]++;
        if (dir == kDir_N)
          hist_n[b]++;
      }
      for (int k = 1; k <= b; k++) {
        int j = (r + kDirY[dir] * k) * n + c + kDirX[dir] * k;
        if (k < g_own_d[j] || (k == g_own_d[j] && dir < s->lines[g_own_line[j]].dir))
          g_own_d[j] = (uint8)k, g_own_b[j] = g_strip_len[dir][i], g_own_line[j] = (int16)line;
      }
    }
  }
  for (int i = 0; i < cells; i++)
    if (g_own_d[i] != 0xff)
      AddPanelCell(s, L, i, g_own_line[i], g_own_d[i], g_own_b[i], kDir_None);
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
    int r = i / n, c = i % n, bs = 99, by = -1, bx = -1, bfr = 0, bfc = 0, lby = 0, lbx = 0;
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
          if ((ly_line >= 0 || lx_line >= 0) && sum < bs) {
            bs = sum, by = ly_line, bx = lx_line, bfr = fr, bfc = fc;
            lby = g_strip_len[fr > 0 ? kDir_N : kDir_S][f], lbx = g_strip_len[fc > 0 ? kDir_W : kDir_E][f];
          }
        }
      }
    }
    // (Not past the band of the floor it goes with: no wall stands under it
    // there, so it would float.)
    if (by >= 0 && abs(bfr) > (lby & ~kBandKinds))
      by = -1;
    if (bx >= 0 && abs(bfc) > (lbx & ~kBandKinds))
      bx = -1;
    if (by < 0 && bx < 0)
      continue;
    uint8 dir_y = bfr > 0 ? kDir_N : kDir_S, dir_x = bfc > 0 ? kDir_W : kDir_E;
    if (by >= 0)
      AddPanelCell(s, L, i, by, abs(bfr), lby, bx >= 0 ? dir_x : kDir_None);
    if (bx >= 0)
      AddPanelCell(s, L, i, bx, abs(bfc), lbx, by >= 0 ? dir_y : kDir_None);
  }
  AddLintels(s, L);
  SlantPanels(s, first);
  ColumnBands(s, L, first);
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

// ---- Geometry: small props indoors ----

static bool PropSolid(const Layer *ly, int i) {
  return IsSolid(ly->cls[i]) && !(ly->cf[i] & (kCf_Shell | kCf_Panel | kCf_Prop));
}

// Pots, skulls and bushes: a piece of solid cells (4-connected,
// not part of a wall) that fits in kPropMaxCells x kPropMaxCells and stands on
// the floor becomes a PropCard. Floor, pit and its own cells may share its box
// (a statue's middle), nothing else may.
static void BuildProps(Hd3dScene *s, int L) {
  Layer *ly = &s->layer[L];
  int n = s->n, cells = n * n;
  memset(g_mark, 0, cells);
  for (int seed = 0; seed < cells && s->num_props < kMaxProps; seed++) {
    if (g_mark[seed] || !PropSolid(ly, seed))
      continue;
    int head = 0, tail = 0, c0 = n, r0 = n, c1 = -1, r1 = -1;
    bool big = false;
    g_list[tail++] = (int16)seed;
    g_mark[seed] = 1;
    while (head < tail) {
      int i = g_list[head++], r = i / n, c = i % n;
      c0 = IntMin(c0, c), c1 = IntMax(c1, c), r0 = IntMin(r0, r), r1 = IntMax(r1, r);
      if (c1 - c0 >= kPropMaxCells || r1 - r0 >= kPropMaxCells)
        big = true;
      for (int dir = 0; dir < 4; dir++) {
        int rr = r + kDirY[dir], cc = c + kDirX[dir], j = rr * n + cc;
        if (rr < 0 || rr >= n || cc < 0 || cc >= n || g_mark[j] || !PropSolid(ly, j))
          continue;
        g_mark[j] = 1;
        if (tail < kMaxCells)
          g_list[tail++] = (int16)j;
      }
    }
    // (The marks stay: a piece too big is a relief, and is not tried again.)
    if (big || r1 + 1 >= n)
      continue;
    // Only what can be picked up (pots, skulls, bushes) is a picture that
    // turns to face the camera: a chest, a statue or a torch stand reads
    // better as a block with its painted top and front.
    bool liftable = false;
    for (int k = 0; k < tail; k++)
      liftable |= ly->attr[g_list[k]] >= 0x50 && ly->attr[g_list[k]] <= 0x57;
    if (!liftable)
      continue;
    // The box may hold only this piece, floor and pits.
    for (int k = 0; k < tail; k++)
      g_mark[g_list[k]] = 2;
    bool ok = true;
    for (int r = r0; r <= r1 && ok; r++) {
      for (int c = c0; c <= c1 && ok; c++) {
        int i = r * n + c;
        uint8 cl = ly->cls[i];
        if (ly->cf[i] & (kCf_Shell | kCf_Panel | kCf_Prop))
          ok = false;
        else if (IsSolid(cl))
          ok = g_mark[i] == 2 && (ly->cf[i] & kCf_Decoded);
        else
          ok = cl == kCls_Ground || cl == kCls_Pit;
      }
    }
    for (int k = 0; k < tail; k++)
      g_mark[g_list[k]] = 1;
    // It stands on the floor: one of the cells right south of it is floor.
    bool foot = false;
    for (int c = c0; c <= c1; c++) {
      int f = (r1 + 1) * n + c;
      foot |= ly->cls[f] == kCls_Ground && !(ly->cf[f] & kCf_Panel);
    }
    if (!ok || !foot)
      continue;
    // Its top often overlaps the floor north of it, painted over Links
    // (walkable cells with the priority bit, like a canopy over a trunk).
    for (int k = 0; k < 2 && r0 > 0 && r1 - r0 + 1 < kPropMaxCells; k++) {
      bool over = false, walk = true;
      for (int c = c0; c <= c1; c++) {
        int i = (r0 - 1) * n + c;
        walk &= ly->cls[i] == kCls_Ground && !(ly->cf[i] & (kCf_Shell | kCf_Panel | kCf_Prop));
        over |= (ly->cf[i] & kCf_Prio) != 0;
      }
      if (!walk || !over)
        break;
      r0--;
    }
    for (int r = r0; r <= r1; r++)
      for (int c = c0; c <= c1; c++)
        ly->cf[r * n + c] |= kCf_Prop;
    PropCard *p = &s->props[s->num_props++];
    p->layer = (uint8)L, p->c0 = (uint8)c0, p->r0 = (uint8)r0;
    p->w = (uint8)(c1 - c0 + 1), p->h = (uint8)(r1 - r0 + 1);
  }
}

// ---- Geometry: the relief ----

static bool ReliefSolid(const Layer *ly, int i) {
  return IsSolid(ly->cls[i]) && !(ly->cf[i] & (kCf_Shell | kCf_Panel | kCf_Prop));
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
  memset(ly->wf, 0, (size_t)n * n * sizeof(uint16));
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
      } else if (f & kCf_Prop) {
        ly->pk[i] = kPk_Prop, r++;  // (the floor under it is inpainted)
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
        while (r < n && ly->cls[r * n + c] == kCls_Pit && !(ly->cf[r * n + c] & (kCf_Panel | kCf_Prop)))
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
        ly->wz[j] = g_col_z[r], ly->wf[j] = (uint16)(kWf_Set | (pk == kPk_Flat ? kWf_NoFace : 0));
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
      bool prop = pk == kPk_Prop;  // (the floor under a standing prop is at the base)
      int z = have && !prop ? above : base, src = r - z / 8, k = 1;
      // Texels from plain ground (or a pit's floor) 16 px steps north, which
      // keeps the floor patterns lined up. But where the surface in front of
      // it has see-through texels, its own (the same ones, so the picture
      // still shows what is behind them).
      if (!prop && (src < 0 || src >= n || (ly->cf[src * n + c] & kCf_Holes)))
        k = 5;
      for (; k <= 4; k++) {
        int sr = src - 2 * k;
        if (sr >= 0 && sr < n && (ly->pk[sr * n + c] == kPk_Ground || ly->pk[sr * n + c] == kPk_PitFloor))
          break;
      }
      uint8 south = 0;
      if (k > 4 && prop) {
        // (Under a prop with no floor north of it, floor from the south.)
        for (k = 1; k <= 4; k++) {
          int sr = src + 2 * k;
          if (sr >= 0 && sr < n && ly->pk[sr * n + c] == kPk_Ground)
            break;
        }
        south = (uint8)kWf_South;
      }
      if (k > 4)
        k = 0, south = 0;
      ly->wz[j] = (int8)z, ly->wf[j] = (uint16)(kWf_Set | kWf_Inpaint | south | k << 4);
      above = z, have = true;
    }
  }
}

// ---- Geometry: outdoors ----
//
// The overworld's ground has levels. The game marks where it drops with
// ledges (attributes 28-2F: which way you jump off) and paints the drop as a
// cliff face below a south ledge. Terraces:
// - Regions: the cells you reach without crossing a ledge. Solid cells count
//   as passable, so a bush or a fence never splits one. A gap of a few cells
//   in a straight line of ledges (stairs or a slope down from a plateau) is
//   bridged at first.
// - A ledge says its high side is above its low side: by the painted face
//   below it (south ledges), else by the other drops of the same region (16
//   px without any). Levels follow, the lowest regions at 0. A bridge whose
//   sides don't come out that way (two plateaus side by side) was wrong: it
//   goes and the levels are worked out again.
// - A region on both sides of one of its own ledges (an opening no bridge
//   closed) is ambiguous: those ledges stay props, as before terraces.
// - Bridges become stairs, 8 px a step. Under the relief rule a step takes
//   two picture rows (a tread and a riser), so stairs reach a little into
//   the plateau and below it.
// - The solid cells below a ledge are its face; their top is its high side.
// Objects (connected solid cells that aren't faces) stand on the level under
// their foot: trees tall, with the canopy's walkable edges floating over the
// ground (slabs), houses a little taller, low props as before.

enum {
  kOw_Ledge = 1,             // a ledge
  kOw_Bridge = 2,            // a gap in a line of ledges, bridged
  kOw_Canopy = 4,            // drawn over Links: a canopy or roof cell, or an edge you walk under
  kOw_Tree = 8,              // a tree's canopy (hookshot attribute with the priority bit)
  kOw_Door = 16,             // a house's doorway: drawn as part of its front wall
  kOwRegions = 4096,
  kOwObjects = 4096,         // objects with their own height (more are measured column by column)
  kOwBridgeGap = 12,         // the widest gap bridged (cells)
  kOwMaxLevel = 64,          // the highest terrace (px)
  kOwMaxZ = 120,             // the highest surface (px; heights are int8)
  kOwTreeRows = 8,           // the tallest tree (cells)
  kOwSlab = 4,               // a floating canopy edge is at least this thick (px)
  kOwHeadroom = 24,          // and its underside this high over the ground, where it can be (a Link's height)
};

enum { kDrop_N, kDrop_S, kDrop_W, kDrop_E };
static const int8 kDropX[4] = { 0, 0, -1, 1 }, kDropY[4] = { -1, 1, 0, 0 };

typedef struct OwEdge {
  int16 lo, hi;              // regions: the low and the high side
  int8 h;                    // the drop (px; -1: unknown)
  uint8 cell_hi;             // (unused padding)
} OwEdge;

static OwEdge g_ow_edges[kMaxCells];
static uint16 g_obj_hist[kOwObjects][8];         // objects: how many columns have a front wall that high
static uint8 g_obj_wall[kOwObjects];
static int16 g_ow_level[kOwRegions];
static int16 g_ow_hdef[kOwRegions];
static uint8 g_ow_rflags[kOwRegions];

// Which way the ground drops at a ledge (-1: not a ledge). A diagonal ledge
// counts as its north or south half.
static int LedgeDrop(uint8 a) {
  switch (a) {
  case 0x28: case 0x2c: case 0x2e: return kDrop_N;
  case 0x29: case 0x2d: case 0x2f: return kDrop_S;
  case 0x2a: return kDrop_W;
  case 0x2b: return kDrop_E;
  default: return -1;
  }
}

static bool OwWalk(const Layer *ly, int i) {
  return ly->cls[i] == kCls_Ground || (ly->cf[i] & kCf_Overhead);
}

static bool OwMass(const Layer *ly, int i) {
  return IsSolid(ly->cls[i]) || (ly->cf[i] & kCf_Overhead);
}

// Regions: flood over everything but ledges and bridges. Returns the count (-1: too many).
static int OwRegions(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int n = s->n, cells = n * n, count = 0;
  for (int i = 0; i < cells; i++)
    g_region[i] = (g_ow[i] & (kOw_Ledge | kOw_Bridge)) ? -1 : -2;
  for (int i = 0; i < cells; i++) {
    if (g_region[i] != -2)
      continue;
    if (count >= kOwRegions)
      return -1;
    int head = 0, tail = 0;
    g_region[i] = (int16)count;
    g_list[tail++] = (int16)i;
    while (head < tail) {
      int j = g_list[head++], r = j / n, c = j % n;
      for (int d = 0; d < 4; d++) {
        int rr = r + kDropY[d], cc = c + kDropX[d];
        if (rr < 0 || rr >= n || cc < 0 || cc >= n || g_region[rr * n + cc] != -2)
          continue;
        // Up or down only between cells that aren't solid: a cliff's body
        // never joins its top to its foot.
        if (d <= kDrop_S && (OwMass(ly, j) || OwMass(ly, rr * n + cc)))
          continue;
        g_region[rr * n + cc] = (int16)count;
        g_list[tail++] = (int16)(rr * n + cc);
      }
    }
    count++;
  }
  return count;
}

// The region of the ground that way from cell i: past other ledges (a step
// or two) and, on the low side, past the solid cells of its face; -1: none.
// (Only walkable ground counts: a ledge in a cliff's body says nothing.)
static int OwSide(const Hd3dScene *s, int i, int d, bool low) {
  const Layer *ly = &s->layer[0];
  int n = s->n, r = i / n, c = i % n, ledges = 0, solid = 0;
  for (int k = 1; k <= 16; k++) {
    int rr = r + kDropY[d] * k, cc = c + kDropX[d] * k, j = rr * n + cc;
    if (rr < 0 || rr >= n || cc < 0 || cc >= n)
      return -1;
    if (g_ow[j] & kOw_Ledge) {
      if (++ledges > 3 || solid)
        return -1;
    } else if (g_region[j] >= 0 && OwWalk(ly, j)) {
      return g_region[j];
    } else if (!low || ++solid > 12 || !OwMass(ly, j)) {
      return -1;
    }
  }
  return -1;
}

// The painted face below a south ledge: how many solid cells down to the ground (px; -1: none found).
static int OwFaceDrop(const Hd3dScene *s, int i) {
  const Layer *ly = &s->layer[0];
  int n = s->n, r = i / n, c = i % n, solid = 0;
  for (int rr = r + 1; rr < n && rr <= r + 12; rr++) {
    int j = rr * n + c;
    if (g_ow[j] & kOw_Ledge)
      continue;
    if (OwWalk(ly, j))
      return solid ? 8 * solid : -1;
    solid++;
  }
  return -1;
}

// A corner of a raised platform (a diagonal ledge).
static bool LedgeDiagonal(uint8 a) {
  return a >= 0x2c && a <= 0x2f;
}

// Bridges the gaps in straight lines of ledges that drop the same way.
// (Not from or to a corner, and not a row right above a ledge that drops
// the same way: that is the threshold of a platform, not a gap in a line,
// and bridging it would leave the ledge below without a high side.)
static void OwBridge(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int n = s->n;
  for (int i = 0; i < n * n; i++) {
    int d = LedgeDrop(ly->attr[i]);
    if (!(g_ow[i] & kOw_Ledge) || d < 0 || LedgeDiagonal(ly->attr[i]))
      continue;
    // Along the line: east for north and south drops, south for west and east ones.
    int sx = d <= kDrop_S ? 1 : 0, sy = 1 - sx, r = i / n, c = i % n;
    int nr = r + sy, nc = c + sx;
    if (nr >= n || nc >= n || (g_ow[nr * n + nc] & kOw_Ledge))
      continue;  // (not the end of a line)
    int k = 1;
    for (; k <= kOwBridgeGap + 1; k++) {
      int rr = r + sy * k, cc = c + sx * k;
      if (rr >= n || cc >= n) {
        k = 99;
        break;
      }
      int j = rr * n + cc;
      if (g_ow[j] & kOw_Ledge) {
        if (LedgeDrop(ly->attr[j]) != d || LedgeDiagonal(ly->attr[j]))
          k = 99;
        break;
      }
      if (ly->cls[j] == kCls_Empty || ly->cls[j] == kCls_Pit) {
        k = 99;
        break;
      }
      int dr = rr + kDropY[d], dc = cc + kDropX[d];
      if (dr >= 0 && dr < n && dc >= 0 && dc < n && (g_ow[dr * n + dc] & kOw_Ledge) &&
          LedgeDrop(ly->attr[dr * n + dc]) == d) {
        k = 99;
        break;
      }
    }
    if (k < 2 || k > kOwBridgeGap + 1)
      continue;
    for (int m = 1; m < k; m++)
      g_ow[(r + sy * m) * n + c + sx * m] |= (uint8)(kOw_Bridge | d << 5);
  }
}

static int CompareEdges(const void *a, const void *b) {
  const OwEdge *x = (const OwEdge *)a, *y = (const OwEdge *)b;
  if (x->lo != y->lo)
    return x->lo - y->lo;
  if (x->hi != y->hi)
    return x->hi - y->hi;
  return x->h - y->h;
}

// The median of known drops in edges [from, to) (sorted by h): -1 if none.
static int MedianDrop(const OwEdge *e, int from, int to) {
  int k0 = from;
  while (k0 < to && e[k0].h < 0)
    k0++;
  return k0 < to ? e[k0 + (to - k0) / 2].h : -1;
}

// Levels of the regions from the ledges between them. False: none (a cycle).
static bool OwSolve(Hd3dScene *s, int regions) {
  int n = s->n, ne = 0;
  memset(g_ow_rflags, 0, (size_t)regions);
  for (int i = 0; i < n * n; i++) {
    if (!(g_ow[i] & kOw_Ledge))
      continue;
    int d = LedgeDrop(s->layer[0].attr[i]);
    int hi = OwSide(s, i, d ^ 1, false), lo = OwSide(s, i, d, true);
    if (hi < 0 || lo < 0)
      continue;
    if (hi == lo) {
      g_ow_rflags[hi] |= 1;  // (both sides of its own ledge)
      continue;
    }
    OwEdge *e = &g_ow_edges[ne++];
    e->lo = (int16)lo, e->hi = (int16)hi, e->cell_hi = 0;
    e->h = (int8)(d == kDrop_S ? IntMin(OwFaceDrop(s, i), kOwMaxLevel) : -1);
  }
  qsort(g_ow_edges, (size_t)ne, sizeof(OwEdge), CompareEdges);
  // Pairs of regions: the median drop each; a region's own default is the
  // median over its pairs that know one.
  int np = 0;
  for (int a = 0; a < ne;) {
    int b = a;
    while (b < ne && g_ow_edges[b].lo == g_ow_edges[a].lo && g_ow_edges[b].hi == g_ow_edges[a].hi)
      b++;
    OwEdge p = g_ow_edges[a];
    p.h = (int8)MedianDrop(g_ow_edges, a, b);
    g_ow_edges[np++] = p;
    a = b;
  }
  for (int r = 0; r < regions; r++)
    g_ow_level[r] = 0, g_ow_hdef[r] = 0;
  for (int k = 0; k < np; k++) {
    int hi = g_ow_edges[k].hi;
    g_ow_rflags[hi] |= 2, g_ow_rflags[g_ow_edges[k].lo] |= 2;  // (has a level of its own)
    if (g_ow_edges[k].h > g_ow_hdef[hi])
      g_ow_hdef[hi] = g_ow_edges[k].h;
  }
  for (int round = 0; round < 48; round++) {
    bool changed = false;
    for (int k = 0; k < np; k++) {
      const OwEdge *e = &g_ow_edges[k];
      int h = e->h > 0 ? e->h : g_ow_hdef[e->hi] > 0 ? g_ow_hdef[e->hi] : 16;
      int want = IntMin(g_ow_level[e->lo] + h, 4 * kOwMaxLevel);
      if (want > g_ow_level[e->hi])
        g_ow_level[e->hi] = (int16)want, changed = true;
    }
    if (!changed)
      return true;
  }
  return false;
}

// Stairs where a bridge stands: 8 px a step over two rows (or columns).
static void OwStairs(Hd3dScene *s, int i, int d) {
  const Layer *ly = &s->layer[0];
  int n = s->n, r = i / n, c = i % n;
  int hi = OwSide(s, i, d ^ 1, false), lo = OwSide(s, i, d, true);
  if (hi < 0 || lo < 0)
    return;
  int lh = g_ow_level[hi], ll = g_ow_level[lo], steps = (lh - ll) / 8;
  if (steps <= 0)
    return;
  // The stairs run along the drop, from the high side through the bridge.
  for (int k = -steps + 1; k <= steps; k++) {
    int rr = r + kDropY[d] * k, cc = c + kDropX[d] * k;
    if (rr < 0 || rr >= n || cc < 0 || cc >= n)
      continue;
    int j = rr * n + cc;
    int reg = g_region[j];
    if (j != i && reg != hi && reg != lo)
      continue;
    if (j != i && !OwWalk(ly, j))
      continue;
    int z = lh - 8 * ((k + steps) / 2);
    if (z < ll)
      z = ll;
    s->ow_level[j] = (int8)z;
  }
}

static void ComputeTerraces(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int n = s->n, cells = n * n;
  memset(s->ow_level, 0, (size_t)cells);
  memset(s->ow_face, 0, (size_t)cells);
  memset(s->ow_fdir, 0, (size_t)cells);
  for (int i = 0; i < cells; i++) {
    uint8 f = 0;
    if (OwWalk(ly, i) && !(ly->cf[i] & kCf_Overhead) && LedgeDrop(ly->attr[i]) >= 0)
      f |= kOw_Ledge;
    g_ow[i] = f;
  }
  OwBridge(s);
  int regions = -1;
  bool ok = false;
  for (int pass = 0; pass < 3; pass++) {
    regions = OwRegions(s);
    if (regions <= 0)
      return;
    ok = OwSolve(s, regions);
    if (!ok)
      return;
    // Bridges whose sides didn't come out higher on the high side go.
    bool removed = false;
    for (int i = 0; i < cells; i++) {
      if (!(g_ow[i] & kOw_Bridge))
        continue;
      int d = g_ow[i] >> 5 & 3, hi = OwSide(s, i, d ^ 1, false), lo = OwSide(s, i, d, true);
      if (hi < 0 || lo < 0 || hi == lo || g_ow_level[hi] <= g_ow_level[lo])
        g_ow[i] &= ~kOw_Bridge, removed = true;
    }
    if (!removed)
      break;
    if (pass == 2)
      return;  // (still changing: leave it flat)
  }
  // A region no ledge says anything about (a strip of cliff, ground walled
  // in by cliffs) is level with what is right below it, past solid cells
  // (else right above).
  static int16 inherit[kOwRegions];
  for (int r = 0; r < regions; r++)
    inherit[r] = -32768;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < cells; i++) {
      int reg = g_region[i];
      if (reg < 0 || (g_ow_rflags[reg] & 2) || inherit[reg] != -32768)
        continue;
      int step = pass ? -n : n, j = i + step, k = 0;
      while (j >= 0 && j < cells && OwMass(ly, j) && ++k < 24)
        j += step;
      if (j >= 0 && j < cells && g_region[j] >= 0 && (g_ow_rflags[g_region[j]] & 2))
        inherit[reg] = g_ow_level[g_region[j]];
    }
  }
  for (int r = 0; r < regions; r++)
    if (inherit[r] != -32768)
      g_ow_level[r] = inherit[r];
  // The biggest region is the ground (0); others are above or below it.
  static int32 size[kOwRegions];
  memset(size, 0, sizeof(size[0]) * (size_t)regions);
  for (int i = 0; i < cells; i++)
    if (g_region[i] >= 0)
      size[g_region[i]]++;
  int big = 0;
  for (int r = 1; r < regions; r++)
    if (size[r] > size[big])
      big = r;
  int base = g_ow_level[big];
  for (int r = 0; r < regions; r++)
    g_ow_level[r] = (int16)IntMin(IntMax(g_ow_level[r] - base, -kOwMaxLevel), kOwMaxLevel);
  for (int i = 0; i < cells; i++)
    if (g_region[i] >= 0)
      s->ow_level[i] = (int8)g_ow_level[g_region[i]];
  // Ledges are the rim of their high side; the solid cells below them are
  // their face (unless the region is on both sides of its own ledges).
  for (int i = 0; i < cells; i++) {
    if (!(g_ow[i] & kOw_Ledge))
      continue;
    int d = LedgeDrop(ly->attr[i]), hi = OwSide(s, i, d ^ 1, false), lo = OwSide(s, i, d, true);
    if (hi < 0)
      continue;
    s->ow_level[i] = (int8)g_ow_level[hi];
    if (lo < 0 || lo == hi || (g_ow_rflags[hi] & 1) || g_ow_level[hi] <= g_ow_level[lo])
      continue;
    // (A band beside an east or west ledge is a few cells wide; past it, or
    // at a tree or a bush, it ends.)
    int r = i / n, c = i % n, reach = d == kDrop_W || d == kDrop_E ? 4 : 12;
    for (int k = 1; k <= reach; k++) {
      int rr = r + kDropY[d] * k, cc = c + kDropX[d] * k;
      if (rr < 0 || rr >= n || cc < 0 || cc >= n)
        break;
      int j = rr * n + cc;
      if (g_ow[j] & kOw_Ledge)
        continue;
      if (!OwMass(ly, j) || (ly->cf[j] & kCf_Overhead) || (g_ow[j] & kOw_Bridge))
        break;
      if (reach == 4 && ((ly->cf[j] & kCf_Prio) || ly->attr[j] == 0x27 || (ly->attr[j] >= 0x50 && ly->attr[j] <= 0x57)))
        break;
      if (!s->ow_fdir[j] || s->ow_face[j] < g_ow_level[hi])
        s->ow_face[j] = (int8)g_ow_level[hi], s->ow_fdir[j] = (uint8)(1 + d);
    }
  }
  for (int i = 0; i < cells; i++)
    if (g_ow[i] & kOw_Bridge)
      OwStairs(s, i, g_ow[i] >> 5 & 3);
}

// Doorways and cave mouths: walkable cells (up to 4 rows) under something
// solid at least 3 cells tall (a house, a cliff), with solid cells close on
// both sides. What is above them floats over them (a lintel): a real
// opening, as deep as the doorway. (Painted on the wall instead, a Link in
// the doorway would be behind it.)
static bool OwLiftable(const Layer *ly, int i) {
  return ly->attr[i] >= 0x50 && ly->attr[i] <= 0x57 && !(ly->cf[i] & kCf_Overhead);
}

// What a cell belongs to, for doorways: its object, or the faces below ledges.
static int OwDoorKey(const Hd3dScene *s, int i) {
  return s->ow_fdir[i] ? -3 : g_obj[i];
}

// The key of the tall solid right above cell i (through doorway cells above
// it, up to 4 rows): -2 if there is none.
static int OwDoorAbove(const Hd3dScene *s, int i) {
  const Layer *ly = &s->layer[0];
  int n = s->n, depth = 1;
  while (i >= 0 && (g_ow[i] & kOw_Door)) {
    if (++depth > 4)
      return -2;
    i -= n;
  }
  for (int k = 0; k < 3; k++)
    if (i - k * n < 0 || !OwMass(ly, i - k * n) || (ly->cf[i - k * n] & kCf_Overhead))
      return -2;
  return OwDoorKey(s, i);
}

static void OwDoors(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int n = s->n;
  for (int r = 3; r < n; r++) {
    for (int c = 1; c < n - 1; c++) {
      int i = r * n + c, key;
      if (!OwWalk(ly, i) || (ly->cf[i] & kCf_Overhead) || (g_ow[i] & kOw_Ledge) ||
          (key = OwDoorAbove(s, i - n)) == -2)
        continue;
      // The same house (or cliff) close on both sides.
      int sides = 0;
      for (int step = -1; step <= 1; step += 2) {
        for (int k = 1; k <= 3; k++) {
          int cc = c + step * k, j = r * n + cc;
          if (cc < 0 || cc >= n)
            break;
          if (OwMass(ly, j) && !(ly->cf[j] & kCf_Overhead)) {
            sides += OwDoorKey(s, j) == key && !OwLiftable(ly, j);  // (a bush beside a doorstep isn't a wall)
            break;
          }
          if (!OwWalk(ly, j) || OwDoorAbove(s, j - n) != key)
            break;
        }
      }
      if (sides == 2)
        g_ow[i] |= kOw_Door;
    }
  }
}

static bool OwSolid(const Layer *ly, int i) {
  return OwMass(ly, i);
}

static int CanopyFlags(const Layer *ly, int i) {
  uint16 f = ly->cf[i];
  if (f & kCf_Overhead)
    return kOw_Canopy;
  if (!IsSolid(ly->cls[i]) || !(f & kCf_Prio))
    return 0;
  return ly->attr[i] == 0x27 ? kOw_Canopy | kOw_Tree : kOw_Canopy;
}

// Objects: connected solid cells (and canopy edges) that aren't faces.
static void OwObjects(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int n = s->n, cells = n * n, count = 0;
  for (int i = 0; i < cells; i++)
    g_ow[i] &= kOw_Ledge | kOw_Bridge;
  for (int i = 0; i < cells; i++) {
    g_obj[i] = OwSolid(ly, i) && !s->ow_fdir[i] ? -2 : -1;
    g_ow[i] = (uint8)(g_ow[i] | CanopyFlags(ly, i));
  }
  for (int i = 0; i < cells; i++) {
    if (g_obj[i] != -2)
      continue;
    int head = 0, tail = 0;
    g_obj[i] = (int16)count;
    g_list[tail++] = (int16)i;
    while (head < tail) {
      int j = g_list[head++], r = j / n, c = j % n;
      for (int d = 0; d < 4; d++) {
        int rr = r + kDropY[d], cc = c + kDropX[d];
        if (rr < 0 || rr >= n || cc < 0 || cc >= n || g_obj[rr * n + cc] != -2)
          continue;
        g_obj[rr * n + cc] = (int16)count;
        g_list[tail++] = (int16)(rr * n + cc);
      }
    }
    count = count < 0x7ffe ? count + 1 : count;
  }
  OwDoors(s);
  // A house is as high all over: by the median front wall (the plain rows
  // at the bottom of its columns, a doorway counted as wall).
  int objs = IntMin(count, kOwObjects);
  memset(g_obj_hist, 0, sizeof(g_obj_hist[0]) * (size_t)objs);
  for (int c = 0; c < n; c++) {
    for (int r = n - 1; r >= 0; r--) {
      int o = g_obj[r * n + c];
      if (o < 0 || o >= objs || (r + 1 < n && g_obj[(r + 1) * n + c] == o))
        continue;  // (not the bottom of a run)
      int plain = 0;
      for (int k = r + 1; k < n && (g_ow[k * n + c] & kOw_Door); k++)
        plain++;
      for (int k = r; k >= 0 && g_obj[k * n + c] == o && !(g_ow[k * n + c] & kOw_Canopy); k--)
        plain++;
      g_obj_hist[o][IntMin(plain, 7)]++;
    }
  }
  for (int o = 0; o < objs; o++) {
    int total = 0, acc = 0, m = 0;
    for (int k = 0; k < 8; k++)
      total += g_obj_hist[o][k];
    while (m < 7 && 2 * (acc + g_obj_hist[o][m]) < total)
      acc += g_obj_hist[o][m++];
    g_obj_wall[o] = (uint8)m;
  }
}

// How a run [a, b) of solid cells in column c stands: its top and the
// bottom of its face (above the ground when its canopy floats over it).
static void OwRun(Hd3dScene *s, int c, int a, int b, int *top, int *bottom) {
  const Layer *ly = &s->layer[0];
  int n = s->n, len = b - a, foot = b < n ? b * n + c : (b - 1) * n + c;
  int g = s->ow_level[foot];
  // Over a doorway or a cave mouth it floats: its lintel.
  int door = 0;
  while (b + door < n && (g_ow[(b + door) * n + c] & kOw_Door))
    door++;
  // A face below a ledge: up to its high side, the rest flush with it.
  int face = -128;
  for (int k = a; k < b; k++)
    if (s->ow_fdir[k * n + c])
      face = IntMax(face, s->ow_face[k * n + c]);
  if (face > -128) {
    // (A drop taller than the painted face: the terrace overhangs it, which
    // keeps the picture: the higher surface wins.)
    // All face only where the terrace itself is right above (it is the top);
    // else (the band beside an east or west ledge) it keeps a top row.
    int above = (a - 1) * n + c, drop = (face - g) / 8;
    bool covered = a > 0 && OwWalk(ly, above) && s->ow_level[above] >= face;
    // Shorter than the drop (a cave mouth under it, a ragged foot): the
    // terrace overhangs the ground below, which stays open under it.
    if (!door && covered && drop > len)
      while (door < drop - len && b + door < n && OwWalk(ly, (b + door) * n + c) &&
             !(g_ow[(b + door) * n + c] & kOw_Ledge))
        door++;
    *bottom = g + 8 * door;
    *top = face > g ? IntMax(*bottom, g + 8 * IntMin(len + door - !covered, (face - g) / 8)) : g;
    return;
  }
  bool tree = false, canopy_only = true, edge_only = true;
  int prio = 0, low_only = 1, plain_bottom = 0;
  for (int k = a; k < b; k++) {
    int i = k * n + c;
    tree |= (g_ow[i] & kOw_Tree) != 0;
    canopy_only &= (g_ow[i] & kOw_Canopy) != 0;
    edge_only &= (ly->cf[i] & kCf_Overhead) != 0;
    prio += (g_ow[i] & kOw_Canopy) != 0;
    low_only &= ly->cls[i] == kCls_Low && !(ly->cf[i] & kCf_Prio);
  }
  for (int k = b - 1; k >= a && !(g_ow[k * n + c] & kOw_Canopy); k--)
    plain_bottom++;
  plain_bottom += door;
  // A tree's canopy, or an edge you walk under, floats at the foot line of
  // the object it belongs to (the nearest columns of it that reach lower).
  int b2 = b + door;
  if (!door && canopy_only && (tree || edge_only) && b < n) {
    int obj = g_obj[(b - 1) * n + c];
    for (int dc = -2; dc <= 2; dc++) {
      int cc = c + dc;
      if (!dc || cc < 0 || cc >= n || obj < 0)
        continue;
      for (int r = b - 1; r < IntMin(b + 4, n); r++) {
        int g2 = g_obj[r * n + cc];
        if (g2 == obj && r + 1 > b2)
          b2 = r + 1;
        else if (g2 != obj && r >= b)
          break;
      }
    }
    for (int k = b; k < b2; k++)
      tree |= (g_ow[k * n + c] & kOw_Tree) != 0;
    if (b2 > b)
      g = s->ow_level[b * n + c];  // (its own ground, under it)
  }
  int h;
  if (tree || (!door && b2 > b)) {
    // Tall, with a third of it (up to 3 rows) left for the top: a tree with
    // some depth to it, not a sign.
    int l2 = b2 - a;
    h = IntMin(l2 - IntMin(IntMax(l2 / 3, 1), 3), kOwTreeRows);
  } else if (prio) {
    int o = g_obj[(b - 1) * n + c];
    h = IntMin(IntMax((o >= 0 && o < kOwObjects ? g_obj_wall[o] : plain_bottom) + 2, 3), 6);
  } else if (low_only) {
    h = len >= 4 ? 2 : 1;
  } else {
    h = len < 4 ? len - 1 : IntMin(3 + (len - 4) / 2, 6);
    // One cell wide (open ground on both sides most of the way): a fence or
    // a row of posts running north and south, long but low.
    int open = 0;
    for (int k = a; k < b; k++)
      open += (c == 0 || !OwMass(ly, k * n + c - 1)) && (c == n - 1 || !OwMass(ly, k * n + c + 1));
    if (2 * open >= len)
      h = IntMin(h, 2);
  }
  int lo = g + 8 * (b2 - b), hi = g + 8 * h;
  // Within what the picture allows: a face of at most len - 1 rows (a top
  // of at least one). A one-cell prop is a cube on its own cell (as indoors).
  hi = IntMin(hi, lo + 8 * IntMax(len - 1, 0));
  if (len == 1 && b2 == b)
    hi = lo + 8;
  hi = IntMax(hi, lo);
  if (hi > kOwMaxZ)
    hi = lo + (kOwMaxZ - lo) / 8 * 8;
  *top = hi, *bottom = lo;
}

// Puts the picture rows [a, b) of solid cells in column c into the relief.
static void OwPlaceRun(Hd3dScene *s, int c, int a, int b) {
  Layer *ly = &s->layer[0];
  int n = s->n, top, bottom;
  OwRun(s, c, a, b, &top, &bottom);
  int foot = b < n ? s->ow_level[b * n + c] : s->ow_level[(b - 1) * n + c];
  int h = (top - bottom) / 8, len = b - a;
  if (bottom > foot) {
    // It floats over walkable ground: whatever lands over that ground
    // higher than its bottom is a slab, not a column down to the ground.
    int b2 = b + (bottom - foot) / 8;
    for (int w = b + foot / 8; w < b2 + foot / 8; w++)
      if (w >= 0 && w < n && (g_open[w] == -128 || g_open[w] > bottom))
        g_open[w] = (int8)bottom, g_head[w] = (int8)IntMin(foot + kOwHeadroom, kOwMaxZ);
  }
  bool face = false;
  for (int k = a; k < b; k++)
    face |= s->ow_fdir[k * n + c] != 0;
  if (len == 1 && bottom <= foot && !face && top > bottom) {
    // A cube on its own cell: the top shows the row above, the face the prop.
    ly->pk[a * n + c] = kPk_Top, g_col_z[a] = (int8)top, g_col_row[a] = (int16)(a + bottom / 8);
    return;
  }
  if (h >= len) {
    for (int k = a; k < b; k++)  // (all face: the top is the terrace above)
      ly->pk[k * n + c] = kPk_Face;
    return;
  }
  if (top == bottom && bottom <= foot) {
    for (int k = a; k < b; k++)  // (flush with the ground)
      ly->pk[k * n + c] = kPk_Ground, g_col_z[k] = (int8)top, g_col_row[k] = (int16)(k + top / 8);
    return;
  }
  for (int k = a; k < b - h; k++)
    ly->pk[k * n + c] = kPk_Top, g_col_z[k] = (int8)top, g_col_row[k] = (int16)(k + top / 8);
  for (int k = b - h; k < b; k++)
    ly->pk[k * n + c] = kPk_Face;
}

// The outdoor relief: like BuildRelief, with terraces under everything and
// floating canopy edges (slabs).
static void BuildReliefOutdoors(Hd3dScene *s) {
  Layer *ly = &s->layer[0];
  int n = s->n;
  ComputeTerraces(s);
  OwObjects(s);
  memset(ly->wz, 0, (size_t)n * n);
  memset(ly->wf, 0, (size_t)n * n * sizeof(uint16));
  memset(s->slab_lo, 0, (size_t)n * n);
  memset(s->slab_hi, 0, (size_t)n * n);
  for (int c = 0; c < n; c++) {
    memset(g_open, -128, sizeof(g_open));
    memset(g_wground, 1, sizeof(g_wground));
    for (int r = 0; r < n;) {
      int i = r * n + c;
      uint8 cl = ly->cls[i];
      if (cl == kCls_Empty) {
        ly->pk[i] = kPk_Hole, r++;
      } else if (OwSolid(ly, i)) {
        int a = r;
        while (r < n && OwSolid(ly, r * n + c))
          r++;
        // Stacked objects stand on their own: a bush or rock (a 16 px liftable
        // object, on the 16 px grid) is cut from what is above or below it.
        for (int b = r, k = r - 1; k >= a; k--) {
          int j = k * n + c;
          if (k == a || OwLiftable(ly, j) != OwLiftable(ly, j - n) || (OwLiftable(ly, j) && !(k & 1))) {
            OwPlaceRun(s, c, k, b);
            b = k;
          }
        }
      } else if (cl == kCls_Pit) {
        int a = r, base = s->ow_level[i];
        while (r < n && ly->cls[r * n + c] == kCls_Pit)
          r++;
        uint8 rim = a > 0 ? ly->pk[(a - 1) * n + c] : kPk_Hole;
        bool sink = rim == kPk_Ground && g_col_z[a - 1] == base;
        int len = r - a, wall = sink ? IntMin(kHd3dPitDepth / 8, len - 1) : 0;
        for (int k = a; k < a + wall; k++)
          ly->pk[k * n + c] = kPk_PitWall;
        for (int k = a + wall; k < r; k++)
          ly->pk[k * n + c] = kPk_PitFloor, g_col_z[k] = (int8)(base - 8 * wall),
          g_col_row[k] = (int16)(k - wall + base / 8);
      } else {
        int z = s->ow_level[i];
        ly->pk[i] = kPk_Ground, g_col_z[r] = (int8)z, g_col_row[r] = (int16)(r + z / 8), r++;
      }
    }
    // The surfaces land; the higher one wins. Over open ground (doorways,
    // under a canopy's edge) what is higher than the opening is a slab.
    memset(s->ow_skirt[0][c], -128, kOwSkirt);
    memset(s->ow_skirt[1][c], -128, kOwSkirt);
    for (int r = 0; r < n; r++) {
      uint8 pk = ly->pk[r * n + c];
      int wr = g_col_row[r];
      if (!HasSurface(pk))
        continue;
      if (wr < 0 || wr >= n) {
        int side = wr < 0, k = side ? -1 - wr : wr - n;
        if (k < kOwSkirt && g_col_z[r] > s->ow_skirt[side][c][k])
          s->ow_skirt[side][c][k] = g_col_z[r];
        continue;
      }
      int j = wr * n + c;
      if (g_open[wr] != -128 && g_col_z[r] > g_open[wr]) {
        if (s->slab_hi[j] <= s->slab_lo[j] || g_col_z[r] > s->slab_hi[j])
          s->slab_hi[j] = g_col_z[r], s->slab_lo[j] = (int8)IntMin(g_open[wr], g_col_z[r] - kOwSlab);
        continue;
      }
      if (!(ly->wf[j] & kWf_Set) || g_col_z[r] > ly->wz[j])
        ly->wz[j] = g_col_z[r], ly->wf[j] = kWf_Set, g_wground[wr] = pk == kPk_Ground || pk == kPk_PitFloor;
    }
    // Holes, and inpainting what was uncovered (texels from the plain
    // ground north of it, as indoors).
    int above = 0;
    bool have = false;
    for (int r = 0; r < n; r++) {
      int j = r * n + c;
      if (ly->wf[j] & kWf_Set) {
        above = ly->wz[j], have = true;
        continue;
      }
      if (ly->pk[j] == kPk_Hole) {
        ly->wf[j] = kWf_Empty;
        have = false;
        continue;
      }
      // (Outdoors tall things uncover a lot: up to 64 px north, else 48 south
      // (stored as 5-7). A see-through texel in front shows the backdrop,
      // which is the ground's color; plain ground is close enough.)
      // (Not above the ground there: a gap between stacked props would hide
      // the face of the one behind.)
      int z = have ? IntMin(above, s->ow_level[j]) : s->ow_level[j], src = r - z / 8, k = 1;
      for (; k <= 7; k++) {
        int sr = k <= 4 ? src - 2 * k : src + 2 * (k - 4);
        if (sr >= 0 && sr < n && ly->pk[sr * n + c] == kPk_Ground && !(ly->cf[sr * n + c] & kCf_Overhead))
          break;
      }
      if (k > 7)
        k = 0;
      // (Seen through the see-through texels of what is in front of it, it
      // shows what the picture shows there: the backdrop.)
      int flat = src >= 0 && src < n && (ly->cf[src * n + c] & kCf_Holes) ? kWf_Flat : 0;
      ly->wz[j] = (int8)z, ly->wf[j] = (uint16)(kWf_Set | kWf_Inpaint | k << 4 | flat);
      above = z, have = true;
    }
    // A slab resting on what is under it is part of it; so is one over
    // nothing that landed (only real ground stays open under a slab).
    for (int r = 0; r < n; r++) {
      int j = r * n + c;
      if (s->slab_hi[j] <= s->slab_lo[j])
        continue;
      if (ly->wz[j] >= s->slab_lo[j] || (ly->wf[j] & kWf_Inpaint)) {
        if (s->slab_hi[j] > ly->wz[j])
          ly->wz[j] = s->slab_hi[j], ly->wf[j] = kWf_Set;
        s->slab_hi[j] = s->slab_lo[j] = 0;
      }
    }
    // Under a slab there is room for a Link where the wall behind the
    // opening is tall enough to fill in what the raised edge uncovers (over
    // a passage it stays where the picture has it).
    for (int w = 0, back = -128; w < n; w++) {
      int j = w * n + c;
      if (g_open[w] == -128) {
        back = (ly->wf[j] & kWf_Set) ? ly->wz[j] : -128;
        continue;
      }
      if (s->slab_hi[j] <= s->slab_lo[j] || back == -128 || !(ly->wf[j] & kWf_Set))
        continue;
      int lo = s->slab_lo[j], want = IntMin(g_head[w], s->slab_hi[j] - kOwSlab);
      s->slab_lo[j] = (int8)IntMax(lo, IntMin(want, lo + back - ly->wz[j]));
    }
    // Past the edges: a gap takes the height before it, toward the area.
    for (int side = 0; side < 2; side++) {
      int8 *sk = s->ow_skirt[side][c];
      int edge = side ? c : (n - 1) * n + c, last = kOwSkirt - 1;
      int8 prev = (int8)((ly->wf[edge] & kWf_Set) ? ly->wz[edge] : 0);
      while (last >= 0 && sk[last] == -128)
        last--;
      for (int k = 0; k <= last; k++)
        prev = sk[k] = (int8)(sk[k] != -128 ? sk[k] : prev);
    }
    // Where each block's front is (its side and back faces take its texels;
    // a slab in front counts: the back of a doorway shows the door).
    // (The next row south lower than this one, with a stack: rows whose
    // height rises going south.)
    int top = 0;
    for (int r = n - 1; r >= 0; r--) {
      int j = r * n + c, z = ly->wz[j];
      g_col_h[r] = (int16)((ly->wf[j] & kWf_Set) ? z : -128);
      if (s->slab_hi[j] > s->slab_lo[j])
        g_col_h[r] = (int16)IntMax(g_col_h[r], s->slab_hi[j]);
      while (top > 0 && g_col_h[g_col_row[top - 1]] >= z)
        top--;
      int next = top > 0 ? g_col_row[top - 1] : n;
      s->ow_front[j] = (uint8)(IntMin(next - r - 1, 32) | (g_wground[r] ? 0x80 : 0));
      g_col_row[top++] = (int16)r;
    }
  }
}

static void OwHeightRange(Hd3dScene *s) {
  const Layer *ly = &s->layer[0];
  int zmax = 0;
  for (int i = 0; i < s->n * s->n; i++) {
    if (ly->wf[i] & kWf_Set)
      zmax = IntMax(zmax, ly->wz[i]);
    if (s->slab_hi[i] > s->slab_lo[i])
      zmax = IntMax(zmax, s->slab_hi[i]);
  }
  s->ow_zmax = (int16)zmax;
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
            if (!s->indoors && s->slab_hi[r * n + c] > s->slab_lo[r * n + c])
              hi = IntMax(hi, s->slab_hi[r * n + c]);  // (outdoors: floating canopy edges)
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
  s->num_lines = s->num_pcells = s->num_props = 0;
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
      BuildProps(s, L);
    }
  }
  // East, west and south walls are as high as the north ones are wide.
  int b = MedianBand(hist_n);
  if (!b)
    b = MedianBand(hist_all);
  s->wall_height = (int16)(b ? 8 * IntMin(IntMax(b, 3), kHd3dBandMax) : 48);
  for (int L = 0; L < layers; L++) {
    if (s->indoors)
      BuildRelief(s, L);
    else
      BuildReliefOutdoors(s), OwHeightRange(s);
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

int Hd3dScene_GroundZ(const Hd3dScene *s, int layer, int x, int y) {
  if (!s || s->indoors || !s->ready)
    return Hd3dScene_WalkZ(s, layer);
  const Layer *ly = &s->layer[0];
  int n = s->n, px = x - s->origin_x, py = y - s->origin_y;
  if (px < 0 || py < 0 || px >= s->size || py >= s->size)
    return 0;
  int c = px >> 3, r = py >> 3, i = r * n + c;
  if (s->ow_fdir[i]) {
    // On the face below a ledge (jumping off it): down the face, so that in
    // the world the feet stay on it (y + z stays the same).
    int top = s->ow_face[i], d = s->ow_fdir[i] - 1;
    if (d == kDrop_S) {
      int b = r;
      while (b < n && s->ow_fdir[b * n + c])
        b++;
      int g = s->ow_level[IntMin(b, n - 1) * n + c];
      return IntMax(g, IntMin(top, 8 * b + g - py));
    }
    if (d == kDrop_W || d == kDrop_E) {
      // Across a cliff band to the east or west: from its high edge to its low one.
      int step = kDropX[d], c0 = c, c1 = c;
      while (c0 - step >= 0 && c0 - step < n && s->ow_fdir[r * n + c0 - step])
        c0 -= step;
      while (c1 >= 0 && c1 < n && s->ow_fdir[r * n + c1])
        c1 += step;
      int g = c1 >= 0 && c1 < n ? s->ow_level[r * n + c1] : 0, width = 8 * abs(c1 - c0);
      int along = step > 0 ? px - 8 * c0 : 8 * c0 + 8 - px;
      return width > 0 ? top + (g - top) * IntMax(IntMin(along, width), 0) / width : top;
    }
    return top;
  }
  int z = s->ow_level[i];
  // Stairs: between this row's level and the next one's.
  int f = py & 7, r2 = f >= 4 ? r + 1 : r - 1;
  if (r2 >= 0 && r2 < n && OwWalk(ly, r2 * n + c)) {
    int z2 = s->ow_level[r2 * n + c];
    if (z2 != z && abs(z2 - z) <= 8)
      z += (z2 - z) * (f >= 4 ? 2 * f - 7 : 7 - 2 * f) / 16;
  }
  return z;
}

int Hd3dScene_StandZ(const Hd3dScene *s, int layer, int x, int y) {
  int z = Hd3dScene_GroundZ(s, layer, x, y);
  if (!s || s->indoors || !s->ready)
    return z;
  // A row a little north lands at world y + its height: when it is higher
  // and lands on the foot (at the top of stairs, the ground and the wall over
  // a doorway climb as fast as the rows go north), it is in front of the foot
  // and would cut through what stands there. Standing on it instead moves
  // the foot along the test camera's line of sight: the picture stays the
  // same. (Only for a foot on walkable ground: in a doorway, the game draws
  // the wall over the Link.)
  const Layer *ly = &s->layer[0];
  int n = s->n, c = (x - s->origin_x) >> 3, r0 = (y - s->origin_y) >> 3;
  if (c < 0 || c >= n || r0 < 0 || r0 >= n)
    return z;
  if (!OwWalk(ly, r0 * n + c)) {
    // In a house's rows (walking out of its door, hidden behind the facade):
    // the house stands on the ground at its foot, so that is where he is,
    // not at its region's level (the terrace north of a house built against
    // a cliff, where he would stand on the roof).
    int i = r0 * n + c, rf = r0;
    if (!OwMass(ly, i) || (ly->cf[i] & kCf_Overhead) || s->ow_fdir[i])
      return z;
    while (rf + 1 < n && rf < r0 + 8 && OwMass(ly, (rf + 1) * n + c) && !(ly->cf[(rf + 1) * n + c] & kCf_Overhead))
      rf++;
    return rf + 1 < n && OwWalk(ly, (rf + 1) * n + c) ? s->ow_level[(rf + 1) * n + c] : z;
  }
  // In an opening (a doorway, a cave mouth): open ground under a slab with a
  // Link's headroom. Nothing lands on the foot; the game draws what is above
  // over the Link.
  int wr = (y + z - s->origin_y) >> 3, j = wr * n + c;
  if (wr >= 0 && wr < n && ly->wz[j] <= z && s->slab_hi[j] > s->slab_lo[j] && s->slab_lo[j] - z <= kOwHeadroom)
    return z;
  // Only where the walkable ground itself climbs just north (stairs): a
  // solid cell's level is only its region's (a house or a tree is built up
  // from its own foot), not a surface over the foot.
  bool climbs = false;
  for (int k = 1; k <= 4 && r0 - k >= 0; k++)
    climbs |= OwWalk(ly, (r0 - k) * n + c) && s->ow_level[(r0 - k) * n + c] > z;
  if (!climbs)
    return z;
  // (A step the stairs' own slope already covers stays: the ramp keeps going
  // down smoothly instead of jumping up a few px before each step.)
  for (int k = 1; k <= 4; k++) {
    int yn = (y & ~7) - 8 * k, zn = Hd3dScene_GroundZ(s, layer, x, yn + 4);
    if (zn > z + 8 && yn + zn <= y + z && y + z < yn + 8 + zn)
      z = zn;
  }
  return z;
}

// A door cell outdoors: walkable ground under a slab with a Link's headroom
// (a doorway, a cave mouth; as Hd3dScene_StandZ has it).
static bool OwDoorCell(const Hd3dScene *s, int r, int c) {
  const Layer *ly = &s->layer[0];
  int n = s->n, i = r * n + c;
  if (!OwWalk(ly, i) || (ly->cf[i] & kCf_Overhead))
    return false;
  int z = s->ow_level[i], wr = r + z / 8, j = wr * n + c;
  return wr >= 0 && wr < n && ly->wz[j] <= z && s->slab_hi[j] > s->slab_lo[j] && s->slab_lo[j] - z <= kOwHeadroom;
}

bool Hd3dScene_DoorEye(const Hd3dScene *s, int layer, int x, int y, float *eye_y) {
  if (!s || !s->ready)
    return false;
  int n = s->n, c = (x - s->origin_x) >> 3, r = (y - s->origin_y) >> 3;
  if (c < 0 || c >= n || r < 0 || r >= n)
    return false;
  if (s->indoors) {
    // The exit's corridor: the door cells (attribute 0x8e, the game's door
    // to the overworld) from the Link's up to the room's floor.
    const Layer *ly = &s->layer[layer < s->num_layers ? layer : 0];
    if (ly->attr[r * n + c] != 0x8e)
      return false;
    int ra = r;
    while (ra > 0 && ly->attr[(ra - 1) * n + c] == 0x8e)
      ra--;
    float mouth = (float)(s->origin_y + 8 * ra + LayerBase(s, layer) - 4);
    if (*eye_y > mouth)
      *eye_y = mouth;
    return true;
  }
  // The door cells under the Link, or under the house's rows he is in.
  const Layer *ly = &s->layer[0];
  int rd = r;
  if (!OwWalk(ly, r * n + c)) {
    if (!OwMass(ly, r * n + c) || (ly->cf[r * n + c] & kCf_Overhead) || s->ow_fdir[r * n + c])
      return false;
    while (rd + 1 < n && rd < r + 8 && OwMass(ly, (rd + 1) * n + c) && !(ly->cf[(rd + 1) * n + c] & kCf_Overhead))
      rd++;
    rd++;
  }
  if (rd >= n || !OwDoorCell(s, rd, c))
    return false;
  int rf = rd;
  while (rf + 1 < n && OwDoorCell(s, rf + 1, c))
    rf++;
  float foot = (float)(s->origin_y + 8 * (rf + 1) + s->ow_level[rf * n + c] + 12);
  if (*eye_y < foot)
    *eye_y = foot;
  return true;
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
  s->backdrop_rgb = lut ? lut[0] : 0;
}

typedef struct EmitCtx {
  Hd3dScene *s;
  const Hd3dEmit *e;
  R3dQuad *out;
  int max, num;
  int c0, r0, c1, r1;        // the emission rectangle, in world cells
  float rise;
  float fp;                  // Hd3dEmit.fp_blend (walls grow to kFpWallHeight)
  bool dark_void;            // indoors in first person: the void is a dark flat color
  float k[4];                // the fold of each wall direction
  float cphi[4], sphi[4];    // cos and sin of its angle (90 degrees times k)
  float plinth[4];           // px: the body a wall of that direction lies on as far as it lies flat
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

// Scenery near the line from the camera to the player's Link is faded (see
// through, with Links and the rest drawn whole over it): what stands above
// that line (outdoors a raised terrace further off is under it and stays
// solid).
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
    if (t < 0 && len2 > 64.0f * 64.0f && t * t * len2 > 16.0f * 16.0f)
      return;  // (well behind Link, away from the camera: it hides nothing)
    t = t < 0 ? 0 : t > 1 ? 1 : t;
  }
  float ex = e->fade_a[0] + dx * t - px, ey = e->fade_a[1] + dy * t - py;
  float za = e->fade_a[2] + 12.0f, zl = za + (e->fade_b[2] - za) * t;
  if (ex * ex + ey * ey < 28.0f * 28.0f && zt > zl)
    q->flags |= kR3dQuad_Faded;
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

// Indoors, the side and back faces of a raised block (a railing, a ledge, a
// table) show its painted front: each height shows the front's row at that
// height (not the slanted smear of one column the relief rule gives there),
// and along the side the columns wrap around the corner from the front, back
// and forth across the block's width. Called before the face is oriented.
static void SideFromFront(const Hd3dScene *s, int L, int c, int r, int dir, R3dQuad *q) {
  const Layer *ly = &s->layer[L];
  int n = s->n, z = ly->wz[r * n + c];
  if ((ly->wf[r * n + c] & kWf_Inpaint) || z <= LayerBase(s, L))
    return;
  // The front: the south end of this top, in this column.
  int rs = r + 1;
  while (rs < n && rs < r + 8 && (ly->wf[rs * n + c] & (kWf_Set | kWf_Inpaint)) == kWf_Set && ly->wz[rs * n + c] == z)
    rs++;
  float ys = (float)(s->origin_y + 8 * rs);
  for (int i = 0; i < 4; i++)
    q->v[i].v = ys - q->v[i].z;
  if (dir == kDir_N)
    return;
  // How wide the top is from this side inward (the wrap goes back and forth over it).
  int step = dir == kDir_E ? -1 : 1, wc = 1;
  for (; wc < 8; wc++) {
    int cc = c + step * wc;
    if (cc < 0 || cc >= n || (ly->wf[r * n + cc] & (kWf_Set | kWf_Inpaint)) != kWf_Set || ly->wz[r * n + cc] != z)
      break;
  }
  float wpx = 8.0f * (float)wc, edge = (float)(s->origin_x + 8 * (dir == kDir_E ? c + 1 : c));
  // (Both rows of this cell's face fall in one sweep: widths and the front are whole cells.)
  float d_mid = ys - (float)(s->origin_y + 8 * r) - 4.0f;
  int sweep = (int)(d_mid / wpx);
  for (int i = 0; i < 4; i++) {
    float t = ys - q->v[i].y - (float)sweep * wpx;
    if (sweep & 1)
      t = wpx - t;
    float in = 0.5f + t * (wpx - 1.0f) / wpx;  // (half a texel inside at both ends)
    q->v[i].u = dir == kDir_E ? edge - in : edge + in;
  }
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
  if (s->indoors && dir != kDir_S)
    SideFromFront(s, L, c, r, dir, q);
  Orient(q, (float)kDirX[dir], (float)kDirY[dir], 0);
  ApplyRise(x, q, LayerBase(s, L));
  Fade(x, q);
}

// Outdoors a face takes its texels from the front (south face) of the block
// it belongs to, front cells south of this one: the rows that front shows at
// each height, and on east and west faces the front's columns near the
// corner, wrapping round it for up to 16 px. A south face is a front: that
// is the relief rule. (Side faces by the relief rule had the picture's rows
// slanting across them: stripes, and shards at a canopy's ragged edge.)
// The side and back of a terrace (ground, 0x80 in front) fold its edge down
// instead: a cliff with the rim's art running down it.
static void EmitFaceOw(EmitCtx *x, int c, int r, int dir, int zlo, int zhi, int front) {
  static const uint8 kShade[4] = { 176, 216, 216, 255 };
  Hd3dScene *s = x->s;
  R3dQuad *q = NewQuad(x, x->e->tex_bg2, kShade[dir], kR3dClass_World);
  if (!q)
    return;
  bool ground = (front & 0x80) != 0;
  front &= 0x7f;
  float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f, y0 = (float)(s->origin_y + 8 * r), y1 = y0 + 8.0f;
  float zl = (float)zlo, zh = (float)zhi, yf = y1 + 8.0f * (float)front, fold = 0.5f * (zh - zl);
  if (dir == kDir_S) {
    SetVertex(&q->v[0], x0, y1, zh, x0, y1 - zh);
    SetVertex(&q->v[1], x1, y1, zh, x1, y1 - zh);
    SetVertex(&q->v[2], x1, y1, zl, x1, y1 - zl);
    SetVertex(&q->v[3], x0, y1, zl, x0, y1 - zl);
  } else if (dir == kDir_N) {
    float vt = ground ? y0 - zh : yf - zh, vb = ground ? y0 - zh + fold : yf - zl;
    SetVertex(&q->v[0], x0, y0, zh, x0, vt);
    SetVertex(&q->v[1], x1, y0, zh, x1, vt);
    SetVertex(&q->v[2], x1, y0, zl, x1, vb);
    SetVertex(&q->v[3], x0, y0, zl, x0, vb);
  } else if (ground) {
    float px = dir == kDir_E ? x1 : x0, in = dir == kDir_E ? -1.0f : 1.0f;
    float ut = px + in * 0.5f, ub = px + in * (0.5f + fold);
    SetVertex(&q->v[0], px, y0, zh, ut, y0 - zh);
    SetVertex(&q->v[1], px, y1, zh, ut, y1 - zh);
    SetVertex(&q->v[2], px, y1, zl, ub, y1 - zh);
    SetVertex(&q->v[3], px, y0, zl, ub, y0 - zh);
  } else {
    float px = dir == kDir_E ? x1 : x0, in = dir == kDir_E ? -1.0f : 1.0f;
    float w0 = yf - y0 < 16.0f ? yf - y0 : 16.0f, w1 = yf - y1 < 16.0f ? yf - y1 : 16.0f;
    float u0 = px + in * (0.5f + w0), u1 = px + in * (0.5f + w1);
    SetVertex(&q->v[0], px, y0, zh, u0, yf - zh);
    SetVertex(&q->v[1], px, y1, zh, u1, yf - zh);
    SetVertex(&q->v[2], px, y1, zl, u1, yf - zl);
    SetVertex(&q->v[3], px, y0, zl, u0, yf - zl);
  }
  Orient(q, (float)kDirX[dir], (float)kDirY[dir], 0);
  ApplyRise(x, q, 0);
  Fade(x, q);
}

// Outdoors, the back of a doorway: the face at the north end of the door
// cells under a lintel. By the relief rule it shows the rows painted over
// the door (the lintel's, magnified a few px from the eye in first person);
// as far as the lintel's face hides it from the oblique test camera it is
// dark instead, the inside of the house. Returns whether it was all drawn.
static bool EmitDoorBack(EmitCtx *x, int c, int r, int zlo, int zhi, int front) {
  Hd3dScene *s = x->s;
  const Layer *ly = &s->layer[0];
  int n = s->n, nb = (r + 1) * n + c, lo = s->slab_lo[nb], hi = s->slab_hi[nb];
  if (r + 1 >= n || hi <= lo || ly->wz[nb] != zlo || lo - zlo > kOwHeadroom)
    return false;
  int depth = 0;
  while (r + 1 + depth < n && ly->wz[nb + depth * n] == zlo && s->slab_hi[nb + depth * n] > s->slab_lo[nb + depth * n])
    depth++;
  depth *= 8;
  int d0 = IntMax(zlo, lo - depth), d1 = IntMin(IntMin(lo, hi - depth), zhi);
  if (d1 <= d0)
    return false;
  R3dQuad *q = NewQuad(x, x->e->tex_bg2, 255, kR3dClass_World);
  if (!q)
    return true;
  float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f, y1 = (float)(s->origin_y + 8 * r + 8);
  float zl = (float)d0, zh = (float)d1;
  q->flags = kR3dQuad_Flat, q->color = 0;
  SetVertex(&q->v[0], x0, y1, zh, x0, y1 - zh);
  SetVertex(&q->v[1], x1, y1, zh, x1, y1 - zh);
  SetVertex(&q->v[2], x1, y1, zl, x1, y1 - zl);
  SetVertex(&q->v[3], x0, y1, zl, x0, y1 - zl);
  Orient(q, 0, 1.0f, 0);
  ApplyRise(x, q, 0);
  Fade(x, q);
  if (d0 > zlo)
    EmitFaceOw(x, c, r, kDir_S, zlo, d0, front);
  if (d1 < zhi)
    EmitFaceOw(x, c, r, kDir_S, d1, zhi, front);
  return true;
}

// Indoors in first person, the void around a room (its filler, flat on the
// floor) darkens toward the fog: seen from eye height it read as a beige or
// green plain out to the horizon, past every doorway and low wall. One quad
// per run of up to 8 void cells from (c, r); returns the run's length.
static int EmitDarkVoid(EmitCtx *x, int L, int c, int r, int cb) {
  Hd3dScene *s = x->s;
  const Layer *ly = &s->layer[L];
  int n = s->n, j = r * n + c, z = ly->wz[j], len = 1;
  while (len < 8 && c + len < cb && (ly->wf[j + len] & (kWf_Set | kWf_NoFace)) == (kWf_Set | kWf_NoFace) &&
         ly->wz[j + len] == z)
    len++;
  R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, 255, kR3dClass_World);
  if (!q)
    return len;
  uint32 v = s->void_rgb;
  int keep = (int)(256.0f * (1.0f - 0.85f * x->fp));
  q->flags = kR3dQuad_Flat;
  q->color = (uint32)((((v >> 16 & 0xff) * keep >> 8) << 16) | (((v >> 8 & 0xff) * keep >> 8) << 8) |
                      ((v & 0xff) * keep >> 8));
  float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f * (float)len;
  float y0 = (float)(s->origin_y + 8 * r), y1 = y0 + 8.0f, zf = (float)z;
  SetVertex(&q->v[0], x0, y0, zf, x0, y0 - zf);
  SetVertex(&q->v[1], x1, y0, zf, x1, y0 - zf);
  SetVertex(&q->v[2], x1, y1, zf, x1, y1 - zf);
  SetVertex(&q->v[3], x0, y1, zf, x0, y1 - zf);
  ApplyRise(x, q, LayerBase(s, L));
  return len;
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
          uint16 f = ly->wf[j];
          int z = ly->wz[j], ink = (f >> 4 & 7) * (f & kWf_South ? -1 : 1);
          if (!s->indoors && ink > 4)
            ink = 4 - ink;  // (outdoors 5-7: south)
          if (!(f & kWf_Set)) {
            c++;
            continue;
          }
          if ((f & kWf_NoFace) && x->dark_void) {
            c += EmitDarkVoid(x, L, c, r, cb);
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
            int before = x->num;
            EmitTop(x, L, c, r, len, z, ink);
            if ((f & kWf_Flat) && x->num > before)
              x->out[before].flags |= kR3dQuad_Flat, x->out[before].color = s->backdrop_rgb;
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
              uint16 nf = ly->wf[nb];
              if (nf & kWf_Set) {
                if ((nf & kWf_NoFace) || ly->wz[nb] >= z)
                  continue;
                zl = ly->wz[nb];
              } else if (slab && (nf & kWf_Empty) && z > kHd3dLowerFloor) {
                zl = kHd3dLowerFloor;  // the upper floor reads as a slab over the lower one
              } else {
                continue;
              }
              if (s->indoors)
                EmitFace(x, L, cc, r, dir, zl, z);
              else if (dir != kDir_S || !EmitDoorBack(x, cc, r, zl, z, s->ow_front[jj]))
                EmitFaceOw(x, cc, r, dir, zl, z, s->ow_front[jj]);
            }
          }
          c += len;
        }
      }
    }
  }
}

// Outdoors: the canopy edges that float over the ground (slabs). The top
// keeps the relief rule; the faces show what the relief puts there; the
// underside (only seen from below, in first person) shows the top's texels.
static void EmitSlabs(EmitCtx *x) {
  Hd3dScene *s = x->s;
  Layer *ly = &s->layer[0];
  int n = s->n, nc = n / kChunkCells;
  bool faces = x->rise > 0.01f;
  for (int cy = x->r0 / kChunkCells; cy * kChunkCells < x->r1; cy++) {
    for (int cx = x->c0 / kChunkCells; cx * kChunkCells < x->c1; cx++) {
      int ch = cy * nc + cx, zlo = ly->zlo[ch], zhi = ly->zhi[ch];
      if (zlo > zhi)
        continue;
      float rise = x->rise, up = (1.0f - rise) * (float)IntMax(zhi, 0);
      float bx = (float)(s->origin_x + 64 * cx), by = (float)(s->origin_y + 64 * cy);
      if (!BoxVisible(x, bx, by - up, rise * (float)zlo, bx + 64.0f, by + 64.0f, rise * (float)zhi))
        continue;
      int rb = IntMin(cy * kChunkCells + kChunkCells, x->r1), cb = IntMin(cx * kChunkCells + kChunkCells, x->c1);
      for (int r = IntMax(cy * kChunkCells, x->r0); r < rb; r++) {
        for (int c = IntMax(cx * kChunkCells, x->c0); c < cb; c++) {
          int j = r * n + c, top = s->slab_hi[j], bot = s->slab_lo[j];
          if (top <= bot)
            continue;
          if (SourceDecoded(ly, n, r - top / 8, c)) {
            EmitTop(x, 0, c, r, 1, top, 0);
            if (faces) {
              R3dQuad *q = NewQuad(x, x->e->tex_bg2, 150, kR3dClass_World);
              if (q) {
                float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f;
                float y0 = (float)(s->origin_y + 8 * r), y1 = y0 + 8.0f, zb = (float)bot, dv = (float)top;
                SetVertex(&q->v[0], x0, y0, zb, x0, y0 - dv);
                SetVertex(&q->v[1], x1, y0, zb, x1, y0 - dv);
                SetVertex(&q->v[2], x1, y1, zb, x1, y1 - dv);
                SetVertex(&q->v[3], x0, y1, zb, x0, y1 - dv);
                Orient(q, 0, 0, -1.0f);
                ApplyRise(x, q, 0);
                Fade(x, q);
              }
            }
          }
          if (!faces)
            continue;
          int front = 0;
          while (r + front + 1 < n && front < 8 && s->slab_hi[j + (front + 1) * n] >= top &&
                 s->slab_hi[j + (front + 1) * n] > s->slab_lo[j + (front + 1) * n])
            front++;
          for (int dir = 0; dir < 4; dir++) {
            int rr = r + kDirY[dir], c2 = c + kDirX[dir];
            if (rr < 0 || rr >= n || c2 < 0 || c2 >= n)
              continue;
            int nb = rr * n + c2, lo = bot;
            // What the neighbor fills: its column, and its own slab.
            if (ly->wf[nb] & kWf_Set)
              lo = IntMax(lo, ly->wz[nb]);
            if (lo >= top)
              continue;
            int nlo = s->slab_lo[nb], nhi = s->slab_hi[nb];
            if (nhi > nlo && nhi > lo && nlo < top) {
              if (nlo > lo)
                EmitFaceOw(x, c, r, dir, lo, nlo, front);
              if (nhi < top)
                EmitFaceOw(x, c, r, dir, nhi, top, front);
            } else {
              EmitFaceOw(x, c, r, dir, lo, top, front);
            }
          }
        }
      }
    }
  }
}

// Outdoors, past the area's south (and north) edge: what raised (lowered)
// ground at the edge put there, so the edge's last picture rows still show.
static void EmitSkirts(EmitCtx *x) {
  Hd3dScene *s = x->s;
  const Layer *ly = &s->layer[0];
  int n = s->n;
  bool faces = x->rise > 0.01f;
  for (int side = 0; side < 2; side++) {
    if (side ? x->r0 > 0 : x->r1 < n)
      continue;
    for (int c = x->c0; c < x->c1; c++) {
      const int8 *sk = s->ow_skirt[side][c];
      int edge = side ? c : (n - 1) * n + c, prev = (ly->wf[edge] & kWf_Set) ? ly->wz[edge] : 0;
      int k = 0;
      for (; k < kOwSkirt && sk[k] != -128; k++) {
        int z = sk[k], r = side ? -1 - k : n + k;
        if (SourceDecoded(ly, n, r - z / 8, c))
          EmitTop(x, 0, c, r, 1, z, 0);
        if (faces) {
          // Toward the area: the step from the row before.
          if (z > prev)
            EmitFaceOw(x, c, r, side ? kDir_S : kDir_N, prev, z, 0x80);
          else if (z < prev)
            EmitFaceOw(x, c, side ? r + 1 : r - 1, side ? kDir_N : kDir_S, z, prev, 0x80);
          for (int dir = kDir_W; dir <= kDir_E; dir++) {
            int c2 = c + kDirX[dir], z2 = c2 >= 0 && c2 < n ? s->ow_skirt[side][c2][k] : -128;
            int zl = z2 == -128 ? IntMin(z, 0) : z2;
            if (zl < z)
              EmitFaceOw(x, c, r, dir, zl, z, 0x80);
          }
        }
        prev = z;
      }
      // Where nothing lands further south, what still stands ends in a face
      // (a wall whose foot is past the edge: the picture's last rows).
      if (!side && faces && prev > 8 * k)
        EmitFaceOw(x, c, n + k - 1, kDir_S, 8 * k, prev, 0x80);
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

// How much a standing wall stretches its painted band (8 B px, B the band
// of the cell's own column) upward. North walls are as high as painted (that
// keeps the default view exact), the others as high as the room's north
// walls. In first person the eye is 20 px up, so every wall grows to at
// least kFpWallHeight: low bands at eye level read as railings, not walls.
// (Per column: a few wide columns where walls meet must not flatten the
// whole wall. Neighbour columns of other bands still reach the same top.)
// A step between two floors is not a room wall: it never grows, and a step
// up from the lower floor ends at the upper floor.
static float PanelStretch(const Hd3dScene *s, int L, int dir, int b, float fp) {
  float band = 8.0f * (float)(b & ~kBandKinds);
  int base = LayerBase(s, L);
  float tp = dir == kDir_N ? 1.0f : (float)(base < 0 && (b & kBandStep) ? -base : s->wall_height) / band;
  if (b & kBandStep)
    return tp;
  float tall = (float)kFpWallHeight / band;
  return fp > 0.0f && tall > tp ? tp + (tall - tp) * fp : tp;
}

static int LidCells(const Hd3dScene *s, const Layer *ly, const PanelCell *p);
static bool IsColumnTop(const Hd3dScene *s, const Layer *ly, const PanelCell *p);

// The fold of a panel cell's wall: its angle, how far a point of the band
// moves (R(t) = t scale, see EmitPanelCell) and the body it lies on.
typedef struct Fold { float k, cp, sp, scale, plinth; } Fold;

static void FoldOf(const EmitCtx *x, int L, const PanelCell *p, Fold *f) {
  const Hd3dScene *s = x->s;
  int dir = s->lines[p->line].dir;
  f->k = x->k[dir], f->cp = x->cphi[dir], f->sp = x->sphi[dir];
  f->plinth = (p->b & kBandKinds) ? 0.0f : x->plinth[dir];  // (steps and lips: none)
  if (p->b & kBandLip)
    f->k = 0.0f, f->cp = 1.0f, f->sp = 0.0f;  // (the upper floor's edge lies as painted)
  f->scale = 1.0f + (PanelStretch(s, L, dir, p->b, x->fp) - 1.0f) * f->k;
}

// A wall column's body, seen along the wall: from the hinge out to *outer
// (the lid's edge), up to the top of its folded band at (*reach cos phi,
// plinth + *reach sin phi) and the lid there. (Lintel and corner cells have
// a column of their own; a column that is not drawn has no body.)
static void ColumnBody(const EmitCtx *x, int L, const PanelCell *p, int lid, float *reach, float *outer) {
  Fold f;
  FoldOf(x, L, p, &f);
  *reach = 8.0f * (float)p->d * f.scale;
  float o = lid >= 0 ? 8.0f * (float)(p->d + lid) : 0.0f, in = *reach * f.cp;
  *outer = o > in ? o : in;
}

// The cell of (line, along) furthest out that is drawn this frame (NULL: none).
static const PanelCell *ColumnTop(const EmitCtx *x, int L, int line, int along) {
  const Hd3dScene *s = x->s;
  const Layer *ly = &s->layer[L];
  const PanelLine *ln = &s->lines[line];
  const PanelCell *top = NULL;
  int n = s->n, h8 = ln->hinge / 8, dir = ln->dir;
  if (along < 0 || along >= n)
    return NULL;
  for (int d = 1; d <= kHd3dBandMax; d++) {
    int r = along, c = along;
    if (dir == kDir_N)
      r = h8 - d;
    else if (dir == kDir_S)
      r = h8 + d - 1;
    else if (dir == kDir_W)
      c = h8 - d;
    else
      c = h8 + d - 1;
    if (r < 0 || r >= n || c < 0 || c >= n)
      break;
    for (int k = 0; k < 2; k++) {
      int rec = ly->rec[k][r * n + c];
      if (rec < 0)
        continue;
      const PanelCell *q = &s->pcells[rec];
      if (q->line == line && (q->other == kDir_None || PanelWins(x, dir, q->other)))
        top = q;
    }
  }
  return top;
}

// A face of a wall's body: its corners in the wall's own frame (a along
// the foot, t out from the hinge, z up from the layer base), facing (na
// along, nt out, nz up). The void's color, or with band[] the painted band
// of the wall at that distance from the hinge (the back of a standing wall).
static void EmitBodyFace(EmitCtx *x, int L, const PanelLine *ln, const float *a, const float *t, const float *z,
                         float na, float nt, float nz, uint8 shade, const float *band) {
  Hd3dScene *s = x->s;
  R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, shade, kR3dClass_World);
  if (!q)
    return;
  int dir = ln->dir;
  bool along_x = dir == kDir_N || dir == kDir_S;
  float nx = (float)kDirX[dir], ny = (float)kDirY[dir], zb = (float)LayerBase(s, L);
  float ox = (float)s->origin_x, oy = (float)s->origin_y, hinge = (float)ln->hinge;
  if (!band)
    q->flags = kR3dQuad_Flat, q->color = s->void_rgb;
  for (int v = 0; v < 4; v++) {
    float px = along_x ? ox + a[v] : ox + hinge + nx * t[v], py = along_x ? oy + hinge + ny * t[v] : oy + a[v];
    float tb = band ? band[v] : t[v];
    SetVertex(&q->v[v], px, py + zb, zb + z[v], along_x ? px : ox + hinge + nx * tb,
              along_x ? oy + hinge + ny * tb : py);
  }
  Orient(q, along_x ? na : nx * nt, along_x ? ny * nt : na, nz);
  Fade(x, q);
}

// The body of a wall column whose top cell is p: the face at its lid's
// outer edge (the front of a cut-down wall; on a thin wall between two
// floors, the step from the cut-down face's body up to the standing face's
// lid), the face at the hinge under a cut-down wall's band, and the ends:
// where the column beside it is shorter or missing, what that leaves open
// (above the neighbour's band and lid, and beyond its lid).
static void EmitColumnBody(EmitCtx *x, int L, const PanelCell *p, int lid) {
  Hd3dScene *s = x->s;
  const PanelLine *ln = &s->lines[p->line];
  int dir = ln->dir, n = s->n;
  bool along_x = dir == kDir_N || dir == kDir_S;
  int along = along_x ? p->cell % n : p->cell / n;
  float a0 = 8.0f * (float)along, a1 = a0 + 8.0f, reach, outer;
  // (Darker than the lid, so the sides of a pale void's bodies read as sides.)
  static const uint8 kFaceShade[4] = { 140, 176, 176, 208 };  // (facing N, W, E, S)
  Fold f;
  FoldOf(x, L, p, &f);
  ColumnBody(x, L, p, lid, &reach, &outer);
  float top = f.plinth + reach * f.sp, in = reach * f.cp;
  float aa[4] = { a0, a1, a1, a0 }, tt[4], zz[4], bb[4];
  if (outer > 0.01f && top > 0.01f) {
    // (Standing, its back shows its own band again, the hinge's end at the
    // foot: seen over the cut-down face of a thin wall between two floors.)
    tt[0] = tt[1] = tt[2] = tt[3] = outer;
    zz[0] = zz[1] = 0.0f, zz[2] = zz[3] = top;
    bb[0] = bb[1] = 0.0f, bb[2] = bb[3] = 8.0f * (float)p->d;
    EmitBodyFace(x, L, ln, aa, tt, zz, 0.0f, 1.0f, 0.0f, kFaceShade[dir], f.sp > 0.5f ? bb : NULL);
  }
  if (f.plinth > 0.01f && p->d == 1) {
    tt[0] = tt[1] = tt[2] = tt[3] = 0.0f;
    zz[0] = zz[1] = 0.0f, zz[2] = zz[3] = f.plinth;
    EmitBodyFace(x, L, ln, aa, tt, zz, 0.0f, -1.0f, 0.0f, kFaceShade[dir ^ 3], NULL);
  }
  for (int side = -1; side <= 1; side += 2) {
    const PanelCell *nb = ColumnTop(x, L, p->line, along + side);
    float reach_n = 0.0f, outer_n = 0.0f, in_n = 0.0f, top_n = f.plinth;
    if (nb) {
      Fold fn;
      FoldOf(x, L, nb, &fn);
      ColumnBody(x, L, nb, IsColumnTop(s, &s->layer[L], nb) ? LidCells(s, &s->layer[L], nb) : -1, &reach_n, &outer_n);
      in_n = reach_n * fn.cp, top_n = fn.plinth + reach_n * fn.sp;
    }
    float a = side < 0 ? a0 : a1, o = outer > outer_n ? outer : outer_n;
    aa[0] = aa[1] = aa[2] = aa[3] = a;
    if (top > top_n + 0.01f && o > in_n + 0.01f) {
      // (From the neighbour's band top along this one's to the lid's edge.)
      tt[0] = in_n, tt[1] = in, tt[2] = o, tt[3] = o;
      zz[0] = top_n, zz[1] = top, zz[2] = top, zz[3] = top_n;
      EmitBodyFace(x, L, ln, aa, tt, zz, (float)side, 0.0f, 0.0f, 176, NULL);
    }
    if (outer > outer_n + 0.01f && top_n > 0.01f) {
      tt[0] = tt[3] = outer_n, tt[1] = tt[2] = outer;
      zz[0] = zz[1] = 0.0f, zz[2] = zz[3] = top_n;
      EmitBodyFace(x, L, ln, aa, tt, zz, (float)side, 0.0f, 0.0f, 176, NULL);
    }
  }
}

// One cell of a wall panel: a point at band distance t (px) along the foot
// is at hinge + R(t) (cos phi n + sin phi z) over the plinth, R(t) = t lerp(1, PanelStretch, k).
static void EmitPanelCell(EmitCtx *x, int L, const PanelCell *p, bool top, int lid) {
  Hd3dScene *s = x->s;
  const PanelLine *ln = &s->lines[p->line];
  int dir = ln->dir, n = s->n, base = LayerBase(s, L);
  Fold f;
  FoldOf(x, L, p, &f);
  float k = f.k, cp = f.cp, sp = f.sp, scale = f.scale, pl = f.plinth;
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
  // (A diagonal wall's slant, in first person; its texels go with the points.)
  float slant = x->fp * k, s0 = slant * (float)p->s0, s1 = slant * (float)p->s1;
  for (int v = 0; v < 4; v++) {
    bool end = v == 1 || v == 2;
    float a = end ? a1 : a0, tt = t[v >> 1], rr = tt * scale, h = hinge + (end ? s1 : s0);
    // hinge point, the folded point, and its texel (where it is painted)
    float hx = along_x ? a : h, hy = along_x ? h : a;
    SetVertex(&q->v[v], ox + hx + nx * rr * cp, oy + hy + ny * rr * cp + zb, zb + pl + rr * sp,
              ox + hx + nx * tt, oy + hy + ny * tt);
  }
  Orient(q, -nx * sp, -ny * sp, cp);
  Fade(x, q);
  if (top && !x->e->test_camera)
    EmitColumnBody(x, L, p, lid);
  // Where the slanted foot leaves the painted floor's edge (it moves into the
  // band), floor fills the corner, with the texels of the floor cell next to it.
  float into = along_x ? -ny : -nx, g0 = s0 * into < 0.0f ? s0 : 0.0f, g1 = s1 * into < 0.0f ? s1 : 0.0f;
  if (p->d == 1 && (g0 != 0.0f || g1 != 0.0f)) {
    R3dQuad *f = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, 255, kR3dClass_World);
    if (f) {
      float fa[4] = { a0, a1, a1, a0 }, fg[4] = { 0.0f, 0.0f, g1, g0 };
      for (int v = 0; v < 4; v++) {
        float h = hinge + fg[v], hx = along_x ? fa[v] : h, hy = along_x ? h : fa[v];
        SetVertex(&f->v[v], ox + hx, oy + hy + zb, zb, ox + hx - nx * 8.0f, oy + hy - ny * 8.0f);
      }
      Orient(f, 0, 0, 1);
    }
  }
  if (lid < 0 || (k <= 0 && pl <= 0))
    return;
  // A lid over the wall top, out over the void past the band (lid cells).
  // North walls show the picture there (at the default view exactly what
  // the original shows above them); the others the void's color.
  float rt = t[1] * scale, inner = rt * cp, outer = 8.0f * (float)(p->d + lid), zt = zb + pl + rt * sp;
  if (inner >= outer - 0.01f)
    return;
  q = NewQuad(x, x->e->tex_bg2, 255, kR3dClass_World);
  if (!q)
    return;
  if (dir != kDir_N)
    q->flags = kR3dQuad_Flat, q->color = s->void_rgb;
  for (int v = 0; v < 4; v++) {
    bool end = v == 1 || v == 2;
    float a = end ? a1 : a0, dist = v >> 1 ? outer : inner, h = hinge + (end ? s1 : s0);
    float hx = along_x ? a : h, hy = along_x ? h : a;
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
    bool top = IsColumnTop(s, ly, p);
    EmitPanelCell(x, L, p, top, top ? LidCells(s, ly, p) : -1);
  }
}

// Small props: each stands as one picture of its whole box, turned to face the
// camera about a point a little behind its front (so it turns like something
// round). Facing the default camera it stands on its front line, where the
// relief rule puts a face: the picture is exact there.
static void EmitProps(EmitCtx *x, int L) {
  Hd3dScene *s = x->s;
  const Hd3dEmit *e = x->e;
  int base = LayerBase(s, L), off = base / 8;
  float vx = e->view_dir[0], vy = e->view_dir[1], zb = (float)base;
  float rx = vy, ry = -vx;  // the viewer's right
  for (int i = 0; i < s->num_props && x->num < x->max; i++) {
    const PropCard *p = &s->props[i];
    if (p->layer != L || p->r0 + off + p->h <= x->r0 || p->r0 + off >= x->r1 || p->c0 + p->w <= x->c0 ||
        p->c0 >= x->c1)
      continue;
    float w = 8.0f * (float)p->w, h = 8.0f * (float)p->h;
    float u0 = (float)(s->origin_x + 8 * p->c0), vb = (float)(s->origin_y + 8 * (p->r0 + p->h));
    float rad = w < 16.0f ? w * 0.5f : 8.0f;
    float cx = u0 + w * 0.5f, cy = vb + zb - rad;  // (the front line is at picture row vb, lowered with the base)
    float reach = w * 0.5f + rad;
    if (!BoxVisible(x, cx - reach, cy - reach, zb, cx + reach, cy + reach, zb + h))
      continue;
    R3dQuad *q = NewQuad(x, L ? e->tex_bg1 : e->tex_bg2, 255, kR3dClass_World);
    if (!q)
      return;
    float fx = cx + vx * rad, fy = cy + vy * rad, hw = w * 0.5f;
    SetVertex(&q->v[0], fx - rx * hw, fy - ry * hw, zb + h, u0, vb - h);
    SetVertex(&q->v[1], fx + rx * hw, fy + ry * hw, zb + h, u0 + w, vb - h);
    SetVertex(&q->v[2], fx + rx * hw, fy + ry * hw, zb, u0 + w, vb);
    SetVertex(&q->v[3], fx - rx * hw, fy - ry * hw, zb, u0, vb);
    Orient(q, vx, vy, 0);
    q->flags |= kR3dQuad_TwoSided;
    ApplyRise(x, q, base);
    Fade(x, q);
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

// Indoors in first person, the way out. An exit door (attribute 0x8e, the
// game's door to the overworld) is a corridor of walkable cells between two
// wall panels, open to the room's edge, and past it the picture shows the
// void: a black screen facing it from inside. Daylight closes the corridor
// instead, where its side walls end: the haze of the world outside.
static void EmitExits(EmitCtx *x, int L) {
  Hd3dScene *s = x->s;
  const Layer *ly = &s->layer[L];
  int n = s->n, base = LayerBase(s, L);
  if (x->fp <= 0.0f)
    return;
  uint32 vr = s->void_rgb, er = x->e->exit_rgb, rgb = 0;
  int k = (int)(256.0f * x->fp);
  for (int sh = 0; sh < 24; sh += 8)
    rgb |= (((vr >> sh & 0xff) * (256 - k) + (er >> sh & 0xff) * k) >> 8) << sh;
  for (int c = x->c0; c < x->c1; c++) {
    for (int r = 0; r < n;) {
      if (ly->attr[r * n + c] != 0x8e) {
        r++;
        continue;
      }
      int ra = r;
      while (r < n && ly->attr[r * n + c] == 0x8e)
        r++;
      if (r < n && ly->cls[r * n + c] != kCls_Void)
        continue;  // (a door into another room)
      int end = r - 1;
      while (end >= ra && !((c > 0 && (ly->cf[end * n + c - 1] & kCf_Panel)) ||
                            (c + 1 < n && (ly->cf[end * n + c + 1] & kCf_Panel))))
        end--;
      if (end < ra || end + 1 < x->r0 || end + 1 > x->r1)
        continue;
      R3dQuad *q = NewQuad(x, L ? x->e->tex_bg1 : x->e->tex_bg2, 255, kR3dClass_World);
      if (!q)
        return;
      q->flags = kR3dQuad_Flat, q->color = rgb;
      float x0 = (float)(s->origin_x + 8 * c), x1 = x0 + 8.0f, y = (float)(s->origin_y + 8 * (end + 1) + base);
      float zb = (float)base, zt = zb + (float)kFpWallHeight;
      SetVertex(&q->v[0], x0, y, zt, x0, y - zt);
      SetVertex(&q->v[1], x1, y, zt, x1, y - zt);
      SetVertex(&q->v[2], x1, y, zb, x1, y - zb);
      SetVertex(&q->v[3], x0, y, zb, x0, y - zb);
      Orient(q, 0, -1.0f, 0);
      ApplyRise(x, q, base);
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
  // (Outdoors a surface z high stands z px south of the picture point it
  // shows: what is that tall at the bottom of the screen stands further south.)
  if (!s->indoors && s->ready)
    y1 = IntMin(e->rect[3] + IntMax(s->ow_zmax - kHd3dFogMargin, 0), s->reach[3] + IntMax(s->ow_zmax, 0));
  x.c0 = IntMax(FloorDiv8(x0 - s->origin_x), 0), x.c1 = IntMin(FloorDiv8(x1 - s->origin_x + 7), s->n);
  x.r0 = IntMax(FloorDiv8(y0 - s->origin_y), 0), x.r1 = IntMin(FloorDiv8(y1 - s->origin_y + 7), s->n);
  if (x.c0 >= x.c1 || x.r0 >= x.r1)
    return 0;
  float fp = e->fp_blend < 0 ? 0 : e->fp_blend > 1 ? 1 : e->fp_blend;
  x.fp = e->test_camera ? 0.0f : fp;
  x.dark_void = s->indoors && x.fp > 0.0f;
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
    // (Once the camera is turned, a wall that lies flat (toward the camera)
    // lies on a low body: the room keeps its walls from every side, cut
    // down where they would hide it. At the classic view, and under the
    // test camera, it lies flat as painted: the picture is the original.)
    float cut = e->test_camera ? 0.0f : e->cut < 0 ? 0.0f : e->cut > 1 ? 1.0f : e->cut;
    x.plinth[dir] = (float)kCutWallHeight * (1.0f - k) * x.rise * cut;
  }

  int layers = GeometryLayers(s);
  for (int L = 0; L < layers; L++) {
    if (!s->ready) {
      EmitFlat(&x, L);
    } else {
      EmitRelief(&x, L);
      if (s->indoors) {
        EmitPanels(&x, L);
        EmitProps(&x, L);
        EmitExits(&x, L);
      } else {
        EmitSlabs(&x), EmitSkirts(&x);
      }
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
      if (geom && (ly->cf[i] & kCf_Prop))
        col[0] = 255, col[1] = 120, col[2] = 200;  // standing props: pink
      else if (geom && (ly->cf[i] & kCf_Thin))
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
