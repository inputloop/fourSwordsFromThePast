// "2D-HD" look: a display-only post-processing pass in the style of games
// like Octopath Traveler. The pixel art is kept, and a lens and light pass
// is put over it: a tilt-shift depth of field (the top and bottom of the
// screen softly out of focus, like a diorama), bloom, color grading and a
// vignette. Sprites and the HUD stay sharp, because the PPU reports which
// layer every pixel came from.
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
} Hd2dSettings;

extern Hd2dSettings g_hd2d;

// Draws the current frame like ZeldaDrawPpuFrame, with the 2D-HD look
// applied when it is enabled.
void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags);

#endif  // ZELDA3_HD2D_H_
