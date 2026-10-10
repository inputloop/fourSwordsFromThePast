# Four player co-op

Up to four Links play through A Link to the Past together. Players can sit at
the same computer, be on the same LAN, or play over the internet, in any mix.
Players can drop in and drop out at any time.

## Playing

- **Menu**: press `Esc`, the controller's Guide button, or Back+Start.
  The menu has the Multiplayer, Video/HD and Gameplay screens.
- **Couch co-op**: the keyboard plus the first controller is player 1. Every
  other controller joins by pressing START. Use *Multiplayer > Remove local
  player* (or unplug the controller) to drop out.
- **Host**: choose *Multiplayer > Host a game*. LAN players see the game under
  *Find LAN games*. For internet play the game asks your router to open UDP
  port 47474 (UPnP); the menu shows the public address to give your friends.
  If UPnP is unavailable, forward UDP 47474 to your PC manually.
- **Join**: *Find LAN games*, or *Join by address* and type `ip` or `ip:port`
  (Ctrl+V pastes). Joining downloads the host's game and you appear next to
  the group.
- **Friendly fire**: toggle it in the Multiplayer screen (the host decides).
  When on, swords, arrows, rods, boomerangs, the hookshot and bombs hurt
  teammates.

## Rules of the shared world

- **One shared inventory.** Every item, key, rupee, bomb and arrow belongs to
  the team. Each player picks their own Y item in the item menu, and Start
  opens the menu for whoever pressed it.
- **Hearts are personal.** The four corners show P1 (top left), P2 (top right),
  P3 (bottom left) and P4 (bottom right). Heart containers raise everyone's
  maximum.
- **Colors**: P1 green, P2 red, P3 blue, P4 purple. These replace the armor
  colors, so mail upgrades still reduce damage but don't change the tunic.
- **Shared screen.** The camera follows the *leader*. Whoever opens a menu,
  talks to someone, takes stairs or walks through a door becomes the leader,
  and everybody else comes along to the new room. Other Links can't leave the
  screen.
- **Downed, not dead.** A Link that runs out of hearts becomes a flickering
  ghost. Stand next to them for 1.5 seconds to revive them with 3 hearts. It
  is game over only when everyone is down.
- **Enemies** go after the nearest Link and can be hurt by anyone.

## How it works (for modders)

The original game has exactly one Link, whose state lives at fixed RAM
addresses. `src/mp.c` gives each player a private copy of those RAM ranges
(the "Link context") and swaps the acting player's context into RAM:

- The **leader**'s context is in RAM at frame boundaries, so camera, rooms,
  menus and cutscenes work as in the original game.
- The gameplay modules run `Mp_RunOtherPlayers()` after the leader's player
  control, which swaps in each other Link and runs the same control code.
  If that code changes the game module, that player becomes the leader.
- Each enemy runs under the context of the nearest Link (or the Link whose
  sword is touching it). The shared damage checks
  (`Sprite_CheckDamageToLink`, `Sprite_CheckDamageFromLink`, ...) try every
  Link.
- Projectiles remember which player created them and run in that context.
- Extra Links are drawn into private sprite lists with their own copies of the
  pose-dependent Link tiles; the PPU (`snes/ppu.c`) draws them on top of the
  regular OAM and recolors palette 7 per player.

### Netplay

`src/session.c` describes every game frame as an `MpFrameInput`: all four
joypads, join/leave events, friendly fire, the enabled game features, and the
"music playing" flag the game reads from the sound chip (the only input that
otherwise depends on timing). The game is deterministic, so feeding the same
frames gives the same game everywhere.

- The host runs at 60 fps using the newest input from each client and streams
  the frames to everyone. It never waits for a slow connection.
- A joining client downloads a compressed snapshot (RAM, VRAM, SRAM, APU and
  co-op state) and fast-forwards through the frames since.
- Every second the host sends a hash of the game state. A client that differs
  reloads a fresh snapshot automatically, and so does everyone when the host
  loads a save state.
- Clients never write the host's world to their own save files.
- UDP only, port 47474. LAN games are found with a broadcast; UPnP mapping is
  in `src/upnp.c`.

All players should run the same build and the same `Language` setting.

### Testing

`./zelda3 --mptest <script>` runs the game headless from a script. The
commands are listed at the top of `src/mptest.c`, and `tests/run_coop_tests.sh`
runs the regression tests, including a real host/client pair over UDP.

## Known limitations

- The camera follows one Link at a time. There's no zoomed-out shared camera.
- The original game has 10 projectile slots and some items allow only one at a
  time (one boomerang, one hookshot). These limits are shared by the team.
- Followers (Zelda, the old man, ...) follow the leader.
- Netplay clients feel one round trip of input latency. There is no rollback
  yet, so it plays best on LAN or under about 80 ms ping.
- Some cutscenes and bosses were designed for one Link. During them, the
  other Links wait and then regroup with the leader.
