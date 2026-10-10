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
//   expect_downed <p>   fail unless player p is down
//   expect_hp <p> <n>   fail unless player p has n health (1/8 hearts)
//   clearsprites        remove all enemies
//   holecheck           verify every overworld fall hole resolves correctly in co-op
//   expect_module <n>   fail unless the game module is n (7 dungeon, 9 overworld)
//   peek <hexaddr> [n]  print RAM bytes
//   spawn <type> <p> [dx]  spawn sprite type (hex) dx (default 24) pixels right of player p
//   hp <p> <health>     set a player's health (1/8 hearts)
// Session (netplay) commands. "hold" then refers to local players.
//   host <port> | connect <addr> | disconnect
//   tick <n>            run n session ticks in real time (16ms apart)
//   waitplay            tick until connected and playing (max 15s)
//   localjoin <i> / localleave <i>
//   corrupt             flip a byte of RAM (to test desync recovery)
//   lan [n]             search for LAN games (and expect n)
//   menu                toggle the overlay menu (navigate with hold + tick)
//   expect_players <n>  fail unless n players are in the game
//   expect_resyncs <n>  fail unless exactly n resyncs happened
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL.h>
#include "types.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "features.h"
#include "mp.h"
#include "snes/ppu.h"
#include "sprite.h"
#include "overworld.h"
#include "assets.h"
#include "session.h"
#include "ui.h"
#include "upnp.h"

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
  const SessionStatus *st = Session_GetStatus();
  printf("[frame %d] module=%d/%d leader=%d resident=%d room=%d ow=%d hash=%08x | session mode=%d state=%d id=%d ping=%d buf=%d resyncs=%d local_slot0=%d msg='%s'\n",
         frame, main_module_index, submodule_index, g_mp.leader, g_mp.resident, dungeon_room_index, overworld_screen_index,
         ZeldaHashState(), st->mode, st->client_state, st->my_id, st->ping_ms, st->frames_buffered, st->resyncs,
         st->local_slot[0], st->message);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined)
      continue;
    printf("  P%d x=%d y=%d hp=%d downed=%d oam=%d dir=%d bframes=%d oamy=%x state=%d\n", p + 1, Mp_PlayerX(p), Mp_PlayerY(p),
           Mp_PlayerHealth(p), g_mp.p[p].downed, g_mp_render.p[p].count, *Mp_PlayerRam(p, 0x2F),
           *Mp_PlayerRam(p, 0x3C), *Mp_PlayerRam(p, 0x44), *Mp_PlayerRam(p, 0x5D));
  }
  for (int k = 0; k < 16; k++)
    if (sprite_state[k])
      printf("  sprite %d type=%x state=%d hp=%d x=%d y=%d\n", k, sprite_type[k], sprite_state[k], sprite_health[k],
             Sprite_GetX(k), Sprite_GetY(k));
}

