#include "mods.h"  // (mods/SPEC.md: this file is the hd3d mod)
#if MOD_HD3D
// The 3D view: which object drew which OAM entries, and sprites as cards
// standing in the scene (see hd3d_internal.h).
#include <SDL.h>
#include <math.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "instance.h"
#include "mp.h"

bool g_hd3d_tagging;
// (Debug) groups the last Hd3dSprites_Build left out: the atlas or the card list was full.
int g_hd3d_sprites_dropped;

// ---------------------------------------------------------------------------
// The OAM tag recorder. The game's draw loops call Hd3d_TagBegin and
// Hd3d_TagEnd around each object: the entries of oam_buf that changed in
// between are that object's. It only reads the game's memory and writes the
// tables below, one per world. They are never in a snapshot or the state
// hash: a tag only counts while the video chip's OAM still holds the 4 bytes
// it was recorded with, which also covers rollbacks and loaded states.

typedef struct Hd3dTag {
  uint8 kind, index;
  uint8 ent[4];              // the entry as drawn: x, y, char, flags
} Hd3dTag;

static Hd3dTag g_hd3d_tags[kInstMax][128];
static uint8 g_tag_before[128 * 4];  // oam_buf when the outermost Hd3d_TagBegin ran
static int g_tag_depth;
static uint8 g_tag_kind, g_tag_index;
static bool g_tag_was_on;

void Hd3d_TagFrameStart(void) {
  // Tagging follows the 3D view (it is only needed while that draws). When
  // it comes on, the tables of every world start empty.
  g_hd3d_tagging = g_hd3d.enabled;
  g_tag_depth = 0;
  if (!g_hd3d_tagging) {
    g_tag_was_on = false;
    return;
  }
  if (!g_tag_was_on)
    memset(g_hd3d_tags, 0, sizeof(g_hd3d_tags));
  g_tag_was_on = true;
  int w = Inst_Current();
  if ((unsigned)w < kInstMax)
    memset(g_hd3d_tags[w], 0, sizeof(g_hd3d_tags[w]));
}

void Hd3d_TagBegin(uint8 kind, uint8 index) {
  // (Nested calls are ignored: the outer object gets the entries.)
  if (!g_hd3d_tagging || g_tag_depth++)
    return;
  g_tag_kind = kind, g_tag_index = index;
  memcpy(g_tag_before, oam_buf, sizeof(g_tag_before));
}

void Hd3d_TagEnd(void) {
  if (g_tag_depth == 0 || --g_tag_depth != 0)
    return;
  int w = Inst_Current();
  if ((unsigned)w >= kInstMax)
    return;
  const uint8 *now = (const uint8 *)oam_buf;
  Hd3dTag *t = g_hd3d_tags[w];
  for (int i = 0; i < 128; i++) {
    if (!memcmp(now + i * 4, g_tag_before + i * 4, 4))
      continue;
    t[i].kind = g_tag_kind, t[i].index = g_tag_index;
    memcpy(t[i].ent, now + i * 4, 4);
  }
}

// ---------------------------------------------------------------------------
// Capture and world reads (phase A)

void Hd3dSprites_Capture(Ppu *ppu, Hd3dSpriteFrame *out) {
  // Inside ZeldaDrawPpuFrame: the leader's colors in cgram 0xF0-0xFF, the
  // extra sets and their palette banks are only right here.
  memcpy(out->oam, ppu->oam, sizeof(out->oam));
  memcpy(out->cgram_obj, &ppu->cgram[0x80], sizeof(out->cgram_obj));
  memcpy(out->extra_pal, ppu->extraPalettes, sizeof(out->extra_pal));
  memcpy(out->obj_vram, &ppu->vram[0x4000], sizeof(out->obj_vram));
  out->obj_adr1 = ppu->objTileAdr1;
  out->obj_adr2 = ppu->objTileAdr2;
  out->obj_size = ppu->objSize;
  int n = ppu->numExtraSpriteSets < kPpuMaxExtraSpriteSets ? ppu->numExtraSpriteSets : kPpuMaxExtraSpriteSets;
  out->num_sets = (uint8)n;
  for (int k = 0; k < kPpuMaxExtraSpriteSets; k++) {
    if (k < n) {
      out->sets[k] = ppu->extraSprites[k];
      out->set_player[k] = g_mp_extra_set_player[k];
      out->set_link_count[k] = g_mp_extra_set_link_count[k];
    } else {
      out->sets[k].count = 0;
      out->set_player[k] = -1;
      out->set_link_count[k] = 0;
    }
  }
  out->brightness = ppu->brightness;
  out->obj_math = (ppu->mathEnabled & 0x10) && ppu->subtractColor;
  out->extra_left_right = ppu->extraLeftRight;
}

