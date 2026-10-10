// Headless scripted test runner for co-op: zelda3 --mptest <script>
//
// Script commands, one per line:
//   load <n>            load reference save chapter n (1..13)
//   join <p> / leave <p>
//   hold <p> <keys>     keys held by player p (B Y s=select S=start U D L R A X l r, "-" for none)
//   run <frames>
//   shot <file.ppm>     render the current frame
//   ff <0|1>            friendly fire
//   status              print player state
//   expect_alive <p>    fail unless player p is standing
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "mp.h"
#include "snes/ppu.h"

static uint16 ParseKeys(const char *s) {
  static const char kKeys[] = "BYsSUDLRAXlr";
  uint16 v = 0;
  for (; *s; s++) {
    const char *t = strchr(kKeys, *s);
    if (t)
      v |= 1 << (t - kKeys);
  }
  return v;
}

static void WritePpm(const char *file, int width, int height, uint32 *pixels, int pitch_px) {
  FILE *f = fopen(file, "wb");
  if (!f) {
    fprintf(stderr, "can't write %s\n", file);
    return;
  }
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint32 c = pixels[y * pitch_px + x];
      uint8 rgb[3] = { c >> 16, c >> 8, c };
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

static void PrintStatus(int frame) {
  printf("[frame %d] module=%d/%d leader=%d resident=%d room=%d ow=%d\n", frame, main_module_index,
         submodule_index, g_mp.leader, g_mp.resident, dungeon_room_index, overworld_screen_index);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    printf("  P%d x=%d y=%d hp=%d downed=%d oam=%d\n", p + 1, Mp_PlayerX(p), Mp_PlayerY(p),
           Mp_PlayerHealth(p), g_mp.p[p].downed, g_mp_render.p[p].count);
  }
}

int RunMpTest(const char *script, int width, int height, uint32 render_flags) {
  FILE *f = fopen(script, "r");
  if (!f) {
    fprintf(stderr, "can't open %s\n", script);
    return 1;
  }
  uint16 held[kMpMaxPlayers] = { 0 };
  uint32 *pixels = calloc(width * height * 4 * 16, 1);
  char line[256];
  int frame = 0, failures = 0;
  while (fgets(line, sizeof(line), f)) {
    char cmd[32] = { 0 }, arg1[200] = { 0 }, arg2[64] = { 0 };
    if (sscanf(line, "%31s %199s %63s", cmd, arg1, arg2) < 1 || cmd[0] == '#')
      continue;
    if (!strcmp(cmd, "load")) {
      SaveLoadSlot(kSaveLoad_Load, 256 + atoi(arg1) - 1);
    } else if (!strcmp(cmd, "join")) {
      Mp_Join(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "leave")) {
      Mp_Leave(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "hold")) {
      held[atoi(arg1) - 1] = ParseKeys(arg2);
    } else if (!strcmp(cmd, "ff")) {
      g_mp.friendly_fire = atoi(arg1);
    } else if (!strcmp(cmd, "run")) {
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        Mp_SetInputs(held);
        ZeldaRunFrame(held[g_mp.leader]);
      }
    } else if (!strcmp(cmd, "shot")) {
      ZeldaDrawPpuFrame((uint8 *)pixels, width * 4, render_flags);
      WritePpm(arg1, width, height, pixels, width);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "status")) {
      PrintStatus(frame);
    } else if (!strcmp(cmd, "expect_alive")) {
      int p = atoi(arg1) - 1;
      if (!Mp_IsAlive(p)) {
        printf("FAIL: P%d not alive at frame %d\n", p + 1, frame);
        failures++;
      }
    } else {
      fprintf(stderr, "unknown command %s\n", cmd);
    }
  }
  fclose(f);
  free(pixels);
  printf(failures ? "MPTEST FAILED\n" : "MPTEST OK\n");
  return failures != 0;
}
