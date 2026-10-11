// Headless scripted test runner for co-op: zelda3 --mptest <script>
//
// Script commands, one per line:
//   load <n>            load reference save chapter n (1..13)
//   join <p> / leave <p>
//   hold <p> <keys>     keys held by player p (B Y s=select S=start U D L R A X l r, "-" for none)
//   run <frames>
//   shot <file.ppm>     render the current frame
//   splitshot <file.ppm> <p> <q>  render the worlds of players p and q side by side
//   ff <0|1>            friendly fire
//   together <0|1>      1: shared screen with group exits, 0: free roaming (default)
//   expect_instances <n>  fail unless the players are spread over n worlds
//   expect_same <p> <q> / expect_apart <p> <q>  players in the same / different worlds
//   view <p>            later commands without a player (shot, peek, expect_module) use p's world
//   status              print player state
//   expect_alive <p>    fail unless player p is standing
//   expect_downed <p>   fail unless player p is down
//   expect_hp <p> <n>   fail unless player p has n health (1/8 hearts)
//   clearsprites        remove all enemies
//   message <hexid>     open a text box
//   holecheck           verify every overworld fall hole resolves correctly in co-op
//   expect_module <n>   fail unless the game module is n (7 dungeon, 9 overworld)
//   peek <hexaddr> [n]  print RAM bytes
//   newgame             create a save file in slot 1 and load it
//   enter <hexroom>     go into a room through the game's first entrance to it
//   spoke <hexoff> <hexval> / loadslot  change a byte of save slot 1 / load slot 1
//   rando <off|random|hexseed>  randomizer setting for new save files
//   randocheck <seed> <count> [v]  check that seeds can be beaten (v: print spoilers)
//   pos <p> <x> <y>     put player p at x,y
//   poke <hexaddr> <hexval>  change a RAM byte (shared progress spreads to every world)
//   expect_ram <hexaddr> <hexval>  fail unless the RAM byte has that value
//   expect_near <p> <x> <y>  fail unless player p is within 4 pixels of x,y
//   spawn <type> <p> [dx]  spawn sprite type (hex) dx (default 24) pixels right of player p
//   hp <p> <health>     set a player's health (1/8 hearts)
// Session (netplay) commands. "hold" then refers to local players.
//   host <port> | connect <addr> | disconnect
//   tick <n>            run n session ticks in real time (16ms apart)
//   waitplay            tick until connected and playing (max 15s)
//   story <0|1>         the Four Swords opening story
//   title <0|1>         "Four Swords of the Past" on the title screen
//   inputdelay <frames> online input delay
//   pram <p> <hexaddr> <hexval>  change a byte of player p's own Link state
//   resetcounters       zero the netplay statistics (resyncs, rollbacks, own corrections)
//   expect_own_corrections <n>  fail if our own Link was corrected more than n times
//   tickwander <n> <seed>  tick n times with local players walking around randomly
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
#include "instance.h"
#include "player.h"
#include "util.h"
#include "randomizer.h"
#include "story.h"
#include "config.h"
#include "select_file.h"
#include "messaging.h"
#include "misc.h"
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

static void SwitchToPlayer(int p) {
  int i = Inst_OfPlayer(p);
  if (i >= 0)
    Inst_Switch(i);
}

