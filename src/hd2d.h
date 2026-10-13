// "2D-HD" look: a display-only post-processing pass in the style of games
// like Octopath Traveler. The pixel art is kept, and a lens and light pass
// is put over it: a tilt-shift depth of field (in focus around the players,
// softly out of focus above and below, like a diorama), far layers hazy,
// lights from the Lamp, torches and fire (dark rooms are lit by them alone),
// optionally Links seen through what hides them, bloom, color grading and a vignette.
// Sprites and the HUD stay sharp, because the PPU reports which layer every
// pixel came from.
//
// It only changes the picture, never the game, so it is safe in netplay.
#ifndef ZELDA3_HD2D_H_
#define ZELDA3_HD2D_H_

#include "types.h"

typedef struct Hd2dSettings {
  bool enabled;
  uint8 depth_of_field;  // 0-100: how blurred the top and bottom of the screen get
  uint8 bloom;           // 0-100
  uint8 vignette;        // 0-100
  uint8 grade;           // 0-100: contrast, saturation and warmth
  uint8 lighting;        // 0-100: light from the Lamp, torches and fire; dark rooms lit by them
  bool see_through;      // show Links hidden behind scenery (off: hidden, like the original)
} Hd2dSettings;

extern Hd2dSettings g_hd2d;

// Draws the current frame like ZeldaDrawPpuFrame, with the 2D-HD look
// applied when it is enabled. view (0-3) is the split-screen pane; each
// keeps its own focus, around the players of the world it shows.
// The widescreen columns on each side every world is drawn with (a display
// setting: each world's PPU can hold an old value, from before the setting
// changed or from a snapshot). -1: leave the PPU's own.
extern int g_draw_extra_left_right;
void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view);
// The 2D picture and the 2D-HD pass (what Hd2d_DrawFrame draws when the 3D
// view doesn't draw the pane).
void Hd2d_Draw2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view);

typedef void Hd2dJob(int begin, int end, void *ctx);
// The worker pool (up to 3 threads plus this one): job over [0, count) in
// parts, and waits. Main thread only, not re-entrant.
void Hd2d_ParallelFor(int count, Hd2dJob *job, void *ctx);

// The 2D-HD look over a picture drawn by someone else (the 3D view).
typedef struct Hd2dPostIn {
  const uint8 *cls;          // per pixel: 0 world, 2 HUD (untouched), 4 sprite (sharp), 5 far, 0xff untouched
  int cls_pitch;
  const uint8 *blur;         // 0-255 out of focus per pixel (pitch w); NULL: the focus band
  bool lit;                  // already lit: skip the lighting stage
  bool cheap;                // only grade and vignette (3 or more panes)
} Hd2dPostIn;
void Hd2d_Post(uint8 *pixels, size_t pitch, int w, int h, int view, const Hd2dPostIn *in);

// A light in world pixels. kind 0 the Lamp, 1 a torch, 2 fire.
typedef struct Hd2dLight { float x, y, radius, power, r, g, b; uint8 kind; } Hd2dLight;
// The lights of the bound world (the room origin from where it was loaded,
// right for rooms 0x100 and up). dark: the Lamp lights wherever the team has it.
int Hd2d_GatherLights(Hd2dLight *out, int max, bool dark);
struct Ppu;
// A dark room the game darkens with color math. *lit: 0 (no torch) to 1 (all lit).
bool Hd2d_IsDarkRoom(const struct Ppu *ppu, float *lit);

// (Tests) how many hidden Link pixels the last frame showed through scenery,
// and whether it lit a dark room itself.
int Hd2d_HiddenPixels(void);
bool Hd2d_LitDarkRoom(void);
int Hd2d_NumLights(void);

#endif  // ZELDA3_HD2D_H_