// Fills in the tags that still hold, the objects, the cameras and the
// leader. hide_player and xray_player are left at -1 and weapon_ancillae at
// 0: the caller sets them afterwards (hd3d.c: the pane player in first
// person is hidden, with his sword sparkles, and gets the x-ray when its
// camera is turned, Cam3d_IsTurned).
void Hd3dSprites_ReadWorld(Hd3dSpriteFrame *out) {
  int w = Inst_Current();
  const Hd3dTag *tags = g_hd3d_tagging && (unsigned)w < kInstMax ? g_hd3d_tags[w] : NULL;
  for (int i = 0; i < 128; i++) {
    const Hd3dTag *t = tags ? &tags[i] : NULL;
    bool ok = t && t->kind != kHd3dTag_None && !memcmp((const uint8 *)out->oam + i * 4, t->ent, 4);
    out->tag_kind[i] = ok ? t->kind : kHd3dTag_None;
    out->tag_index[i] = ok ? t->index : 0;
  }

  bool indoors = player_is_indoors != 0;
  int n = 0, max = (int)countof(out->objects);
  for (int k = 0; k < 16 && n < max; k++) {
    if (!sprite_state[k])
      continue;
    Hd3dObject *o = &out->objects[n++];
    o->kind = kHd3dTag_Sprite, o->index = (uint8)k;
    o->layer = indoors && sprite_floor[k] != 0;
    o->x = (int16)((sprite_x_lo[k] | sprite_x_hi[k] << 8) + 8);
    o->y = (int16)((sprite_y_lo[k] | sprite_y_hi[k] << 8) + 16);
    o->z = sprite_z[k];
  }
  for (int k = 0; k < 10 && n < max; k++) {
    if (!ancilla_type[k])
      continue;
    Hd3dObject *o = &out->objects[n++];
    int z = (int8)ancilla_z[k];
    o->kind = kHd3dTag_Ancilla, o->index = (uint8)k;
    o->layer = indoors && ancilla_floor[k] != 0;
    o->x = (int16)(ancilla_x_lo[k] | ancilla_x_hi[k] << 8);
    o->y = (int16)(ancilla_y_lo[k] | ancilla_y_hi[k] << 8);
    o->z = (int16)(z > 0 ? z : 0);
  }
  // Every Link in this world, downed ones too. The others are drawn moved by
  // their netplay smoothing offset (Mp_PrepareRender); the leader is not.
  for (int p = 0; p < kMpMaxPlayers && n < max; p++) {
    if (!g_mp.p[p].joined)
      continue;
    Hd3dObject *o = &out->objects[n++];
    int dx = p == g_mp.leader ? 0 : g_mp_draw_offset[p][0], dy = p == g_mp.leader ? 0 : g_mp_draw_offset[p][1];
    uint16 z = WORD(*Mp_PlayerRam(p, 0x24));  // link_z_coord (0xffff on the ground)
    o->kind = kHd3dTag_Link, o->index = (uint8)p;
    o->layer = indoors && *Mp_PlayerRam(p, 0xEE) != 0;  // link_is_on_lower_level
    o->x = (int16)(Mp_PlayerX(p) + 8 + dx);
    o->y = (int16)(Mp_PlayerY(p) + 24 + dy);
    o->z = (int16)((int16)z >= 0 || (z & 0xff) < 0xf0 ? z & 0xff : 0);
  }
  out->num_objects = n;
  out->cam_x = (int16)BG2HOFS_copy2, out->cam_y = (int16)BG2VOFS_copy2;
  out->shaken_x = (int16)BG2HOFS_copy, out->shaken_y = (int16)BG2VOFS_copy;
  out->leader = (int8)g_mp.leader;
  out->hide_player = out->xray_player = -1;
  out->weapon_ancillae = 0;
}

// ---------------------------------------------------------------------------
// Cards and shadow decals (phase B: only the frame, no game memory)

// Small and large sprite sizes by OBSEL, as the video chip has them.
static const uint8 kHd3dSpriteSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
};

enum {
  kSprMaxEntries = 128 + kPpuMaxExtraSpriteSets * kPpuMaxExtraSprites + kHd3dMaxLinks * kHd3dPoseMax,
  kSprMaxCard = 256,          // a group bigger than this either way is left out
  kSprLutBanks = 129,         // lut index of extra palette bank 1, color 0
  // A first-person weapon (SprWeaponCard).
  kWpnMax = 64,               // its picture is cut to this many px either way
  kWpnFine = 2,               // the turned picture has this many texels per picture px either way
  kWpnScreenPx = 52,          // the picture's height is this many picture px (4.3 screen px each at 224 rows)
  kSprPoseSet = kPpuMaxExtraSpriteSets,  // SprEnt.set of player p's pose: this + p
};
#define kWpnAhead 16.0f       // its hands: this far ahead of the eye
#define kWpnBelow 11.0f       // and this far below it

// How an entry finds its group.
enum {
  kJoin_Key,                 // every entry of the same object (kind, index, set)
  kJoin_Touch,               // untagged: entries whose boxes come within 2 px
  kJoin_TouchPush,           // push blocks (all drawn in one call): the same, among themselves
  kJoin_Alone,               // a projectile of an extra set
};

