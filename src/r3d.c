// The 3D view's software rasterizer (see r3d.h).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "r3d.h"

struct R3dWork {
  int max_quads, max_cards;
};

R3dWork *R3d_NewWork(int max_quads, int max_cards) {
  R3dWork *w = (R3dWork *)calloc(1, sizeof(R3dWork));
  if (w)
    w->max_quads = max_quads, w->max_cards = max_cards;
  return w;
}

void R3d_FreeWork(R3dWork *work) {
  free(work);
}

void R3d_Draw(R3dWork *work, const R3dFrame *f, const R3dTarget *t, R3dRunner *run, R3dStats *stats) {
  if (stats)
    stats->quads_drawn = stats->cards_drawn = 0;
}

bool R3d_Project(const R3dCamera *cam, const float p[3], float *sx, float *sy, float *depth) {
  *sx = *sy = 0, *depth = 0;
  return false;
}

float R3d_ScaleAt(const R3dCamera *cam, float depth) {
  return 1.0f;
}

bool R3d_BoxVisible(const R3dCamera *cam, int w, int h, const float lo[3], const float hi[3]) {
  return true;
}
