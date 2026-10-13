// Headless scripted test runner for co-op: zelda3 --mptest <script>
//
// One command per line; "#" starts a comment. Numbers are decimal unless
// marked hex. <p> is a player 1..4. The script fails (MPTEST FAILED) on any
// expect_* that doesn't hold, an unknown command or key, or a bad player.
//
// Game and players:
//   load <n>            load reference save chapter n (1..13; 2 = outside Link's house)
//   newgame / loadslot  make a new save file in slot 1 and load it / load slot 1
//   spoke <hexoff> <hexval>  change a byte of save slot 1 (fixing its checksum)
//   rando <off|random|hexseed>  randomizer setting for new save files
//   fullinv <0|1>       new save files start with every item
//   join <p> / leave <p>
//   hold <p> <keys>     keys held by player p (B Y s=select S=start U D L R A X l r, P=ping, "-" for none)
//   run <frames>        (with the 3D view on, held keys turn with local p's camera first: see remap3d)
//   pos <p> <x> <y>     put player p there (no collision check)
//   hp <p> <health>     set a player's health (1/8 hearts)
//   pram <p> <hexaddr> <hexval>  change a byte of player p's own Link state
//   poke <hexaddr> <hexval>  change a RAM byte (shared progress spreads to every world)
//   peek <hexaddr> [n]  print RAM bytes
//   receive <hexitem>   the viewed player gets an item, as from a chest
//   enter <hexroom>     go into a room through the game's first entrance to it
//   message <hexid>     open a text box
//   spawn <type> <p> [dx]  spawn sprite type (hex) dx (default 24) pixels right of player p
//   clearsprites        remove all enemies
//   settiles <x0> <y0> <x1> <y1> <hexattr>  (dungeon) make those tiles behave like attr (20: a pit)
//   down <p> / revive <p>  player p goes down right now / gets up again
//   setleader <p>       player p leads its world (as if it had opened a door)
//   ff <0|1>            friendly fire
//   together <0|1>      1: shared screen with group exits, 0: free roaming (default)
//   story <0|1> / title <0|1>  the Four Swords opening story / title screen text
//   features <hex>      turn game features on (2: switch items with L/R)
//   pitem               print every player's items and the HUD item box
//   hd2d <0|1> [lighting] [see through 0|1]  the 2D-HD look for screenshots
//   view <p>            later commands without a player (shot, peek, expect_module) use p's world
//   wander <frames> <seed>  every player walks around randomly and swings the sword
//   groupcam <0|1>      a shared world's camera follows the players' middle (1, default) or the leader (0)
//   cam <x> <y>         put the viewed world's camera there right away
//   camlog <n> [every]  run n frames, printing every world's camera, leader and the players' positions
//   status              print player state
// Checks:
//   expect_instances <n>  the players are spread over n worlds
//   expect_same <p> <q> / expect_apart <p> <q>  players in the same / different worlds
//   expect_module <n>   the game module of the viewed world (7 dungeon, 9 overworld, 14 menus and text)
//   expect_playing <p>  player p's world is in plain gameplay (not stuck in a transition)
//   expect_running <p> <frames>  player p's world keeps running for that many frames
//   expect_leader <p>   player p leads its world (the camera follows it)
//   expect_alive <p> / expect_downed <p>
//   expect_hp <p> <n>   player p has n health (1/8 hearts)
//   expect_carried <p> <q|0>  player p is held over player q's head (0: by nobody)
//   expect_ping <p> <none|here|help> [x y]  player p's ping marker (where it is, within 2 pixels)
//   expect_near <p> <x> <y>  player p is within 4 pixels of x,y
//   expect_camx <p> <x> / expect_camy <p> <y>  the camera of player p's world
//   expect_ram <hexaddr> <hexval> / expect_pram <p> <hexaddr> <hexval>  a RAM byte / a byte of p's own state
//   expect_ram_not <hexaddr> <hexval>  a RAM byte isn't that
//   expect_ram_counts <hexaddr> <frames> <0|1>  run that many frames: the byte changes (1) or stays (0)
//   expect_sees <a> <b> / expect_not_sees <a> <b>  player a's screen shows player b, who is in another world
//   expect_drawn_right <a> <b>  player b (from another world) is drawn on a's screen where b really is
//   expect_bg3hofs <x>  the HUD layer's horizontal scroll (after drawing)
//   expect_hd2d <min hidden> <max hidden> <dark 0|1> [min lights]  the 2D-HD pass's see-through, darkness and lights
//   check_area          no standing Link on the overworld is outside the area its world has loaded
//   expect_sprite_gone <hexN> / expect_sprite_there <hexN>  the area/room sprite with that list index is dead / alive
//   holecheck           every overworld fall hole resolves to the right entrance from any camera position
//   reload              save and load every world the way netplay sends them; nothing may change
//   randocheck <seed> <count> [v]  every seed gives a beatable placement (v: print spoilers)
//   storycheck          every rewritten story message fits the text box
// Archipelago (a pretend room; no server):
//   aproom <hexbind|off> [deathlink 0|1]  new files are Archipelago files of that room
//   apitem <hexcode>    the room sends an item (item id = base + code; ff: one the game doesn't know)
//   apdeathlink         someone else in the room died
//   srun <n>            run n frames the way the game does (session frames: Archipelago events, then Ap_Tick;
//                       first the 3D view's cameras, View buttons and camera-turned d-pad, as tick)
//   expect_apcount <n>  the file has received n of the room's items
//   expect_apsent <n>   the game has sent n checked locations
//   expect_apchecked <hexloc> <0|1>  location base+hexloc (chest number, or 100+n for special place n) was sent
//   expect_apgoal <0|1> / expect_apdeaths <n>  the goal / so many DeathLinks were sent
//   aplogic <file>      the APWorld's logic (archipelago/tools/logic_vectors.py) agrees with the randomizer's
// Archipelago with a real server (archipelago/tests/e2e.sh):
//   apconnect <server> <slot> [password]  connect from the Archipelago screen
//   apwait <connected|items n> [seconds]  run in real time until connected / n items received (default 20 s)
//   apnotices           print the lines the game would show
//   expect_apnotices <n>  print them: there must be n (a netplay client: the host's)
// Pictures (with the 3D view on, a picture of the viewed player uses the camera of the local player playing it):
//   shot <file.ppm>     render the current frame
//   splitshot <file.ppm> <p> <q>  render the worlds of players p and q side by side
//   splitshot3 <file.ppm> <p> <q> <r>  three players' worlds in a 2x2 window, the team panel in the fourth quarter
//   expect_shot <name> [max % of pixels that may differ] [channel tolerance]  compare with tests/baseline/<name>.ppm
//   benchdraw <n>       times n frames drawn (with the current 2D-HD setting)
//   drawrun <n>         run n frames, drawing each into a buffer exactly the size the renderer asks for (fails on an overrun)
//   stalewidth <p> <n>  p's world's video chip keeps n widescreen columns per side (drawing must use the setting)
//   runcheckdraw <p> <n>  run n frames; player p (not its world's leader) must never be drawn with shrunken body pieces
//   runcheckseen <a> <b> <n>  run n frames; on every one, a and b are drawn on each other's screen (same world or not)
//                       where they really are, and can have a name tag or arrow
//   runcheckobjowner <n> <hextype> <p>  run up to n frames: the first new object of that type in p's world belongs to p
// 3D view (beta). Here <p> of a camera, button or stick is a local player 1..4 (offline: local p plays player p),
// <c> a controller 0..3.
//   view3d <0|1>        the 3D view (like hd2d)
//   view3dset <returns|turn|mouselook|fade|stick|mouse|invert|fov|perspective> <value>
//   cam3d <p> <tp|fp> [yaw] [pitch] [zoom%]  local p-1's camera right away (degrees; pitch below the horizon;
//                       defaults tp 0 45 100, fp: the Link's facing, 8)
//   cam3dstep <ms>      advance the cameras (Hd3d_Update) in 16 ms steps
//   stick3d <p> <x%> <y%>  local p-1's right stick (-100..100) until changed
//   viewbutton <p> [hold ms]  a tap (or hold) of local p-1's View button
//   click <left|middle> [shift|ctrl|double]  a mouse click for player 1 (through Hd3d_OnMouseButton)
//                       (also: right, alt, touch, and down / up: only the press / the release)
//   wheel <steps> / motion <dx> <dy> / focus <0|1>  player 1's mouse wheel and mouse moves; the window's focus
//   key3d <toggle|left|right|up|down|reset> [0|1]  a 3D view hotkey (F11 Delete PageDown Home End PageUp)
//                       pressed (1), let go (0) or both
//   pad <c> <button> <0|1>  controller c's button through the bindings (A B X Y Back Start L3 R3 LB RB ...)
//   rstick <c> <x%> <y%>  controller c's right stick through the bindings
//   remap3d <0|1>       run/drawrun pass held keys through the camera remap (default 1)
//   drawtick <0|1>      tick also draws the viewed world (netplay tests with 3D)
//   widescreen <0-96>   columns per side for later pictures (g_draw_extra_left_right and the width)
//   drawuntil <hexmodule> <hexsub> <max>  run and draw frame by frame until the viewed world is there (fails after max)
//   mark / rewind       snapshot / restore every world (Inst_SnapshotSave/Load)
//   markpos <p>         remember player p's position
//   hashmark <name>     remember Inst_HashState
//   dump3d <file.ppm>   (debug) the viewed world's classes and heights
//   expect_3d <0|1> [reason]  draw view 0: drawn in 3D (1) / fell back (0, optionally for that reason)
//   expect_exact3d <max %> <tolerance>  draw in 2D, then in 3D with the oblique test camera; compare
//   expect_drawpure <n>  after one settling draw, n 3D draws change no RAM, g_mp or any world's PPU state
//   drawguard <n>       run n frames, drawing view 0 into the middle of a sentinel buffer (pitch 3 x width)
//   expect_hashmark <name>  Inst_HashState is what hashmark remembered
//   expect_remap <p> <keys> <keys>  local p-1's remap of the first keys gives the second (gating included; stateful)
//   expect_moved <p> <north|south|east|west>  at least 8 px that way since markpos, under 4 px across
//   expect_fp <p> <0|1>  local p-1's camera is in first person
//   expect_cam3d <p> <yaw> [pitch] [tolerance]  local p-1's camera (degrees, default tolerance 0.5)
//   expect_zoom3d <p> <zoom%> [tolerance%]  local p-1's third-person zoom (default tolerance 1%)
//   expect_panes <n>    Hd3d_ComputeViews gives n panes
//   expect_input <p> <keys>  Bindings_GetInput of local p-1 (letters as hold)
//   expect_view <p> <0|1>  local p-1's View button is down
//   expect_luma <x0> <y0> <x1> <y1> <min> <max>  draw view 0: average luma (0-255) of pixels x0..x1-1, y0..y1-1
//   expect_mouse_captured <0|1>  Hd3d_WantsRelativeMouse
//   expect_3dstats <min quads> <min cards>  draw view 0: at least that many quads and cards were drawn
// Session (netplay) commands. "hold" then refers to local players.
//   host <port> | connect <addr> | disconnect
//   tick <n>            run n session ticks in real time (16ms apart), as the game's loop: the overlay, then the
//                       3D view's cameras, View buttons and camera-turned d-pad, then the session (srun too)
//   waitplay            tick until connected and playing (max 15s)
//   waitplayers <n>     tick until n players are in the game (max 15s)
//   waitseat <p> <open|playing|reconnecting>  tick until seat p is so (max 25s)
//   waitoffline / waitclientstate <n>  tick until offline / the client state is n (4 = reconnecting) (max 25s)
//   name <name>         this computer's player name online
//   netblackout <0|1>   pull the network cable out / put it back (nothing sent or received)
//   kick <p>            the host removes the remote player in seat p
//   expect_seat <p> <open|playing|reconnecting> [name] / expect_myseat <p> / expect_client_state <n>
//   expect_message <word>  the latest netplay message says it
//   roomcode <a.b.c.d:port>  the room code decodes back (also loosely typed); connectcode <a.b.c.d:port> joins by it
//   expect_badcode <code>  a mistyped room code is refused
//   keepcopy <file 1-3> <path>  keep a copy of the game we play in (a client's) in that file of the save at path
//   expect_save_file <path> <file> <empty|valid|copy>  that file is empty / in use and intact / this game as it is now
//   expect_saves_blocked <0|1>  our own save file is protected (we play someone else's world)
//   expect_fullinv <0|1>  new save files start with every item (a client: as the host's frames say)
//   removefile <path>
//   bindline <key> <value>  a controls line of the settings file (value without spaces)
//   expect_keys <p> <button 0-13> <text>  the keys of a button as the Controls screen shows them (12 Ping, 13 View)
//   tickwander <n> <seed>  tick n times with local players walking around randomly
//   inputdelay <frames> online input delay
//   netsim <delay ms> <jitter ms> <loss %>  simulate a bad link (before host/connect), like ZELDA3_NETSIM
//   atframe <f> spawn <hextype> <p> [dx] / atframe <f> clearsprites  done inside the simulation at session frame f (the same on every machine)
//   dumpevery <n>       print the confirmed state (hash, sprites, players) every n frames: tests/cmpdump.py compares two machines
//   trackkills <0|1>    count the viewed world's kills, and sprites brought back by rollbacks (1 starts, 0 prints)
//   netstats            rollback / misprediction counters
//   localjoin <i> / localleave <i>
//   resetcounters       zero the netplay statistics (resyncs, rollbacks, own corrections)
//   expect_players <n> / expect_resyncs <n> / expect_own_corrections <n>
//   expect_rollbacks <n>  at most n rollbacks since resetcounters
//   expect_resurrections <n>  at most n sprites of the viewed world came back from the dead in a rollback (trackkills)
//   corrupt             flip a byte of RAM (to test desync recovery)
//   lan [n]             search for LAN games (and expect n)
//   menu                toggle the overlay menu (navigate with hold + tick)
//   key <up|down|left|right|enter|back> [times]  a button press in the menu
//   type <text>         typing on the keyboard (in a menu's text field)
//   expect_apsettings <server> <slot> <auto|on|off>  the Archipelago screen's settings ("-": empty)
// Debug only (not for tests): trace <0|1>, chestdump, dumprender <p>, dumpworldmap <file> <dark>,
//   findtile <p> <hexattr>, findroomtag <hextag>, spoiler <hexseed> <file>, upnp (asks the router for a port mapping),
//   stun <server:port> (asks a STUN server for our public address).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
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
#include "archipelago.h"
#include "ap_client.h"
#include "hd2d.h"
#include "hd3d.h"
#include "tile_detect.h"
#include "story.h"
#include "config.h"
#include "select_file.h"
#include "messaging.h"
#include "misc.h"
#include "net.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "sprite.h"
#include "overworld.h"
#include "dungeon.h"
#include "assets.h"
#include "session.h"
#include "bindings.h"
#include "ui.h"
#include "upnp.h"