typedef struct SprEnt {
  int x, y;                  // its top-left in picture px
  uint16 oam1;               // char, name select, palette, priority, flips
  uint16 order;              // regular: its index; extra set k: 128 + 64k + i
  uint8 size, bank;          // bank: the extra palette bank that replaces palette 7 (0: none)
  int8 set;                  // -1 regular; else the extra set (or pose), whose tiles replace VRAM 0x4000-0x41FF
  uint8 kind, index, join;
  bool shadow;
  bool weapon;               // the first-person player's weapon, or a sparkle of it
  int16 parent;              // union-find
  int16 next;                // the next entry of its group, in OAM order (-1: last)
} SprEnt;

typedef struct SprGroup {
  int16 first, last;
  int box[4], sbox[4];       // the other entries' and the shadows' boxes: x0, y0, x1, y1 (x1 <= x0: none)
  uint8 kind, index;
  uint16 order;              // the lowest OAM order of its other (not shadow) entries
  uint8 prio;                // the OAM priority of that entry
  bool weapon;
} SprGroup;

typedef struct SprShelf { int x, y, h; } SprShelf;

static int SprFind(SprEnt *e, int i) {
  while (e[i].parent != i) {
    e[i].parent = e[e[i].parent].parent;
    i = e[i].parent;
  }
  return i;
}

static void SprUnion(SprEnt *e, int a, int b) {
  a = SprFind(e, a), b = SprFind(e, b);
  if (a < b)
    e[b].parent = (int16)a;
  else if (b < a)
    e[a].parent = (int16)b;
}

static bool SprTouch(const SprEnt *a, const SprEnt *b) {
  return a->x <= b->x + b->size + 2 && b->x <= a->x + a->size + 2 &&
         a->y <= b->y + b->size + 2 && b->y <= a->y + a->size + 2;
}

static bool SprIsShadow(uint16 oam1) {
  int c = oam1 & 0x1ff, pal = (oam1 >> 9) & 7;
  return (pal == 4 || pal == 5) && (c == 0x6c || c == 0x6e || c == 0x7e || c == 0x28 || c == 0x38);
}

// An entry of a Link that is his weapon: the sword or rod tiles (chars 5, 6,
// 0x15, 0x16), the tiles of an item in his hand (9, 0xA, 0x19, 0x1A: the
// bow, the hammer...) or the glint at the tip of his sword. (His body is
// 0-4 and 0x10-0x14, the shield 7, 8, 0x17, 0x18; player_oam.c.)
static bool SprIsWeapon(uint16 oam1) {
  int c = oam1 & 0x1ff;
  if (c < 0x20)
    return (c & 0xf) == 5 || (c & 0xf) == 6 || (c & 0xf) == 9 || (c & 0xf) == 0xa;
  return c == 0x2f || c == 0x3e || c == 0x3f;
}

static void BoxAdd(int b[4], int x0, int y0, int x1, int y1) {
  if (b[2] <= b[0]) {
    b[0] = x0, b[1] = y0, b[2] = x1, b[3] = y1;
    return;
  }
  if (x0 < b[0]) b[0] = x0;
  if (y0 < b[1]) b[1] = y0;
  if (x1 > b[2]) b[2] = x1;
  if (y1 > b[3]) b[3] = y1;
}

static uint32 SprLutColor(uint16 c, int brightness) {
  int r = c & 0x1f, g = (c >> 5) & 0x1f, b = (c >> 10) & 0x1f;
  r = ((r << 3) | (r >> 2)) * brightness / 15;
  g = ((g << 3) | (g >> 2)) * brightness / 15;
  b = ((b << 3) | (b >> 2)) * brightness / 15;
  return (uint32)(r << 16 | g << 8 | b);
}

// Both bit planes of one row of a tile, as the video chip fetches them.
static uint32 SprFetch(const Hd3dSpriteFrame *sf, int vaddr, const uint16 *tiles) {
  unsigned o = (unsigned)(vaddr - kPpuExtraTilesBase);
  if (tiles && o < (unsigned)(kPpuExtraTilesSize - 8))
    return tiles[o] | (uint32)tiles[o + 8] << 16;
  o = (unsigned)(vaddr - 0x4000);
  if (o >= countof(sf->obj_vram) - 8)
    return 0;  // (outside the OBJ tiles: nothing)
  return sf->obj_vram[o] | (uint32)sf->obj_vram[o + 8] << 16;
}