int RunMpTest(const char *script, int width, int height, uint32 render_flags) {
  FILE *f = fopen(script, "r");
  if (!f) {
    fprintf(stderr, "can't open %s\n", script);
    return 1;
  }
  uint16 held[kMpMaxPlayers] = { 0 };
  Session_Init("Tester");
  Ui_Init(NULL);
  uint32 *pixels = calloc(width * height * 4 * 16, 1);
  char line[256];
  int frame = 0, failures = 0;
  while (fgets(line, sizeof(line), f)) {
    char cmd[32] = { 0 }, arg1[200] = { 0 }, arg2[64] = { 0 }, arg3[64] = { 0 };
    if (sscanf(line, "%31s %199s %63s %63s", cmd, arg1, arg2, arg3) < 1 || cmd[0] == '#')
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
      Ui_Draw((uint8 *)pixels, width * 4, width, height, 1);
      WritePpm(arg1, width, height, pixels, width);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "status")) {
      PrintStatus(frame);
    } else if (!strcmp(cmd, "spawn")) {
      for (int j = 15; j >= 0; j--) {
        if (sprite_state[j] == 0) {
          sprite_type[j] = strtol(arg1, NULL, 16);
          sprite_state[j] = 9;
          SpritePrep_LoadProperties(j);
          int sp = atoi(arg2) - 1;
          Sprite_SetX(j, Mp_PlayerX(sp) + (arg3[0] ? atoi(arg3) : 24));
          Sprite_SetY(j, Mp_PlayerY(sp));
          sprite_floor[j] = link_is_on_lower_level;
          if (player_is_indoors) sprite_N[j] = 0xff; else sprite_N_word[j] = 0xffff;
          printf("spawned sprite %d type %x\n", j, sprite_type[j]);
          break;
        }
      }
    } else if (!strcmp(cmd, "hp")) {
      *Mp_PlayerRam(atoi(arg1) - 1, 0xF36D) = atoi(arg2);
    } else if (!strcmp(cmd, "host")) {
      Session_Host(atoi(arg1), false);
    } else if (!strcmp(cmd, "connect")) {
      Session_Join(arg1);
    } else if (!strcmp(cmd, "disconnect")) {
      Session_Disconnect();
    } else if (!strcmp(cmd, "localjoin")) {
      Session_LocalJoin(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "localleave")) {
      Session_LocalLeave(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "tick") || !strcmp(cmd, "waitplay")) {
      bool wait = !strcmp(cmd, "waitplay");
      int n = wait ? 15 * 60 : atoi(arg1);
      for (int i = 0; i < n; i++) {
        uint16 in[kMpMaxPlayers];
        memcpy(in, held, sizeof(in));
        Ui_Update(in, 0);
        if (!Ui_PausesGame())
          frame += Session_Tick(in, g_wanted_zelda_features);
        SDL_Delay(16);
        if (wait && Session_GetStatus()->client_state == 3)
          break;
      }
      if (wait && Session_GetStatus()->client_state != 3) {
        printf("FAIL: not connected: %s\n", Session_GetStatus()->message);
        failures++;
      }
    } else if (!strcmp(cmd, "lan")) {
      Session_RefreshLan();
      for (int i = 0; i < 30; i++) {
        Session_Tick(held, g_wanted_zelda_features);
        SDL_Delay(16);
      }
      SessionLanGame games[8];
      int n = Session_GetLanGames(games, 8);
      printf("LAN games found: %d\n", n);
      for (int i = 0; i < n; i++)
        printf("  %s %s players=%d\n", games[i].name, games[i].addr, games[i].players);
      if (arg1[0] && n != atoi(arg1)) {
        printf("FAIL: expected %s LAN games\n", arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "upnp")) {
      char ip[64] = { 0 };
      bool ok = Upnp_MapPort(atoi(arg1), ip, sizeof(ip));
      printf("UPnP map port %s: %s external=%s\n", arg1, ok ? "OK" : "FAILED", ip);
      if (!ok)
        failures++;
      Upnp_UnmapPort();
    } else if (!strcmp(cmd, "menu")) {
      Ui_HandleKey(SDLK_ESCAPE, true);
    } else if (!strcmp(cmd, "corrupt")) {
      g_ram[0xF360] ^= 0x55;  // rupees
    } else if (!strcmp(cmd, "expect_players")) {
      if (Mp_NumJoined() != atoi(arg1)) {
        printf("FAIL: %d players, expected %s\n", Mp_NumJoined(), arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_resyncs")) {
      if (Session_GetStatus()->resyncs != atoi(arg1)) {
        printf("FAIL: %d resyncs, expected %s\n", Session_GetStatus()->resyncs, arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "peek")) {
      int addr = strtol(arg1, NULL, 16), n = arg2[0] ? atoi(arg2) : 1;
      printf("[frame %d] peek %x:", frame, addr);
      for (int i = 0; i < n; i++)
        printf(" %02x", g_ram[addr + i]);
      printf("\n");
    } else if (!strcmp(cmd, "expect_module")) {
      if (main_module_index != atoi(arg1)) {
        printf("FAIL: module %d, expected %s at frame %d\n", main_module_index, arg1, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "holecheck")) {
      // For every overworld fall hole, put a Link anywhere the original camera
      // could show it and check the co-op search finds that hole's entrance.
      int bad = 0, total = 0;
      for (int i = 0; i <= 36 / 2; i++) {
        int a = kFallHole_Area[i];
        int j = a & 0xbf;
        bool big = !kOverworldMapIsSmall[a & 0x3f];
        overworld_area_index = a;
        overworld_offset_base_y = kOverworld_OffsetBaseY[j];
        overworld_offset_base_x = kOverworld_OffsetBaseX[j] >> 3;
        overworld_offset_mask_y = big ? 0x3f0 : 0x1f0;
        overworld_offset_mask_x = overworld_offset_mask_y >> 3;
        uint16 pos = kFallHole_Pos[i];
        int x = ((pos & overworld_offset_mask_x) + overworld_offset_base_x) << 3;
        int cam16 = ((pos >> 3) & overworld_offset_mask_y) + overworld_offset_base_y;
        for (int r = 16 + 8; r < 16 + 216; r++) {
          int k = Overworld_FindFallHoleNear(x, cam16 + r);
          total++;
          if (k < 0 || kFallHole_Entrances[k] != kFallHole_Entrances[i])
            bad++;
        }
      }
      printf("holecheck: %d of %d positions resolved to the wrong hole\n", bad, total);
      if (bad)
        failures++;
    } else if (!strcmp(cmd, "clearsprites")) {
      for (int k = 0; k < 16; k++)
        sprite_state[k] = 0;
    } else if (!strcmp(cmd, "expect_hp")) {
      int p = atoi(arg1) - 1;
      if (Mp_PlayerHealth(p) != atoi(arg2)) {
        printf("FAIL: P%d has %d health, expected %s\n", p + 1, Mp_PlayerHealth(p), arg2);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_downed")) {
      int p = atoi(arg1) - 1;
      if (!g_mp.p[p].joined || !g_mp.p[p].downed) {
        printf("FAIL: P%d not downed at frame %d\n", p + 1, frame);
        failures++;
      }
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