static void PrintStatus(int frame) {
  const SessionStatus *st = Session_GetStatus();
  int r = Inst_Current();
  for (int i = 0; i < kInstMax; i++) {
    if (!Inst_IsActive(i))
      continue;
    Inst_Switch(i);
    printf("[frame %d] world %d: module=%d/%d leader=%d room=%d ow=%d indoors=%d cam=%d,%d quad=%d,%d off=%d%s\n", frame, i, main_module_index,
           submodule_index, g_mp.leader, dungeon_room_index, overworld_screen_index, player_is_indoors, BG2HOFS_copy2, BG2VOFS_copy2,
           link_quadrant_x, link_quadrant_y, g_mp.quad_offset, g_mp.quad_valid ? "" : "?");
  }
  Inst_Switch(r);
  printf("[frame %d] module=%d/%d leader=%d resident=%d room=%d ow=%d hash=%08x | session mode=%d state=%d id=%d ping=%d buf=%d pred=%d rb=%d resyncs=%d local_slot0=%d msg='%s'\n",
         frame, main_module_index, submodule_index, g_mp.leader, g_mp.resident, dungeon_room_index, overworld_screen_index,
         Inst_HashState(), st->mode, st->client_state, st->my_id, st->ping_ms, st->frames_buffered, st->predicted, st->rollbacks, st->resyncs,
         st->local_slot[0], st->message);
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!Inst_IsJoined(p))
      continue;
    SwitchToPlayer(p);
    printf("  P%d world=%d x=%d y=%d hp=%d downed=%d oam=%d dir=%d bframes=%d oamy=%x state=%d z=%d vx=%d aux=%d inc=%d carried=%d\n", p + 1, Inst_Current(), Mp_PlayerX(p), Mp_PlayerY(p),
           Mp_PlayerHealth(p), g_mp.p[p].downed, g_mp_render.p[p].count, *Mp_PlayerRam(p, 0x2F),
           *Mp_PlayerRam(p, 0x3C), *Mp_PlayerRam(p, 0x44), *Mp_PlayerRam(p, 0x5D), (int16)WORD(*Mp_PlayerRam(p, 0x24)), (int8)*Mp_PlayerRam(p, 0x28),
           *Mp_PlayerRam(p, 0x4D), *Mp_PlayerRam(p, 0x46), g_mp.carried_by[p]);
  }
  Inst_Switch(r);
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
  int view = 0;
  Session_Init("Tester");
  Ui_Init(NULL);
  uint32 *pixels = calloc(width * height * 4 * 16, 1);
  char line[256];
  int frame = 0, failures = 0;
  while (fgets(line, sizeof(line), f)) {
    char cmd[32] = { 0 }, arg1[200] = { 0 }, arg2[64] = { 0 }, arg3[64] = { 0 };
    if (sscanf(line, "%31s %199s %63s %63s", cmd, arg1, arg2, arg3) < 1 || cmd[0] == '#')
      continue;
    // Commands about the game look at the world of the viewed player.
    SwitchToPlayer(view);
    if (!strcmp(cmd, "load")) {
      SaveLoadSlot(kSaveLoad_Load, 256 + atoi(arg1) - 1);
    } else if (!strcmp(cmd, "join")) {
      Inst_Join(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "leave")) {
      Inst_Leave(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "hold")) {
      held[atoi(arg1) - 1] = ParseKeys(arg2);
    } else if (!strcmp(cmd, "ff")) {
      Inst_SetFriendlyFire(atoi(arg1));
    } else if (!strcmp(cmd, "together")) {
      Inst_SetIndependent(!atoi(arg1));
    } else if (!strcmp(cmd, "view")) {
      view = atoi(arg1) - 1;
    } else if (!strcmp(cmd, "expect_instances")) {
      if (Inst_Count() != atoi(arg1)) {
        printf("FAIL: %d worlds, expected %s at frame %d\n", Inst_Count(), arg1, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_same") || !strcmp(cmd, "expect_apart")) {
      bool same = Inst_OfPlayer(atoi(arg1) - 1) == Inst_OfPlayer(atoi(arg2) - 1);
      if (same != !strcmp(cmd, "expect_same")) {
        printf("FAIL: P%s and P%s are %s at frame %d\n", arg1, arg2, same ? "together" : "apart", frame);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_sees") || !strcmp(cmd, "expect_not_sees")) {
      // expect_sees <a> <b>: player a's screen shows player b, who is in another world.
      int a = atoi(arg1) - 1, b = atoi(arg2) - 1;
      bool seen = false;
      SwitchToPlayer(a);
      InstRemoteLink rl[kMpMaxPlayers * 2];
      int nr = Inst_LinksFromOtherWorlds(rl, countof(rl));
      for (int k = 0; k < nr; k++) {
        if (rl[k].player != b)
          continue;
        int x = (rl[k].rp->oam[0] & 0xff) | (rl[k].rp->high[0] & 1) << 8, y = rl[k].rp->oam[0] >> 8;
        x = (x >= 256 ? x - 512 : x) + rl[k].dx;
        y = (y >= 240 ? y - 256 : y) + rl[k].dy;
        seen |= x > -16 && x < 256 && y > -16 && y < 224;
      }
      if (seen != !strcmp(cmd, "expect_sees")) {
        printf("FAIL: P%s %s P%s at frame %d\n", arg1, seen ? "sees" : "does not see", arg2, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "reload")) {
      // reload: save and load every world the way netplay sends them; nothing may change.
      ByteArray arr = { 0 };
      uint32 before = Inst_HashState();
      Inst_SaveState(&arr);
      if (!Inst_LoadState(arr.data, arr.size) || Inst_HashState() != before) {
        printf("FAIL: the state changed when reloaded at frame %d\n", frame);
        failures++;
      }
      ByteArray_Destroy(&arr);
    } else if (!strcmp(cmd, "expect_bg3hofs")) {
      // expect_bg3hofs <x>: the HUD layer's horizontal scroll (after drawing)
      if (g_zenv.ppu->bgLayer[2].hScroll != atoi(arg1)) {
        printf("FAIL: BG3 scroll is %d, expected %s at frame %d\n", g_zenv.ppu->bgLayer[2].hScroll, arg1, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "run")) {
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        Inst_RunFrame(held);
      }
    } else if (!strcmp(cmd, "shot")) {
      g_mp_local_mask = 1 << view;
      Ui_SetViews(&(UiView){ 0, 0, width, height, view }, 1);
      ZeldaDrawPpuFrame((uint8 *)pixels, width * 4, render_flags);
      Ui_Draw((uint8 *)pixels, width * 4, width, height, 1);
      WritePpm(arg1, width, height, pixels, width);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "randocheck")) {
      // randocheck <first seed> <count> [verbose]: every seed must give a beatable placement
      int bad = 0;
      for (uint32 seed = strtoul(arg1, NULL, 16), n = atoi(arg2); n > 0; n--, seed++) {
        if (arg3[0])
          printf("seed %08x\n", seed);
        if (!Rando_Check(seed, arg3[0] != 0))
          bad++, printf("seed %08x: no placement found\n", seed);
      }
      printf("randocheck: %d of %s seeds failed\n", bad, arg2);
      if (bad)
        failures++;
    } else if (!strcmp(cmd, "newgame")) {
      // newgame: make a new save file in slot 1 and load it, as if picked on the file select screen
      uint8 *sram = g_zenv.sram;
      memset(sram, 0, 0x500);
      static const uint16 kName[6] = { 0x18e, 0x18e, 0x18e, 0x18e, 0x18e, 0x18e };  // spaces? any tile works
      memcpy(sram + 0x3d9, kName, sizeof(kName));
      FileSelect_InitNewSave(sram);
      srm_var1 = 2;
      WORD(g_ram[0]) = 0;
      CopySaveToWRAM();
      music_control = 0xf1;
    } else if (!strcmp(cmd, "enter")) {
      // enter <hex room>: go through the first entrance that leads into that room
      int room = strtol(arg1, NULL, 16);
      for (int i = 0; i < 0x85; i++) {
        if (kEntranceData_rooms[i] == room) {
          which_entrance = i;
          main_module_index = 6;
          submodule_index = 0;
          printf("entering room %x through entrance %x\n", room, i);
          break;
        }
      }
    } else if (!strcmp(cmd, "spoke")) {
      // spoke <hexoffset> <hexval>: change a byte of save slot 1 (fixing its checksum)
      g_zenv.sram[strtol(arg1, NULL, 16)] = strtol(arg2, NULL, 16);
      Intro_FixCksum(g_zenv.sram);
    } else if (!strcmp(cmd, "loadslot")) {
      // loadslot: load save slot 1, as if picked on the file select screen
      srm_var1 = 2;
      WORD(g_ram[0]) = 0;
      CopySaveToWRAM();
      music_control = 0xf1;
    } else if (!strcmp(cmd, "rando")) {
      // rando <off|random|hexseed>: randomizer setting for new save files
      Rando_SetNewGameSetting(!strcmp(arg1, "off") ? kRando_Off : !strcmp(arg1, "random") ? kRando_RandomSeed :
                              (uint32)strtoul(arg1, NULL, 16));
    } else if (!strcmp(cmd, "chestdump")) {
      for (int i = 0; i < (int)kDungeonRoomChests_SIZE; i += 3)
        printf("chest %3d room %03x big %d item %02x\n", i / 3, WORD(kDungeonRoomChests[i]) & 0x7fff,
               WORD(kDungeonRoomChests[i]) >> 15, kDungeonRoomChests[i + 2]);
    } else if (!strcmp(cmd, "trace")) {
      extern bool g_inst_trace;
      g_inst_trace = atoi(arg1);
    } else if (!strcmp(cmd, "wander")) {
      // wander <frames> <seed>: every player walks around randomly and swings the sword.
      uint32 seed = atoi(arg2) * 2654435761u + 1;
      bool checks = arg3[0] != 0;  // wander <frames> <seed> check: report jumps / invisible Links
      // wander <frames> <seed> together: everybody walks the same way (and sometimes
      // stands still), so groups reach screen edges at the same time.
      bool together = !strcmp(arg3, "together");
      int jumps = 0, invisible = 0, runaways = 0;
      int idle_moving[kMpMaxPlayers] = { 0 };
      const char *trace_env = getenv("ZELDA3_TRACE_FRAMES");  // "from,to": trace those frames
      int trace_from = -1, trace_to = -1;
      if (trace_env)
        sscanf(trace_env, "%d,%d", &trace_from, &trace_to);
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        if (trace_env) {
          extern bool g_inst_trace;
          g_inst_trace = frame >= trace_from && frame <= trace_to;
          if (frame >= trace_from && frame <= trace_to && (frame - trace_from) % 5 == 0)
            PrintStatus(frame);
        }
        if (n % 24 == 0) {
          for (int p = 0; p < kMpMaxPlayers; p++) {
            seed = seed * 1103515245 + 12345;
            held[p] = ParseKeys((const char *[]){ "U", "D", "L", "R", "UL", "UR", "DL", "DR", "B" }[(seed >> 16) % 9]);
            if (together)
              held[p] = (seed >> 16) % 4 == 0 ? 0 : held[0];
          }
        }
        // Remember where everybody was, to report sudden jumps and missing sprites.
        int pw[kMpMaxPlayers], px[kMpMaxPlayers], py[kMpMaxPlayers], pm[kMpMaxPlayers], pdown[kMpMaxPlayers];
        for (int p = 0; p < kMpMaxPlayers; p++) {
          pw[p] = Inst_OfPlayer(p);
          if (pw[p] < 0)
            continue;
          SwitchToPlayer(p);
          px[p] = Mp_PlayerX(p), py[p] = Mp_PlayerY(p);
          pm[p] = main_module_index << 8 | submodule_index;
          pdown[p] = g_mp.p[p].downed;
        }
        Inst_RunFrame(held);
        // A player in two worlds at once.
        for (int p = 0; p < kMpMaxPlayers && (checks || together); p++) {
          int r0 = Inst_Current(), worlds = 0;
          for (int i = 0; i < kInstMax; i++) {
            if (!Inst_IsActive(i))
              continue;
            Inst_Switch(i);
            worlds += g_mp.p[p].joined;
          }
          Inst_Switch(r0);
          if (worlds > 1) {
            printf("[frame %d] DUPLICATE P%d in %d worlds\n", frame, p + 1, worlds);
            runaways++;
          }
        }
        // A world stuck in a screen or room transition.
        static int in_trans[kInstMax];
        for (int i = 0; i < kInstMax && (checks || together); i++) {
          int r0 = Inst_Current();
          if (!Inst_IsActive(i)) {
            in_trans[i] = 0;
            continue;
          }
          Inst_Switch(i);
          bool trans = (main_module_index == 7 || main_module_index == 9) && submodule_index != 0;
          in_trans[i] = trans ? in_trans[i] + 1 : 0;
          if (in_trans[i] == 600) {
            printf("[frame %d] STUCK TRANSITION world %d module %d/%d\n", frame, i, main_module_index, submodule_index);
            runaways++;
          }
          Inst_Switch(r0);
        }
        // A world whose camera lost its leader.
        static int lost[kInstMax];
        for (int i = 0; i < kInstMax && (checks || together); i++) {
          if (!Inst_IsActive(i)) {
            lost[i] = 0;
            continue;
          }
          int r0 = Inst_Current();
          Inst_Switch(i);
          int sx = (int16)(Mp_PlayerX(g_mp.leader) - BG2HOFS_copy2), sy = (int16)(Mp_PlayerY(g_mp.leader) - BG2VOFS_copy2);
          bool st = (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
          st = st && *Mp_PlayerRam(g_mp.leader, 0x5D) == 0 && !g_mp.p[g_mp.leader].downed;  // standing on the ground
          lost[i] = st && (sx < -16 || sx > 272 || sy < -16 || sy > 240) ? lost[i] + 1 : 0;
          if (lost[i] == 30) {
            printf("[frame %d] CAMERA LOST world %d leader P%d at (%d,%d) screen (%d,%d)\n", frame, i, g_mp.leader + 1,
                   Mp_PlayerX(g_mp.leader), Mp_PlayerY(g_mp.leader), sx, sy);
            runaways++;
          }
          Inst_Switch(r0);
        }
        // A Link outside the overworld area its world has loaded.
        for (int p = 0; p < kMpMaxPlayers && (checks || together); p++) {
          if (Inst_OfPlayer(p) < 0)
            continue;
          SwitchToPlayer(p);
          if (main_module_index != 9 || submodule_index != 0 || g_mp.p[p].downed)
            continue;
          int a = BYTE(overworld_area_index) & 0x3f, size = kOverworldMapIsSmall[a] ? 512 : 1024;
          int dx = Mp_PlayerX(p) - kOverworld_OffsetBaseX[a], dy = Mp_PlayerY(p) - kOverworld_OffsetBaseY[a];
          if (dx < -8 || dx > size + 8 || dy < -16 || dy > size + 8) {
            printf("[frame %d] OUT OF AREA P%d at (%d,%d) area %02x world %d leader P%d\n", frame, p + 1,
                   Mp_PlayerX(p), Mp_PlayerY(p), a, Inst_Current(), g_mp.leader + 1);
            runaways++;
          }
        }
        // A Link outside the dungeon room its world has loaded.
        for (int p = 0; p < kMpMaxPlayers && (checks || together); p++) {
          if (Inst_OfPlayer(p) < 0)
            continue;
          SwitchToPlayer(p);
          if (main_module_index != 7 || submodule_index != 0 || g_mp.p[p].downed)
            continue;
          // (Rooms from 0x100 on lie below the others, from y = 8192.)
          int rx = (dungeon_room_index & 0xf) * 512, ry = (dungeon_room_index >> 4 & 0x1f) * 512;
          int dx = (Mp_PlayerX(p) & 0x1fff) - rx, dy = (Mp_PlayerY(p) & 0x3fff) - ry;
          if (dx < -16 || dx > 512 + 16 || dy < -16 || dy > 512 + 16) {
            printf("[frame %d] OUT OF ROOM P%d at (%d,%d) room %x world %d leader P%d\n", frame, p + 1,
                   Mp_PlayerX(p), Mp_PlayerY(p), dungeon_room_index, Inst_Current(), g_mp.leader + 1);
            runaways++;
          }
        }
        // A Link that keeps moving although its player holds nothing ran away.
        for (int p = 0; p < kMpMaxPlayers && (checks || together); p++) {
          if (pw[p] < 0 || Inst_OfPlayer(p) != pw[p])
            continue;
          SwitchToPlayer(p);
          bool moved = Mp_PlayerX(p) != px[p] || Mp_PlayerY(p) != py[p];
          bool stable = (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
          // (Swimming Links drift on their own.)
          stable = stable && *Mp_PlayerRam(p, 0x5D) == 0;
          idle_moving[p] = held[p] == 0 && moved && stable && !g_mp.p[p].downed ? idle_moving[p] + 1 : 0;
          if (idle_moving[p] == 40) {
            printf("[frame %d] RUNAWAY P%d moving with no input at (%d,%d) world %d leader=%d state=%d\n", frame, p + 1,
                   Mp_PlayerX(p), Mp_PlayerY(p), Inst_Current(), g_mp.leader, *Mp_PlayerRam(p, 0x5D));
            runaways++;
          }
        }
        for (int p = 0; p < kMpMaxPlayers && checks; p++) {
          if (Inst_IsJoined(p) && Inst_ExitBlockedTimer(p) == 90) {
            printf("[frame %d] P%d was told everyone has to head that way\n", frame, p + 1);
            jumps++;
          }
          if (pw[p] < 0)
            continue;
          SwitchToPlayer(p);
          int d = abs(Mp_PlayerX(p) - px[p]) + abs(Mp_PlayerY(p) - py[p]);
          bool stable = pm[p] == (7 << 8) || pm[p] == (9 << 8);
          if (d > 12 && stable && !pdown[p] && main_module_index == pm[p] >> 8 && submodule_index == 0 && !g_mp.p[p].downed && *Mp_PlayerRam(p, 0x5D) == 0) {
            printf("[frame %d] JUMP P%d by %d (%d,%d)->(%d,%d) world %d->%d leader=%d downed=%d module=%d state=%d recoil=%d\n", frame, p + 1, d,
                   px[p], py[p], Mp_PlayerX(p), Mp_PlayerY(p), pw[p], Inst_Current(), g_mp.leader, g_mp.p[p].downed, main_module_index, *Mp_PlayerRam(p, 0x5D), *Mp_PlayerRam(p, 0x46));
            jumps++;
          }
          if (g_mp.p[p].downed && pdown[p] && d != 0) {
            printf("[frame %d] GHOST MOVED P%d by %d world %d->%d\n", frame, p + 1, d, pw[p], Inst_Current());
            jumps++;
          }
          if (p != g_mp.leader && !g_mp.p[p].downed && Mp_InStableGameplay() && Mp_NumJoined() > 1 &&
              g_mp_render.p[p].count == 0 && pw[p] == Inst_Current()) {
            int sx = (int16)(Mp_PlayerX(p) - BG2HOFS_copy2), sy = (int16)(Mp_PlayerY(p) - BG2VOFS_copy2);
            if (sx > 0 && sx < 240 && sy > 0 && sy < 200) {
              printf("[frame %d] INVISIBLE P%d world %d leader=%d visible=%d screen=%d,%d\n", frame, p + 1, Inst_Current(),
                     g_mp.leader, g_mp_render.p[p].visible, sx, sy);
              invisible++;
            }
          }
        }
        if (n % 600 == 0) {
          printf("[frame %d] worlds=%d hash=%08x", frame, Inst_Count(), Inst_HashState());
          for (int p = 0; p < kMpMaxPlayers; p++)
            printf(" P%d:w%d", p + 1, Inst_OfPlayer(p));
          printf("\n");
        }
      }
      memset(held, 0, sizeof(held));
      if (checks || together) {
        printf("wander: %d jumps, %d invisible, %d runaways\n", jumps, invisible, runaways);
        if (jumps || invisible || runaways)
          failures++;
      }
    } else if (!strcmp(cmd, "splitshot")) {
      int r = Inst_Current();
      uint32 *wide = calloc(width * 2 * height * 4, 1);
      for (int v = 0; v < 2; v++) {
        SwitchToPlayer(atoi(v ? arg3 : arg2) - 1);
        ZeldaDrawPpuFrame((uint8 *)(wide + v * width), width * 2 * 4, render_flags & ~kPpuRenderFlags_4x4Mode7);
      }
      Inst_Switch(r);
      Ui_Draw((uint8 *)wide, width * 2 * 4, width * 2, height, 1);
      WritePpm(arg1, width * 2, height, wide, width * 2);
      free(wide);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "status")) {
      PrintStatus(frame);
    } else if (!strcmp(cmd, "down") || !strcmp(cmd, "revive")) {
      // down <p>: player p goes down right now (0: the leader of the viewed player's world)
      // revive <p>: player p gets up again
      // (revive 0: the player the last "down 0" put down)
      static int last_down = -1;
      int p = atoi(arg1) - 1;
      if (p < 0 && !strcmp(cmd, "down")) {
        SwitchToPlayer(view);
        p = last_down = g_mp.leader;
      } else if (p < 0) {
        p = last_down;
      }
      if (p >= 0 && Inst_OfPlayer(p) >= 0) {
        SwitchToPlayer(p);
        g_mp.p[p].downed = !strcmp(cmd, "down");
      }
    } else if (!strcmp(cmd, "spoiler")) {
      // spoiler <hexseed> <file>: writes the spoiler log of a seed
      if (!Rando_WriteSpoiler(strtoul(arg1, NULL, 16), arg2)) {
        printf("FAIL: no spoiler for seed %s\n", arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "receive")) {
      // receive <hexitem>: the viewed player gets an item, as from a chest
      SwitchToPlayer(view);
      Link_ReceiveItem(strtol(arg1, NULL, 16), 0);
    } else if (!strcmp(cmd, "setleader")) {
      // setleader <p>: player p leads its world (as if it had opened a door)
      SwitchToPlayer(atoi(arg1) - 1);
      Mp_SetLeader(atoi(arg1) - 1);
    } else if (!strcmp(cmd, "expect_playing")) {
      // expect_playing <p>: player p's world is in plain gameplay (not stuck in a transition)
      SwitchToPlayer(atoi(arg1) - 1);
      if (!((main_module_index == 7 || main_module_index == 9) && submodule_index == 0)) {
        printf("FAIL: P%s's world is in module %d/%d at frame %d\n", arg1, main_module_index, submodule_index, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "check_area")) {
      // check_area: fail if a standing Link on the overworld is outside the area its world has loaded
      for (int p = 0; p < kMpMaxPlayers; p++) {
        if (Inst_OfPlayer(p) < 0)
          continue;
        SwitchToPlayer(p);
        if (main_module_index != 9 || submodule_index != 0 || g_mp.p[p].downed)
          continue;
        int a = BYTE(overworld_area_index) & 0x3f, size = kOverworldMapIsSmall[a] ? 512 : 1024;
        int dx = Mp_PlayerX(p) - kOverworld_OffsetBaseX[a], dy = Mp_PlayerY(p) - kOverworld_OffsetBaseY[a];
        if (dx < -8 || dx > size + 8 || dy < -16 || dy > size + 8) {
          printf("FAIL: P%d at (%d,%d) is outside area %02x at frame %d\n", p + 1, Mp_PlayerX(p), Mp_PlayerY(p), a, frame);
          failures++;
        }
      }
    } else if (!strcmp(cmd, "spawn")) {
      SwitchToPlayer(atoi(arg2) - 1);
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
      SwitchToPlayer(atoi(arg1) - 1);
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
    } else if (!strcmp(cmd, "story")) {
      Story_SetEnabled(atoi(arg1));
      Session_SetStory(atoi(arg1));
    } else if (!strcmp(cmd, "title")) {
      g_config.four_swords_title = atoi(arg1);
    } else if (!strcmp(cmd, "dumprender")) {
      // dumprender <p>: the extra sprites of player p (count, first entries' flags)
      int p = atoi(arg1) - 1;
      SwitchToPlayer(view);
      MpRenderPlayer *rp = &g_mp_render.p[p];
      printf("[frame %d] P%d render: visible=%d count=%d leader=P%d resident=P%d area_key=%x aux=%d", frame, p + 1, rp->visible, rp->count, g_mp.leader + 1, g_mp.resident + 1, g_mp.area_key, link_auxiliary_state);
      for (int i = 0; i < rp->count && i < 4; i++)
        printf(" [x%d y%d chr%02x fl%02x]", rp->oam[i * 2] & 0xff, rp->oam[i * 2] >> 8, rp->oam[i * 2 + 1] & 0xff, rp->oam[i * 2 + 1] >> 8);
      printf("\n");
    } else if (!strcmp(cmd, "pram")) {
      // pram <p> <hexaddr> <hexval>: change a byte of player p's own state (its Link context)
      int p = atoi(arg1) - 1;
      SwitchToPlayer(p);
      *Mp_PlayerRam(p, strtol(arg2, NULL, 16)) = strtol(arg3, NULL, 16);
    } else if (!strcmp(cmd, "expect_pram")) {
      // expect_pram <p> <hexaddr> <hexval>: a byte of player p's own state
      int p = atoi(arg1) - 1;
      SwitchToPlayer(p);
      int v = *Mp_PlayerRam(p, strtol(arg2, NULL, 16));
      if (v != strtol(arg3, NULL, 16)) {
        printf("FAIL: P%s ram %s is %02x, expected %s at frame %d\n", arg1, arg2, v, arg3, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "resetcounters")) {
      Session_ResetCounters();
    } else if (!strcmp(cmd, "expect_own_corrections")) {
      if (Session_GetStatus()->own_corrections > atoi(arg1)) {
        printf("FAIL: %d own corrections (rubber banding), at most %s expected\n", Session_GetStatus()->own_corrections, arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "inputdelay")) {
      Session_SetInputDelay(atoi(arg1));
    } else if (!strcmp(cmd, "tickwander")) {
      // tickwander <n> <seed>: like tick, but local players walk around randomly.
      // Reports Links that jump (move further than they can walk).
      uint32 seed = atoi(arg2) * 2654435761u + 1;
      int jumps = 0, remote_jumps = 0;
      for (int i = 0, n = atoi(arg1); i < n; i++) {
        if (i % 24 == 0) {
          for (int p = 0; p < kMpMaxPlayers; p++) {
            seed = seed * 1103515245 + 12345;
            held[p] = ParseKeys((const char *[]){ "U", "D", "L", "R", "UL", "UR", "DL", "DR", "B" }[(seed >> 16) % 9]);
          }
        }
        int pw[kMpMaxPlayers], px[kMpMaxPlayers], py[kMpMaxPlayers], pm[kMpMaxPlayers];
        for (int p = 0; p < kMpMaxPlayers; p++) {
          if ((pw[p] = Inst_OfPlayer(p)) < 0)
            continue;
          SwitchToPlayer(p);
          // Where it is drawn: other players' corrections are smoothed out.
          px[p] = Mp_PlayerX(p) + g_mp_draw_offset[p][0], py[p] = Mp_PlayerY(p) + g_mp_draw_offset[p][1];
          pm[p] = main_module_index << 8 | submodule_index;
        }
        uint16 in[kMpMaxPlayers];
        memcpy(in, held, sizeof(in));
        int ran = Session_Tick(in, g_wanted_zelda_features);
        frame += ran;
        for (int p = 0; p < kMpMaxPlayers && ran > 0 && ran < 4; p++) {
          if (pw[p] < 0 || Inst_OfPlayer(p) != pw[p])
            continue;
          SwitchToPlayer(p);
          int x = Mp_PlayerX(p) + g_mp_draw_offset[p][0], y = Mp_PlayerY(p) + g_mp_draw_offset[p][1];
          int d = abs(x - px[p]) + abs(y - py[p]);
          bool stable = pm[p] == (7 << 8) || pm[p] == (9 << 8);
          bool local = false;
          for (int i = 0; i < kSessionMaxLocal; i++)
            local |= Session_GetStatus()->local_slot[i] == p;
          // The leader of a world isn't smoothed (the camera follows it).
          if (d > 12 * ran && stable && main_module_index == pm[p] >> 8 && submodule_index == 0 && *Mp_PlayerRam(p, 0x5D) == 0) {
            printf("[frame %d] %s P%d by %d (%d,%d)->(%d,%d) in %d frames%s\n", frame, local ? "JUMP" : "remote jump", p + 1, d, px[p], py[p], x, y, ran,
                   p == g_mp.leader ? " (leader)" : "");
            if (local)
              jumps++;
            else
              remote_jumps++;
          }
        }
        SDL_Delay(16);
      }
      memset(held, 0, sizeof(held));
      printf("tickwander: %d jumps of local players, %d of other players, %d rollbacks, %d own corrections\n", jumps, remote_jumps,
             Session_GetStatus()->rollbacks, Session_GetStatus()->own_corrections);
      if (jumps) {
        printf("FAIL: %d jumps\n", jumps);
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
      if (Inst_NumJoined() != atoi(arg1)) {
        printf("FAIL: %d players, expected %s\n", Inst_NumJoined(), arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_resyncs")) {
      if (Session_GetStatus()->resyncs != atoi(arg1)) {
        printf("FAIL: %d resyncs, expected %s\n", Session_GetStatus()->resyncs, arg1);
        failures++;
      }
    } else if (!strcmp(cmd, "pos")) {
      // pos <p> <x> <y>: put player p there (no collision check)
      int p = atoi(arg1) - 1;
      SwitchToPlayer(p);
      WORD(*Mp_PlayerRam(p, 0x22)) = atoi(arg2);
      WORD(*Mp_PlayerRam(p, 0x20)) = atoi(arg3);
    } else if (!strcmp(cmd, "poke")) {
      g_ram[strtol(arg1, NULL, 16)] = strtol(arg2, NULL, 16);
    } else if (!strcmp(cmd, "expect_camx")) {
      // expect_camx <p> <x>: the camera of player p's world is at x
      SwitchToPlayer(atoi(arg1) - 1);
      if (BG2HOFS_copy2 != atoi(arg2)) {
        printf("FAIL: P%s's camera x is %d, expected %s at frame %d\n", arg1, BG2HOFS_copy2, arg2, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_ram")) {
      int v = g_ram[strtol(arg1, NULL, 16)];
      if (v != strtol(arg2, NULL, 16)) {
        printf("FAIL: ram %s is %02x, expected %s at frame %d\n", arg1, v, arg2, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_near")) {
      int p = atoi(arg1) - 1;
      SwitchToPlayer(p);
      if (abs(Mp_PlayerX(p) - atoi(arg2)) > 4 || abs(Mp_PlayerY(p) - atoi(arg3)) > 4) {
        printf("FAIL: P%d is at %d,%d, expected %s,%s at frame %d\n", p + 1, Mp_PlayerX(p), Mp_PlayerY(p), arg2, arg3, frame);
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
    } else if (!strcmp(cmd, "message")) {
      dialogue_message_index = strtol(arg1, NULL, 16);
      Main_ShowTextMessage();
    } else if (!strcmp(cmd, "clearsprites")) {
      for (int k = 0; k < 16; k++)
        sprite_state[k] = 0;
    } else if (!strcmp(cmd, "expect_hp")) {
      int p = atoi(arg1) - 1;
      SwitchToPlayer(p);
      if (Mp_PlayerHealth(p) != atoi(arg2)) {
        printf("FAIL: P%d has %d health, expected %s\n", p + 1, Mp_PlayerHealth(p), arg2);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_downed")) {
      int p = atoi(arg1) - 1;
      if (!Inst_IsJoined(p) || !Inst_PlayerDowned(p)) {
        printf("FAIL: P%d not downed at frame %d\n", p + 1, frame);
        failures++;
      }
    } else if (!strcmp(cmd, "expect_alive")) {
      int p = atoi(arg1) - 1;
      if (!Inst_IsJoined(p) || Inst_PlayerDowned(p)) {
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