// Draws entry e into the w x h picture at dst (pitch bytes a row), its
// top-left at (dx, dy) there, only into pixels that are still 0 (the lower
// OAM order wins, as on the video chip). dst NULL: only measures. bb gets
// the box of the opaque pixels (picture-relative, x1 and y1 exclusive).
// The same decoding as ppu_drawSpriteRow: name select, flips, the wrap within
// 16x16 tile pages, extra sets' own Link tiles and palette banks.
static void SprDrawEntry(const Hd3dSpriteFrame *sf, const SprEnt *e, uint8 *dst, int pitch, int dx, int dy, int w,
                         int h, int bb[4]) {
  int size = e->size, oam1 = e->oam1;
  int obj_adr = (oam1 & 0x100) ? sf->obj_adr2 : sf->obj_adr1;
  int pal = (oam1 >> 9) & 7;
  int base = (pal == 7 && e->bank) ? kSprLutBanks + (e->bank - 1) * 16 : 1 + pal * 16;
  const uint16 *tiles = e->set < 0 ? NULL : e->set < kSprPoseSet ? sf->sets[e->set].tiles :
                        sf->poses[e->set - kSprPoseSet].tiles;
  for (int row = 0; row < size; row++) {
    int y = dy + row;
    if ((unsigned)y >= (unsigned)h)
      continue;
    int r = (oam1 & 0x8000) ? size - 1 - row : row;
    for (int col = 0; col < size; col += 8) {
      int used_col = (oam1 & 0x4000) ? size - 1 - col : col;
      int tile = (((((oam1 & 0xff) >> 4) + (r >> 3)) << 4) | (((oam1 & 0xf) + (used_col >> 3)) & 0xf));
      uint32 plane = SprFetch(sf, (obj_adr + tile * 16 + (r & 7)) & 0x7fff, tiles);
      if (!plane)
        continue;
      for (int px = 0; px < 8; px++) {
        uint32 bits = plane >> ((oam1 & 0x4000) ? px : 7 - px);
        int pixel = (bits & 1) | ((bits >> 7) & 2) | ((bits >> 14) & 4) | ((bits >> 21) & 8);
        int x = dx + col + px;
        if (!pixel || (unsigned)x >= (unsigned)w)
          continue;
        BoxAdd(bb, x, y, x + 1, y + 1);
        if (dst && !dst[y * pitch + x])
          dst[y * pitch + x] = (uint8)(base + pixel);
      }
    }
  }
}

// Draws a group's shadows or its other entries into the atlas; box: their
// box in picture px.
static void SprDrawGroup(const Hd3dSpriteFrame *sf, const SprEnt *ents, const SprGroup *g, bool shadows,
                         const int box[4], uint8 *dst, int bb[4]) {
  bb[0] = bb[1] = bb[2] = bb[3] = 0;
  for (int i = g->first; i >= 0; i = ents[i].next)
    if (ents[i].shadow == shadows)
      SprDrawEntry(sf, &ents[i], dst, kHd3dAtlasW, ents[i].x - box[0], ents[i].y - box[1], box[2] - box[0],
                   box[3] - box[1], bb);
}

// A place for a w x h picture in the atlas, with a clear 1 px border around it
// (so sampling at its edge never reads a neighbour). Shelves, left to right.
static uint8 *SprAtlasAlloc(SprShelf *s, uint8 *atlas, int w, int h, int *ax, int *ay) {
  if (w > kSprMaxCard || h > kSprMaxCard)
    return NULL;
  if (s->x + w + 1 > kHd3dAtlasW)
    s->y += s->h + 1, s->x = 1, s->h = 0;
  if (s->y + h + 1 > kHd3dAtlasH)
    return NULL;
  *ax = s->x, *ay = s->y;
  s->x += w + 1;
  if (h > s->h)
    s->h = h;
  for (int y = *ay - 1; y <= *ay + h; y++)
    memset(&atlas[y * kHd3dAtlasW + *ax - 1], 0, w + 2);
  return &atlas[*ay * kHd3dAtlasW + *ax];
}

static void SprVertex(R3dVertex *v, float x, float y, float z, float u, float tv) {
  v->x = x, v->y = y, v->z = z, v->u = u, v->v = tv;
}

static const Hd3dObject *SprFindObject(const Hd3dSpriteFrame *sf, int kind, int index) {
  for (int i = 0; i < sf->num_objects; i++)
    if (sf->objects[i].kind == kind && sf->objects[i].index == index)
      return &sf->objects[i];
  return NULL;
}

