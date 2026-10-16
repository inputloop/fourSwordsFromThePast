#include "mods.h"  // (mods/SPEC.md: this file is the hd3d mod)
#if MOD_HD3D
// The 3D view: every Link shows the side the pane's camera sees of him
// (see hd3d_internal.h).
//
// The game draws a Link for the way he faces in the world. With the camera
// turned, a Link who runs away from it should show his back, not his side.
// So in phase A, with the world bound, the game's own Link drawing
// (LinkOam_MainOne) runs once more for each Link the camera sees from
// another side: with that facing, into a cleared OAM buffer. His animation
// step, items and state are his own, so walking, the sword, the shield and
// the left and right mirroring all come out as the game would draw them.
// The pose's tiles are put together from the ROM graphics the way the NMI
// would upload them. Then everything is put back byte for byte: the first
// 4 KB of RAM (OAM and Link's variables) and the co-op state. The game never
// sees any of it (tests/coop/hd3d_linkpose.txt checks that).
// Hd3dSprites_Build then shows the new picture in place of the drawn one.
#include <math.h>
#include <string.h>
#include "hd3d_internal.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "features.h"
#include "assets.h"
#include "mp.h"
#include "misc.h"
#include "player_oam.h"

enum {
  kPoseRamBytes = 0x1000,    // RAM the game's Link drawing writes: OAM at 0x800, Link's variables below
};

// The world directions of the facings (RAM 0x2F: 0 up, 2 down, 4 left, 6 right).
static const int8 kFacingDir[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };

static uint8 g_pose_ram[kPoseRamBytes];
static MpState g_pose_mp;

// The camera's right, level: from the same view the pane is drawn with
// (third person, first person or the blend between them).
static void ViewRight(const Cam3d *cam, float *rx, float *ry) {
  Cam3dPose pose;
  R3dCamera view;
  memset(&pose, 0, sizeof(pose));
  pose.w = 256, pose.h = 224;
  Cam3d_MakeView(cam, &pose, NULL, &view);
  *rx = view.right[0], *ry = view.right[1];
}

// A world direction as the camera sees it: to its right (sx) and away from it (sa).
static void Seen(float x, float y, float rx, float ry, float *sx, float *sa) {
  *sx = x * rx + y * ry;
  *sa = x * ry - y * rx;
}

// The facing Link shows the camera (rx, ry): 0 his back, 2 his front, 4 and
// 6 facing left and right on the screen. moving: the way he walks (RAM
// 0x67: 8 up, 4 down, 2 left, 1 right). Walking diagonally the game turns
// him to one of its two sides; then the way he runs counts, unless it lies
// near a diagonal on the screen too (as at the classic view: there his
// facing decides, as in the game). last: the facing shown last frame (-1:
// none); it stays up to 8 degrees past a boundary, so he never flickers
// between two.
static int SeenFacing(int facing, int moving, float rx, float ry, int last) {
  const int8 *f = kFacingDir[(facing >> 1) & 3];
  int mx = (moving & 1) - (moving >> 1 & 1), my = (moving >> 2 & 1) - (moving >> 3 & 1);
  bool runs = mx && my && mx * f[0] + my * f[1] == 1;
  float sx = 0.0f, sa = 0.0f;
  if (runs) {
    Seen((float)mx, (float)my, rx, ry, &sx, &sa);
    float ax = fabsf(sx), aa = fabsf(sa);
    runs = (ax < aa ? ax : aa) < 0.57735f * (ax < aa ? aa : ax);  // (within 30 degrees of an axis)
  }
  if (!runs)
    Seen((float)f[0], (float)f[1], rx, ry, &sx, &sa);
  float len = sqrtf(sx * sx + sa * sa);
  if (!(len > 1e-4f))
    return facing;
  int k = fabsf(sx) >= fabsf(sa) ? (sx > 0.0f ? 6 : 4) : (sa > 0.0f ? 0 : 2);
  if (last >= 0 && last != k) {
    float d = last == 0 ? sa : last == 2 ? -sa : last == 4 ? -sx : sx;
    if (d >= 0.60182f * len)  // (cos 53 degrees)
      k = last;
  }
  return k;
}

static void CopyRom(uint16 *dst, uint16 src, size_t bytes) {
  size_t o = (size_t)src - 0x8000;
  if (src >= 0x8000 && o + bytes <= (size_t)kLinkGraphics_SIZE)
    memcpy(dst, &kLinkGraphics[o], bytes);
}

