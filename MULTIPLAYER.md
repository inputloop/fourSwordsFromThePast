# Four player co-op

Up to four Links play through A Link to the Past together. Players can sit at
the same computer, be on the same LAN, or play over the internet, in any mix.
Players can drop in and drop out at any time.

## Playing

- **Menu**: press `Esc`, the controller's Guide button, or Back+Start.
  The menu has the Multiplayer, Video/HD and Gameplay screens.
- **Controls**: *Pause menu > Controls* sets up each player (P1-P4): two
  keyboard keys and two controller buttons per SNES button (press to assign),
  which controller the player uses (or keyboard only, so two players can
  share a keyboard), left stick deadzone, and an Xbox/Nintendo face button
  swap. A live "Pressed" line shows what the game sees. Arrows, Enter,
  Backspace and the d-pad always work in the menu.
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
- **Free roaming** is always on: nobody ever has to wait for the others or
  head the same way.
- Online, the pause and turbo hotkeys do nothing (they would stop or outrun
  everybody else). Use the menu; it doesn't stop the game online.

## Rules of the shared world

- **One shared inventory.** Every item, key, rupee, bomb and arrow belongs to
  the team. Each player picks their own Y item in the item menu, and the
  HUD's item box shows the item of the player whose screen it is.
- **Private menus and maps.** Start (item menu) and the map open for whoever
  pressed them, in a world of their own: the others keep playing, and when
  the menu is closed the worlds merge again.
- **Hearts and magic are personal.** The four corners show P1 (top left), P2
  (top right), P3 (bottom left) and P4 (bottom right), each with a small magic
  bar under the hearts. Heart containers raise everyone's maximum. The HUD's
  magic meter shows the magic of the player whose screen it is.
- **Colors**: P1 green, P2 red, P3 blue, P4 purple. These replace the armor
  colors, so mail upgrades still reduce damage but don't change the tunic.
- **Free roaming (default).** Everybody goes wherever they want. Players who
  are close together share a screen. A player who walks (mostly) out of view or
  through a door, stairs or a pit on their own continues in a world of their
  own with their own camera. When players meet again (same room or screen,
  close enough that their cameras line up), they share a screen again. Until
  then, players in the same room or screen still see each other walking
  around. Players on the same
  computer in different places get a split screen (side by side for two,
  2x2 for three or four). Netplay players always see their own Link.
  - Progress is shared between the worlds right away: items, rupees, bombs,
    arrows, keys, opened chests and doors, defeated bosses, pendants and
    crystals. Room contents (enemies, pots, switches) are per world until the
    worlds merge.
  - A follower (Zelda, the old man, ...) stays with whoever was leading the
    group.
  - The sound is from the world of the first player on this computer.