// A first-person player's weapon (his Link is hidden, so it would never
// show). It is drawn as if seen over his shoulder: the 2D picture around his
// Link turned so that the way the camera looks is up, his middle (pivot, in
// picture px) at his hands a little ahead of and below the eye, at a fixed
// size. So a swing sweeps across the bottom of the view, and the sword held
// out points ahead. Its sparkles are laid out the same way, so they stay on
// it. It is a front card: never hidden by a wall right in front.
// False: nothing to draw, or no room in the atlas (*dropped).
static bool SprWeaponCard(const Hd3dSpriteFrame *sf, const SprEnt *ents, const SprGroup *g, const R3dCamera *cam,
                          const float pivot[2], SprShelf *shelf, uint8 *atlas, uint8 atlas_slot, R3dCard *c,
                          bool *dropped) {
  static uint8 src[kWpnMax * kWpnMax];
  *dropped = false;
  // The picture as the 2D view has it (cut to kWpnMax px).
  int sw = IntMin(g->box[2] - g->box[0], kWpnMax), sh = IntMin(g->box[3] - g->box[1], kWpnMax);
  if (sw <= 0 || sh <= 0)
    return false;
  memset(src, 0, sizeof(src));
  int bb[4] = { 0, 0, 0, 0 };
  for (int i = g->first; i >= 0; i = ents[i].next)
    if (!ents[i].shadow)
      SprDrawEntry(sf, &ents[i], src, kWpnMax, ents[i].x - g->box[0], ents[i].y - g->box[1], sw, sh, bb);
  if (bb[2] <= bb[0])
    return false;
  // Turned by the camera's heading: a picture offset (x, y) from the pivot
  // shows at (x co + y si, y co - x si), the way it looks being up.
  float co = cam->right[0], si = cam->right[1], len = sqrtf(co * co + si * si);
  if (!(len > 1e-3f))
    return false;
  co /= len, si /= len;
  float px = pivot[0] - (float)g->box[0], py = pivot[1] - (float)g->box[1];  // (in src)
  float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
  for (int k = 0; k < 4; k++) {
    float ox = (float)bb[k & 1 ? 2 : 0] - px, oy = (float)bb[k & 2 ? 3 : 1] - py;
    float tx = ox * co + oy * si, ty = oy * co - ox * si;
    lo[0] = tx < lo[0] ? tx : lo[0], hi[0] = tx > hi[0] ? tx : hi[0];
    lo[1] = ty < lo[1] ? ty : lo[1], hi[1] = ty > hi[1] ? ty : hi[1];
  }
  // The turned picture, kWpnFine texels per px (crisp edges at any angle).
  int u0 = (int)floorf(lo[0] * kWpnFine), v0 = (int)floorf(lo[1] * kWpnFine);
  int dw = (int)floorf(hi[0] * kWpnFine) + 1 - u0, dh = (int)floorf(hi[1] * kWpnFine) + 1 - v0;
  int ax = 0, ay = 0;
  uint8 *dst = SprAtlasAlloc(shelf, atlas, dw, dh, &ax, &ay);
  if (!dst) {
    *dropped = true;
    return false;
  }
  int tb[4] = { 0, 0, 0, 0 };
  for (int v = 0; v < dh; v++) {
    float ty = ((float)(v0 + v) + 0.5f) * (1.0f / kWpnFine);
    for (int u = 0; u < dw; u++) {
      float tx = ((float)(u0 + u) + 0.5f) * (1.0f / kWpnFine);
      float sx = px + tx * co - ty * si, sy = py + tx * si + ty * co;
      if (!(sx >= 0.0f && sy >= 0.0f && sx < (float)sw && sy < (float)sh))
        continue;
      uint8 t = src[(int)sy * kWpnMax + (int)sx];
      if (t) {
        dst[v * kHd3dAtlasW + u] = t;
        BoxAdd(tb, u, v, u + 1, v + 1);
      }
    }
  }
  if (tb[2] <= tb[0])
    return false;
  // Its hands: ahead along the camera's heading and below the eye. The
  // picture's height decides the size, not the distance.
  memset(c, 0, sizeof(*c));
  c->foot[0] = cam->eye[0] + si * kWpnAhead;
  c->foot[1] = cam->eye[1] - co * kWpnAhead;
  c->foot[2] = cam->eye[2] - kWpnBelow;
  c->atlas_x = (int16)(ax + tb[0]), c->atlas_y = (int16)(ay + tb[1]);
  c->w = (int16)(tb[2] - tb[0]), c->h = (int16)(tb[3] - tb[1]);
  c->foot_x = (int16)(-u0 - tb[0]), c->foot_y = (int16)(-v0 - tb[1]);
  c->order = g->order;
  c->tex = atlas_slot;
  c->flags = kR3dCard_Front;
  c->scale = 2.0f * cam->cy / (float)(kWpnScreenPx * kWpnFine);
  return true;
}

// A Link the camera sees from another side (sf->poses, hd3d_linkpose.c):
// where the game's picture of him shows his body, its entries make way for
// the pose's. (A blinking or hidden Link keeps the game's picture. The
// regular OAM's Link entries only count when tagged.) Returns the new n.
static int SprUsePoses(const Hd3dSpriteFrame *sf, SprEnt *ents, int n, bool any_tag) {
  bool body[kHd3dMaxLinks] = { false }, any = false;
  for (int i = 0; i < n; i++) {
    const SprEnt *e = &ents[i];
    if (e->kind == kHd3dTag_Link && e->index < kHd3dMaxLinks && !e->shadow && (e->set >= 0 || any_tag) &&
        sf->poses[e->index].count)
      body[e->index] = any = true;
  }
  if (!any)
    return n;
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (ents[i].kind == kHd3dTag_Link && ents[i].index < kHd3dMaxLinks && body[ents[i].index])
      continue;
    ents[m] = ents[i];
    ents[m].parent = (int16)m;
    m++;
  }
  const uint8 *sizes = kHd3dSpriteSizes[sf->obj_size & 7];
  for (int p = 0; p < kHd3dMaxLinks; p++) {
    const Hd3dLinkPose *ps = &sf->poses[p];
    for (int i = 0; body[p] && i < ps->count && m < kSprMaxEntries; i++) {
      SprEnt *e = &ents[m];
      // (Every field: the slot holds an old entry, or nothing yet. A pose is
      // never the hidden first-person Link's, so never his weapon.)
      memset(e, 0, sizeof(*e));
      e->x = ps->x[i] + sf->cam_x;
      e->y = ps->y[i] + sf->cam_y + 1;
      e->oam1 = ps->oam1[i];
      e->size = sizes[ps->big[i] & 1];
      e->order = ps->order[i];
      e->bank = ps->bank;
      e->set = (int8)(kSprPoseSet + p);
      e->kind = kHd3dTag_Link, e->index = (uint8)p, e->join = kJoin_Key;
      e->shadow = SprIsShadow(e->oam1);
      e->parent = (int16)m;
      e->next = -1;
      m++;
    }
  }
  return m;
}

