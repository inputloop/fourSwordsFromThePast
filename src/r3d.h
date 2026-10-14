// A small software rasterizer for the 3D view: textured quads with a depth
// buffer, flat sprite cards that face the camera, per-vertex light and fog,
// drawn in row bands on worker threads.
// Plain C on types.h only (no SDL, no game state), so tests/unit/hd3d_unit_test.c
// checks it on its own in CI.
#ifndef ZELDA3_R3D_H_
#define ZELDA3_R3D_H_

#include "types.h"

enum {
  kR3dMaxW = 448,            // 256 + 2 * 96 widescreen columns
  kR3dMaxH = 240,
  kR3dBands = 8,             // row bands, the units of parallel work
  kR3dMaxTextures = 6,
  kR3dMaxLights = 48,
};

// What a pixel shows (R3dTarget.cls), for the 2D-HD pass.
enum {
  kR3dClass_World = 0,       // ground, walls, objects
  kR3dClass_Hud = 2,         // (written by hd3d.c: the HUD and text boxes, left untouched)
  kR3dClass_Sprite = 4,      // cards: kept sharp
  kR3dClass_Far = 5,         // background, far planes, fog
};

// 8-bit color indices (0 = see-through) looked up in lut. Texel (u, v) is
// read at ((u - origin_x) & (w - 1), (v - origin_y) & (h - 1)), w = 1 << log2_w.
typedef struct R3dTexture {
  const uint8 *texels;
  const uint32 *lut;         // 256 colors 0x00RRGGBB, brightness applied
  int32 origin_x, origin_y;
  uint8 log2_w, log2_h;
  uint8 subtract_from;       // the game's darkness reaches indices >= this (0 all; 65 OBJ palettes 4-7; 255 none)
} R3dTexture;

typedef struct R3dVertex {
  float x, y, z;             // world: x east, y south, z up, in SNES pixels
  float u, v;                // texel coordinates
} R3dVertex;

enum {
  kR3dQuad_TwoSided = 1,     // drawn from both sides
  kR3dQuad_Flat = 2,         // a solid color (R3dQuad.color), no texture
  kR3dQuad_Half = 4,         // 50% blend over what is behind; writes no depth
  kR3dQuad_Add = 8,          // added to what is behind; writes no depth
  kR3dQuad_Decal = 16,       // pulled 1 px toward the camera (shadows on the ground)
  kR3dQuad_Dither = 32,      // only every other pixel (faded scenery)
};

// A planar convex quad. Its front is the side (v1 - v0) x (v2 - v0) points to.
typedef struct R3dQuad {
  R3dVertex v[4];
  uint32 color;              // kR3dQuad_Flat: 0x00RRGGBB, brightness applied
  uint8 tex;                 // index into R3dFrame.tex
  uint8 flags;               // kR3dQuad_*
  uint8 shade;               // 0-255 face shading (255: as painted)
  uint8 cls;                 // kR3dClass_* for its pixels
} R3dQuad;

enum {
  kR3dCard_Link = 1,         // a Link: its hidden pixels show 3:2 when R3dFrame.see_through
  kR3dCard_XRay = 2,         // the pane player's own Link: a silhouette where hidden, when R3dFrame.xray
  kR3dCard_Front = 4,        // a first-person weapon: drawn at R3dCard.scale over everything, after the other cards
};

// A sprite: a flat picture facing the camera, standing on its foot point,
// drawn at the foot's scale and depth (2 px nearer), tested against the
// depth buffer but not written to it.
typedef struct R3dCard {
  float foot[3];             // world
  int16 atlas_x, atlas_y, w, h;   // its picture in R3dFrame.tex[tex]
  int16 foot_x, foot_y;      // the foot point inside the picture
  uint16 order;              // among cards at equal depth, lower draws on top (OAM order)
  uint8 tex;
  uint8 flags;               // kR3dCard_*
  uint32 xray_rgb;
  float scale;               // kR3dCard_Front: screen px per texel (0: the foot's scale, as any card)
} R3dCard;