- **The leader.** In a group sharing a screen, whoever opens a menu, talks
  to someone or takes an exit becomes the leader that the camera, rooms and
  cutscenes follow. (In a dungeon, the room's "which half is Link in" state is
  reset to the new leader's position, or an exit could turn into a scroll.)
- **Downed, not dead.** A Link that runs out of hearts becomes a pale ghost
  that lies where it fell. Someone has to come and stand next to it
  for 1.5 seconds to revive it with half a heart. It is game over only when
  everyone is down; then everybody continues together.
  - A fairy in a bottle (shared by the team, like all items) saves a Link
    that would go down: the bottle empties and the Link gets up to 7 hearts.
  - A ghost lying alone in some room keeps its world, paused, until a
    standing teammate enters that room or screen; then the ghost is there,
    right where it fell. That also works when the ghost's world got stuck on
    a text box (nobody there can press A).
  - While down, your screen shows a standing teammate (if your world is
    paused) or your own Link; L and R switch between them.
  - If Zelda (or another follower) was following the ghost, the reviver
    joins the ghost's world instead, so the follower isn't lost.
- **Enemies** go after the nearest Link and can be hurt by anyone.

## How it works (for modders)

The original game has exactly one Link, whose state lives at fixed RAM
addresses. `src/mp.c` gives each player a private copy of those RAM ranges
(the "Link context") and swaps the acting player's context into RAM:

- The **leader**'s context is in RAM at frame boundaries, so camera, rooms,
  menus and cutscenes work as in the original game.
- Exits (`Mp_RunRouting`): if a player takes an exit alone, that player
  continues in a new world (see below). The engine also still has the old
  "group exits" shared-screen mode (`Inst_SetIndependent(false)`), which
  the game no longer offers; `tests/coop/group_exits.txt` keeps it working.
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

### World instances (free roaming)

`src/instance.c` lets players be in different places although the game only
knows one world and one camera. Every group of players gets its own copy of
the game: RAM, PPU and DMA state, and the co-op state above. Each frame every
world is simulated in turn (`Inst_RunFrame`), switching `g_ram` and
`g_zenv` between them.

- **Split.** When a player takes an exit alone, the frame's result becomes a
  new world for that player, and everybody else replays the frame from a
  snapshot without them. A player walking out of their group's view is
  copied into a new world without replaying.
- **Merge.** Two worlds in the same room or overworld screen, with one
  world's players in view of the other's camera and the two cameras within
  48 pixels of each other, become one world.
- Each world tracks which area it is in, even with one player, so players
  merging in are never mistaken for a group arriving (which would put them
  on top of the leader).
- **Shared progress.** After every frame, the save file area of RAM
  (`7EF000-7EF4FF`) is synced: changed bytes are copied to every world, room
  and overworld flags are combined, and counters (bombs, arrows, rupees,
  keys) add up the changes of each world. Health and magic, the dungeon key
  counter in use, refills in progress, light/dark world and the follower
  stay per world.
- Snapshots and the netplay state hash cover all worlds, so netplay works
  unchanged.
- **Seeing other worlds.** Each world records its leader's Link sprites
  (`Mp_BeginLeaderOam` / `Mp_EndLeaderOam`) next to the other players'. When a
  screen is drawn, `Inst_LinksFromOtherWorlds` lists the Links of every other
  world in the same room or area, and they are drawn as extra sprite sets,
  moved by the difference between the two cameras. So players walking toward
  each other are visible before their worlds merge.

### Netplay

`src/session.c` describes every game frame as an `MpFrameInput`: all four
joypads, join/leave events, friendly fire, the enabled game features, and the
"music playing" flag the game reads from the sound chip (the only input that
otherwise depends on timing). The game is deterministic, so feeding the same
frames gives the same game everywhere.

- The host runs at 60 fps and streams the frames to everyone. It never waits
  for a slow connection.
- **The host rolls back too.** The host also runs ahead with guesses. When a
  client's buttons arrive after the host already guessed them, the host goes
  back and simulates again with the real ones. A frame becomes final, and is
  sent to the clients, only once everyone's buttons for it are known (a client
  that is still loading or has been silent for half a second isn't waited for,
  nor for longer than 30 frames). So nobody's own Link is ever moved by a
  guess: no rubber banding, and a door, stairs or screen edge is taken exactly
  where its player took it. Joining clients get a snapshot of the newest final
  frame.
- **Inputs are scheduled.** A client stamps every joypad state with the frame
  it is meant for and sends it ahead of time (each one 8 times, against packet
  loss). The host tells each client how early its inputs arrive, and the client
  runs a little faster or slower so they arrive 1-3 frames before the host
  needs them. So the host almost never has to guess a client's buttons.
- **Clients predict and roll back.** A client doesn't wait for the host: it
  runs ahead using its own buttons right away and guesses that everyone else
  keeps holding what they held. Each predicted frame's state is kept in memory
  (`Inst_SnapshotSave`). When the host's real frame differs from the guess, the
  client goes back to the last confirmed frame and simulates again (a game
  frame takes about 45 microseconds, so this is invisible). Sounds of frames
  that are simulated again aren't played twice.
- **Corrections are smoothed.** When a rollback moves another player's Link,
  it slides there over a few frames instead of jumping (`g_mp_draw_offset`,
  drawing only). The Link that leads a world isn't smoothed, since the camera
  follows it.
- **Input delay.** Online, button presses take effect `InputDelay` frames
  later (default 2, about 33 ms; set it in the Multiplayer menu). Every
  computer then learns everyone's buttons 2 frames sooner, so guesses are wrong
  less often and for less time.
- `ZELDA3_NETSIM=delay_ms,jitter_ms,loss_percent` simulates a bad connection
  for testing (it delays and drops received packets).
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

- Players sharing a screen share one camera, which follows the leader.
  There's no zoomed-out camera; walking out of view gives a player their own.
- Free roaming with up to four worlds costs up to four times the CPU, which
  is still far below what any PC needs.
- Room contents in two worlds can differ (for example an enemy killed in one
  world still lives in the other) until the worlds merge. Then the world
  that stays keeps its room. A chest opened in one copy of a room may still
  look closed in another copy, but it is empty there.
- The original game has 10 projectile slots and some items allow only one at a
  time (one boomerang, one hookshot). These limits are shared by the team.
- Followers (Zelda, the old man, ...) follow the leader of their world.
- Online, other players' Links can still be corrected a little when they
  change direction, more so on high ping. The host's own view never needs
  corrections.
- Some cutscenes and bosses were designed for one Link. During them, the
  other Links wait and then regroup with the leader.
