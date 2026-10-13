// The 3D view: which object drew which OAM entries, and sprites as cards
// standing in the scene (see hd3d_internal.h).
#include <SDL.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"

bool g_hd3d_tagging;

void Hd3d_TagFrameStart(void) {
}

void Hd3d_TagBegin(uint8 kind, uint8 index) {
}

void Hd3d_TagEnd(void) {
}

void Hd3dSprites_Capture(Ppu *ppu, Hd3dSpriteFrame *out) {
}

void Hd3dSprites_ReadWorld(Hd3dSpriteFrame *out) {
}

int Hd3dSprites_Build(const Hd3dSpriteFrame *sf, const Hd3dScene *scene, uint8 atlas_slot,
                      uint8 *atlas, uint32 *lut, R3dCard *cards, int max_cards,
                      R3dQuad *decals, int max_decals, int *num_decals) {
  *num_decals = 0;
  return 0;
}