// Player p's Link drawn by the game facing `facing`, into out. tiles: VRAM
// 0x4000-0x41FF of the picture the game drew of him; bank: his palette bank;
// order_base: where his entries come in Hd3dSprites_Build's order (-1: by
// their OAM index, as the leader's in the regular OAM).
static void DrawPose(Hd3dLinkPose *out, int p, int facing, const uint16 *tiles, uint8 bank, int order_base) {
  memcpy(g_pose_ram, g_ram, sizeof(g_pose_ram));
  memcpy(&g_pose_mp, &g_mp, sizeof(g_pose_mp));
  int r = g_mp.resident;
  Mp_SwapTo(p);
  link_direction_facing = (uint8)facing;
  // A blinking Link: the game's own picture says whether he shows (Hd3dSprites_Build
  // keeps it when it has no body).
  countdown_for_blink = 0;
  // (A hidden sprite goes below the screen instead of past its right edge.)
  enhanced_features0 |= kFeatures0_WidescreenVisualFixes;
  for (int i = 0; i < 128; i++)
    oam_buf[i].y = 0xf0;
  LinkOam_MainOne();

  // The entries, with x worked out again from where the Link really is (as
  // Mp_DrawOtherPlayers does), moved by his netplay smoothing like the
  // picture the game drew of him.
  int dx = p == g_mp.leader ? 0 : g_mp_draw_offset[p][0], dy = p == g_mp.leader ? 0 : g_mp_draw_offset[p][1];
  int sx = (int16)(link_x_coord - BG2HOFS_copy2);
  int n = 0;
  for (int i = 0; i < 128 && n < kHd3dPoseMax; i++) {
    const OamEnt *e = &oam_buf[i];
    if (e->y == 0xf0)
      continue;
    int x = e->x | (bytewise_extended_oam[i] & 1) << 8, y = e->y;
    x = x >= 256 ? x - 512 : x;
    while (x < sx - 128)
      x += 256;
    while (x > sx + 128)
      x -= 256;
    y = y > 0xf0 ? y - 256 : y;  // (above the screen)
    out->x[n] = (int16)(x + dx), out->y[n] = (int16)(y + dy);
    out->oam1[n] = (uint16)(e->charnum | e->flags << 8);
    out->big[n] = (uint8)(bytewise_extended_oam[i] >> 1 & 1);
    out->order[n] = (uint16)(order_base < 0 ? i : order_base + n);
    n++;
  }
  out->count = (uint8)n;
  out->bank = bank;

  // The tiles: his body, the extra pieces, his sword and shield, as
  // NMI_DoUpdates would upload them for this pose (and Mp_DrawOtherPlayers
  // builds them for the other Links).
  uint16 srcs[12];
  Link_CalcDmaSources(srcs);
  uint16 *t = out->tiles;
  memcpy(t, tiles, sizeof(out->tiles));
  CopyRom(&t[0x100], srcs[0], 0x40);
  CopyRom(&t[0x120], srcs[1], 0x40);
  CopyRom(&t[0x140], srcs[2], 0x20);
  CopyRom(&t[0x000], srcs[3], 0x40);
  CopyRom(&t[0x020], srcs[4], 0x40);
  CopyRom(&t[0x040], srcs[5], 0x20);
  memcpy(&t[0x050], &g_ram[srcs[6]], 0x40);
  memcpy(&t[0x070], &g_ram[srcs[7]], 0x40);
  memcpy(&t[0x090], &g_ram[srcs[8]], 0x40);
  memcpy(&t[0x150], &g_ram[srcs[9]], 0x40);
  memcpy(&t[0x170], &g_ram[srcs[10]], 0x40);
  memcpy(&t[0x190], &g_ram[srcs[11]], 0x40);

  // Everything back as it was.
  Mp_SwapTo(r);
  memcpy(g_ram, g_pose_ram, sizeof(g_pose_ram));
  memcpy(&g_mp, &g_pose_mp, sizeof(g_mp));
}

void Hd3dLinkPose_Read(Hd3dSpriteFrame *sf, const Cam3d *cam) {
  for (int p = 0; p < kHd3dMaxLinks; p++)
    sf->poses[p].count = 0;
  if (!cam) {
    memset(sf->pose_facing, 0, sizeof(sf->pose_facing));
    return;
  }
  float rx, ry;
  ViewRight(cam, &rx, &ry);
  for (int p = 0; p < kHd3dMaxLinks && p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined || p == sf->hide_player) {
      sf->pose_facing[p] = 0;
      continue;
    }
    int facing = *Mp_PlayerRam(p, 0x2F) & 6;  // link_direction_facing
    // The way he walks counts where walking turns him (not with the sword
    // held: then he walks without turning).
    bool steers = Mp_MovesFreely(*Mp_PlayerRam(p, 0x5D)) && !(*Mp_PlayerRam(p, 0x3A) & 0x80) &&
                  submodule_index == 0;
    int moving = steers ? *Mp_PlayerRam(p, 0x67) & 15 : 0;  // link_direction
    int seen = SeenFacing(facing, moving, rx, ry, sf->pose_facing[p] - 1);
    sf->pose_facing[p] = (uint8)(seen + 1);
    if (seen == facing)
      continue;
    // The leader is in the regular OAM with the video chip's tiles; the
    // others each have an extra sprite set with their own tiles and colors.
    if (p == sf->leader) {
      DrawPose(&sf->poses[p], p, seen, sf->obj_vram, 0, -1);
      continue;
    }
    for (int s = 0; s < sf->num_sets && s < kPpuMaxExtraSpriteSets; s++) {
      if (sf->set_player[s] == p && sf->set_link_count[s]) {
        DrawPose(&sf->poses[p], p, seen, sf->sets[s].tiles, sf->sets[s].bank & 3, 128 + s * kPpuMaxExtraSprites);
        break;
      }
    }
  }
}
#endif  // MOD_HD3D