static int g_failures;

// --- test instrumentation ---
// atframe <f> spawn <hextype> <p> [dx] / atframe <f> clearsprites: done inside
// the simulation at session frame f, on every machine alike.
typedef struct Scheduled { uint32 frame; uint8 type, player; int dx; bool clear; } Scheduled;
static Scheduled g_sched[64];
static int g_nsched;
static void SpawnNear(uint8 type, int sp, int dx);
static void FrameHook(uint32 f) {
  int r = Inst_Current();
  for (int i = 0; i < g_nsched; i++) {
    if (g_sched[i].frame != f)
      continue;
    if (g_sched[i].clear) {
      for (int w = 0; w < kInstMax; w++) {
        if (!Inst_IsActive(w)) continue;
        Inst_Switch(w);
        for (int k = 0; k < 16; k++)
          sprite_state[k] = 0;
      }
    } else if (Inst_OfPlayer(g_sched[i].player) >= 0) {
      Inst_Switch(Inst_OfPlayer(g_sched[i].player));
      SpawnNear(g_sched[i].type, g_sched[i].player, g_sched[i].dx);
    }
  }
  Inst_Switch(r);
}
// trackkills: per display tick, sprites of the viewed world whose state went
// 0 -> nonzero (came back) or nonzero -> 0 (died) since the last tick.
static bool g_track;
static uint8 g_track_state[16], g_track_type[16];
static uint16 g_track_x[16], g_track_y[16];
static int g_track_resurrections, g_track_kills, g_track_rb_resurrections, g_track_rb_unkills, g_track_last_rb;
static void TrackSprites(int frame, int view) {
  int r = Inst_Current(), w = Inst_ViewOfPlayer(view);
  if (!g_track || w < 0)
    return;
  const SessionStatus *st = Session_GetStatus();
  bool rolled = st->rollbacks != g_track_last_rb;
  g_track_last_rb = st->rollbacks;
  Inst_Switch(w);
  for (int k = 0; k < 16; k++) {
    if (g_track_state[k] && !sprite_state[k])
      g_track_kills++;
    // Back from the dead (state 0 -> alive, same type, where it died): only a
    // rollback does that; the game's own reloads put sprites at their spawn
    // points with state 8/9 after a screen change.
    if (!g_track_state[k] && sprite_state[k] && sprite_type[k] == g_track_type[k]) {
      bool same_spot = abs((int16)(Sprite_GetX(k) - g_track_x[k])) < 24 && abs((int16)(Sprite_GetY(k) - g_track_y[k])) < 24;
      g_track_resurrections++;
      if (rolled && same_spot)
        g_track_rb_resurrections++;
      printf("[frame %d] %s sprite %d type %02x state %d hp %d at %d,%d (was %d,%d)%s\n", frame, rolled && same_spot ? "RESURRECTED" : "reloaded", k, sprite_type[k],
             sprite_state[k], sprite_health[k], Sprite_GetX(k), Sprite_GetY(k), g_track_x[k], g_track_y[k], rolled ? " [rollback this tick]" : "");
    }
    // Dying (state 1..8, the death animation) and back on its feet (9+): a kill undone.
    if (g_track_state[k] >= 1 && g_track_state[k] <= 8 && sprite_state[k] >= 9 && sprite_type[k] == g_track_type[k] && rolled) {
      g_track_rb_unkills++;
      printf("[frame %d] UNKILLED sprite %d type %02x state %d->%d hp %d at %d,%d [rollback this tick]\n", frame, k, sprite_type[k], g_track_state[k], sprite_state[k], sprite_health[k], Sprite_GetX(k), Sprite_GetY(k));
    }
    g_track_state[k] = sprite_state[k];
    g_track_type[k] = sprite_type[k];
    g_track_x[k] = Sprite_GetX(k), g_track_y[k] = Sprite_GetY(k);
  }
  Inst_Switch(r);
}

// A player argument "1".."4" as an index; anything else fails the test (and
// counts as player 1 so the script can go on without touching memory it
// shouldn't).
static int PlayerArg(const char *s) {
  int p = atoi(s) - 1;
  if (p >= 0 && p < kMpMaxPlayers)
    return p;
  printf("FAIL: bad player '%s'\n", s);
  g_failures++;
  return 0;
}

static uint16 ParseKeys(const char *s) {
  static const char kKeys[] = "BYsSUDLRAXlrP";  // (P: the co-op Ping button)
  uint16 v = 0;
  for (; *s; s++) {
    const char *t = strchr(kKeys, *s);
    if (t) {
      v |= 1 << (t - kKeys);
    } else if (*s != '-') {
      printf("FAIL: unknown key '%c' in '%s'\n", *s, s);
      g_failures++;
    }
  }
  return v;
}