typedef struct R3dLight {
  float x, y, z;             // world
  float radius, power;       // adds (1 - d^2 / radius^2)^1.5 * power, d in 3D
  float r, g, b;
} R3dLight;

// A ready-to-use camera (cam3d.c makes it).
typedef struct R3dCamera {
  float eye[3];
  float right[3], up[3], fwd[3];  // unit vectors
  float focal;               // perspective: pixels at depth 1; ortho: pixels per world pixel
  float stretch_y;           // extra vertical scale (sqrt 2 at the default third-person view)
  float cx, cy;              // the screen point straight ahead
  float znear, zfar;
  bool ortho;                // parallel projection (the oblique test camera)
} R3dCamera;

typedef struct R3dFrame {
  R3dCamera cam;
  int w, h;                  // at most kR3dMaxW x kR3dMaxH
  R3dTexture tex[kR3dMaxTextures];
  const R3dQuad *quads;
  int num_quads;
  const R3dCard *cards;
  int num_cards;
  // Light per channel: ambient + the lights, capped at light_max.
  float ambient[3];
  const R3dLight *lights;
  int num_lights;
  float light_max;           // 1.25
  float card_min_light;      // cards are lit at least this much (0.75 in a dark room lit by the 2D-HD look)
  // The game's own darkness (a dark room without the 2D-HD lighting): this
  // much (0-255 per channel) is subtracted, less where lights reach if subtract_lights.
  float subtract[3];
  bool subtract_lights;
  bool subtract_cards;       // cards too (the game's color math covers OBJ palettes 4-7)
  // Fog toward fog_rgb: by how far the picture point (x, y - z) lies outside
  // the live rectangle (smoothstep live_fog0..live_fog1 px), and by distance
  // from the eye (dist_fog0..dist_fog1; dist_fog1 == 0: none). The larger wins.
  float live_x0, live_y0, live_x1, live_y1;
  float live_fog0, live_fog1;
  float dist_fog0, dist_fog1;
  uint32 fog_rgb;
  const uint32 *background;  // drawn first: w x h, background_pitch pixels a row; NULL: fog_rgb
  int background_pitch;
  bool see_through, xray;
} R3dFrame;

typedef struct R3dTarget {
  uint8 *pixels;             // the caller's picture: exactly w x h pixels at pitch bytes are written
  size_t pitch;
  float *depth;              // w x h scratch (pitch w)
  uint8 *cls;                // w x h classes (pitch cls_pitch)
  int cls_pitch;
} R3dTarget;

typedef struct R3dStats { int quads_drawn, cards_drawn; } R3dStats;

typedef void R3dJob(int begin, int end, void *ctx);
// Runs job over [0, count) in parts (maybe on other threads) and waits. NULL: here.
typedef void R3dRunner(int count, R3dJob *job, void *ctx);

typedef struct R3dWork R3dWork;
R3dWork *R3d_NewWork(int max_quads, int max_cards);  // NULL: out of memory
void R3d_FreeWork(R3dWork *work);
// Draws the frame. Writes nothing outside the target's w x h pixels whatever
// the input (NaN, huge or degenerate geometry, the eye inside it). Not re-entrant.
void R3d_Draw(R3dWork *work, const R3dFrame *f, const R3dTarget *t, R3dRunner *run, R3dStats *stats);
// Where a world point lands. False: behind the camera (sx, sy then point its way, mirrored).
bool R3d_Project(const R3dCamera *cam, const float p[3], float *sx, float *sy, float *depth);
// Screen pixels per world pixel at that depth (horizontally).
float R3d_ScaleAt(const R3dCamera *cam, float depth);
// Can the world box (lo..hi) be in view?
bool R3d_BoxVisible(const R3dCamera *cam, int w, int h, const float lo[3], const float hi[3]);

#endif  // ZELDA3_R3D_H_