int Hd3dSprites_Build(const Hd3dSpriteFrame *sf, const Hd3dScene *scene, const R3dCamera *cam, uint8 atlas_slot,
                      uint8 *atlas, uint32 *lut, R3dCard *cards, int max_cards,
                      R3dQuad *decals, int max_decals, int *num_decals) {
  // The colors: 0 see-through, 1-128 cgram 0x80-0xFF, 129-176 the extra
  // palette banks 1-3, with the screen brightness (as brightnessMult has it).
  int bright = sf->brightness & 15;
  memset(lut, 0, 256 * sizeof(uint32));
  for (int i = 0; i < 128; i++)
    lut[1 + i] = SprLutColor(sf->cgram_obj[i], bright);
  for (int b = 1; b < 4; b++)
    for (int j = 0; j < 16; j++)
      lut[kSprLutBanks + (b - 1) * 16 + j] = SprLutColor(sf->extra_pal[b][j], bright);

  // The entries. Positions in picture px: (sx + camera x, sy + camera y + 1),
  // since the video chip shows background row scroll + r + 1 on screen row r
  // and sprites on their own y. The leader's Link and the other Links were
  // drawn against BG2HOFS_copy2; everything else (in Sprite_Main) against
  // the shaken BG2HOFS_copy.
  SprEnt ents[kSprMaxEntries];
  int n = 0, e_lr = sf->extra_left_right;
  const uint8 *sizes = kHd3dSpriteSizes[sf->obj_size & 7];
  bool any_tag = false;
  for (int i = 0; i < 128; i++)
    any_tag |= sf->tag_kind[i] != kHd3dTag_None;
  for (int s = -1; s < (int)sf->num_sets && s < kPpuMaxExtraSpriteSets; s++) {
    const PpuExtraSpriteSet *set = s >= 0 ? &sf->sets[s] : NULL;
    int count = !set ? 128 : set->count < kPpuMaxExtraSprites ? set->count : kPpuMaxExtraSprites;
    for (int i = 0; i < count; i++) {
      uint16 oam0 = set ? set->oam[i * 2] : sf->oam[i * 2];
      uint16 oam1 = set ? set->oam[i * 2 + 1] : sf->oam[i * 2 + 1];
      int high = set ? set->high[i] : sf->oam[0x100 + (i >> 3)] >> ((i & 7) * 2);
      int y = oam0 >> 8;
      if (y == 0xf0)
        continue;
      int size = sizes[(high >> 1) & 1];
      int x = (oam0 & 0xff) + (high & 1) * 256;
      x -= (x >= 256 + e_lr) * 512;
      if (x <= -(size + e_lr))
        continue;  // (the video chip doesn't draw it either)
      if (y > 0xf0)
        y -= 256;  // the game keeps y in -16..239 (SetOamHelper0): this is above the screen
      SprEnt *e = &ents[n];
      e->oam1 = oam1;
      e->size = (uint8)size;
      e->set = (int8)s;
      e->bank = set ? set->bank & 3 : 0;
      e->order = (uint16)(set ? 128 + s * kPpuMaxExtraSprites + i : i);
      e->shadow = SprIsShadow(oam1);
      e->parent = (int16)n;
      e->next = -1;
      bool unshaken;
      if (set) {
        bool link = i < sf->set_link_count[s];
        e->kind = link ? kHd3dTag_Link : kHd3dTag_None;
        e->index = link ? (uint8)sf->set_player[s] : 0;
        e->join = link ? kJoin_Key : kJoin_Alone;
        // (A set's projectiles were drawn in Sprite_Main like any other.)
        unshaken = link;
      } else if (sf->tag_kind[i] != kHd3dTag_None) {
        e->kind = sf->tag_kind[i], e->index = sf->tag_index[i];
        e->join = e->kind == kHd3dTag_PushBlock ? kJoin_TouchPush : kJoin_Key;
        unshaken = e->kind == kHd3dTag_Link;
      } else if (!any_tag && (oam1 & 0x1e0) == 0) {
        // No tags at all (tagging only just came on, or the game is paused
        // since): Link's own tiles (chars 0-0x1F, VRAM 0x4000-0x41FF, which
        // nothing else uses) are the leader, so a first-person player's own
        // Link still disappears.
        e->kind = kHd3dTag_Link, e->index = (uint8)sf->leader;
        e->join = kJoin_Key;
        unshaken = true;
      } else {
        e->kind = kHd3dTag_None, e->index = 0;
        e->join = kJoin_Touch;
        unshaken = false;
      }
      e->x = x + (unshaken ? sf->cam_x : sf->shaken_x);
      e->y = y + (unshaken ? sf->cam_y : sf->shaken_y) + 1;
      // First person: the hidden Link's weapon and its sparkles are drawn on their own.
      e->weapon = sf->hide_player >= 0 &&
                  ((e->kind == kHd3dTag_Link && e->index == sf->hide_player && SprIsWeapon(oam1)) ||
                   (e->kind == kHd3dTag_Ancilla && e->index < 16 && (sf->weapon_ancillae >> e->index & 1)));
      n++;
    }
  }
  n = SprUsePoses(sf, ents, n, any_tag);

  // Groups: an object's entries (a hidden Link's weapon apart); touching
  // untagged ones; each projectile alone.
  for (int i = 0; i < n; i++) {
    SprEnt *a = &ents[i];
    for (int j = 0; j < i; j++) {
      SprEnt *b = &ents[j];
      if (a->join == kJoin_Key && b->join == kJoin_Key) {
        if (a->kind == b->kind && a->index == b->index && a->set == b->set && a->weapon == b->weapon) {
          SprUnion(ents, i, j);
          break;  // (that object's earlier entries are one group already)
        }
      } else if ((a->join == b->join && (a->join == kJoin_Touch || a->join == kJoin_TouchPush)) ||
                 // (Without tags: the shadow under the leader's Link joins him.)
                 (!any_tag && a->join == kJoin_Touch && a->shadow && b->kind == kHd3dTag_Link && b->set < 0) ||
                 (!any_tag && b->join == kJoin_Touch && b->shadow && a->kind == kHd3dTag_Link && a->set < 0)) {
        if (SprTouch(a, b))
          SprUnion(ents, i, j);
      }
    }
  }
  SprGroup groups[kSprMaxEntries];
  int16 group_of[kSprMaxEntries];
  int ng = 0;
  for (int i = 0; i < n; i++) {
    SprEnt *e = &ents[i];
    int r = SprFind(ents, i);
    SprGroup *g;
    if (r == i) {
      group_of[i] = (int16)ng;
      g = &groups[ng++];
      memset(g, 0, sizeof(*g));
      g->first = g->last = (int16)i;
      g->kind = e->kind, g->index = e->index;
      g->order = 0xffff;
      g->weapon = e->weapon;
    } else {
      g = &groups[group_of[r]];
      ents[g->last].next = (int16)i;
      g->last = (int16)i;
      if (e->kind != kHd3dTag_None)
        g->kind = e->kind, g->index = e->index;  // (a fallback Link and the shadow under it)
    }
    if (e->shadow) {
      BoxAdd(g->sbox, e->x, e->y, e->x + e->size, e->y + e->size);
    } else {
      BoxAdd(g->box, e->x, e->y, e->x + e->size, e->y + e->size);
      if (e->order < g->order)
        g->order = e->order, g->prio = (e->oam1 >> 12) & 3;
    }
  }

  // Cards and decals, the Links first (they keep their place when the atlas
  // fills up).
  Hd3dSceneInfo info;
  memset(&info, 0, sizeof(info));
  if (scene)
    Hd3dScene_GetInfo(scene, &info);
  SprShelf shelf = { 1, 1, 0 };
  int nc = 0, nd = 0, dropped = 0;
  // The middle of the hidden Link's picture (his weapon turns around it).
  const Hd3dObject *hidden = sf->hide_player >= 0 ? SprFindObject(sf, kHd3dTag_Link, sf->hide_player) : NULL;
  float pivot[2] = { 0.0f, 0.0f };
  if (hidden)
    pivot[0] = (float)hidden->x, pivot[1] = (float)(hidden->y - 12 - hidden->z);
  for (int pass = 0; pass < 2; pass++) {
    for (int gi = 0; gi < ng; gi++) {
      const SprGroup *g = &groups[gi];
      bool link = g->kind == kHd3dTag_Link;
      if (link != (pass == 0))
        continue;
      if (g->weapon) {
        bool full = nc >= max_cards, drop = false;
        if (hidden && cam && !full &&
            SprWeaponCard(sf, ents, g, cam, pivot, &shelf, atlas, atlas_slot, &cards[nc], &drop))
          nc++;
        if (drop || (hidden && cam && full))
          dropped++;
        continue;
      }
      if (link && g->index == sf->hide_player)
        continue;  // (first person: your own Link and his shadow)
      const Hd3dObject *obj = NULL;
      if (g->kind == kHd3dTag_Link || g->kind == kHd3dTag_Sprite || g->kind == kHd3dTag_Ancilla)
        obj = SprFindObject(sf, g->kind, g->index);
      // What it stands on: its object's floor; else, indoors, a sprite drawn
      // with OAM priority 1 is on the lower floor (as the game draws them).
      int layer = obj ? obj->layer : (info.indoors && g->box[2] > g->box[0] && g->prio == 1);
      // (Outdoors the ground has terraces: the height under its ground point.)
      int gx = obj ? obj->x : (g->box[0] + g->box[2]) / 2, gy = obj ? obj->y : g->box[3];
      if (!obj && g->sbox[2] > g->sbox[0])
        gx = (g->sbox[0] + g->sbox[2]) / 2, gy = (g->sbox[1] + g->sbox[3]) / 2;
      int zw = scene ? Hd3dScene_StandZ(scene, layer, gx, gy) : 0;

      // The shadows: a ground decal, and the foot.
      int sbb[4] = { 0, 0, 0, 0 };
      bool has_shadow = false;
      if (g->sbox[2] > g->sbox[0]) {
        int ax = 0, ay = 0, sw = g->sbox[2] - g->sbox[0], sh = g->sbox[3] - g->sbox[1];
        uint8 *dst = nd < max_decals ? SprAtlasAlloc(&shelf, atlas, sw, sh, &ax, &ay) : NULL;
        SprDrawGroup(sf, ents, g, true, g->sbox, dst, sbb);
        has_shadow = sbb[2] > sbb[0];
        if (has_shadow && dst) {
          float x0 = (float)(g->sbox[0] + sbb[0]), x1 = (float)(g->sbox[0] + sbb[2]);
          float y0 = (float)(g->sbox[1] + sbb[1] + zw), y1 = (float)(g->sbox[1] + sbb[3] + zw), z = (float)zw;
          float u0 = (float)(ax + sbb[0]), u1 = (float)(ax + sbb[2]);
          float v0 = (float)(ay + sbb[1]), v1 = (float)(ay + sbb[3]);
          R3dQuad *q = &decals[nd++];
          memset(q, 0, sizeof(*q));
          // (Relief: the ground point (x, y, z) shows picture point (x, y - z).)
          SprVertex(&q->v[0], x0, y0, z, u0, v0);
          SprVertex(&q->v[1], x1, y0, z, u1, v0);
          SprVertex(&q->v[2], x1, y1, z, u1, v1);
          SprVertex(&q->v[3], x0, y1, z, u0, v1);
          q->tex = atlas_slot;
          q->flags = kR3dQuad_Decal | kR3dQuad_TwoSided;
          q->shade = 255;
          q->cls = kR3dClass_World;
        } else if (has_shadow) {
          dropped++;
        }
      }
      if (g->box[2] <= g->box[0])
        continue;  // (only a shadow)

      // The card.
      int ax = 0, ay = 0, bb[4], cw = g->box[2] - g->box[0], ch = g->box[3] - g->box[1];
      uint8 *dst = nc < max_cards ? SprAtlasAlloc(&shelf, atlas, cw, ch, &ax, &ay) : NULL;
      if (!dst) {
        dropped++;
        continue;
      }
      SprDrawGroup(sf, ents, g, false, g->box, dst, bb);
      if (bb[2] <= bb[0])
        continue;  // (nothing opaque)
      // The foot (picture px): the middle of the shadow; else the bottom of
      // the picture, lowered by the object's height (jumps, throws). Its x
      // is a sprite's or Link's own, else the middle of the picture.
      int fx, fy;
      if (has_shadow) {
        fx = g->sbox[0] + (sbb[0] + sbb[2]) / 2;
        fy = g->sbox[1] + (sbb[1] + sbb[3]) / 2;
      } else {
        fx = obj && g->kind != kHd3dTag_Ancilla ? obj->x : g->box[0] + (bb[0] + bb[2]) / 2;
        fy = g->box[1] + bb[3] + (obj ? obj->z : 0);
      }
      R3dCard *c = &cards[nc++];
      memset(c, 0, sizeof(*c));
      c->foot[0] = (float)fx;
      c->foot[1] = (float)(fy + zw);
      c->foot[2] = (float)zw;
      c->atlas_x = (int16)(ax + bb[0]), c->atlas_y = (int16)(ay + bb[1]);
      c->w = (int16)(bb[2] - bb[0]), c->h = (int16)(bb[3] - bb[1]);
      c->foot_x = (int16)(fx - (g->box[0] + bb[0]));
      c->foot_y = (int16)(fy - (g->box[1] + bb[1]));
      c->order = g->order;
      c->tex = atlas_slot;
      if (link) {
        c->flags = kR3dCard_Link;
        if (g->index == sf->xray_player) {
          c->flags |= kR3dCard_XRay;
          c->xray_rgb = Mp_PlayerColorRgb(g->index);
        }
      }
    }
  }
  g_hd3d_sprites_dropped = dropped;
  *num_decals = nd;
  return nc;
}
#endif  // MOD_HD3D
