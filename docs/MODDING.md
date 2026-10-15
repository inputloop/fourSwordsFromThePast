# Modding Guide

*A hands-on guide to changing and extending Four Swords of the Past.*

This guide is for you if you know some C (or are learning it) and want to change
how the game plays, looks or feels. It shows where things live, the few rules
that keep co-op and online play working, and step-by-step recipes you can copy.
Each recipe names the exact files to open and includes a small code sketch
based on the real code.

For how the mod works on the inside, in plain language, read
[HOW_THE_MOD_WORKS.md](HOW_THE_MOD_WORKS.md). For co-op and netplay internals,
read the "How it works (for modders)" part of [MULTIPLAYER.md](../MULTIPLAYER.md).

## Contents

1. [What you need](#1-what-you-need)
2. [A map of the code](#2-a-map-of-the-code)
3. [The golden rules](#3-the-golden-rules)
4. [Recipes](#4-recipes)
   - [4.1 Change a game constant: Link's walking speed](#41-change-a-game-constant-links-walking-speed)
   - [4.2 Add a gameplay option (ini key + menu entry)](#42-add-a-gameplay-option-ini-key--menu-entry)
   - [4.3 Add an on-screen overlay](#43-add-an-on-screen-overlay)
   - [4.4 Add a hotkey](#44-add-a-hotkey)
   - [4.5 Change a randomizer logic rule](#45-change-a-randomizer-logic-rule)
   - [4.6 Tweak the 2D-HD look](#46-tweak-the-2d-hd-look)
   - [4.7 Tweak the 3D view](#47-tweak-the-3d-view)
   - [4.8 Add or change a co-op mechanic](#48-add-or-change-a-co-op-mechanic)
5. [Testing your mod](#5-testing-your-mod)
6. [Debugging tips](#6-debugging-tips)
7. [Sharing your mod](#7-sharing-your-mod)
8. [Glossary and where to learn more](#8-glossary-and-where-to-learn-more)

---

## 1. What you need

### The ROM: bring your own, never commit it

The code in this repository does not contain any of Nintendo's game data. You
need your own copy of *The Legend of Zelda: A Link to the Past* (US version)
as a ROM file. A small Python script turns it into `zelda3_assets.dat`, which
the game reads at startup.

Everything made from the ROM belongs to Nintendo. **Never commit or share any
of these files** (most are already in `.gitignore`, but check before you push):

| File | What it is |
| --- | --- |
| `zelda3.sfc`, `*.smc` (any `.sfc` / `.smc`) | The ROM itself |
| `zelda3_assets.dat` | Graphics, sound, levels and text, extracted from the ROM |
| `assets/dialogue.txt` (and `assets/dialogue*.txt`) | The game's text, extracted |
| `assets/dungeon/*.yaml`, `assets/overworld/*.yaml` | Level data, extracted |
| `assets/img/`, `assets/sound/` and the other files listed in `assets/.gitignore` | More extracted data |
| `tests/baseline/*` | Reference screenshots made from your own ROM's graphics |
| `tests/out/*` | Test logs and screenshots |
| `saves/*.sav`, `saves/sram.dat` | Your own save files |

(The project does keep a few screenshots in `docs/screenshots/` for the
README. That was the maintainers' choice. Data files made from the ROM are
never committed.)

### Building on Linux and macOS

You need a C compiler, SDL2, Python 3 and the Python libraries Pillow and
PyYAML.

```sh
# Ubuntu/Debian (Fedora: SDL2-devel; Arch: sdl2; macOS: brew install sdl2)
sudo apt-get install libsdl2-dev python3-pil python3-yaml   # or: pip install -r requirements.txt
cp /path/to/your/Legend_of_Zelda_USA.sfc zelda3.sfc
make -j          # builds ./zelda3 and, the first time, extracts zelda3_assets.dat
./zelda3
```

Good to know about the `Makefile`:

- It compiles every `src/*.c` and `snes/*.c` file (a wildcard), so a new `.c`
  file in `src/` is picked up by itself.
- It builds with `-O2 -Werror`: **any compiler warning stops the build.**
  Fix warnings, don't silence them.
- To build with debug info, set `CFLAGS` in the environment (it replaces the
  default `-O2 -Werror`): `CFLAGS="-O0 -g" make -j`.
- `make clean all` rebuilds everything. `CC=clang make` picks another compiler.
- The `zelda3_assets.dat` rule runs `python3 assets/restool.py --extract-from-rom`.

If you change extracted assets (for example a level's YAML), rebuild the data
file with `python3 assets/restool.py` (it still needs the ROM next to it). See
[BUILDING.md](../BUILDING.md) for more, including other languages.

### Building on Windows

There are three ways:

- **Cross build from Linux with MinGW** (this is what CI uses):

  ```sh
  sudo apt-get install gcc-mingw-w64-x86-64
  tools/build_windows_mingw.sh
  ```

  It downloads SDL2, builds `build-windows/zelda3.exe` and puts a
  ready-to-play folder in `build-windows/dist/`. Like the Makefile, it
  compiles `src/*.c` with a wildcard.
- **Visual Studio**: open `Zelda3.sln` (the project is `zelda3.vcxproj`; SDL2
  comes from NuGet, see `packages.config`). The project lists every source
  file by name, so **a new `.c` file must also be added to `zelda3.vcxproj`
  (and `zelda3.vcxproj.filters`)**.
- **`run_with_tcc.bat`**: downloads a small compiler and builds on the
  player's own PC.

To make `zelda3_assets.dat` on Windows, put your ROM next to the game and run
`extract_assets.bat` (it needs Python 3 once).

---

## 2. A map of the code

The code comes in two layers:

- **The game itself.** snesrev's zelda3 is a C rewrite of the original game,
  function by function. Most function names come from the community's
  disassemblies, and many functions carry a comment with the original ROM
  address, like `void Link_HandleVelocity() {  // 87e245`. The mod changes
  this code only where it has to.
- **The mod layers.** Co-op, world instances, netplay, menus, the
  randomizer, Archipelago, 2D-HD and the 3D view. These are new files, plus
  small hooks in the game code.

### Where things live

| File(s) | Layer | What lives there |
| --- | --- | --- |
| [`src/player.c`](../src/player.c), [`player_oam.c`](../src/player_oam.c) | game | Link: movement, items, sword, how he is drawn |
| [`src/sprite_main.c`](../src/sprite_main.c), [`sprite.c`](../src/sprite.c), [`overlord.c`](../src/overlord.c) | game | Enemies and NPCs, damage, spawners |
| [`src/ancilla.c`](../src/ancilla.c) | game | Projectiles and effects (arrows, bombs, boomerang, hookshot...) |
| [`src/dungeon.c`](../src/dungeon.c), [`overworld.c`](../src/overworld.c) | game | Rooms, doors, chests, the overworld and its transitions |
| [`src/hud.c`](../src/hud.c), [`messaging.c`](../src/messaging.c), [`select_file.c`](../src/select_file.c) | game | The HUD and item menu, text boxes and maps, the file select screen |
| [`src/tagalong.c`](../src/tagalong.c), [`ending.c`](../src/ending.c), [`attract.c`](../src/attract.c) | game | Followers, the ending, the title and opening |
| [`src/misc.c`](../src/misc.c), [`nmi.c`](../src/nmi.c), [`load_gfx.c`](../src/load_gfx.c), [`tile_detect.c`](../src/tile_detect.c) | game | Module routing, the random number generator, video uploads, tile collision |
| [`src/variables.h`](../src/variables.h) | game | Names for RAM addresses (`#define link_x_coord (*(uint16*)(g_ram+0x22))`) |
| [`src/zelda_rtl.c`](../src/zelda_rtl.c) | glue | Runs one game frame (`ZeldaRunFrame`), draws it (`ZeldaDrawPpuFrame`), state hash |
| [`snes/`](../snes/) | glue | The SNES picture and sound chips (PPU, DSP, APU), DMA |
| [`src/main.c`](../src/main.c) | glue | Window, input, the main loop, hotkeys (`HandleCommand`) |
| [`src/config.c`](../src/config.c), [`config.h`](../src/config.h) | glue | Reading `zelda3.ini` and `zelda3.coop.ini`, hotkey names |
| [`src/features.h`](../src/features.h) | glue | Optional game features (`kFeatures0_*` bits) |
| [`src/mp.c`](../src/mp.c), [`mp.h`](../src/mp.h) | mod | Up to four Links: the Link context swap, leader, revives, carrying, pings, co-op rules |
| [`src/instance.c`](../src/instance.c), [`instance.h`](../src/instance.h) | mod | World instances: split, merge, shared progress, snapshots, `Inst_HashState` |
| [`src/session.c`](../src/session.c), [`session.h`](../src/session.h) | mod | Netplay: frames, rollback, joining, reconnecting, room codes |
| [`src/net.c`](../src/net.c), [`upnp.c`](../src/upnp.c) | mod | UDP sockets, UPnP port mapping, STUN |
| [`src/bindings.c`](../src/bindings.c) | mod | Per-player keys and controller buttons |
| [`src/ui.c`](../src/ui.c), [`ui.h`](../src/ui.h) | mod | The overlay: menus, corner HUD, arrows, pings, mini map, name tags, the check tracker page (`Ui_TrackerShown`), "Watch everyone" panes (`Ui_AddWatchedPanes`) |
| [`src/screenshot.c`](../src/screenshot.c) | mod, display only | The screenshot keys (F12, Ctrl+F12) |
| [`src/story.c`](../src/story.c) | mod | The Four Swords opening story |
| [`src/randomizer.c`](../src/randomizer.c) | mod | The item randomizer and its logic; `Rando_Tracker` counts checks for the tracker page |
| [`src/archipelago.c`](../src/archipelago.c) | mod | Archipelago, game side: save data, item delivery, checks, DeathLink |
| [`src/ap_client.c`](../src/ap_client.c), [`ap_ws.c`](../src/ap_ws.c), [`ap_tls.c`](../src/ap_tls.c), [`ap_json.c`](../src/ap_json.c) | mod | Archipelago network client (own thread, WebSocket, TLS, JSON) |
| [`archipelago/fsotp/`](../archipelago/fsotp/) | mod | The Archipelago world in Python (`rules.py` mirrors the randomizer's logic) |
| [`src/hd2d.c`](../src/hd2d.c), [`hd2d.h`](../src/hd2d.h) | mod, display only | The 2D-HD post pass |
| [`src/hd3d.c`](../src/hd3d.c), [`hd3d_world.c`](../src/hd3d_world.c), [`hd3d_sprites.c`](../src/hd3d_sprites.c), [`hd3d_linkpose.c`](../src/hd3d_linkpose.c), [`hd3d_input.c`](../src/hd3d_input.c) | mod, display only | The 3D view: scene building, sprite cards, Link poses, camera input, the outdoor sky (`hd3d.c`) |
| [`src/r3d.c`](../src/r3d.c), [`cam3d.c`](../src/cam3d.c) | mod, display only | The software rasterizer and the camera math |
| [`src/mptest.c`](../src/mptest.c) | tests | The headless test runner (`./zelda3 --mptest`) |

### How one frame flows

This is the main loop in `src/main.c`, in order:

1. **Events.** SDL keyboard, mouse and controller events. A key goes to the
   menu first (`Ui_HandleKey`), then to the player bindings, then to hotkeys
   (`HandleInput` calls `HandleCommand`).
2. **Read inputs.** `Bindings_GetInput(i)` gives a 16-bit joypad word for each
   local player. A controller pressing Start joins (`Session_LocalJoin`).
3. **`Ui_Update`.** The overlay menu takes the inputs it uses (it clears them).
4. **3D cameras.** `Hd3d_Update`, `Hd3d_HandleViewButtons`, then
   `Hd3d_RemapInputs`, which turns the d-pad for a turned camera. This
   happens **before** the inputs are sent, so only the turned buttons are
   ever sent, recorded or replayed.
5. **`Session_Tick`** (under `g_audio_mutex`), unless the menu pauses a solo
   game. It builds (or receives) an `MpFrameInput` for the frame and runs it:
   - `RunFrame` in `src/session.c` applies joins and leaves and the frame's
     settings (friendly fire, story, randomizer, full inventory, Archipelago,
     game features), then calls `Inst_RunFrame`.
   - `Inst_RunFrame` (`src/instance.c`) runs every world in turn. For each
     one it calls `ZeldaRunFrame` with the leader's input.
   - Inside, `ZeldaRunGameLoop` (`src/zelda_rtl.c`) calls `Mp_BeginFrame`,
     then `Mp_RunRouting(&Module_MainRouting)`, which runs the game module
     (`kMainRouting[main_module_index]` in `src/misc.c`), then `Mp_EndFrame`.
     The overworld and dungeon modules call `Mp_RunOtherPlayers` to move the
     other Links.
   - After all worlds ran: menus that closed are folded back, worlds that
     met are merged, and the shared save area is synced.
6. **`Ap_Tick`.** Archipelago sends what the confirmed frames did.
7. **Draw** (`DrawPpuFrameWithPerf`). For each pane: `Hd2d_DrawFrame`, which
   first asks `Hd3d_DrawFrame` (3D view). If that didn't draw the pane,
   `Hd2d_Draw2D` runs `ZeldaDrawPpuFrame` (the original picture) and adds the
   2D-HD pass when it is on. Finally `Ui_Draw` paints the overlay on top
   (the screenshot keys grab the picture just before and just after it).

Steps 1 to 4 and 6 to 7 happen on this computer only. Step 5 is the
simulation, and it must run the same on every computer.

---

## 3. The golden rules

### Rule 1: the simulation must be deterministic

Online play sends only button presses. Every computer runs the same frames
and must end up with exactly the same game. Once a second the host sends a
hash of its state, and a client that differs downloads a fresh copy. Rollback
also saves and reloads the state many times a second. So:

**What counts as game state.** Anything that is hashed and snapshotted:

- `g_ram`, the 128 KB block of game memory (every world has its own copy).
  `ZeldaHashState` in `src/zelda_rtl.c` hashes all of it (minus a few
  skipped bytes) **plus every byte of `g_mp`**. `Inst_HashState` combines this
  over all worlds.
- `g_mp`, the `MpState` struct in `src/mp.h`: co-op state that the original
  game doesn't have (leader, carrying, revives, pings, per-player Link
  contexts...). `Mp_SaveLoad` saves the whole struct.
- The rest of each world's snapshot: PPU, DMA, SRAM and sound state
  (`Inst_SaveState`, `Inst_SnapshotSave`).

**What may change game state.** Only code that runs inside `Session_Tick`,
and only from:

- the joypad words of the frame (`MpFrameInput.inputs`),
- the settings that travel with the frame (friendly fire, game features,
  randomizer, full inventory, story, Archipelago events),
- the current state itself.

**Where randomness comes from.** Use the game's own generator,
`GetRandomNumber()` in `src/misc.c`. It works from `frame_counter` and a RAM
byte, so it is the same everywhere. The randomizer has its own seeded `Rng`
in `src/randomizer.c`. **Never** use `rand()`, `time()`, `SDL_GetTicks()`,
thread timing, uninitialized memory or pointer values in the simulation.
Prefer integer math there, as the game does.

**No hidden state.** Don't keep game state in a C `static` variable, a
global outside `g_ram`/`g_mp`, or a heap object. It would not be rolled
back, saved or hashed, and clients would drift apart. Put it in `g_mp` (see
[recipe 4.8](#48-add-or-change-a-co-op-mechanic)).

**Display only.** Code that only draws (the overlay in `src/ui.c`, the 2D-HD
pass, the 3D view) may read game state but must never write it. Display
state lives outside `g_ram` and `g_mp`: for example `g_mp_render`,
`g_mp_draw_offset`, `g_hd2d`, `g_hd3d` and the statics in `ui.c`. Display
code can use real time (`SDL_GetTicks()`) and floats freely. Each player can
then choose their own display settings, also online.

The 3D view shows how strict this is: `hd3d_linkpose.c` runs the game's own
Link drawing code again and then puts back the first 4 KB of RAM and `g_mp`
byte for byte. `tests/coop/hd3d_pure.txt` and `tests/coop/hd3d_linkpose.txt`
check that the state hash doesn't change.

**Hotkeys and the frame stream.** A hotkey that changes game state (load,
reset, cheats) runs outside the frame stream. `HandleCommand_Locked` in
`src/main.c` refuses those on a netplay client
(`Session_AllowsLocalStateChanges`) and makes the host resync everyone
(`Session_ResyncAll`). Pause and turbo do nothing online.

**Everyone runs the same build.** A change to the simulation (a speed, a
rule, a new `g_mp` field) is fine, as long as every player uses your build.
Different builds will desync.

### Rule 2: keep each file's line endings

Some files use Windows line endings (CRLF) and some use Unix ones (LF). Both
kinds are in `src/`: for example `src/variables.h` and `src/sprite_main.c`
are CRLF, `src/player.c` and `src/mp.c` are LF. Git doesn't convert them
(only `*.bat` has an attribute). Check a file before you edit it:

```sh
git ls-files --eol src/variables.h src/player.c
```

`w/crlf` means CRLF, `w/lf` means LF. Make your editor keep what the file
has. If `git diff` shows the whole file as changed, or `^M` at every line end,
the line endings were converted: undo that.

### Rule 3: never commit ROM data

See the table in [section 1](#the-rom-bring-your-own-never-commit-it). Run
`git status` before every commit and look at what you are adding.

---

## 4. Recipes

Each recipe is small on purpose. Build with `make -j`, try it in the game,
then add a test (see [section 5](#5-testing-your-mod)).

### 4.1 Change a game constant: Link's walking speed

**Files:** `src/player.c`

Link's speed comes from a small table in `Link_HandleVelocity`:

```c
  static const uint8 kSpeedMod[27] = { 24, 16, 10, 24, 16, 8, 8, 4, 12, 16, 9, 25, 20, 13, 16, 8, 64, 42, 16, 8, 4, 2, 48, 24, 32, 21, 0 };

  uint8 vel = link_speed_modifier + kSpeedMod[r0];
```

The index `r0` is `link_speed_setting`, plus 1 when Link moves diagonally.
Entry 0 is walking straight (24), entry 1 walking diagonally (16), and entry
16 dashing (64). The unit is 1/16 of a pixel per frame, so 24 is 1.5 pixels
a frame. (Measured headless: 32 frames of walking right moved Link 48
pixels.)

To make walking a third faster, change the first two entries:

```c
  static const uint8 kSpeedMod[27] = { 32, 21, 10, 24, ...
```

Things to know:

- This is a simulation change: everyone online needs the same build.
- Collision checks expect the original speeds. Big jumps can push Link into
  walls. Try doors, ledges and stairs.
- To make it optional, gate it with a feature bit (next recipe).
- Other easy-to-find tables: `kEnemyDamages` in `src/sprite.c` (how much
  each damage class takes off an enemy), and in `src/mp.h` the co-op
  constants `kMpReviveFrames` (90 frames to revive) and
  `kMpGhostHeartCooldown`.

### 4.2 Add a gameplay option (ini key + menu entry)

**Files:** `src/features.h`, `src/config.c`, `src/ui.c`, `zelda3.ini`, and
the game code that uses it.

There are two kinds of options. Pick the right one.

| Kind | Example | Where it lives | Online |
| --- | --- | --- | --- |
| Changes the game | `TurnWhileDashing` | a `kFeatures0_*` bit in `g_config.features0` | the host's value travels in every frame |
| Changes only the picture or this PC | `NameTags`, `Hd2d` | its own field in `Config` | each player chooses |

**Gameplay option as a feature bit.** Feature bits are the easy, safe way:
`main.c` passes `g_config.features0` to `Session_Tick`, the frame carries it
(`MpFrameInput.features`), and `ZeldaRunFrame` copies it into RAM at
`kRam_Features0`, where game code reads it as `enhanced_features0`. So it is
part of the hashed state, and online the host's value is used.

Let's make the faster walking from 4.1 optional, as `FastWalk`.

1. `src/features.h`: add a bit after the last one (`kFeatures0_DimFlashes`
   is `65536`):

   ```c
     kFeatures0_DimFlashes = 65536,
     kFeatures0_FastWalk = 0x20000,
   ```

2. `src/config.c`, in `HandleIniConfig`, section 4 (`[Features]`), next to
   the other `ParseBoolBit` lines:

   ```c
       } else if (StringEqualsNoCase(key, "FastWalk")) {
         return ParseBoolBit(value, &g_config.features0, kFeatures0_FastWalk);
   ```

3. `src/ui.c`, `ScreenGameplay`: add a line to the `kFeatures` table. The
   loop below it draws the menu item and toggles the bit:

   ```c
       {"Cancel bird travel", kFeatures0_CancelBirdTravel},
       {"Fast walking", kFeatures0_FastWalk},
   ```

   A menu screen holds at most 24 lines (`MenuLine lines[24]` in `g_ui`).

4. `src/ui.c`, `Ui_SaveSettings`: add it to the `kKeys` table under
   `[Features]`, so the menu's change is written to `zelda3.coop.ini`:

   ```c
       {"MiscBugFixes", kFeatures0_MiscBugFixes}, {"CancelBirdTravel", kFeatures0_CancelBirdTravel},
       {"FastWalk", kFeatures0_FastWalk},
   ```

5. `zelda3.ini`, `[Features]`: document it.

   ```ini
   # Link walks a third faster.
   FastWalk = 0
   ```

6. Use it in the game code, `src/player.c`, `Link_HandleVelocity`:

   ```c
     uint8 vel = link_speed_modifier + kSpeedMod[r0];
     if (enhanced_features0 & kFeatures0_FastWalk)
       vel += vel / 3;
   ```

**Display-only option.** Follow `NameTags`: a `bool mp_name_tags` in `Config`
(`src/config.h`), a `StringEqualsNoCase(key, "NameTags")` line in
`src/config.c` (section 6 is `[Multiplayer]`; `GetIniSection` has the list),
an `Item("Name tags", ...)` in `ScreenMultiplayer` in `src/ui.c`, and a
`fprintf(f, "NameTags = %d\n", ...)` in `Ui_SaveSettings`. Settings that
something else needs at runtime are copied from `g_config` at startup in
`main.c` (for example `g_hd2d.see_through = g_config.hd2d_see_through;`) and
again by the menu when they change.

**Settings files.** `zelda3.ini` holds the defaults with comments. The menu
writes `zelda3.coop.ini`, which is read after `zelda3.ini` and wins. If your
new key seems to have no effect, check that file. You can also start the
game with another file: `./zelda3 --config my.ini`.

### 4.3 Add an on-screen overlay

**Files:** `src/ui.c` (and `src/ui.h` if something outside needs to reach it)

The overlay draws on top of the finished picture. It is never part of the
simulation, so it is the safest place to start. `src/ui.c` has small drawing
helpers that work in unscaled screen pixels:

- `FillRect(x, y, w, h, 0xRRGGBB)`, `ShadeRect(x, y, w, h)` (darkens, for a
  backdrop)
- `DrawText(x, y, "TEXT", 0xRRGGBB)` (a 5x7 font, shown in capitals) and
  `TextWidth(s)`
- `DrawHeart(x, y, fill, color)`
- `Mp_PlayerColorRgb(p)`: player p's color (respects the color-blind setting)

The screen can be split into panes. `g_views[0..g_nviews-1]` (`UiView` in
`src/ui.h`) gives each pane's position, size and the player whose eyes it
looks through (`player`, or -1). To read the game, use the per-player
queries in `src/instance.h`, which work whatever world a player is in:
`Inst_PlayerPos`, `Inst_PlayerHealth`, `Inst_PlayerMagic`,
`Inst_PlayerDowned`, `Inst_PlayerPlace` and so on.

**Example: show each pane's player position.** Handy for writing tests,
because `pos`, `status` and `expect_near` in the test runner use the same
numbers.

```c
// src/ui.c, near DrawNameTags
static bool g_show_coords;

void Ui_ToggleCoords(void) {
  g_show_coords = !g_show_coords;
}

static void DrawCoords() {
  if (!g_show_coords)
    return;
  for (int v = 0; v < g_nviews; v++) {
    const UiView *view = &g_views[v];
    uint16 x, y;
    if (view->player < 0 || Inst_PlayerPos(view->player, &x, &y) < 0)
      continue;
    char buf[32];
    snprintf(buf, sizeof(buf), "P%d %d,%d", view->player + 1, x, y);
    ShadeRect(view->x + 2, view->y + 40, TextWidth(buf) + 3, 10);
    DrawText(view->x + 3, view->y + 41, buf, Mp_PlayerColorRgb(view->player));
  }
}
```

Then call it from `Ui_Draw`, before `DrawMenu()` so the menu stays on top:

```c
  DrawApNotices();
  DrawCoords();
  DrawMenu();
```

And declare `void Ui_ToggleCoords(void);` in `src/ui.h`. The next recipe
binds it to a key.

For a short message at the bottom of the screen, there is already
`Ui_Toast("...")`.

### 4.4 Add a hotkey

**Files:** `src/config.h`, `src/config.c`, `src/main.c`, `zelda3.ini`

Hotkeys are commands numbered by the `kKeys_*` enum. Let's add
`ToggleCoords` on Ctrl+G (free today; F11 is the 3D view, Ctrl+F11 "Watch
everyone", F12 and Ctrl+F12 the screenshot keys).

1. `src/config.h`: add the command as the last entry before `kKeys_Total`
   (today that is right after `kKeys_WatchEveryone`):

   ```c
     kKeys_ScreenshotClean,
     // "Watch everyone": every joined player's world on screen (display only).
     kKeys_WatchEveryone,
     kKeys_ToggleCoords,
     kKeys_Total,
   ```

2. `src/config.c`: give it a default key. `kDefaultKbdControls` is in enum
   order, so add the entry at the end (`_()` is a plain key, `S()` Shift,
   `C()` Ctrl, `A()` Alt; `N` means no default):

   ```c
     // Screenshot, ScreenshotClean
     _(SDLK_F12), C(SDLK_F12),
     // WatchEveryone
     C(SDLK_F11),
     // ToggleCoords
     C(SDLK_g),
   ```

   Then add its ini name to `kKeyNameId`, so `[KeyMap]` can rebind it:

   ```c
     S(Screenshot), S(ScreenshotClean),
     S(WatchEveryone),
     S(ToggleCoords),
   ```

3. `src/main.c`, `HandleCommand`: handle it. A display-only command is
   handled near the top, like the screenshot and "Watch everyone" keys,
   before the audio lock:

   ```c
     if (j == kKeys_ToggleCoords) {
       if (pressed)  // (display only: fine online)
         Ui_ToggleCoords();
       return;
     }
   ```

   Commands that touch the game go into the `switch` in
   `HandleCommand_Locked` instead (its `default:` case asserts, so a missing
   case shows up quickly).

4. `zelda3.ini`, `[KeyMap]`: document it.

   ```ini
   ToggleCoords = Ctrl+g
   ```

Things to know:

- A key that is bound to a game button (in *Controls*) doesn't also fire a
  hotkey, and keys go to the open menu first.
- A hotkey that changes the game must be added to the list at the top of
  `HandleCommand_Locked`, so a netplay client refuses it and the host
  resyncs everyone (see the golden rules).
- `HandleCommand` also sees key releases (`pressed` is false), which the 3D
  camera keys use to act while held.

### 4.5 Change a randomizer logic rule

**Files:** `src/randomizer.c`, `archipelago/fsotp/rules.py`, and maybe
`archipelago/tools/logic_vectors.py`

The randomizer works with an "assumed fill": it places the important items
one by one in chests that are reachable, then checks that the game can be
beaten (`TryFill`, `Generate`). What "reachable" means is in two functions:

- `Evaluate(const State *s, Ev *e)`: from the items the player has, works
  out what they can do and where they can go. For example
  `e->ep = e->lamp && e->bow && e->sword1;  // Eastern Palace`.
- `RoomAccess(int room, const Ev *e)`: for each room with a chest, whether it
  can be reached. 1 yes, 0 no, -1 unknown (then only filler goes there).

The Archipelago world has **the same logic, ported line by line**, in
`archipelago/fsotp/rules.py` (`_evaluate` and the `_rooms(...)` table). The
two must always agree: the `aplogic` test compares them on thousands of item
combinations.

**Example: the Mire shed should also need the Lamp.** In C:

```c
  case 0x10D: return e->lift2 && e->flute && e->pearl && e->lamp; // Mire shed
```

In Python:

```python
_rooms((0x10D,), lambda e: e.lift2 and e.flute and e.pearl and e.lamp)  # Mire shed
```

**Keep old save files stable.** A randomized file stores only its seed and a
placement version (`kRandoPlacementVersion`, save byte 7EF4F9). When it
loads, the placement is generated again from the seed. If your rule changes
which chest gets what, files made before your change would have their items
moved. So gate the new rule on the version and raise it, like version 2 did
for Turtle Rock (`(g_logic_version < 2 || e->sword1)`):

```c
enum { kRandoPlacementVersion = 3 };
...
  case 0x10D: return e->lift2 && e->flute && e->pearl && (g_logic_version < 3 || e->lamp);
```

`rules.py` always has the newest logic, so it gets the plain rule.

**A rule that needs an item the logic doesn't track yet.** The logic only
looks at items that are shuffled between chests (see `IsProgression` and the
`State`/`Ev` structs; in Python `LOGIC_ITEMS`). Then you need to add it in all
of these places: `Evaluate` and `Ev` in C, `IsProgression`, `_evaluate`, `Ev`
and `FLAGS` in `rules.py`, and the `ITEMS` list in
`archipelago/tools/logic_vectors.py` (that list feeds the test). Item ids are
the codes the game gives to `Link_ReceiveItem` (the `kI_*` enum in
`randomizer.c`, `ITEMS` in `archipelago/fsotp/items.py`).

**What gets shuffled.** The pool is the original contents of the chests plus
six special places (`kSpecialLocations`: the uncle, the pedestal, the smiths,
two fairies). Dungeon keys, maps and compasses stay put (`IsDungeonItem`).

**Check your change:**

```sh
make -j
# 1. Every seed must still be beatable (also part of tests/coop/randomizer.txt):
printf 'randocheck 1 5000\n' > /tmp/rc.txt && ./zelda3 --mptest /tmp/rc.txt
# 2. C and Python must agree:
python3 archipelago/tools/logic_vectors.py tests/out/ap_logic_vectors.txt 3000 1
./zelda3 --mptest tests/coop/archipelago_logic.txt
```

`randocheck <seed> <count> v` prints spoilers. *Gameplay > Write spoiler log*
in the game writes `spoiler_<seed>.txt`. If you change what the Archipelago
world sends in its slot data, look at `SLOT_DATA_VERSION` in
`archipelago/fsotp/__init__.py` and `kApSlotDataVersion` in
`src/ap_client.h`: the game refuses rooms of another version.

### 4.6 Tweak the 2D-HD look

**Files:** `src/hd2d.c` (and `hd2d.h`, `config.c`, `ui.c` for a new slider)

The 2D-HD look is display only, so you can experiment freely: nothing you
do here can desync a game. The settings are in `Hd2dSettings g_hd2d`
(`src/hd2d.h`), copied from `g_config` at startup in `main.c` and changed
live by the *Video > 2D-HD settings* screen (`ScreenHd2d` in `src/ui.c`).

Places to start:

- **Color grade.** `MakeGrade()` turns the 0-100 `grade` setting into
  saturation, contrast and warmth (fixed point, 256 = unchanged):

  ```c
  Grade gr = { 256 + 64 * g / 100, 256 + 38 * g / 100, 256 + 15 * g / 100, 256 - 15 * g / 100 };
  ```

  For a cooler, bluer look, flip the signs of the last two terms.
- **Lights.** The Lamp's light around a Link has radius 92, a torch 96 and
  fire 56. These numbers appear twice: in `GatherScene` (the 2D picture,
  `AddLight(...)`) and in `Hd2d_GatherLights` (used by the 3D view,
  `ADD_LIGHT(...)`). Change both so the two views agree.
- **A new slider.** Add a `uint8` to `Hd2dSettings` and to `Config`, a
  `Strength("My effect", &g_config.x, &g_hd2d.x);` line in `ScreenHd2d`, an
  ini key in `config.c` (section 1 is `[Graphics]`), a `fprintf` in
  `Ui_SaveSettings`, a default in `ParseConfigFile`, and the copy in
  `main.c`.

Rules for this code:

- Read the game, never write it. The pass reads RAM for players and lights.
- It is fast because it blurs at half and quarter size, on worker threads
  (`Hd2d_ParallelFor`). Keep per-pixel work cheap.
- The PPU tells the pass which layer each pixel came from
  (`g_ppu_layer_out`), so sprites and the HUD stay sharp. Keep it that way.

Tests: `tests/coop/hd2d_light.txt`, `tests/coop/hd2d_no_lamp.txt` and the
2D-HD screenshots in `tests/coop/visual.txt`. After an intended change to
the look, refresh the screenshots with `tests/update_baselines.sh`.

### 4.7 Tweak the 3D view

**Files:** `src/hd3d_world.c`, `src/hd3d_internal.h` (and `hd3d.c`,
`hd3d_sprites.c`, `r3d.c`, `cam3d.c`)

The 3D view is display only too, with **one** exception: with a turned
camera (or in first person) `Hd3d_RemapInputs` turns a local player's d-pad
before the inputs are sent. Everything else, the cameras included, lives
outside `g_ram`, `g_mp`, the snapshots and the joypad word. Keep it so.

The view is chosen per local player (`Hd3d_SetLocalEnabled`,
`Hd3d_LocalEnabled` in `hd3d_input.c`); `g_hd3d.enabled` only says whether
anyone has it on. A pane is drawn in 3D when the player whose camera it
uses has it on (`Hd3d_DrawFrame`), and `Hd3d_ComputeViews` gives a 2D
player and a 3D player in one world a pane each. Online nothing of this is
sent: each computer draws its own choice.

**Where heights and shapes come from.** The scene is rebuilt from the game's
own tiles (`src/hd3d_world.c`, see its header comment):

- Each 8x8 cell gets a **class** from its collision attribute:
  `kCls_Ground`, `kCls_Pit`, `kCls_Low`, `kCls_Wall`, `kCls_Empty`,
  `kCls_Void`. The table is built in `InitTables()` (`g_class[indoors][attr]`),
  with special cases outdoors (thick grass, ledges, deep water), and
  adjusted per cell in `Classify()`.
- Dungeon room walls become hinged panels (`BuildShell`). Chests become
  boxes (`BuildProps`, `EmitProps`: `kChestDepth` px deep with their rim on
  top, the rest of their picture on the front). The rest is a relief
  (`BuildRelief`): pots, statues and torch stands are its blocks. Outdoors, ledges you can jump down make terraces
  (`ComputeTerraces`).
- Sizes and limits are constants: `kFpWallHeight` (room walls in first
  person) in `hd3d_world.c`, and `kHd3dPitDepth`, `kHd3dFogMargin`,
  `kHd3dWideMargin`, `kHd3dMaxRelief` in `src/hd3d_internal.h`.

**A key property:** at the starting (oblique test) camera, a surface point
(x, y, z) shows picture texel (x, y - z). So the 3D view looks exactly like
the original picture whatever heights you give things. Wrong heights only
show when the camera turns. `tests/coop/hd3d_exact.txt` checks this with
`expect_exact3d`.

**Seeing what the scene thinks.** The test command `dump3d <file.ppm>` writes
the viewed world's classes and heights to a picture.

Tests to run after a change: `tests/coop/hd3d_*.txt` (especially
`hd3d_pure.txt`, `hd3d_determinism.txt`, `hd3d_exact.txt` and
`hd3d_buffer.txt`), plus the unit tests for the rasterizer and the camera
(no ROM needed):

```sh
cc -O2 -Werror tests/unit/r3d_test.c src/r3d.c -lm -o r3d_test && ./r3d_test
cc -O2 -Werror tests/unit/cam3d_test.c src/cam3d.c src/r3d.c -lm -o cam3d_test && ./cam3d_test
```

`tools/readme_shots.sh` remakes the README's 3D screenshots from your ROM
(needs Pillow).

### 4.8 Add or change a co-op mechanic

**Files:** `src/mp.c`, `src/mp.h`, and a hook in the game code if needed

**How co-op works in one paragraph.** The game knows one Link, whose state
sits at fixed RAM addresses. `src/mp.c` keeps a private copy of those bytes
for each player (the "Link context", listed in `kMpCtxRanges`) and swaps the
acting player's copy into RAM. The **leader**'s context is in RAM between
frames. `g_mp.resident` is whose context is in RAM right now. To read another
player without swapping, use `Mp_PlayerRam(p, addr)`, `Mp_PlayerX(p)`,
`Mp_PlayerY(p)`, `Mp_PlayerHealth(p)`, `Mp_IsAlive(p)`. Items, rupees and
progress are not in the context, so they are shared.

**Patterns to copy** (all in `src/mp.c`):

- `Mp_GhostHelp`: a downed player presses A to drop a heart near the
  nearest teammate. Shows how to read a player's buttons
  (`g_mp.inputs[p]`), detect a fresh press with a "previous input" field,
  keep a cooldown in `g_mp`, and spawn a sprite.
- `Mp_GloveLevelForLift`: a teammate next to you holding A counts as one
  more glove. A tiny function the game code calls from `src/player.c`.
  This is the usual way to hook co-op into the game: the game code calls a
  `Mp_*` function at one spot.
- `Mp_HandleRevives`: standing next to a ghost for `kMpReviveFrames`
  revives it.

Joypad bits in `g_mp.inputs[p]`: B `0x1`, Y `0x2`, Select `0x4`, Start `0x8`,
Up `0x10`, Down `0x20`, Left `0x40`, Right `0x80`, A `0x100`, X `0x200`,
L `0x400`, R `0x800`, and the mod's Ping button `kMpPingBit` (`0x1000`),
which the game never sees.

**Example: two rescuers revive twice as fast.** Today `Mp_HandleRevives`
only checks whether anybody stands near the ghost. Count them instead:

```c
static void Mp_HandleRevives() {
  for (int p = 0; p < kMpMaxPlayers; p++) {
    if (!g_mp.p[p].joined || !g_mp.p[p].downed)
      continue;
    int helpers = 0;
    for (int q = 0; q < kMpMaxPlayers; q++) {
      if (q != p && Mp_IsAlive(q) &&
          abs((int16)(Mp_PlayerX(q) - Mp_PlayerX(p))) < 24 &&
          abs((int16)(Mp_PlayerY(q) - Mp_PlayerY(p))) < 24)
        helpers++;
    }
    if (!helpers) {
      g_mp.p[p].revive_ctr = 0;
    } else if ((g_mp.p[p].revive_ctr += helpers) >= kMpReviveFrames) {
      Mp_Revive(p, 4);  // half a heart
      sound_effect_2 = 0x2f;
    }
  }
}
```

This only uses state that is already in `g_mp` and the game's RAM, so it is
deterministic, rolled back and hashed with no extra work.

**When you need new state.** Add a field to `MpState` in `src/mp.h`. Then:

- Keep it plain data: fixed-size integers and arrays, no pointers, no floats.
- **No padding bytes.** The struct is hashed byte by byte. Order fields so
  they are naturally aligned, and add explicit `pad` bytes where needed (see
  how `MpPing` and the `pad` fields are laid out).
- `Mp_Reset` clears the whole struct, so zero must be a sensible start
  value.
- The struct is saved whole by `Mp_SaveLoad`. A new layout means save
  states from older builds won't match, and online players need the same
  build.
- Per-player values go in arrays of `kMpMaxPlayers`. State a player's own
  Link carries around (like its health) belongs in the Link context instead
  (`kMpCtxRanges`), but changing that list is a bigger job.

**Think about worlds.** Each world instance has its own `g_mp`. Players in
other worlds are not "in" this world. The code in `src/mp.c` runs for one
world at a time; loops over `kMpMaxPlayers` with `Mp_IsAlive(q)` only find
Links of the current world. For things that must be the same in every world
(items, keys, flags), the save area `7EF000-7EF4FF` is synced between worlds
after every frame (`src/instance.c`). The randomizer (7EF4F0 and up) and
Archipelago (7EF407 and up) already keep data in it; check before you use a
byte there.

**Test it** with a script like the one in the next section, and run the
whole suite: co-op touches a lot.

---

## 5. Testing your mod

### The headless test runner

`./zelda3 --mptest <script>` runs the game without a window, following a
text script, and prints `MPTEST OK` or `MPTEST FAILED`. It needs
`zelda3_assets.dat`, so it runs on your computer. The full list of commands
is the comment at the top of [`src/mptest.c`](../src/mptest.c). The most
useful ones:

| Command | What it does |
| --- | --- |
| `load <n>` | Load reference save chapter n from `saves/ref/` (`2` = outside Link's house) |
| `join <p>` / `leave <p>` | Player p (1-4) joins or leaves |
| `hold <p> <keys>` | Keys held by player p: `B Y s S U D L R A X l r P` (`s` Select, `S` Start, `P` Ping), `-` for none |
| `run <frames>` | Run that many frames |
| `pos <p> <x> <y>` | Put a player somewhere (no collision check) |
| `hp <p> <n>` | Set health (in 1/8 hearts, decimal) |
| `down <p>` / `revive <p>` | A player goes down / gets up |
| `clearsprites` / `spawn <hextype> <p> [dx]` | Remove enemies / spawn one next to a player |
| `peek <hexaddr> [n]` / `poke <hexaddr> <hexval>` | Read / change a RAM byte |
| `pram <p> <hexaddr> <hexval>` | Change a byte of player p's own Link context |
| `enter <hexroom>` | Go into a dungeon room |
| `status` | Print every world and player (position, health, state, sprites) |
| `expect_hp`, `expect_alive`, `expect_downed`, `expect_near`, `expect_ram`, `expect_module`, `expect_instances`, ... | Checks; any failing one fails the script |
| `shot <file.ppm>` / `expect_shot <name>` | Save a picture / compare with `tests/baseline/<name>.ppm` |
| `rando`, `randocheck`, `fullinv`, `ff`, `story` | Settings for new files and the session |

Addresses are hex offsets into `g_ram`: `f36d` is 7EF36D,
`link_health_current`. Values for `poke`, `pram` and `expect_ram` are hex
too; `hp`, `expect_hp` and positions are decimal.

### Writing a new test

Put it in `tests/coop/` with a comment at the top saying what it checks.
Here is a test for the revive mechanic. It passes on the current code
(one rescuer, `kMpReviveFrames` = 90):

```text
# Standing next to a downed teammate for 1.5 seconds revives it with half
# a heart.
load 2
run 30
join 2
run 60
clearsprites
pos 2 3860 1690
pos 1 3940 1690
run 5
down 2
run 10
expect_downed 2
pos 1 3876 1690
run 60
expect_downed 2
run 40
expect_alive 2
expect_hp 2 4
```

For the "two rescuers" example from recipe 4.8, `join 3`, put P3 next to the
ghost too (`pos 3 3860 1706`), and expect P2 to be up after 50 frames.

Tips:

- Start from a reference save (`load 2`) and `run 30` to let it settle.
- Use `status` while writing a test to see positions and states, then turn
  what you see into `expect_*` lines.
- `clearsprites` keeps enemies from interfering.
- Prefer checks on state (`expect_hp`, `expect_ram`) over screenshots.
- Run one test: `./zelda3 --mptest tests/coop/my_test.txt`.

### Screenshots and baselines

`expect_shot <name> [max % of pixels] [channel tolerance]` compares the
picture with `tests/baseline/<name>.ppm`. The first run makes the baseline
from your ROM. These files are never committed. When a picture differs, the
new picture and a diff with the changed pixels in red go to `tests/out/`.
After an intended change to how things look:

```sh
tests/update_baselines.sh      # runs tests/coop/visual.txt with ZELDA3_UPDATE_BASELINES=1
```

### The full suite

```sh
tests/run_coop_tests.sh
```

It writes the Archipelago logic vectors, runs every `tests/coop/*.txt` on its
own (logs in `tests/out/<name>.log`), then the netplay pairs: a host script
and a client script talking over real UDP on your machine (`net_*_host.txt`
with `net_*_client.txt`), some over a simulated bad connection
(`ZELDA3_NETSIM=60,30,3` means 60 ms delay, 30 ms jitter, 3% loss). It prints
`PASS` or `FAIL` per test and exits non-zero if anything failed. Run it
before you share a change to the simulation.

### Unit tests and CI

- `tests/unit/r3d_test.c` and `tests/unit/cam3d_test.c` test the 3D
  rasterizer and camera without a ROM (commands in recipe 4.7).
- `archipelago/tests/run_client_tests.sh` tests the Archipelago client
  against a mock server (needs Python `websockets` 13).
- GitHub Actions (`.github/workflows/build.yaml`) builds Linux, macOS and
  Windows, runs the two unit tests and the Archipelago client tests, and
  packs the `.apworld`. **The co-op tests don't run on GitHub**, because they
  need the game's data. Run them yourself.

---

## 6. Debugging tips

**Look at the state.** In a test script, `status` prints every world (module,
leader, room, camera) and every player (position, health, state). `peek`
prints RAM bytes, `pitem` everyone's items. `camlog <n>` prints cameras and
positions frame by frame.

**Change the state.** `poke` and `pram` change bytes; `hp`, `pos`, `down`,
`revive`, `setleader`, `spawn`, `settiles` (paint pit or other tiles into a
room), `receive <hexitem>` (get an item as from a chest) and `message <hexid>`
(open a text box) set up a situation fast.

**Find out what a RAM address means.**

- `src/variables.h` names almost every address:
  `#define link_health_current (*(uint8*)(g_ram+0xF36D))`. Search it for the
  address (`0xF36D`) or for a word (`health`).
- `other/names.txt` maps addresses to names, for RAM
  (`0x7ef36d: link_health_current`) and for ROM functions
  (`0x87e245: Link_HandleVelocity`). The comment after a function,
  like `// 87e245`, is that ROM address, so you can look it up in community
  disassemblies of the game.
- `other/name_remap.txt` maps older disassembly names to the C names.
- Per-player addresses are listed (with short comments) in `kMpCtxRanges` in
  `src/mp.c`.

**Hash mismatches and desyncs.** If online games keep resyncing, something in
the simulation isn't deterministic. To find it:

1. Look for the usual causes (golden rule 1): `rand()`, time, a `static`
   holding game state, uninitialized struct padding in `g_mp`, a display
   function that writes RAM, reading a setting from `g_config` inside the
   simulation instead of from the frame.
2. Test commands: `hashmark <name>` and `expect_hashmark <name>` check that
   something (like drawing) didn't change the state; `reload` saves and loads
   every world the way netplay does and fails if anything changes;
   `expect_drawpure <n>` checks that 3D drawing writes nothing.
3. For two machines: `dumpevery <n>` prints the confirmed state every n
   frames; compare the host's and client's logs with
   `python3 tests/cmpdump.py host.log client.log`. It shows the first frame
   where they differ.
4. `netstats` prints rollback and misprediction counters; `corrupt` flips a
   RAM byte to test that recovery works.

**Tracing worlds.** `ZELDA3_TRACE_FRAMES=from,to` with the `wander` command
prints what the world instances do in those frames (splits, merges, menus).

**Netplay on one machine.** Run a host script and a client script at once,
like `tests/run_coop_tests.sh` does. `ZELDA3_NETSIM=delay_ms,jitter_ms,loss_percent`
(or the `netsim` command) simulates a bad link.

**In the game.** `F` shows the renderer's frame rate, `W` fills health and
magic, `Ctrl+E` walks through walls, `F1`-`F10` load and `Shift+F1`-`F10`
save snapshots, `F12` saves a screenshot (the full list is in the README's zelda3 section and
`[KeyMap]` in `zelda3.ini`). Cheats and loads are refused on a netplay
client.

**Crashes.** Build with `CFLAGS="-O0 -g" make -j` and run under a debugger,
or run the test that crashes. On Windows, the game writes
`zelda3_crash.txt` (`src/crash_handler.c`).

---

## 7. Sharing your mod

**Fork and branch.** Fork the repository on GitHub, make a branch per mod,
and keep your commits small, with messages that say what changed and why.
The project's pull request template asks what the change does, whether it
breaks anything, and how to test it.

**Before you share:**

- `git status`: no ROM, no `zelda3_assets.dat`, no extracted assets, no
  baselines, no saves.
- `git diff`: no line-ending changes to whole files.
- `make -j` builds without warnings, and `tests/run_coop_tests.sh` passes.
- If you changed the simulation, say so: players must all use your build to
  play online together.
- If you changed what new save files or randomizer seeds get, say so, and
  version it (recipe 4.5).
- Update `zelda3.ini` comments, and the README or this guide if you changed
  something they describe.

**What to share.** Share source code (a fork, a patch or a pull request) or
builds of the program. Players bring their own ROM and extract the assets
themselves, as with this project.

**Licensing.** `LICENSE.txt` contains the MIT License (copyright 2022
snesrev and 2021 elzo_d), followed by the licenses of Opus and of cJSON
(MIT). The README says the project is licensed under the MIT license.
stb_image's license is in its header in `third_party/stb`. The MIT License
lets you use, change and share the code, as long as the copyright notice
and the permission notice stay with it. Keep `LICENSE.txt` in your fork. The
license covers the code only: the game's graphics, sound and text belong to
Nintendo and are not in the repository.

**AI disclosure.** This project was written almost entirely with an AI
coding assistant, and `AI_DISCLOSURE.txt` says what the AI wrote, what it
didn't, and how the work was checked. Every commit in the history carries a
`Co-Authored-By` line naming the AI. If you use AI tools for your mod, being
open about it in the same way helps players and other modders. If you add a
lot of your own work to a fork, update the disclosure so it stays true for
your version.

---

## 8. Glossary and where to learn more

| Term | Meaning |
| --- | --- |
| `g_ram` | The game's 128 KB of memory, as one array. Every world has its own copy. |
| Link context | The bytes of RAM that describe one Link (`kMpCtxRanges` in `src/mp.c`). Each player has a copy. |
| Leader | The player whose context is in RAM between frames. Rooms, doors, menus and cutscenes follow it. |
| Resident | The player whose context is in RAM right now (`g_mp.resident`). |
| `g_mp` | The co-op state (`MpState`). Part of the simulation: hashed and snapshotted. |
| World instance | One copy of the game world, for a group of players in one place (`src/instance.c`). |
| Split / merge | A player gets a world of their own / two worlds become one again. |
| Module | The game's top-level mode, `main_module_index` (7 dungeon, 9 overworld, 14 menus and text...). |
| Frame | One step of the game, 60 per second. |
| `MpFrameInput` | Everything one frame needs from outside: four joypad words, joins and leaves, settings. |
| Deterministic | Same start and same inputs always give the same result. |
| Rollback | Going back to the last confirmed frame and replaying with the real inputs. |
| State hash | One number summing up the game state (`Inst_HashState`), compared online. |
| Display only | Code that draws but never changes game state (overlay, 2D-HD, 3D view). |
| Feature bit | A `kFeatures0_*` flag in `features.h`, a game option that travels with each frame. |
| Ancilla | The game's name for projectiles and effects (arrows, bombs, sparkles). |
| OAM | The SNES sprite list. |
| PPU | The SNES picture chip, emulated in `snes/ppu.c`. |
| Placement version | Which version of the randomizer logic a save file's items were placed with. |
| Baseline | A reference screenshot made from your own ROM, in `tests/baseline/`. |

**Read next:**

- [HOW_THE_MOD_WORKS.md](HOW_THE_MOD_WORKS.md): the whole mod in plain words.
- [MULTIPLAYER.md](../MULTIPLAYER.md): co-op rules, plus "How it works (for
  modders)" on the Link context, instances and netplay.
- [README.md](../README.md): features, the randomizer, Archipelago (with a
  "For developers" part), 2D-HD and the 3D view.
- [BUILDING.md](../BUILDING.md): building and the Archipelago world.
- The header comments of `src/mp.h`, `src/instance.h`, `src/session.h`,
  `src/hd2d.h`, `src/hd3d_world.c` and `src/mptest.c`.
- The upstream zelda3 wiki: https://github.com/snesrev/zelda3/wiki

**Ask questions:**

- The fourSwordsFromThePast Discord, for this mod: https://discord.gg/jEsJGwhyNS
- snesrev's Discord, for the zelda3 engine itself: https://discord.gg/AJJbJAzNNJ
- The repository's issue tracker, for bugs.