static bool ReadPpm(const char *file, int width, int height, uint32 *pixels) {
  FILE *f = fopen(file, "rb");
  if (!f)
    return false;
  int w = 0, h = 0, maxv = 0;
  bool ok = fscanf(f, "P6 %d %d %d", &w, &h, &maxv) == 3 && w == width && h == height && fgetc(f) != EOF;
  for (int i = 0; ok && i < width * height; i++) {
    uint8 rgb[3];
    ok = fread(rgb, 1, 3, f) == 3;
    pixels[i] = rgb[0] << 16 | rgb[1] << 8 | rgb[2];
  }
  fclose(f);
  return ok;
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

static int SeatStateArg(const char *s) {
  if (!strcmp(s, "open")) return kSeat_Open;
  if (!strcmp(s, "playing")) return kSeat_Playing;
  if (!strcmp(s, "reconnecting")) return kSeat_Reconnecting;
  printf("FAIL: unknown seat state '%s'\n", s);
  g_failures++;
  return -1;
}

static void SwitchToPlayer(int p) {
  int i = Inst_OfPlayer(p);
  if (i >= 0)
    Inst_Switch(i);
}

// What is wrong with how player b shows on player a's screen (from a's own
// world or another one): NULL if b is drawn where it really is and a name
// tag or arrow can point at it.
static const char *ShownProblem(int a, int b) {
  uint16 bx, by;
  if (Inst_PlayerPos(b, &bx, &by) < 0 || Inst_OfPlayer(a) < 0)
    return "isn't playing";
  SwitchToPlayer(a);
  const MpRenderPlayer *rp = NULL;
  int dx = 0, dy = 0;
  if (Inst_OfPlayer(a) == Inst_OfPlayer(b)) {
    rp = b == g_mp_render.self_player ? &g_mp_render.self : &g_mp_render.p[b];
    if (!rp->visible)
      rp = NULL;
  } else {
    InstRemoteLink rl[kMpMaxPlayers * 2];
    int nr = Inst_LinksFromOtherWorlds(rl, countof(rl));
    for (int k = 0; k < nr; k++)
      if (rl[k].player == b)
        rp = rl[k].rp, dx = rl[k].dx, dy = rl[k].dy;
  }
  // (The first of its sprites that lands on the screen.)
  int i = 0, x = 0, y = 0;
  for (; rp && i < rp->count; i++) {
    x = (rp->oam[i * 2] & 0xff) | (rp->high[i] & 1) << 8, y = rp->oam[i * 2] >> 8;
    x = (x >= 256 ? x - 512 : x) + dx;
    y = (y >= 240 ? y - 256 : y) + dy;
    if (x > -16 && x < 256 && y > -16 && y < 224)
      break;
  }
  if (!rp || i == rp->count)
    return "is not drawn";
  if (abs(x - (int16)(bx - BG2HOFS_copy2)) > 24 || abs(y - (int16)(by - BG2VOFS_copy2)) > 32)
    return "is drawn in the wrong place";
  InstPlace pa, pb;
  if (!Inst_PlayerPlace(a, &pa) || !Inst_PlayerPlace(b, &pb) || pa.space != pb.space)
    return "has no name tag or arrow";
  return NULL;
}

static void SpawnNear(uint8 type, int sp, int dx) {
  for (int j = 15; j >= 0; j--) {
    if (sprite_state[j] == 0) {
      sprite_type[j] = type;
      sprite_state[j] = 9;
      SpritePrep_LoadProperties(j);
      Sprite_SetX(j, Mp_PlayerX(sp) + dx);
      Sprite_SetY(j, Mp_PlayerY(sp));
      sprite_floor[j] = *Mp_PlayerRam(sp, 0xEE);  // that player's floor (link_is_on_lower_level)
      if (player_is_indoors) sprite_N[j] = 0xff; else sprite_N_word[j] = 0xffff;
      printf("spawned sprite %d type %x\n", j, sprite_type[j]);
      break;
    }
  }
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
  // What each pane on this machine shows: whose eyes, which world, its camera.
  for (int i = 0; i < kSessionMaxLocal; i++) {
    if (st->local_slot[i] < 0)
      continue;
    int w = Ui_WatchedPlayer(i), inst = Inst_ViewOfPlayer(w);
    if (inst >= 0)
      Inst_Switch(inst);
    printf("  pane %d: P%d watches P%d in world %d cam=%d,%d module=%d/%d leader=P%d\n", i, st->local_slot[i] + 1, w + 1, inst,
           BG2HOFS_copy2, BG2VOFS_copy2, main_module_index, submodule_index, g_mp.leader + 1);
  }
  Inst_Switch(r);
  for (int k = 0; k < 16; k++)
    if (sprite_state[k])
      printf("  sprite %d type=%x state=%d hp=%d x=%d y=%d z=%d ai=%d floor=%d N=%x\n", k, sprite_type[k], sprite_state[k], sprite_health[k],
             Sprite_GetX(k), Sprite_GetY(k), sprite_z[k], sprite_ai_state[k], sprite_floor[k], player_is_indoors ? sprite_N[k] : sprite_N_word[k]);
}

// --- 3D view (beta) ---
static bool g_remap3d = true;  // run and drawrun turn held keys with the camera (remap3d)
static bool g_drawtick;        // tick draws too, as the game's loop (drawtick)
static InstSnapshot *g_mark;   // mark / rewind
static uint16 g_markpos[kMpMaxPlayers][2];
static bool g_markpos_set[kMpMaxPlayers];
typedef struct HashMark { char name[32]; uint32 hash; } HashMark;
static HashMark g_hashmarks[16];
static int g_nhashmarks;

// The local player playing player p on this computer: its camera draws p's
// view. (Offline tests without one: local p, the convention of run.)
static int LocalOfPlayer(int p) {
  const SessionStatus *st = Session_GetStatus();
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (st->local_slot[i] == p)
      return i;
  return p >= 0 && p < kSessionMaxLocal ? p : 0;
}

// The player local i plays (offline tests without one: player i).
static int PlayerOfLocal(int i) {
  int p = Session_GetStatus()->local_slot[i];
  return p >= 0 ? p : i;
}

// The next pictures show player p's view as one pane, with the camera of
// the local player playing p (as the game draws a single view).
static void UsePane(int p) {
  int8 local = (int8)LocalOfPlayer(p);
  Hd3d_SetPanes(&local, 1);
  Hd3d_BeginPanes(false);
}

// What the game gets for the held keys in run and drawrun: turned with each
// local player's camera (local i plays player i, as offline), unless remap3d 0.
static void HeldInputs(const uint16 *held, uint16 *in) {
  for (int p = 0; p < kMpMaxPlayers; p++)
    in[p] = g_remap3d ? Hd3d_RemapLocal(p, p, held[p]) : held[p];
}

// Draws what player p sees as a single pane into buf (room for the 4x world
// map), without the overlay. Returns the render scale (pitch: width * scale).
static int DrawView(int p, uint32 *buf, int width, uint32 render_flags) {
  int home = Inst_Current(), w = Inst_ViewOfPlayer(p);
  if (w >= 0)
    Inst_Switch(w);
  int scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
  g_mp_local_mask = 1 << p;
  UsePane(p);
  Hd2d_DrawFrame((uint8 *)buf, (size_t)width * scale * 4, render_flags, 0);
  Inst_Switch(home);
  return scale;
}

// tick's drawing (drawtick 1), as the game's loop draws: a pane per world
// seen and per local player in first person; split screen in two phases.
static void DrawLikeGame(uint32 *buf, int width, int height, uint32 render_flags) {
  int views[kInstMax];
  int8 pane_local[kInstMax];
  int n = Hd3d_ComputeViews(views, pane_local, kInstMax);
  if (n < 1)
    return;
  n = n > 4 ? 4 : n;
  Hd3d_SetPanes(pane_local, n);
  const SessionStatus *st = Session_GetStatus();
  g_mp_local_mask = 0;
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (st->local_slot[i] >= 0)
      g_mp_local_mask |= 1 << st->local_slot[i];
  UiView uv[4];
  for (int v = 0; v < n; v++)
    uv[v] = (UiView){ n > 1 ? (v & 1) * width : 0, n > 1 ? (v >> 1) * height : 0, width, height,
                      pane_local[v] >= 0 ? Ui_WatchedPlayer(pane_local[v]) : -1 };
  Ui_SetViews(uv, n);
  int home = Inst_Current();
  if (n == 1) {
    Inst_Switch(views[0]);
    int scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
    Hd3d_BeginPanes(false);
    Hd2d_DrawFrame((uint8 *)buf, (size_t)width * scale * 4, render_flags, 0);
    Ui_Draw((uint8 *)buf, width * scale * 4, width * scale, height * scale, scale);
  } else {
    Hd3d_BeginPanes(true);
    for (int v = 0; v < n; v++) {
      Inst_Switch(views[v]);
      uint32 *dst = buf + (v & 1) * width + (v >> 1) * height * width * 2;
      Hd2d_DrawFrame((uint8 *)dst, (size_t)width * 2 * 4, render_flags & ~kPpuRenderFlags_4x4Mode7, v);
    }
    Hd3d_FinishPanes();
    Ui_Draw((uint8 *)buf, width * 2 * 4, width * 2, n > 2 ? height * 2 : height, 1);
  }
  Inst_Switch(home);
}

static void HashBytes(void *ctx, void *data, size_t size) {
  uint32 *h = (uint32 *)ctx;
  const uint8 *d = (const uint8 *)data;
  for (size_t i = 0; i < size; i++)
    *h = (*h ^ d[i]) * 16777619u;
}

// What drawing may never change: every world's RAM and g_mp (the state
// hash), its video chip as a save state holds it, its DMA channels and the
// video registers the game set.
static uint32 DrawPureHash(void) {
  uint32 h = Inst_HashState();
  int home = Inst_Current();
  for (int i = 0; i < kInstMax; i++) {
    if (!Inst_IsActive(i))
      continue;
    Inst_Switch(i);
    Ppu *ppu = g_zenv.ppu;
    HashBytes(&h, &i, sizeof(i));
    dma_saveload(g_zenv.dma, &HashBytes, &h);
    ppu_saveload(ppu, &HashBytes, &h);
    uint8 regs[] = {
      ppu->screenEnabled[0], ppu->screenEnabled[1], ppu->screenWindowed[0], ppu->screenWindowed[1],
      ppu->mosaicEnabled, ppu->mosaicSize, ppu->objSize, ppu->window1left, ppu->window1right, ppu->window2left,
      ppu->window2right, ppu->clipMode, ppu->preventMathMode, ppu->addSubscreen, ppu->subtractColor,
      ppu->halfColor, ppu->mathEnabled, ppu->fixedColorR, ppu->fixedColorG, ppu->fixedColorB, ppu->forcedBlank,
      ppu->brightness, ppu->mode, ppu->scrollPrev, ppu->scrollPrev2, ppu->m7prev, ppu->numExtraSpriteSets,
    };
    HashBytes(&h, regs, sizeof(regs));
    uint32 words[] = { ppu->objTileAdr1, ppu->objTileAdr2, ppu->windowsel };
    HashBytes(&h, words, sizeof(words));
    for (int k = 0; k < 4; k++) {
      const BgLayer *bg = &ppu->bgLayer[k];
      uint16 b[] = { bg->hScroll, bg->vScroll, bg->tilemapWider, bg->tilemapHigher, bg->tilemapAdr, bg->tileAdr };
      HashBytes(&h, b, sizeof(b));
    }
    HashBytes(&h, ppu->m7matrix, sizeof(ppu->m7matrix));
    HashBytes(&h, ppu->oam, sizeof(ppu->oam));
    HashBytes(&h, ppu->extraPalettes, sizeof(ppu->extraPalettes));
    HashBytes(&h, ppu->extraSprites, sizeof(ppu->extraSprites));
  }
  Inst_Switch(home);
  return h;
}

// Degrees into [-180, 180).
static float Wrap180(float a) {
  if (!(a > -1e6f && a < 1e6f))
    return 0.0f;
  a -= 360.0f * floorf((a + 180.0f) * (1.0f / 360.0f));
  return a >= 180.0f ? a - 360.0f : a;
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
  // (A 3D setting saved from the menu must not change the tests.)
  Hd3d_ResetForTests();
  g_remap3d = true, g_drawtick = false, g_nhashmarks = 0;
  memset(g_markpos_set, 0, sizeof(g_markpos_set));
  // (Room for a picture at 4x, or 2x2 panes.)
  size_t pixels_size = (size_t)width * height * 16;
  uint32 *pixels = calloc(pixels_size, 4);
  char line[256];
  int frame = 0;
  g_failures = 0;
  while (fgets(line, sizeof(line), f)) {
    // (arg2 is long enough for a 14-button controls line.)
    char cmd[32] = { 0 }, arg1[200] = { 0 }, arg2[200] = { 0 }, arg3[64] = { 0 }, arg4[64] = { 0 }, arg5[64] = { 0 };
    char arg6[64] = { 0 };
    if (sscanf(line, "%31s %199s %199s %63s %63s %63s %63s", cmd, arg1, arg2, arg3, arg4, arg5, arg6) < 1 ||
        cmd[0] == '#')
      continue;
    // Commands about the game look at the world the viewed player sees (its
    // menu, if it has one open; commands about a player use where its Link is).
    if (Inst_ViewOfPlayer(view) >= 0)
      Inst_Switch(Inst_ViewOfPlayer(view));
    if (!strcmp(cmd, "load")) {
      int n = atoi(arg1);
      if (n < 1 || n > 13) {
        printf("FAIL: no chapter %s at frame %d\n", arg1, frame);
        g_failures++;
      } else if (!SaveLoadSlot(kSaveLoad_Load, 256 + n - 1)) {
        printf("FAIL: chapter %s didn't load (saves/ref missing?) at frame %d\n", arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "join")) {
      Inst_Join(PlayerArg(arg1));
    } else if (!strcmp(cmd, "leave")) {
      Inst_Leave(PlayerArg(arg1));
    } else if (!strcmp(cmd, "hold")) {
      held[PlayerArg(arg1)] = ParseKeys(arg2);
    } else if (!strcmp(cmd, "ff")) {
      Inst_SetFriendlyFire(atoi(arg1));
    } else if (!strcmp(cmd, "together")) {
      Inst_SetIndependent(!atoi(arg1));
    } else if (!strcmp(cmd, "view")) {
      view = PlayerArg(arg1);
    } else if (!strcmp(cmd, "expect_instances")) {
      if (Inst_Count() != atoi(arg1)) {
        printf("FAIL: %d worlds, expected %s at frame %d\n", Inst_Count(), arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_same") || !strcmp(cmd, "expect_apart")) {
      bool same = Inst_OfPlayer(PlayerArg(arg1)) == Inst_OfPlayer(PlayerArg(arg2));
      if (same != !strcmp(cmd, "expect_same")) {
        printf("FAIL: P%s and P%s are %s at frame %d\n", arg1, arg2, same ? "together" : "apart", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_drawn_right")) {
      // expect_drawn_right <a> <b>: wherever player b is drawn on player a's screen (from
      // another world), it is drawn where b really is
      int a = PlayerArg(arg1), b = PlayerArg(arg2);
      uint16 bx, by;
      Inst_PlayerPos(b, &bx, &by);
      SwitchToPlayer(a);
      uint16 cx = BG2HOFS_copy2, cy = BG2VOFS_copy2;
      InstRemoteLink rl[kMpMaxPlayers * 2 + 1];
      int nr = Inst_LinksFromOtherWorlds(rl, countof(rl) - 1);
      if (Inst_OfPlayer(a) == Inst_OfPlayer(b) && b != g_mp.leader && g_mp_render.p[b].visible && g_mp_render.p[b].count) {
        // (in the same world: the sprites drawn for it this frame)
        rl[nr].player = b, rl[nr].rp = &g_mp_render.p[b], rl[nr].dx = rl[nr].dy = 0;
        nr++;
      }
      for (int k = 0; k < nr; k++) {
        if (rl[k].player != b)
          continue;
        int x = (rl[k].rp->oam[0] & 0xff) | (rl[k].rp->high[0] & 1) << 8, y = rl[k].rp->oam[0] >> 8;
        x = (x >= 256 ? x - 512 : x) + rl[k].dx;
        y = (y >= 240 ? y - 256 : y) + rl[k].dy;
        int ex = (int16)(bx - cx), ey = (int16)(by - cy);
        if (abs(x - ex) > 24 || abs(y - ey) > 32) {
          printf("FAIL: P%s is drawn at %d,%d on P%s's screen but is at %d,%d at frame %d\n", arg2, x, y, arg1, ex, ey, frame);
          g_failures++;
        }
      }
    } else if (!strcmp(cmd, "expect_sees") || !strcmp(cmd, "expect_not_sees")) {
      // expect_sees <a> <b>: player a's screen shows player b, who is in another world.
      int a = PlayerArg(arg1), b = PlayerArg(arg2);
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
        g_failures++;
      }
    } else if (!strcmp(cmd, "reload")) {
      // reload: save and load every world the way netplay sends them; nothing may change.
      ByteArray arr = { 0 };
      uint32 before = Inst_HashState();
      Inst_SaveState(&arr);
      if (!Inst_LoadState(arr.data, arr.size) || Inst_HashState() != before) {
        printf("FAIL: the state changed when reloaded at frame %d\n", frame);
        g_failures++;
      }
      ByteArray_Destroy(&arr);
    } else if (!strcmp(cmd, "expect_bg3hofs")) {
      // expect_bg3hofs <x>: the HUD layer's horizontal scroll (after drawing)
      if (g_zenv.ppu->bgLayer[2].hScroll != atoi(arg1)) {
        printf("FAIL: BG3 scroll is %d, expected %s at frame %d\n", g_zenv.ppu->bgLayer[2].hScroll, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_hd2d")) {
      // expect_hd2d <min hidden> <max hidden> <dark 0|1> [min lights]: draws the frame
      // with the 2D-HD look and checks how many Link pixels hidden behind
      // scenery it showed, and whether it lit a dark room itself.
      bool on = g_hd2d.enabled;
      g_hd2d.enabled = true;
      UsePane(view);
      Hd2d_DrawFrame((uint8 *)pixels, width * 4, render_flags, 0);
      g_hd2d.enabled = on;
      int n = Hd2d_HiddenPixels();
      // [min lights]: how many light sources (Lamps, torches, fire) it found
      if (n < atoi(arg1) || n > atoi(arg2) || Hd2d_LitDarkRoom() != atoi(arg3) || Hd2d_NumLights() < atoi(arg4)) {
        printf("FAIL: 2D-HD showed %d hidden pixels (expected %s-%s), dark room %d, %d lights at frame %d\n",
               n, arg1, arg2, Hd2d_LitDarkRoom(), Hd2d_NumLights(), frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "hd2d")) {
      // hd2d <0|1> [lighting] [see through 0|1]: the 2D-HD look for screenshots
      g_hd2d.enabled = atoi(arg1);
      if (arg2[0])
        g_hd2d.lighting = atoi(arg2);
      if (arg3[0])
        g_hd2d.see_through = atoi(arg3);
    } else if (!strcmp(cmd, "run")) {
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        uint16 in[kMpMaxPlayers];
        HeldInputs(held, in);
        Inst_RunFrame(in);
      }
    } else if (!strcmp(cmd, "drawrun")) {
      // drawrun <n>: runs n frames and draws each the way the game does on
      // one screen: into a buffer exactly as big as the PPU asks for (4x for
      // the HD world map), with a guard after it. Fails if drawing wrote past
      // the buffer (the OpenGL output's buffer is exactly that big).
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        uint16 in[kMpMaxPlayers];
        HeldInputs(held, in);
        Inst_RunFrame(in);
        int home = Inst_Current();
        Inst_Switch(Inst_ViewOfPlayer(view));
        int scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
        size_t px = (size_t)width * scale * height * scale, guard = 4096;
        uint32 *buf = malloc((px + guard) * 4);
        for (size_t i = px; i < px + guard; i++)
          buf[i] = 0xdeadbeef;
        g_mp_local_mask = 1 << view;
        Ui_SetViews(&(UiView){ 0, 0, width, height, view }, 1);
        UsePane(view);
        Hd2d_DrawFrame((uint8 *)buf, width * scale * 4, render_flags, 0);
        Ui_Draw((uint8 *)buf, width * scale * 4, width * scale, height * scale, scale);
        size_t bad = 0;
        for (size_t i = px; i < px + guard; i++)
          bad += buf[i] != 0xdeadbeef;
        free(buf);
        Inst_Switch(home);
        if (bad) {
          printf("FAIL: drawing frame %d (module %d/%d, scale %d) wrote past the frame buffer\n", frame,
                 main_module_index, submodule_index, scale);
          g_failures++;
          break;
        }
      }
    } else if (!strcmp(cmd, "stalewidth")) {
      // stalewidth <p> <n>: player p's world's video chip still has n widescreen
      // columns per side (as from before the setting changed, or a snapshot)
      int home = Inst_Current();
      Inst_Switch(Inst_ViewOfPlayer(PlayerArg(arg1)));
      g_zenv.ppu->extraLeftRight = atoi(arg2);
      Inst_Switch(home);
    } else if (!strcmp(cmd, "shot")) {
      g_mp_local_mask = 1 << view;
      Ui_SetViews(&(UiView){ 0, 0, width, height, view }, 1);
      UsePane(view);
      Hd2d_DrawFrame((uint8 *)pixels, width * 4, render_flags, 0);
      Ui_Draw((uint8 *)pixels, width * 4, width, height, 1);
      WritePpm(arg1, width, height, pixels, width);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "benchdraw")) {
      // benchdraw <n>: times n frames drawn (with the current 2D-HD setting)
      UsePane(view);
      uint64 t0 = SDL_GetPerformanceCounter();
      for (int n = atoi(arg1); n > 0; n--)
        Hd2d_DrawFrame((uint8 *)pixels, width * 4, render_flags, 0);
      double ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency() / atoi(arg1);
      printf("benchdraw: %.2f ms per frame\n", ms);
    } else if (!strcmp(cmd, "expect_shot")) {
      // expect_shot <name> [max %% of pixels that may differ, default 0.5]
      //   [how far a color channel may be off and still count as the same, default 0]:
      // draws the screen like "shot" and compares it with
      // tests/baseline/<name>.ppm. Baselines come from your own ROM, so they
      // are never committed: the first run (or tests/update_baselines.sh)
      // makes them. On a mismatch tests/out/<name>.ppm and <name>_diff.ppm
      // (changed pixels in red) show what changed.
      g_mp_local_mask = 1 << view;
      Ui_SetViews(&(UiView){ 0, 0, width, height, view }, 1);
      UsePane(view);
      Hd2d_DrawFrame((uint8 *)pixels, width * 4, render_flags, 0);
      Ui_Draw((uint8 *)pixels, width * 4, width, height, 1);
      char base[256], out[256];
      snprintf(base, sizeof(base), "tests/baseline/%s.ppm", arg1);
      snprintf(out, sizeof(out), "tests/out/%s.ppm", arg1);
      uint32 *ref = malloc(width * height * 4);
      if (getenv("ZELDA3_UPDATE_BASELINES") || !ReadPpm(base, width, height, ref)) {
        // (Not from a script that already failed: the picture may be wrong.)
        if (g_failures && !getenv("ZELDA3_UPDATE_BASELINES")) {
          printf("FAIL: no baseline %s, and none written after a failure\n", base);
          g_failures++;
        } else {
          WritePpm(base, width, height, pixels, width);
          printf("expect_shot %s: baseline written\n", arg1);
        }
      } else {
        int diff = 0, tol = arg3[0] ? atoi(arg3) : 0;
        for (int i = 0; i < width * height; i++) {
          uint32 a = pixels[i], b = ref[i];
          diff += abs((int)(a >> 16 & 0xff) - (int)(b >> 16 & 0xff)) > tol ||
                  abs((int)(a >> 8 & 0xff) - (int)(b >> 8 & 0xff)) > tol || abs((int)(a & 0xff) - (int)(b & 0xff)) > tol;
        }
        double pct = 100.0 * diff / (width * height), limit = arg2[0] ? atof(arg2) : 0.5;
        if (pct > limit) {
          printf("FAIL: screenshot %s differs from its baseline in %.2f%% of the pixels (limit %.2f%%) at frame %d; see tests/out/%s_diff.ppm\n",
                 arg1, pct, limit, frame, arg1);
          g_failures++;
          WritePpm(out, width, height, pixels, width);
          for (int i = 0; i < width * height; i++) {
            uint32 c = ref[i];
            int l = ((c >> 16 & 0xff) + (c >> 8 & 0xff) + (c & 0xff)) / 6;
            uint32 a = pixels[i];
            bool changed = abs((int)(a >> 16 & 0xff) - (int)(c >> 16 & 0xff)) > tol ||
                           abs((int)(a >> 8 & 0xff) - (int)(c >> 8 & 0xff)) > tol || abs((int)(a & 0xff) - (int)(c & 0xff)) > tol;
            ref[i] = changed ? 0xff0000 : l << 16 | l << 8 | l;
          }
          snprintf(out, sizeof(out), "tests/out/%s_diff.ppm", arg1);
          WritePpm(out, width, height, ref, width);
        }
      }
      free(ref);
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
        g_failures++;
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
    } else if (!strcmp(cmd, "fullinv")) {
      FileSelect_SetFullInventory(atoi(arg1));
      Session_SetFullInventory(atoi(arg1));
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
          g_failures++;
      }
    } else if (!strcmp(cmd, "splitshot")) {
      int r = Inst_Current();
      uint32 *wide = calloc(width * 2 * height * 4, 1);
      // (As the game draws split screen: the worlds are read first, 3D panes drawn after.)
      int8 locals[2] = { (int8)LocalOfPlayer(atoi(arg2) - 1), (int8)LocalOfPlayer(atoi(arg3) - 1) };
      Hd3d_SetPanes(locals, 2);
      Hd3d_BeginPanes(true);
      for (int v = 0; v < 2; v++) {
        Inst_Switch(Inst_ViewOfPlayer(atoi(v ? arg3 : arg2) - 1));
        Hd2d_DrawFrame((uint8 *)(wide + v * width), width * 2 * 4, render_flags & ~kPpuRenderFlags_4x4Mode7, v);
      }
      Hd3d_FinishPanes();
      Inst_Switch(r);
      Ui_Draw((uint8 *)wide, width * 2 * 4, width * 2, height, 1);
      WritePpm(arg1, width * 2, height, wide, width * 2);
      free(wide);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "splitshot3")) {
      // splitshot3 <file.ppm> <p> <q> <r>: three players' worlds in a 2x2
      // window as on one computer (the fourth quarter shows the team)
      int r0 = Inst_Current(), ps[3] = { PlayerArg(arg2), PlayerArg(arg3), PlayerArg(arg4) };
      uint32 *quad = calloc(width * 2 * height * 2 * 4, 1);
      UiView uv[3];
      g_mp_local_mask = 0;
      int8 locals[3];
      for (int v = 0; v < 3; v++)
        locals[v] = (int8)LocalOfPlayer(ps[v]);
      Hd3d_SetPanes(locals, 3);
      Hd3d_BeginPanes(true);
      for (int v = 0; v < 3; v++) {
        Inst_Switch(Inst_ViewOfPlayer(ps[v]));
        uint32 *dst = quad + (v & 1) * width + (v >> 1) * height * width * 2;
        Hd2d_DrawFrame((uint8 *)dst, width * 2 * 4, render_flags & ~kPpuRenderFlags_4x4Mode7, v);
        uv[v] = (UiView){ (v & 1) * width, (v >> 1) * height, width, height, ps[v] };
        g_mp_local_mask |= 1 << ps[v];
      }
      Hd3d_FinishPanes();
      Inst_Switch(r0);
      Ui_SetViews(uv, 3);
      Ui_Draw((uint8 *)quad, width * 2 * 4, width * 2, height * 2, 1);
      WritePpm(arg1, width * 2, height * 2, quad, width * 2);
      free(quad);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "groupcam")) {
      // groupcam <0|1>: the camera of a shared world follows the players' middle (1) or the leader (0)
      g_mp_group_camera = atoi(arg1);
    } else if (!strcmp(cmd, "cam")) {
      // cam <x> <y>: put the viewed world's camera at x,y right away (the
      // scroll thresholds go with it, as after an area load)
      BG2HOFS_copy2 = BG1HOFS_copy2 = atoi(arg1);
      BG2VOFS_copy2 = BG1VOFS_copy2 = atoi(arg2);
      camera_x_coord_scroll_hi = BG2HOFS_copy2 + 128;
      camera_x_coord_scroll_low = camera_x_coord_scroll_hi + 2;
      camera_y_coord_scroll_hi = BG2VOFS_copy2 + 116;
      camera_y_coord_scroll_low = camera_y_coord_scroll_hi + 2;
    } else if (!strcmp(cmd, "camlog")) {
      // camlog <n> [every]: run n frames, printing every world's camera,
      // leader and the players' positions every frame (or every k frames)
      int n = atoi(arg1), every = arg2[0] ? atoi(arg2) : 1;
      for (int k = 0; k < n; k++, frame++) {
        Inst_RunFrame(held);
        if (k % every)
          continue;
        int r = Inst_Current();
        printf("camlog %d:", frame + 1);
        for (int i = 0; i < kInstMax; i++) {
          if (!Inst_IsActive(i))
            continue;
          Inst_Switch(i);
          printf(" w%d[m%d/%d L%d cam=%d,%d]", i, main_module_index, submodule_index, g_mp.leader + 1, BG2HOFS_copy2, BG2VOFS_copy2);
        }
        for (int p = 0; p < kMpMaxPlayers; p++) {
          if (!Inst_IsJoined(p))
            continue;
          SwitchToPlayer(p);
          printf(" P%d(w%d %d,%d%s%s)", p + 1, Inst_Current(), Mp_PlayerX(p), Mp_PlayerY(p), g_mp.p[p].downed ? " down" : "", g_mp.away[p] ? " away" : "");
        }
        Inst_Switch(r);
        printf("\n");
      }
    } else if (!strcmp(cmd, "status")) {
      PrintStatus(frame);
    } else if (!strcmp(cmd, "down") || !strcmp(cmd, "revive")) {
      // down <p>: player p goes down right now (0: the leader of the viewed player's world)
      // revive <p>: player p gets up again
      // (revive 0: the player the last "down 0" put down)
      static int last_down = -1;
      int p = PlayerArg(arg1);
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
        g_failures++;
      }
    } else if (!strcmp(cmd, "receive")) {
      // receive <hexitem>: the viewed player gets an item, as from a chest
      SwitchToPlayer(view);
      Link_ReceiveItem(strtol(arg1, NULL, 16), 0);
    } else if (!strcmp(cmd, "setleader")) {
      // setleader <p>: player p leads its world (as if it had opened a door)
      SwitchToPlayer(PlayerArg(arg1));
      Mp_SetLeader(PlayerArg(arg1));
    } else if (!strcmp(cmd, "expect_playing")) {
      // expect_playing <p>: player p's world is in plain gameplay (not stuck in a transition)
      SwitchToPlayer(PlayerArg(arg1));
      if (!((main_module_index == 7 || main_module_index == 9) && submodule_index == 0)) {
        printf("FAIL: P%s's world is in module %d/%d at frame %d\n", arg1, main_module_index, submodule_index, frame);
        g_failures++;
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
          g_failures++;
        }
      }
    } else if (!strcmp(cmd, "spawn")) {
      SwitchToPlayer(PlayerArg(arg2));
      SpawnNear(strtol(arg1, NULL, 16), PlayerArg(arg2), arg3[0] ? atoi(arg3) : 24);
    } else if (!strcmp(cmd, "atframe")) {
      // atframe <f> spawn <hextype> <p> [dx] | atframe <f> clearsprites
      if (g_nsched < 64) {
        Scheduled *sc = &g_sched[g_nsched++];
        memset(sc, 0, sizeof(*sc));
        sc->frame = strtoul(arg1, NULL, 10);
        if (!strcmp(arg2, "clearsprites")) {
          sc->clear = true;
        } else {
          sc->type = strtol(arg3, NULL, 16);
          sc->player = PlayerArg(arg4);
          sc->dx = arg5[0] ? atoi(arg5) : 24;
        }
        g_session_frame_hook = &FrameHook;
      }
    } else if (!strcmp(cmd, "dumpevery")) {
      g_session_dump_every = atoi(arg1);
    } else if (!strcmp(cmd, "trackkills")) {
      g_track = atoi(arg1);
      if (g_track) {
        int w = Inst_ViewOfPlayer(view), r = Inst_Current();
        if (w >= 0) { Inst_Switch(w); memcpy(g_track_state, sprite_state, 16); memcpy(g_track_type, sprite_type, 16); for (int k = 0; k < 16; k++) g_track_x[k] = Sprite_GetX(k), g_track_y[k] = Sprite_GetY(k); Inst_Switch(r); }
        g_track_last_rb = Session_GetStatus()->rollbacks;
      } else {
        printf("trackkills: %d kills, %d sprites back from state 0 (%d of them resurrected where they died in a tick with a rollback), %d kills undone by a rollback\n",
               g_track_kills, g_track_resurrections, g_track_rb_resurrections, g_track_rb_unkills);
      }
    } else if (!strcmp(cmd, "expect_resurrections")) {
      // expect_resurrections <n>: at most n sprites of the viewed world came back
      // from the dead (state 0 -> alive at the spot where they died) in a tick
      // with a rollback since trackkills 1 (see trackkills 0 for the counts)
      if (g_track_rb_resurrections > atoi(arg1)) {
        printf("FAIL: %d sprites resurrected by rollbacks, at most %s expected\n", g_track_rb_resurrections, arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "netstats")) {
      const SessionStatus *st = Session_GetStatus();
      printf("netstats: rollbacks=%d replayed_frames=%d max_rollback=%d mispredicted=%d b_flips=%d own_corrections=%d resyncs=%d ping=%d predicted=%d margin=%d lead_debt=%d\n",
             st->rollbacks, st->replayed_frames, st->max_rollback, st->mispredicted, st->b_flips, st->own_corrections, st->resyncs, st->ping_ms, st->predicted, st->margin, st->lead_debt);
    } else if (!strcmp(cmd, "hp")) {
      SwitchToPlayer(PlayerArg(arg1));
      *Mp_PlayerRam(PlayerArg(arg1), 0xF36D) = atoi(arg2);
    } else if (!strcmp(cmd, "host")) {
      Session_Host(atoi(arg1), false);
    } else if (!strcmp(cmd, "connect")) {
      Session_Join(arg1);
    } else if (!strcmp(cmd, "disconnect")) {
      Session_Disconnect();
    } else if (!strcmp(cmd, "localjoin")) {
      Session_LocalJoin(PlayerArg(arg1));
    } else if (!strcmp(cmd, "localleave")) {
      Session_LocalLeave(PlayerArg(arg1));
    } else if (!strcmp(cmd, "tick") || !strcmp(cmd, "waitplay") || !strcmp(cmd, "waitplayers") ||
               !strcmp(cmd, "waitseat") || !strcmp(cmd, "waitoffline") || !strcmp(cmd, "waitclientstate")) {
      // tick <n>: n display frames of the real game loop (netplay included)
      // waitplay: tick until connected and playing (max 15 s)
      // waitplayers <n>: tick until n players are in the game (max 15 s)
      // waitseat <p> <open|playing|reconnecting>: tick until seat p is so (max 25 s)
      // waitoffline: tick until not hosting or connected any more (max 25 s)
      // waitclientstate <n>: tick until the client state is n (max 25 s)
      enum { kTick, kPlay, kPlayers, kSeat, kOffline, kClientState } what =
          !strcmp(cmd, "waitplay") ? kPlay : !strcmp(cmd, "waitplayers") ? kPlayers : !strcmp(cmd, "waitseat") ? kSeat :
          !strcmp(cmd, "waitoffline") ? kOffline : !strcmp(cmd, "waitclientstate") ? kClientState : kTick;
      int seat = what == kSeat ? PlayerArg(arg1) : 0, want = 0;
      if (what == kSeat)
        want = SeatStateArg(arg2);
      int n = what == kTick ? atoi(arg1) : what == kPlay || what == kPlayers ? 15 * 60 : 25 * 60;
      bool done = false;
      for (int i = 0; i < n && !done; i++) {
        uint16 in[kMpMaxPlayers];
        memcpy(in, held, sizeof(in));
        Ui_Update(in, 0);
        // As in the game's loop: the 3D view's cameras and View buttons, and
        // a turned camera turns the d-pad before the input is sent.
        Hd3d_Update(1.0f / 60);
        Hd3d_HandleViewButtons();
        Hd3d_RemapInputs(in);
        // As in the game's loop: the world of our first player is heard, and
        // is the current one between frames (so it differs between computers).
        Inst_SetAudioPlayer(Ui_WatchedPlayer(0));
        if (!Ui_PausesGame())
          frame += Session_Tick(in, g_wanted_zelda_features);
        Ap_Tick();
        if (g_drawtick)
          DrawLikeGame(pixels, width, height, render_flags);
        TrackSprites(frame, view);
        SDL_Delay(16);
        SessionPlayer seats[kMpMaxPlayers];
        Session_GetPlayers(seats);
        const SessionStatus *st = Session_GetStatus();
        done = what == kPlay ? st->client_state == 3 : what == kPlayers ? Inst_NumJoined() >= atoi(arg1) :
               what == kSeat ? seats[seat].state == want : what == kOffline ? st->mode == kSessionOffline :
               what == kClientState ? st->client_state == atoi(arg1) : false;
      }
      if (what != kTick && !done) {
        printf("FAIL: %s %s %s didn't happen by frame %d (%d players, client state %d: %s)\n", cmd, arg1, arg2, frame,
               Inst_NumJoined(), Session_GetStatus()->client_state, Session_GetStatus()->message);
        g_failures++;
      }
    } else if (!strcmp(cmd, "name")) {
      // name <player name>: what this computer is called online
      Session_SetName(arg1);
    } else if (!strcmp(cmd, "netblackout")) {
      // netblackout <0|1>: pull the network cable out (1) or put it back (0)
      Net_SetBlackout(atoi(arg1));
    } else if (!strcmp(cmd, "kick")) {
      // kick <p>: the host removes the remote player in seat p
      if (!Session_Kick(PlayerArg(arg1))) {
        printf("FAIL: couldn't remove P%s at frame %d\n", arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_seat")) {
      // expect_seat <p> <open|playing|reconnecting> [name]
      SessionPlayer seats[kMpMaxPlayers];
      Session_GetPlayers(seats);
      int p = PlayerArg(arg1);
      if (seats[p].state != SeatStateArg(arg2) || (arg3[0] && strcmp(seats[p].name, arg3))) {
        printf("FAIL: seat %d is %d '%s', expected %s %s at frame %d\n", p + 1, seats[p].state, seats[p].name, arg2, arg3, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_myseat")) {
      // expect_myseat <p>: this computer's first player plays in seat p
      int p = PlayerArg(arg1);
      if (Session_GetStatus()->local_slot[0] != p) {
        printf("FAIL: our first player is in seat %d, expected %d at frame %d\n", Session_GetStatus()->local_slot[0] + 1, p + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_client_state")) {
      if (Session_GetStatus()->client_state != atoi(arg1)) {
        printf("FAIL: client state %d, expected %s at frame %d\n", Session_GetStatus()->client_state, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_message")) {
      // expect_message <word>: the latest netplay message has this word in it
      if (!strstr(Session_GetStatus()->message, arg1)) {
        printf("FAIL: message '%s' doesn't say '%s' at frame %d\n", Session_GetStatus()->message, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "roomcode") || !strcmp(cmd, "connectcode")) {
      // roomcode <a.b.c.d:port>: the room code for it decodes back to it (also
      // typed in lower case, with O for 0 and without dashes)
      // connectcode <a.b.c.d:port>: join the game by its room code
      NetAddr a;
      char code[32], loose[32];
      uint32 ip = 0;
      uint16 port = 0;
      if (!Net_ParseAddr(arg1, kSessionPort, &a)) {
        printf("FAIL: bad address %s\n", arg1);
        g_failures++;
        continue;
      }
      Session_EncodeRoomCode(a.ip, a.port, code, sizeof(code));
      int k = 0;
      for (const char *c = code; *c; c++)
        if (*c != '-')
          loose[k++] = *c == '0' ? 'o' : (*c >= 'A' && *c <= 'Z') ? *c + 32 : *c;
      loose[k] = 0;
      printf("room code for %s: %s\n", arg1, code);
      if (!Session_DecodeRoomCode(code, &ip, &port) || ip != a.ip || port != a.port ||
          !Session_DecodeRoomCode(loose, &ip, &port) || ip != a.ip || port != a.port) {
        printf("FAIL: room code %s (%s) doesn't decode back to %s\n", code, loose, arg1);
        g_failures++;
      }
      if (!strcmp(cmd, "connectcode"))
        Session_Join(code);
    } else if (!strcmp(cmd, "expect_badcode")) {
      // expect_badcode <code>: a mistyped room code isn't taken for a game
      uint32 ip;
      uint16 port;
      if (Session_DecodeRoomCode(arg1, &ip, &port)) {
        printf("FAIL: room code %s was accepted\n", arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "stun")) {
      // stun <server:port>: ask a STUN server for our public address (needs the internet)
      char ip[64];
      bool ok = Upnp_StunPublicIp(arg1, ip, sizeof(ip));
      printf("stun %s: %s\n", arg1, ok ? ip : "no answer");
    } else if (!strcmp(cmd, "aproom")) {
      Ap_TestRoom(strcmp(arg1, "off") ? strtoul(arg1, NULL, 16) : 0, atoi(arg2) != 0);
    } else if (!strcmp(cmd, "apitem")) {
      Ap_TestGiveItem(strtol(arg1, NULL, 16) == 0xff ? kApIdBase + 0x99 : kApIdBase + strtol(arg1, NULL, 16));
    } else if (!strcmp(cmd, "apdeathlink")) {
      Ap_TestDeathLink();
    } else if (!strcmp(cmd, "srun")) {
      for (int n = atoi(arg1); n > 0; n--) {
        uint16 in[kMpMaxPlayers];
        memcpy(in, held, sizeof(in));
        Hd3d_Update(1.0f / 60);
        Hd3d_HandleViewButtons();
        Hd3d_RemapInputs(in);
        frame += Session_Tick(in, g_wanted_zelda_features);
        Ap_Tick();
      }
    } else if (!strcmp(cmd, "apconnect")) {
      snprintf(g_ap_settings.server, sizeof(g_ap_settings.server), "%s", arg1);
      snprintf(g_ap_settings.slot, sizeof(g_ap_settings.slot), "%s", arg2);
      snprintf(g_ap_settings.password, sizeof(g_ap_settings.password), "%s", arg3);
      Ap_Connect();
    } else if (!strcmp(cmd, "apwait")) {
      bool items = !strcmp(arg1, "items");
      int want = items ? atoi(arg2) : 0, secs = atoi(items ? arg3 : arg2);
      bool done = false;
      for (int i = 0; i < (secs ? secs : 20) * 60 && !done; i++) {
        frame += Session_Tick(held, g_wanted_zelda_features);
        Ap_Tick();
        SDL_Delay(16);
        done = items ? (g_ram[0xF000 + kApSave_ItemCount] | g_ram[0xF000 + kApSave_ItemCount + 1] << 8) >= want :
                       Ap_HostBind() != 0;
      }
      char status[96];
      Ap_StatusLine(status, sizeof(status));
      printf("apwait %s: %s (%s)\n", arg1, done ? "done" : "timed out", status);
      if (!done)
        g_failures++;
    } else if (!strcmp(cmd, "apnotices") || !strcmp(cmd, "expect_apnotices")) {
      char text[96];
      int kind, n = 0;
      while (Ap_PollNotice(text, sizeof(text), &kind))
        printf("[ap %d] %s\n", kind, text), n++;
      if (!strcmp(cmd, "expect_apnotices") && n != atoi(arg1)) {
        printf("FAIL: %d Archipelago lines, expected %s at frame %d\n", n, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_apcount")) {
      int n = g_ram[0xF000 + kApSave_ItemCount] | g_ram[0xF000 + kApSave_ItemCount + 1] << 8;
      if (n != atoi(arg1)) {
        printf("FAIL: %d Archipelago items received, expected %s at frame %d\n", n, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_apsent")) {
      if (Ap_TestSent()->checks != atoi(arg1)) {
        printf("FAIL: %d locations sent, expected %s at frame %d\n", Ap_TestSent()->checks, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_apchecked")) {
      int x = strtol(arg1, NULL, 16);
      int64 id = x >= 0x100 ? kApSpecialLocationBase + x - 0x100 : kApIdBase + x;
      bool sent = false;
      for (int i = 0; i < Ap_TestSent()->checks; i++)
        sent |= Ap_TestSent()->check[i] == id;
      if (sent != (atoi(arg2) != 0)) {
        printf("FAIL: location %s %s at frame %d\n", arg1, sent ? "was sent" : "wasn't sent", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_apgoal")) {
      if (Ap_TestSent()->goal != (atoi(arg1) != 0)) {
        printf("FAIL: goal %s at frame %d\n", Ap_TestSent()->goal ? "sent" : "not sent", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_apdeaths")) {
      if (Ap_TestSent()->deaths != atoi(arg1)) {
        printf("FAIL: %d DeathLinks sent, expected %s at frame %d\n", Ap_TestSent()->deaths, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "aplogic")) {
      // Each line: "items 49=2 04=1 ... | rooms 0104=1 0055=1 ... | win=0"
      FILE *lf = fopen(arg1, "r");
      if (!lf) {
        printf("FAIL: can't read %s\n", arg1);
        g_failures++;
        continue;
      }
      char lbuf[4096];
      int nlines = 0, bad = 0;
      while (fgets(lbuf, sizeof(lbuf), lf)) {
        uint8 counts[256] = { 0 };
        char *rooms = strstr(lbuf, "| rooms"), *win = strstr(lbuf, "| win=");
        if (strncmp(lbuf, "items", 5) || !rooms || !win)
          continue;
        nlines++;
        for (char *p = lbuf + 5; p < rooms;) {
          unsigned code, cnt;
          int used;
          if (sscanf(p, " %x=%u%n", &code, &cnt, &used) != 2)
            break;
          counts[code & 0xff] = cnt;
          p += used;
        }
        for (char *p = rooms + 7; p < win;) {
          unsigned room;
          int v, used;
          if (sscanf(p, " %x=%d%n", &room, &v, &used) != 2)
            break;
          int c = Rando_TestRoomAccess(counts, room);
          if (c != v && bad++ < 10)
            printf("FAIL: logic line %d room %03x: C says %d, the APWorld %d\n", nlines, room, c, v);
          p += used;
        }
        if (Rando_TestWin(counts) != (atoi(win + 6) != 0) && bad++ < 10)
          printf("FAIL: logic line %d: C says win=%d\n", nlines, Rando_TestWin(counts));
      }
      fclose(lf);
      printf("aplogic: %d states, %d disagreements\n", nlines, bad);
      if (bad || !nlines)
        g_failures++;
    } else if (!strcmp(cmd, "expect_fullinv")) {
      // expect_fullinv <0|1>: new save files start with every item (on a
      // client: the host's setting, as it arrived with the frames)
      if (Session_GetFullInventory() != (atoi(arg1) != 0)) {
        printf("FAIL: full inventory for new files %d, expected %d at frame %d\n", Session_GetFullInventory(),
               atoi(arg1) != 0, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_saves_blocked")) {
      // expect_saves_blocked <0|1>: the game may not write our own save file
      // (we play in a world from someone else's game)
      if (g_sram_write_disabled != (atoi(arg1) != 0)) {
        printf("FAIL: saves %s, expected %s at frame %d\n", g_sram_write_disabled ? "blocked" : "allowed",
               atoi(arg1) ? "blocked" : "allowed", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "bindline")) {
      // bindline <key> <value>: a line of the settings file (no spaces in the
      // value), then the clean-up done after loading the settings
      char value[256];
      snprintf(value, sizeof(value), "%s", arg2);
      Bindings_ParseConfig(arg1, value);
      Bindings_ResolveLateClashes();
    } else if (!strcmp(cmd, "expect_keys")) {
      // expect_keys <p> <button index> <description>: as the Controls screen shows it
      char keys[64];
      int b = atoi(arg2);
      if (b < 0 || b >= kBindButtons) {
        printf("FAIL: no button %s (0-%d)\n", arg2, kBindButtons - 1);
        g_failures++;
        continue;
      }
      Bindings_DescribeKeys(PlayerArg(arg1), b, keys, sizeof(keys));
      if (strcmp(keys, arg3)) {
        printf("FAIL: P%s button %s keys are '%s', expected '%s'\n", arg1, arg2, keys, arg3);
        g_failures++;
      }
    } else if (!strcmp(cmd, "removefile")) {
      remove(arg1);
    } else if (!strcmp(cmd, "keepcopy")) {
      // keepcopy <file 1-3> <path>: keep a copy of the game we play in, in that save file
      char msg[96];
      if (!Session_KeepCopy(atoi(arg1), arg2, msg, sizeof(msg))) {
        printf("FAIL: %s\n", msg);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_save_file")) {
      // expect_save_file <path> <file 1-3> <empty|valid|copy>: valid: a file
      // in use with a right checksum and backup; copy: one holding this
      // game's progress as it is right now
      FILE *sf = fopen(arg1, "rb");
      uint8 sram[8192] = { 0 };
      bool ok = sf && fread(sram, 1, sizeof(sram), sf) == sizeof(sram);
      if (sf)
        fclose(sf);
      const uint8 *slot = sram + (atoi(arg2) - 1) * 0x500;
      if (!strcmp(arg3, "empty")) {
        for (int i = 0; i < 0x500; i++)
          ok &= slot[i] == 0;
      } else {
        uint16 t = 0x5a5a;
        for (int i = 0; i < 0x4fe; i += 2)
          t -= WORD(slot[i]);
        const uint8 *ram = Session_GetStatus()->local_slot[0] >= 0 ? Inst_PlayerWorldRam(Session_GetStatus()->local_slot[0]) : g_ram;
        ok &= WORD(slot[0x4fe]) == t && !memcmp(slot, slot + 0xf00, 0x500) && WORD(slot[0x3e5]) == 0x55aa &&
              (strcmp(arg3, "copy") || !memcmp(slot, ram + 0xF000, 0x3e0));
      }
      if (!ok) {
        printf("FAIL: file %s of %s isn't %s at frame %d\n", arg2, arg1, arg3, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "storycheck")) {
      // storycheck: every rewritten message fits the text box (measured with the font)
      int bad = Story_CheckText(true);
      Story_MeasureOriginals(true);
      if (bad) {
        printf("FAIL: %d story text lines don't fit\n", bad);
        g_failures++;
      }
    } else if (!strcmp(cmd, "story")) {
      Story_SetEnabled(atoi(arg1));
      Session_SetStory(atoi(arg1));
    } else if (!strcmp(cmd, "title")) {
      g_config.four_swords_title = atoi(arg1);
    } else if (!strcmp(cmd, "dumprender")) {
      // dumprender <p>: the extra sprites of player p (count, first entries' flags)
      int p = PlayerArg(arg1);
      SwitchToPlayer(view);
      MpRenderPlayer *rp = &g_mp_render.p[p];
      printf("[frame %d] P%d render: visible=%d count=%d leader=P%d resident=P%d area_key=%x aux=%d", frame, p + 1, rp->visible, rp->count, g_mp.leader + 1, g_mp.resident + 1, g_mp.area_key, link_auxiliary_state);
      for (int i = 0; i < rp->count && i < 4; i++)
        printf(" [x%d y%d chr%02x fl%02x]", rp->oam[i * 2] & 0xff, rp->oam[i * 2] >> 8, rp->oam[i * 2 + 1] & 0xff, rp->oam[i * 2 + 1] >> 8);
      printf("\n");
    } else if (!strcmp(cmd, "features")) {
      // features <hex>: OR bits into the wanted game features (2 = switch items with L/R)
      g_wanted_zelda_features |= strtoul(arg1, NULL, 16);
    } else if (!strcmp(cmd, "pitem")) {
      // pitem: print every player's item state and the HUD item box tile of the viewed world
      int r = Inst_Current();
      printf("[frame %d] items:", frame);
      for (int p = 0; p < kMpMaxPlayers; p++) {
        if (!Inst_IsJoined(p))
          continue;
        SwitchToPlayer(p);
        printf(" P%d(w%d%s y=%02x cy=%02x x=%02x l=%02x r=%02x bot=%02x)", p + 1, Inst_Current(), p == g_mp.leader ? "L" : "",
               *Mp_PlayerRam(p, 0x202), *Mp_PlayerRam(p, 0x303), *Mp_PlayerRam(p, 0x656), *Mp_PlayerRam(p, 0x657),
               *Mp_PlayerRam(p, 0x658), *Mp_PlayerRam(p, 0xF34F));
      }
      Inst_Switch(r);
      printf(" | w%d ram202=%02x hudtile=%04x %04x feat=%x mod=%d/%d\n", Inst_Current(), hud_cur_item,
             hud_tile_indices_buffer[1 * 32 + 5], hud_tile_indices_buffer[2 * 32 + 5], enhanced_features0, main_module_index, submodule_index);
    } else if (!strcmp(cmd, "pram")) {
      // pram <p> <hexaddr> <hexval>: change a byte of player p's own state (its Link context)
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      *Mp_PlayerRam(p, strtol(arg2, NULL, 16)) = strtol(arg3, NULL, 16);
    } else if (!strcmp(cmd, "dumpworldmap")) {
      // dumpworldmap <file> <dark 0|1>: (debug) the world map from the assets
      extern void Ui_DebugWorldMap(const char *file, bool dark);
      Ui_DebugWorldMap(arg1, atoi(arg2));
    } else if (!strcmp(cmd, "expect_running")) {
      // expect_running <p> <frames>: the world player p's Link is in keeps
      // running (its frame counter advances) for that many frames
      int p = PlayerArg(arg1), n = atoi(arg2);
      SwitchToPlayer(p);
      uint8 last = frame_counter;
      int ran = 0;
      for (int k = 0; k < n; k++, frame++) {
        Inst_RunFrame(held);
        SwitchToPlayer(p);
        ran += (uint8)(frame_counter - last);
        last = frame_counter;
      }
      if (ran != n) {
        printf("FAIL: P%d's world ran %d of %d frames at frame %d\n", p + 1, ran, n, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_pram")) {
      // expect_pram <p> <hexaddr> <hexval>: a byte of player p's own state
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      int v = *Mp_PlayerRam(p, strtol(arg2, NULL, 16));
      if (v != strtol(arg3, NULL, 16)) {
        printf("FAIL: P%s ram %s is %02x, expected %s at frame %d\n", arg1, arg2, v, arg3, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "resetcounters")) {
      Session_ResetCounters();
    } else if (!strcmp(cmd, "expect_own_corrections")) {
      if (Session_GetStatus()->own_corrections > atoi(arg1)) {
        printf("FAIL: %d own corrections (rubber banding), at most %s expected\n", Session_GetStatus()->own_corrections, arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_rollbacks")) {
      // expect_rollbacks <n>: at most n rollbacks since resetcounters
      if (Session_GetStatus()->rollbacks > atoi(arg1)) {
        printf("FAIL: %d rollbacks, at most %s expected at frame %d\n", Session_GetStatus()->rollbacks, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "inputdelay")) {
      Session_SetInputDelay(atoi(arg1));
    } else if (!strcmp(cmd, "netsim")) {
      // netsim <delay ms> <jitter ms> <loss %>: simulate a bad link (before host/connect)
      Net_SetSim(atoi(arg1), atoi(arg2), atoi(arg3));
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
        TrackSprites(frame, view);
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
            printf("[frame %d] %s P%d by %d (%d,%d)->(%d,%d) in %d frames%s [pred=%d margin=%d own=%d rb=%d]\n", frame, local ? "JUMP" : "remote jump", p + 1, d, px[p], py[p], x, y, ran,
                   p == g_mp.leader ? " (leader)" : "", Session_GetStatus()->predicted, Session_GetStatus()->margin, Session_GetStatus()->own_corrections, Session_GetStatus()->rollbacks);
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
        g_failures++;
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
        g_failures++;
      }
    } else if (!strcmp(cmd, "upnp")) {
      char ip[64] = { 0 };
      bool ok = Upnp_MapPort(atoi(arg1), ip, sizeof(ip));
      printf("UPnP map port %s: %s external=%s\n", arg1, ok ? "OK" : "FAILED", ip);
      if (!ok)
        g_failures++;
      Upnp_UnmapPort();
    } else if (!strcmp(cmd, "menu")) {
      Ui_HandleKey(SDLK_ESCAPE, true);
    } else if (!strcmp(cmd, "key")) {
      // key <up|down|left|right|enter|back> [times]: a button press in the menu
      uint16 bit = !strcmp(arg1, "up") ? 0x10 : !strcmp(arg1, "down") ? 0x20 : !strcmp(arg1, "left") ? 0x40 :
                   !strcmp(arg1, "right") ? 0x80 : !strcmp(arg1, "back") ? 1 : 0x100;
      for (int n = arg2[0] ? atoi(arg2) : 1; n > 0; n--) {
        uint16 none[4] = {0};
        Ui_Update(none, bit);
        Ui_Update(none, 0);
      }
    } else if (!strcmp(cmd, "type")) {
      Ui_HandleText(arg1);
    } else if (!strcmp(cmd, "expect_apsettings")) {
      static const char *const kDl[3] = { "auto", "on", "off" };
      const char *server = g_ap_settings.server[0] ? g_ap_settings.server : "-";
      const char *slot = g_ap_settings.slot[0] ? g_ap_settings.slot : "-";
      if (strcmp(server, arg1) || strcmp(slot, arg2) || strcmp(kDl[g_ap_settings.death_link % 3], arg3)) {
        printf("FAIL: Archipelago settings %s %s %s, expected %s %s %s\n", server, slot,
               kDl[g_ap_settings.death_link % 3], arg1, arg2, arg3);
        g_failures++;
      }
    } else if (!strcmp(cmd, "corrupt")) {
      g_ram[0xF360] ^= 0x55;  // rupees
    } else if (!strcmp(cmd, "expect_players")) {
      if (Inst_NumJoined() != atoi(arg1)) {
        printf("FAIL: %d players, expected %s\n", Inst_NumJoined(), arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_resyncs")) {
      if (Session_GetStatus()->resyncs != atoi(arg1)) {
        printf("FAIL: %d resyncs, expected %s\n", Session_GetStatus()->resyncs, arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "pos")) {
      // pos <p> <x> <y>: put player p there (no collision check)
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      WORD(*Mp_PlayerRam(p, 0x22)) = atoi(arg2);
      WORD(*Mp_PlayerRam(p, 0x20)) = atoi(arg3);
    } else if (!strcmp(cmd, "poke")) {
      long addr = strtol(arg1, NULL, 16);
      if (addr < 0 || addr >= 0x20000) {  // (a RAM offset like f35c, not 7ef35c)
        printf("FAIL: poke %s: not a RAM offset\n", arg1);
        g_failures++;
        continue;
      }
      g_ram[addr] = strtol(arg2, NULL, 16);
    } else if (!strcmp(cmd, "expect_camx")) {
      // expect_camx <p> <x>: the camera of player p's world is at x
      SwitchToPlayer(PlayerArg(arg1));
      if (BG2HOFS_copy2 != atoi(arg2)) {
        printf("FAIL: P%s's camera x is %d, expected %s at frame %d\n", arg1, BG2HOFS_copy2, arg2, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_ram_counts")) {
      // expect_ram_counts <hexaddr> <frames> <0|1>: the byte changes (1) or
      // stays (0) over that many frames
      int a = strtol(arg1, NULL, 16);
      int before = g_ram[a];
      bool changed = false;
      for (int n = atoi(arg2); n > 0; n--, frame++) {
        Inst_RunFrame(held);
        changed |= g_ram[a] != before;
      }
      if (changed != (atoi(arg3) != 0)) {
        printf("FAIL: ram %s %s over %s frames at frame %d\n", arg1, changed ? "changed" : "stayed", arg2, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_ram") || !strcmp(cmd, "expect_ram_not")) {
      // expect_ram <hexaddr> <hexval> / expect_ram_not <hexaddr> <hexval>
      int v = g_ram[strtol(arg1, NULL, 16)];
      bool is = v == strtol(arg2, NULL, 16);
      if (is != !strcmp(cmd, "expect_ram")) {
        printf("FAIL: ram %s is %02x, expected %s%s at frame %d\n", arg1, v, is ? "not " : "", arg2, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_near")) {
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      if (abs(Mp_PlayerX(p) - atoi(arg2)) > 4 || abs(Mp_PlayerY(p) - atoi(arg3)) > 4) {
        printf("FAIL: P%d is at %d,%d, expected %s,%s at frame %d\n", p + 1, Mp_PlayerX(p), Mp_PlayerY(p), arg2, arg3, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_ping")) {
      // expect_ping <player> none|here|help [x y]: the player's ping marker
      // (where it is, within 2 pixels)
      int p = PlayerArg(arg1);
      InstPing pg;
      bool has = Inst_PlayerPing(p, &pg), ok;
      if (!strcmp(arg2, "none"))
        ok = !has;
      else if (!strcmp(arg2, "here") || !strcmp(arg2, "help"))
        ok = has && pg.kind == (arg2[2] == 'l') &&
             (!arg3[0] || (abs(pg.x - atoi(arg3)) <= 2 && abs(pg.y - atoi(arg4)) <= 2));
      else
        ok = false, printf("FAIL: expect_ping wants none, here or help, not '%s'\n", arg2);
      if (!ok) {
        if (has)
          printf("FAIL: P%d's ping is %s at %d,%d (%d frames left), expected %s %s %s at frame %d\n", p + 1,
                 pg.kind ? "help" : "here", pg.x, pg.y, pg.ttl, arg2, arg3, arg4, frame);
        else
          printf("FAIL: P%d has no ping, expected %s %s %s at frame %d\n", p + 1, arg2, arg3, arg4, frame);
        g_failures++;
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
        g_failures++;
      }
    } else if (!strcmp(cmd, "holecheck")) {
      // For every overworld fall hole, put a Link anywhere the original camera
      // could show it and check the co-op search finds that hole's entrance.
      int bad = 0, total = 0;
      int holes = kFallHole_Area_SIZE / 2;
      for (int i = 0; i < holes; i++) {
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
        g_failures++;
    } else if (!strcmp(cmd, "message")) {
      dialogue_message_index = strtol(arg1, NULL, 16);
      Main_ShowTextMessage();
    } else if (!strcmp(cmd, "findroomtag")) {
      // findroomtag <hextag>: (debug) prints the dungeon rooms whose header has that tag routine
      int want = strtol(arg1, NULL, 16);
      for (int room = 0; room < 0x128; room++) {
        const uint8 *h = GetRoomHeaderPtr(room);
        if (h[5] == want || h[6] == want)
          printf("room %x tags %x %x\n", room, h[5], h[6]);
      }
    } else if (!strcmp(cmd, "findtile")) {
      // findtile <p> <hexattr>: (debug) prints the tiles with that behavior
      // within 200 pixels of player p (in a dungeon: on its floor of the room)
      int p = PlayerArg(arg1), want = strtol(arg2, NULL, 16), n = 0;
      SwitchToPlayer(p);
      if (player_is_indoors) {
        // (the whole room, its 64x64 tiles, on that player's floor)
        int base_x = Mp_PlayerX(p) & ~0x1ff, base_y = Mp_PlayerY(p) & ~0x1ff, fl = *Mp_PlayerRam(p, 0xEE) ? 0x1000 : 0;
        for (int t = 0; t < 4096 && n < 12; t++)
          if (dung_bg2_attr_table[t + fl] == want)
            printf("tile %x at %d,%d\n", want, base_x + (t & 63) * 8, base_y + (t >> 6) * 8), n++;
      }
      for (int dy = -200; dy <= 200 && n < 12 && !player_is_indoors; dy += 8)
        for (int dx = -200; dx <= 200 && n < 12; dx += 8) {
          uint16 x = Mp_PlayerX(p) + dx, y = Mp_PlayerY(p) + dy;
          int attr = player_is_indoors ?
              dung_bg2_attr_table[(y >> 3 & 63) * 64 + (x >> 3 & 63) + (*Mp_PlayerRam(p, 0xEE) ? 0x1000 : 0)] :
              Overworld_GetTileAttributeAtLocation(x >> 3, y);
          if (attr == want)
            printf("tile %x at %d,%d\n", want, x & ~7, y & ~7), n++;
        }
    } else if (!strcmp(cmd, "settiles")) {
      // settiles <x0> <y0> <x1> <y1> <hexattr>: (dungeon) make the tiles of that
      // rectangle (pixels) behave like attr, e.g. 20 for a pit
      for (int y = atoi(arg2) & ~7; y <= atoi(arg4); y += 8)
        for (int x = atoi(arg1) & ~7; x <= atoi(arg3); x += 8)
          dung_bg2_attr_table[(y >> 3 & 63) * 64 + (x >> 3 & 63)] = dung_bg1_attr_table[(y >> 3 & 63) * 64 + (x >> 3 & 63)] = strtol(arg5, NULL, 16);
    } else if (!strcmp(cmd, "expect_sprite_gone") || !strcmp(cmd, "expect_sprite_there")) {
      // expect_sprite_gone <hexN> / expect_sprite_there <hexN>: the area/room sprite
      // with that index (sprite_N_word outdoors, sprite_N indoors) is dead / alive
      // in the viewed world (a prize it dropped keeps its index: not the sprite)
      int want = strtol(arg1, NULL, 16), found = -1;
      for (int k = 0; k < 16; k++)
        if (sprite_state[k] && (player_is_indoors ? sprite_N[k] : sprite_N_word[k]) == want &&
            !(sprite_type[k] >= 0xd8 && sprite_type[k] <= 0xe6))
          found = k;
      bool gone = !strcmp(cmd, "expect_sprite_gone");
      if ((found >= 0) == gone) {
        printf("FAIL: sprite N=%s is %s in world %d at frame %d\n", arg1, found >= 0 ? "alive" : "gone", Inst_Current(), frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "clearsprites")) {
      for (int k = 0; k < 16; k++)
        sprite_state[k] = 0;
    } else if (!strcmp(cmd, "expect_hp")) {
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      if (Mp_PlayerHealth(p) != atoi(arg2)) {
        printf("FAIL: P%d has %d health, expected %s\n", p + 1, Mp_PlayerHealth(p), arg2);
        g_failures++;
      }
    } else if (!strcmp(cmd, "runcheckdraw")) {
      // runcheckdraw <p> <n>: runs n frames; player p (not its world's leader)
      // must be drawn whole every frame: its head and body as 16x16 sprites
      // (a blinking Link is left out on alternate frames, not cut to 8x8).
      int p = PlayerArg(arg1);
      for (int n = atoi(arg2); n > 0; n--, frame++) {
        Inst_RunFrame(held);
        int home = Inst_Current();
        SwitchToPlayer(p);
        const MpRenderPlayer *rp = &g_mp_render.p[p];
        int bad = 0;
        for (int i = 0; i < rp->count; i++) {
          uint8 tile = rp->oam[i * 2 + 1] & 0xff;
          if ((tile == 0x00 || tile == 0x02) && !(rp->high[i] & 2))
            bad++;
        }
        Inst_Switch(home);
        if (bad) {
          printf("FAIL: P%d drawn with %d shrunken body pieces at frame %d\n", p + 1, bad, frame);
          g_failures++;
          break;
        }
      }
    } else if (!strcmp(cmd, "runcheckobjowner")) {
      // runcheckobjowner <n> <hextype> <p>: runs until an object (ancilla) of
      // that type appears in p's world and has been given an owner (at most n
      // frames); it must belong to player p.
      int type = strtol(arg2, NULL, 16), p = PlayerArg(arg3), home = Inst_Current();
      bool old[10], found = false;
      SwitchToPlayer(p);
      for (int i = 0; i < 10; i++)
        old[i] = ancilla_type[i] == type;
      Inst_Switch(home);
      for (int n = atoi(arg1); n > 0 && !found; n--, frame++) {
        Inst_RunFrame(held);
        home = Inst_Current();
        SwitchToPlayer(p);
        for (int i = 0; i < 10 && !found; i++) {
          bool owned = ancilla_type[i] == type && g_mp.ancilla_seen[i] == type;
          if (owned && !old[i]) {
            found = true;
            if (g_mp.ancilla_owner[i] != p) {
              printf("FAIL: object %02x in slot %d belongs to P%d, not P%d, at frame %d\n", type, i,
                     g_mp.ancilla_owner[i] + 1, p + 1, frame);
              g_failures++;
            }
          }
          old[i] = owned;
        }
        Inst_Switch(home);
      }
      if (!found) {
        printf("FAIL: no new object %02x in P%d's world\n", type, p + 1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "runcheckseen")) {
      // runcheckseen <a> <b> <n>: runs n frames; on every one, players a and b see
      // each other: each is drawn on the other's screen where it really is, and
      // can have a name tag or an arrow. (Only the first miss is reported; all n
      // frames run either way.)
      int a = PlayerArg(arg1), b = PlayerArg(arg2);
      bool missed = false;
      for (int n = atoi(arg3); n > 0; n--, frame++) {
        Inst_RunFrame(held);
        int home = Inst_Current();
        const char *why = ShownProblem(a, b);
        int p = b, q = a;
        if (!why)
          why = ShownProblem(b, a), p = a, q = b;
        Inst_Switch(home);
        if (why && !missed) {
          printf("FAIL: P%d %s on P%d's screen at frame %d\n", p + 1, why, q + 1, frame);
          g_failures++;
          missed = true;
        }
      }
    } else if (!strcmp(cmd, "expect_carried")) {
      // expect_carried <p> <carrier|0>: p is held over the carrier's head (0: by nobody)
      int p = PlayerArg(arg1), c = atoi(arg2) - 1;
      SwitchToPlayer(p);
      int by = Inst_OfPlayer(p) >= 0 ? g_mp.carried_by[p] - 1 : -1;
      if (by != c) {
        printf("FAIL: P%d is carried by %s%d, expected %s%d at frame %d\n", p + 1, by < 0 ? "nobody " : "P",
               by + 1, c < 0 ? "nobody " : "P", c + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_downed")) {
      int p = PlayerArg(arg1);
      if (!Inst_IsJoined(p) || !Inst_PlayerDowned(p)) {
        printf("FAIL: P%d not downed at frame %d\n", p + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_alive")) {
      int p = PlayerArg(arg1);
      if (!Inst_IsJoined(p) || Inst_PlayerDowned(p)) {
        printf("FAIL: P%d not alive at frame %d\n", p + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_leader")) {
      // expect_leader <p>: player p leads its world (the camera follows it)
      int p = PlayerArg(arg1);
      SwitchToPlayer(p);
      if (!Inst_IsJoined(p) || g_mp.leader != p) {
        printf("FAIL: P%d's world is led by P%d, expected P%d at frame %d\n", p + 1, g_mp.leader + 1, p + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_camy")) {
      // expect_camy <p> <y>: the camera of player p's world is at y
      SwitchToPlayer(PlayerArg(arg1));
      if (BG2VOFS_copy2 != atoi(arg2)) {
        printf("FAIL: P%s's camera y is %d, expected %s at frame %d\n", arg1, BG2VOFS_copy2, arg2, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "view3d")) {
      // view3d <0|1>: the 3D view (as the menu and F11 turn it on and off)
      Hd3d_SetEnabled(atoi(arg1) != 0);
    } else if (!strcmp(cmd, "view3dset")) {
      // view3dset <setting> <value>: a 3D view setting, clamped as the settings file is
      int v = atoi(arg2);
      if (!arg2[0]) {
        printf("FAIL: view3dset %s needs a value\n", arg1);
        g_failures++;
      } else if (!strcmp(arg1, "returns")) {
        g_hd3d.camera_returns = v != 0;
      } else if (!strcmp(arg1, "turn")) {
        g_hd3d.turn_controls = v != 0;
      } else if (!strcmp(arg1, "mouselook")) {
        g_hd3d.mouse_look = v != 0;
      } else if (!strcmp(arg1, "fade")) {
        g_hd3d.fade_scenery = v != 0;
      } else if (!strcmp(arg1, "invert")) {
        g_hd3d.invert_y = v != 0;
      } else if (!strcmp(arg1, "stick")) {
        g_hd3d.stick_speed = (uint8)IntMin(IntMax(v, 25), 200);
      } else if (!strcmp(arg1, "mouse")) {
        g_hd3d.mouse_speed = (uint8)IntMin(IntMax(v, 0), 100);
      } else if (!strcmp(arg1, "fov")) {
        g_hd3d.fov = (uint8)IntMin(IntMax(v, 50), 90);
      } else if (!strcmp(arg1, "perspective")) {
        g_hd3d.perspective = (uint8)IntMin(IntMax(v, 0), 100);
      } else {
        printf("FAIL: no 3D view setting '%s'\n", arg1);
        g_failures++;
      }
    } else if (!strcmp(cmd, "cam3d")) {
      // cam3d <p> <tp|fp> [yaw] [pitch] [zoom%]: local p-1's camera, right away
      int i = PlayerArg(arg1);
      Hd3dCamState c = { 0.0f, 45.0f, 1.0f, false };
      if (!strcmp(arg2, "fp")) {
        // (By default it looks where the Link faces, a little down.)
        static const float kFacingYaw[4] = { 0.0f, 180.0f, -90.0f, 90.0f };  // RAM 0x2F: up, down, left, right
        int p = PlayerOfLocal(i);
        SwitchToPlayer(p);
        c.first_person = true;
        c.yaw = Inst_OfPlayer(p) >= 0 ? kFacingYaw[*Mp_PlayerRam(p, 0x2F) >> 1 & 3] : 0.0f;
        c.pitch = 8.0f;
      } else if (strcmp(arg2, "tp")) {
        printf("FAIL: cam3d wants tp or fp, not '%s'\n", arg2);
        g_failures++;
        continue;
      }
      if (arg3[0])
        c.yaw = (float)atof(arg3);
      if (arg4[0])
        c.pitch = (float)atof(arg4);
      if (arg5[0])
        c.zoom = (float)atof(arg5) / 100.0f;
      Hd3d_SetCamera(i, &c);
    } else if (!strcmp(cmd, "cam3dstep")) {
      // cam3dstep <ms>: the cameras move on (springs, sticks) in 16 ms steps
      for (int ms = atoi(arg1); ms > 0; ms -= 16)
        Hd3d_Update(0.016f);
    } else if (!strcmp(cmd, "stick3d")) {
      // stick3d <p> <x%> <y%>: local p-1's right stick, until set back to 0 0
      Hd3d_SetRightStick(PlayerArg(arg1), IntMin(IntMax(atoi(arg2), -100), 100) / 100.0f,
                         IntMin(IntMax(atoi(arg3), -100), 100) / 100.0f);
    } else if (!strcmp(cmd, "viewbutton")) {
      // viewbutton <p> [hold ms]: local p-1 taps its View button (or holds it that long)
      Hd3d_PressViewButton(PlayerArg(arg1), arg2[0] ? atoi(arg2) : 100);
    } else if (!strcmp(cmd, "click")) {
      // click <left|middle|right> [shift|ctrl|alt|double|touch|down|up]: a mouse
      // click for player 1 (down / up: only the press / the release)
      int b = !strcmp(arg1, "left") ? SDL_BUTTON_LEFT : !strcmp(arg1, "middle") ? SDL_BUTTON_MIDDLE :
              !strcmp(arg1, "right") ? SDL_BUTTON_RIGHT : -1;
      uint32 mods = !strcmp(arg2, "shift") ? KMOD_LSHIFT : !strcmp(arg2, "ctrl") ? KMOD_LCTRL :
                    !strcmp(arg2, "alt") ? KMOD_LALT : 0;
      bool touch = !strcmp(arg2, "touch"), press = strcmp(arg2, "up") != 0, release = strcmp(arg2, "down") != 0;
      int clicks = !strcmp(arg2, "double") ? 2 : 1;
      if (b < 0 || (arg2[0] && !mods && !touch && clicks == 1 && press && release)) {
        printf("FAIL: click %s %s?\n", arg1, arg2);
        g_failures++;
        continue;
      }
      if (press)
        Hd3d_OnMouseButton(b, true, clicks, mods, touch);
      if (release)
        Hd3d_OnMouseButton(b, false, clicks, mods, touch);
    } else if (!strcmp(cmd, "wheel")) {
      // wheel <steps>: player 1's mouse wheel (+ zooms in)
      Hd3d_OnMouseWheel(atoi(arg1));
    } else if (!strcmp(cmd, "motion")) {
      // motion <dx> <dy>: player 1's mouse moves
      Hd3d_OnMouseMotion(atoi(arg1), atoi(arg2));
    } else if (!strcmp(cmd, "focus")) {
      // focus <0|1>: the window loses or gets the focus
      Hd3d_OnWindowFocus(atoi(arg1) != 0);
    } else if (!strcmp(cmd, "key3d")) {
      // key3d <toggle|left|right|up|down|reset> [0|1]: a 3D view hotkey pressed (1), let go (0) or both
      static const char *const kNames[6] = { "toggle", "left", "right", "up", "down", "reset" };
      int k = 0;
      while (k < 6 && strcmp(arg1, kNames[k]))
        k++;
      if (k == 6) {
        printf("FAIL: no 3D view hotkey '%s'\n", arg1);
        g_failures++;
        continue;
      }
      if (!arg2[0] || atoi(arg2))
        Hd3d_OnKeyCommand(kKeys_View3d + k, true);
      if (!arg2[0] || !atoi(arg2))
        Hd3d_OnKeyCommand(kKeys_View3d + k, false);
    } else if (!strcmp(cmd, "pad")) {
      // pad <c> <button> <0|1>: a button of controller c, through the bindings
      int b = -1;
      for (int k = 0; k < kGamepadBtn_Count; k++)
        if (!SDL_strcasecmp(Bindings_GamepadButtonName(k), arg2))
          b = k;
      if (b < 0 || !arg3[0]) {
        printf("FAIL: pad %s %s %s: no such button (A B X Y Back Guide Start L3 R3 LB RB DpadUp ... LT RT)\n",
               arg1, arg2, arg3);
        g_failures++;
        continue;
      }
      Bindings_OnPadButton(atoi(arg1), b, atoi(arg3) != 0);
    } else if (!strcmp(cmd, "rstick")) {
      // rstick <c> <x%> <y%>: controller c's right stick, through the bindings
      int x = IntMin(IntMax(atoi(arg2), -100), 100), y = IntMin(IntMax(atoi(arg3), -100), 100);
      Bindings_OnPadRightAxis(atoi(arg1), x * 32767 / 100, y * 32767 / 100);
    } else if (!strcmp(cmd, "remap3d")) {
      g_remap3d = atoi(arg1) != 0;
    } else if (!strcmp(cmd, "drawtick")) {
      g_drawtick = atoi(arg1) != 0;
    } else if (!strcmp(cmd, "widescreen")) {
      // widescreen <0-96>: later pictures are that many columns wider on each side
      int n = atoi(arg1);
      if (n < 0 || n > kPpuExtraLeftRight) {
        printf("FAIL: widescreen %s: 0 to %d columns\n", arg1, kPpuExtraLeftRight);
        g_failures++;
        continue;
      }
      g_draw_extra_left_right = n;
      width = 256 + 2 * n;
      if ((size_t)width * height * 16 > pixels_size) {
        free(pixels);
        pixels_size = (size_t)width * height * 16;
        pixels = calloc(pixels_size, 4);
      }
    } else if (!strcmp(cmd, "drawuntil")) {
      // drawuntil <hexmodule> <hexsub> <max>: runs and draws frame by frame
      // (as drawrun) until the viewed world is in that module and submodule
      int m = (int)strtol(arg1, NULL, 16), sub = (int)strtol(arg2, NULL, 16), max = atoi(arg3), k = 0;
      bool there = false;
      for (;; k++, frame++) {
        int home = Inst_Current(), w = Inst_ViewOfPlayer(view);
        if (w >= 0)
          Inst_Switch(w);
        there = w >= 0 && main_module_index == m && submodule_index == sub;
        Inst_Switch(home);
        if (there || k >= max)
          break;
        uint16 in[kMpMaxPlayers];
        HeldInputs(held, in);
        Inst_RunFrame(in);
        DrawView(view, pixels, width, render_flags);
      }
      if (!there) {
        printf("FAIL: the viewed world didn't get to module %s/%s in %s frames (at frame %d)\n", arg1, arg2, arg3,
               frame);
        g_failures++;
      } else {
        printf("drawuntil %s %s: after %d frames\n", arg1, arg2, k);
      }
    } else if (!strcmp(cmd, "mark")) {
      // mark: a snapshot of every world (rewind goes back to it)
      if (!g_mark)
        g_mark = Inst_SnapshotNew();
      Inst_SnapshotSave(g_mark);
    } else if (!strcmp(cmd, "rewind")) {
      if (!g_mark) {
        printf("FAIL: rewind without mark\n");
        g_failures++;
        continue;
      }
      Inst_SnapshotLoad(g_mark);
    } else if (!strcmp(cmd, "markpos")) {
      // markpos <p>: where player p is now (expect_moved compares)
      int p = PlayerArg(arg1);
      g_markpos_set[p] = Inst_PlayerPos(p, &g_markpos[p][0], &g_markpos[p][1]) >= 0;
      if (!g_markpos_set[p]) {
        printf("FAIL: P%d isn't playing (markpos) at frame %d\n", p + 1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_moved")) {
      // expect_moved <p> <north|south|east|west>: at least 8 pixels that way
      // since markpos, and less than 4 pixels sideways
      int p = PlayerArg(arg1), dir = !strcmp(arg2, "north") ? 0 : !strcmp(arg2, "south") ? 1 :
                                      !strcmp(arg2, "east") ? 2 : !strcmp(arg2, "west") ? 3 : -1;
      uint16 x, y;
      if (dir < 0 || !g_markpos_set[p] || Inst_PlayerPos(p, &x, &y) < 0) {
        printf("FAIL: expect_moved %s %s: no such direction, no markpos or not playing\n", arg1, arg2);
        g_failures++;
        continue;
      }
      int dx = (int16)(x - g_markpos[p][0]), dy = (int16)(y - g_markpos[p][1]);
      int along = dir == 0 ? -dy : dir == 1 ? dy : dir == 2 ? dx : -dx, across = dir < 2 ? dx : dy;
      printf("P%d moved %d,%d since markpos\n", p + 1, dx, dy);
      if (along < 8 || abs(across) >= 4) {
        printf("FAIL: P%d moved %d,%d, expected 8 or more %s (under 4 across) at frame %d\n", p + 1, dx, dy, arg2,
               frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "hashmark") || !strcmp(cmd, "expect_hashmark")) {
      // hashmark <name>: remember the state hash; expect_hashmark <name>: it is the same now
      int k = 0;
      while (k < g_nhashmarks && strcmp(g_hashmarks[k].name, arg1))
        k++;
      uint32 h = Inst_HashState();
      if (!strcmp(cmd, "hashmark")) {
        if (k == g_nhashmarks && k < (int)countof(g_hashmarks))
          snprintf(g_hashmarks[g_nhashmarks++].name, sizeof(g_hashmarks[0].name), "%s", arg1);
        if (k < g_nhashmarks)
          g_hashmarks[k].hash = h;
        printf("hashmark %s: %08x\n", arg1, h);
      } else if (k == g_nhashmarks) {
        printf("FAIL: no hashmark %s\n", arg1);
        g_failures++;
      } else if (g_hashmarks[k].hash != h) {
        printf("FAIL: state hash %08x, hashmark %s was %08x at frame %d\n", h, arg1, g_hashmarks[k].hash, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "dump3d")) {
      // dump3d <file.ppm>: (debug) what the 3D view made of the viewed world
      Hd3d_DumpScene(arg1);
      printf("wrote %s\n", arg1);
    } else if (!strcmp(cmd, "expect_3d")) {
      // expect_3d <0|1> [reason]: draws the viewed player's view as one pane
      int scale = DrawView(view, pixels, width, render_flags);
      bool is3d = Hd3d_PaneIs3D(0);
      const char *why = Hd3d_LastWhy2D(0);
      printf("expect_3d: %s (%s, scale %d)\n", is3d ? "3D" : "2D", why, scale);
      if (is3d != (atoi(arg1) != 0) || (!is3d && arg2[0] && strcmp(why, arg2))) {
        printf("FAIL: the view was drawn in %s (why 2D: %s), expected %s %s at frame %d\n", is3d ? "3D" : "2D", why,
               atoi(arg1) ? "3D" : "2D", arg2, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_exact3d")) {
      // expect_exact3d <max %> <tolerance>: at most that many percent of the
      // pixels differ by more than the tolerance (any channel) between the 2D
      // picture and the 3D one seen through the parallel oblique test camera
      bool hd2d_on = g_hd2d.enabled, hd3d_on = g_hd3d.enabled;
      size_t npx = (size_t)width * height;
      uint32 *a = calloc(npx * 16, 4), *b = calloc(npx * 16, 4);
      g_hd2d.enabled = false;
      Hd3d_SetEnabled(false);
      // (The 2D picture as the PPU draws it.)
      int home = Inst_Current();
      if (Inst_ViewOfPlayer(view) >= 0)
        Inst_Switch(Inst_ViewOfPlayer(view));
      if (g_draw_extra_left_right >= 0)
        g_zenv.ppu->extraLeftRight = g_draw_extra_left_right;
      int scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
      g_mp_local_mask = 1 << view;
      Hd2d_Draw2D((uint8 *)a, (size_t)width * scale * 4, render_flags, 0);
      Inst_Switch(home);
      Hd3d_SetEnabled(true);
      Hd3d_SetTestCamera(true);
      DrawView(view, b, width, render_flags);
      bool is3d = Hd3d_PaneIs3D(0);
      const char *why = Hd3d_LastWhy2D(0);
      Hd3d_SetTestCamera(false);
      Hd3d_SetEnabled(hd3d_on);
      g_hd2d.enabled = hd2d_on;
      int diff = 0, tol = atoi(arg2);
      for (size_t k = 0; k < npx && scale == 1; k++) {
        uint32 p = a[k], q = b[k];
        diff += abs((int)(p >> 16 & 0xff) - (int)(q >> 16 & 0xff)) > tol ||
                abs((int)(p >> 8 & 0xff) - (int)(q >> 8 & 0xff)) > tol || abs((int)(p & 0xff) - (int)(q & 0xff)) > tol;
      }
      double pct = 100.0 * diff / (double)npx;
      printf("expect_exact3d: %s, %.2f%% of the pixels differ\n", is3d ? "3D" : "2D", pct);
      if (!is3d || scale != 1 || pct > atof(arg1)) {
        if (!is3d || scale != 1)
          printf("FAIL: the test camera's picture wasn't drawn in 3D (why 2D: %s) at frame %d\n", why, frame);
        else
          printf("FAIL: the 3D picture differs from the 2D one in %.2f%% of the pixels (limit %s%%) at frame %d; "
                 "see tests/out/exact3d_diff.ppm\n", pct, arg1, frame);
        g_failures++;
        if (scale == 1) {
          WritePpm("tests/out/exact3d_2d.ppm", width, height, a, width);
          WritePpm("tests/out/exact3d_3d.ppm", width, height, b, width);
          for (size_t k = 0; k < npx; k++) {
            uint32 p = a[k], q = b[k];
            int l = ((p >> 16 & 0xff) + (p >> 8 & 0xff) + (p & 0xff)) / 6;
            bool changed = abs((int)(p >> 16 & 0xff) - (int)(q >> 16 & 0xff)) > tol ||
                           abs((int)(p >> 8 & 0xff) - (int)(q >> 8 & 0xff)) > tol ||
                           abs((int)(p & 0xff) - (int)(q & 0xff)) > tol;
            a[k] = changed ? 0xff0000 : (uint32)(l << 16 | l << 8 | l);
          }
          WritePpm("tests/out/exact3d_diff.ppm", width, height, a, width);
        }
      }
      free(a);
      free(b);
    } else if (!strcmp(cmd, "expect_drawpure")) {
      // expect_drawpure <n>: after one settling draw, n more draws of the
      // viewed player's view (in 3D when it is on) change nothing of the game
      DrawView(view, pixels, width, render_flags);
      uint32 before = DrawPureHash();
      int n = atoi(arg1), drawn3d = 0;
      for (int k = 0; k < n; k++) {
        DrawView(view, pixels, width, render_flags);
        drawn3d += Hd3d_PaneIs3D(0);
      }
      uint32 after = DrawPureHash();
      printf("expect_drawpure: %d of %d draws in 3D, %08x -> %08x\n", drawn3d, n, before, after);
      if (before != after) {
        printf("FAIL: drawing changed the game's state or its video chip (%08x -> %08x) at frame %d\n", before, after,
               frame);
        g_failures++;
      }
      if (g_hd3d.enabled && drawn3d != n) {
        printf("FAIL: only %d of %d draws were in 3D (why 2D: %s) at frame %d\n", drawn3d, n, Hd3d_LastWhy2D(0), frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "drawguard")) {
      // drawguard <n>: runs n frames, drawing each into the middle of a buffer
      // three times as wide (and 2 rows higher and lower) filled with a
      // pattern, the way split screen draws (the world read first, 3D drawn
      // after), then the overlay. Fails if anything was written outside the pane.
      for (int n = atoi(arg1); n > 0; n--, frame++) {
        uint16 in[kMpMaxPlayers];
        HeldInputs(held, in);
        Inst_RunFrame(in);
        int home = Inst_Current();
        Inst_Switch(Inst_ViewOfPlayer(view));
        int scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
        int w = width * scale, h = height * scale, pitch = 3 * w;
        size_t total = (size_t)pitch * (h + 4), bad = 0;
        uint32 *buf = malloc(total * 4);
        for (size_t k = 0; k < total; k++)
          buf[k] = 0xdeadbeef;
        uint32 *pane = buf + 2 * pitch + w;
        g_mp_local_mask = 1 << view;
        Ui_SetViews(&(UiView){ 0, 0, width, height, view }, 1);
        int8 local = (int8)LocalOfPlayer(view);
        Hd3d_SetPanes(&local, 1);
        Hd3d_BeginPanes(true);
        Hd2d_DrawFrame((uint8 *)pane, (size_t)pitch * 4, render_flags, 0);
        Hd3d_FinishPanes();
        Ui_Draw((uint8 *)pane, pitch * 4, w, h, scale);
        for (int y = 0; y < h + 4; y++)
          for (int x = 0; x < pitch; x++)
            bad += (y < 2 || y >= h + 2 || x < w || x >= 2 * w) && buf[(size_t)y * pitch + x] != 0xdeadbeef;
        free(buf);
        if (bad) {
          printf("FAIL: drawing frame %d (module %d/%d, scale %d, %s) wrote %d pixels outside the pane\n", frame,
                 main_module_index, submodule_index, scale, Hd3d_PaneIs3D(0) ? "3D" : "2D", (int)bad);
          g_failures++;
        }
        Inst_Switch(home);
        if (bad)
          break;
      }
    } else if (!strcmp(cmd, "expect_remap")) {
      // expect_remap <p> <keys> <keys>: local p-1's d-pad as its camera turns
      // it (with every check the game does; it remembers, as in the game)
      int i = PlayerArg(arg1);
      uint16 in = ParseKeys(arg2), want = ParseKeys(arg3);
      uint16 got = Hd3d_RemapLocal(i, Session_GetStatus()->local_slot[i], in);
      if (got != want) {
        printf("FAIL: local P%d's %s turned into %03x, expected %s (%03x) at frame %d\n", i + 1, arg2, got, arg3, want,
               frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_fp") || !strcmp(cmd, "expect_cam3d") || !strcmp(cmd, "expect_zoom3d")) {
      // expect_fp <p> <0|1> / expect_cam3d <p> <yaw> [pitch] [tolerance] /
      // expect_zoom3d <p> <zoom%> [tolerance%]: local p-1's camera
      Hd3dCamState c;
      Hd3d_GetCamera(PlayerArg(arg1), &c);
      bool ok;
      if (!strcmp(cmd, "expect_fp")) {
        ok = c.first_person == (atoi(arg2) != 0);
      } else if (!strcmp(cmd, "expect_zoom3d")) {
        ok = fabsf(c.zoom * 100.0f - (float)atof(arg2)) <= (arg3[0] ? (float)atof(arg3) : 1.0f);
      } else {
        float tol = arg4[0] ? (float)atof(arg4) : 0.5f;
        ok = fabsf(Wrap180(c.yaw - (float)atof(arg2))) <= tol &&
             (!arg3[0] || fabsf(c.pitch - (float)atof(arg3)) <= tol);
      }
      printf("%s: P%s's camera %s yaw %.2f pitch %.2f zoom %.3f\n", cmd, arg1, c.first_person ? "fp" : "tp",
             c.yaw, c.pitch, c.zoom);
      if (!ok) {
        printf("FAIL: %s %s %s %s %s doesn't hold at frame %d\n", cmd, arg1, arg2, arg3, arg4, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_panes")) {
      // expect_panes <n>: the panes the game would draw now (a pane per world
      // seen, and one for each local player in first person)
      int views[kInstMax];
      int8 pane_local[kInstMax];
      int n = Hd3d_ComputeViews(views, pane_local, kInstMax);
      printf("panes:");
      for (int v = 0; v < n; v++)
        printf(" world %d (local P%d)", views[v], pane_local[v] + 1);
      printf("\n");
      if (n != atoi(arg1)) {
        printf("FAIL: %d panes, expected %s at frame %d\n", n, arg1, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_input")) {
      // expect_input <p> <keys>: what local p-1's keys and controller give the game
      int i = PlayerArg(arg1);
      uint16 got = Bindings_GetInput(i), want = ParseKeys(arg2);
      if (got != want) {
        printf("FAIL: local P%d's input is %04x, expected %s (%04x) at frame %d\n", i + 1, got, arg2, want, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_view")) {
      // expect_view <p> <0|1>: local p-1's View button (first person) is held
      int i = PlayerArg(arg1);
      bool down = Bindings_ButtonDown(i, kBind_View);
      if (down != (atoi(arg2) != 0)) {
        printf("FAIL: local P%d's View button is %s at frame %d\n", i + 1, down ? "down" : "up", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_luma")) {
      // expect_luma <x0> <y0> <x1> <y1> <min> <max>: draws the viewed player's
      // view; the average brightness of pixels x0..x1-1, y0..y1-1 (0-255)
      int scale = DrawView(view, pixels, width, render_flags);
      int x0 = IntMax(atoi(arg1), 0), y0 = IntMax(atoi(arg2), 0);
      int x1 = IntMin(atoi(arg3), width), y1 = IntMin(atoi(arg4), height);
      int64 sum = 0, n = 0;
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++, n++) {
          uint32 c = pixels[(size_t)y * scale * width * scale + (size_t)x * scale];
          sum += ((c >> 16 & 0xff) * 299 + (c >> 8 & 0xff) * 587 + (c & 0xff) * 114) / 1000;
        }
      }
      int luma = n ? (int)(sum / n) : -1;
      printf("expect_luma %d,%d-%d,%d: %d (%s)\n", x0, y0, x1, y1, luma, Hd3d_PaneIs3D(0) ? "3D" : "2D");
      if (!n || luma < atoi(arg5) || luma > atoi(arg6)) {
        printf("FAIL: average luma %d, expected %s-%s at frame %d\n", luma, arg5, arg6, frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_mouse_captured")) {
      // expect_mouse_captured <0|1>: the game would capture the mouse (mouse look, right drag)
      if (Hd3d_WantsRelativeMouse() != (atoi(arg1) != 0)) {
        printf("FAIL: the mouse is %scaptured at frame %d\n", Hd3d_WantsRelativeMouse() ? "" : "not ", frame);
        g_failures++;
      }
    } else if (!strcmp(cmd, "expect_3dstats")) {
      // expect_3dstats <min quads> <min cards>: draws the viewed player's view
      DrawView(view, pixels, width, render_flags);
      int quads = 0, cards = 0;
      Hd3d_GetStats(0, &quads, &cards);
      printf("expect_3dstats: %d quads, %d cards (%s)\n", quads, cards, Hd3d_PaneIs3D(0) ? "3D" : Hd3d_LastWhy2D(0));
      if (quads < atoi(arg1) || cards < atoi(arg2)) {
        printf("FAIL: %d quads and %d cards drawn, expected at least %s and %s at frame %d\n", quads, cards, arg1, arg2,
               frame);
        g_failures++;
      }
    } else {
      printf("FAIL: unknown command %s\n", cmd);
      g_failures++;
    }
  }
  fclose(f);
  free(pixels);
  Inst_SnapshotFree(g_mark);
  g_mark = NULL;
  printf(g_failures ? "MPTEST FAILED\n" : "MPTEST OK\n");
  return g_failures != 0;
}
