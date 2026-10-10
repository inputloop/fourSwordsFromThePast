# fourSwordsFromThePast

*The Legend of Zelda: A Link to the Past* as a native PC game with **four player
drop-in/drop-out co-op** (same PC, LAN and internet), each Link in its own
color, free roaming (split screen when couch players go separate ways) or a
shared screen, per-player hearts in the screen corners, toggleable friendly fire,
native widescreen and an in-game options menu.

Built on the [snesrev/zelda3](https://github.com/snesrev/zelda3) C
reimplementation (vendored from commit `45a149d8`, MIT licensed). You need your
own US ROM to extract the game's assets.

See **[MULTIPLAYER.md](MULTIPLAYER.md)** for how co-op and netplay work.

## Item randomizer

*Pause menu > Gameplay > Randomize new games* turns on a basic item
randomizer for save files created from then on (or set `Randomizer` in the
`[Features]` section of `zelda3.ini`).

- **Open start, no tutorial.** A randomized file skips the intro and the
  escape from Hyrule Castle: it starts as if Zelda was just brought to the
  sanctuary. Link has the fighter's sword and shield and full magic, starts
  at his house or the sanctuary, and can go anywhere from the first step.
  Non-randomized files start the usual way.
- The contents of every chest in the game are shuffled: Bow, Hookshot,
  Hammer, gloves, Moon Pearl, Fire and Ice Rods, Lamp, Cane of Somaria,
  Magic Cape, Cane of Byrna, mails, Mirror Shield, boomerangs, a bottle,
  heart pieces, rupees, bombs and arrows. Small keys, big keys, maps and
  compasses stay in their dungeons, so dungeons play as before.
- Items given by people or found outside chests (Boots, Book,
  Flippers, Mirror, Flute, Master Sword, medallions, pendants and crystals)
  are where they always were.
- Every seed is checked to be beatable without glitches, starting from the
  open start. One Lamp is always in Link's house or the sanctuary chest.
  The castle basement and sewer chests (normally opened during the escape)
  only get filler. Gloves and mails are
  progressive: the first glove found is the Power Glove, the second the
  Titan's Mitt.
- The seed is stored in the save file, so a file stays randomized the same
  way. *Gameplay > This game* shows the seed; choosing "on, seed ..." makes
  new files with that same seed, to race a friend or replay it.
- In netplay the host's setting is used, and everybody plays the same seed.
- `src/randomizer.c` has the location logic. `./zelda3 --mptest` with
  `randocheck <first seed> <count> [verbose]` checks seeds and prints spoilers.

## Quick start (Linux / macOS)

```sh
sudo apt-get install libsdl2-dev python3-pil python3-yaml   # or: pip install -r requirements.txt
cp /path/to/your/Legend_of_Zelda_USA.sfc zelda3.sfc        # SHA1 6d4f10a8b10e10dbe624cb23cf03b88bb8252973
make -j                                                     # builds ./zelda3 and extracts zelda3_assets.dat
./zelda3
```

## Windows

1. Download the `zelda3-windows-<commit>` artifact from the latest
   [Build run](../../actions/workflows/build.yaml) and **unzip it** (running the
   exe from inside the zip won't work).
2. Copy your own ROM of *A Link to the Past (USA)* (`.sfc`/`.smc`, any name)
   into the folder.
3. Start `zelda3.exe` and choose **Extract now** (or run `extract_assets.bat`).
   This needs [Python 3](https://www.python.org/downloads/) once; tick
   "Add python.exe to PATH" when installing it.

To build it yourself: `run_with_tcc.bat` (downloads its own compiler), Visual
Studio (`Zelda3.sln`), or `tools/build_windows_mingw.sh` from Linux.

The ROM, `zelda3_assets.dat` and saves are gitignored. **Never commit them.**

## Controls

- Keyboard: arrows, X = A, Z = B, S = X, A = Y, Enter = Start, Right Shift = Select.
- `Esc` (or the controller's Guide button, or Back+Start) opens the menu:
  Multiplayer, Video and HD options (widescreen 16:9/16:10/18:9, HD world map,
  smooth scaling, no sprite flicker, ...) and Gameplay options.
- Extra controllers press START to drop in as P2-P4.

## Where to mod

- `src/` holds the game logic as C: `player.c` (Link), `sprite_main.c` (enemies/NPCs), `dungeon.c`, `overworld.c`, `ancilla.c` (projectiles/effects), `hud.c`.
- `src/mp.c` (multi-Link engine), `src/session.c` (netplay), `src/ui.c` (overlay and menus).
- `assets/` holds the extracted resources (overworld/dungeon YAML, images, text). Edit these, then rebuild them with `python3 assets/restool.py` (see BUILDING.md).
- `zelda3.ini` holds the runtime options. The in-game menu saves its changes to `zelda3.coop.ini`.
- `tests/run_coop_tests.sh` runs headless co-op and netplay regression tests.

---

# Zelda3

A reimplementation of Zelda 3.

Our discord server is: https://discord.gg/AJJbJAzNNJ

## About

This is a reverse engineered clone of Zelda 3 - A Link to the Past.

It's around 70-80kLOC of C code, and reimplements all parts of the original game. The game is playable from start to end.

You need a copy of the ROM to extract game resources (levels, images). Then once that's done, the ROM is no longer needed.

It uses the PPU and DSP implementation from [LakeSnes](https://github.com/elzo-d/LakeSnes), but with lots of speed optimizations.
Additionally, it can be configured to also run the original machine code side by side. Then the RAM state is compared after each frame, to verify that the C implementation is correct.

I got much assistance from spannerism's Zelda 3 JP disassembly and the other ones that documented loads of function names and variables.

## Additional features

A bunch of features have been added that are not supported by the original game. Some of them are: basic widescreen, shaders, 240 vertical res, hi-res mode7 map, MSU-1 audio, item switching, and over 30 bugfixes.

## Building

You must self-build for now. Steps for 64-bit Windows:<br>
(0) Download [Python](https://www.python.org/ftp/python/3.11.4/python-3.11.4-amd64.exe) and install with "Add to PATH" checked<br>
(1) Click the green button "Code > Download ZIP" on the github page and extract the ZIP<br>
(2) Place your USA rom named zelda3.sfc in that folder<br>
(3) Download [TCC](https://github.com/FitzRoyX/tinycc/releases/download/tcc_20230519/tcc_20230519.zip) and [SDL2](https://github.com/libsdl-org/SDL/releases/download/release-2.28.2/SDL2-devel-2.28.2-VC.zip) and extract each ZIP into the "third-party" subfolder<br>
(4) Double-click "get_python_libs.bat" in the main dir.<br>
(5) Double-click "extract_assets.bat" in the main dir. This will create zelda3_assets.dat.<br>
(6) Double-click "run_with_tcc.bat" in the main dir. This will create zelda3.exe and run it.<br>
(7) Configure with zelda3.ini in a text editor like notepad++<br>

For other platforms and compilers, see: https://github.com/snesrev/zelda3/blob/main/BUILDING.md

## Usage and controls

The game supports snapshots. The joypad input history is also saved in the snapshot. It's thus possible to replay a playthrough in turbo mode to verify that the game behaves correctly.

The game is run with `./zelda3` and takes an optional path to the ROM-file, which will verify for each frame that the C code matches the original behavior.

| Button | Key         |
| ------ | ----------- |
| Up     | Up arrow    |
| Down   | Down arrow  |
| Left   | Left arrow  |
| Right  | Right arrow |
| Start  | Enter       |
| Select | Right shift |
| A      | X           |
| B      | Z           |
| X      | S           |
| Y      | A           |
| L      | C           |
| R      | V           |

The keys can be reconfigured in zelda3.ini

Additionally, the following commands are available:

| Key | Action                |
| --- | --------------------- |
| Tab | Turbo mode |
| W   | Fill health/magic     |
| Shift+W   | Fill rupees/bombs/arrows     |
| Ctrl+E | Reset            |
| P   | Pause (with dim)                |
| Shift+P   | Pause (without dim)                |
| Ctrl+Up   | Increase window size                |
| Ctrl+Down   | Decrease window size                |
| T   | Toggle replay turbo mode  |
| O   | Set dungeon key to 1  |
| K   | Clear all input history from the joypad log  |
| L   | Stop replaying a shapshot  |
| R   | Toggle between fast and slow renderer |
| F   | Display renderer performance |
| F1-F10 | Load snapshot      |
| Alt+Enter | Toggle Fullscreen     |
| Shift+F1-F10 | Save snapshot |
| Ctrl+F1-F10 | Replay the snapshot |
| 1-9 | Load a dungeons playthrough snapshot |
| Ctrl+1-9 | Run a dungeons playthrough in turbo mode |


## License

This project is licensed under the MIT license. See 'LICENSE.txt' for details.
