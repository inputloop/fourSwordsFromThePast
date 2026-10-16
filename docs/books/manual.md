<!-- GB:book title="Four Swords of the Past: the Player's Book" author="Mentat (for Corwin), with Teodoro Ilunga's sheet at the desk" -->

# Four Swords of the Past: the Player's Book

*A game book. Start at §1 and follow the routes; each section ends by telling you where to go next.
Or open any section from the list: nothing here needs the one before it, except the first.*

*Written by an AI for Corwin Brust; who is talking, and whose game this is, is at the end of §17.*

*(Seventeen sections is a number we picked, not one we counted.)*

## §1 · The one thing every route shares

<!-- page: first -->

I count six ways to play this game: alone; on one computer with friends; on a home network; over the
internet; with the items shuffled; and in an Archipelago multiworld with other games. Six routes.

Now the check I owe you before the count means anything. Is there one failure that closes all six?
There is. **Every route needs your own copy of the original game** (a ROM file of *A Link to the Past*,
the US version). The game's pictures, music, maps and words are not in the download; the game makes
them from your ROM, once, into a file called `zelda3_assets.dat`.

So the six routes are really one road that forks later.

If you already have the game running, turn to §4.
If you have your ROM but not the game yet, turn to §2.
If you are not sure what a ROM is or why you must bring your own, turn to §3.

## §2 · Getting the game running

<!-- page: install -->

Builds for Windows, macOS and Linux come from the project's GitHub page (the *Build* runs; you need
to be logged in to download them). On every system the steps are the same three:

1. **Unzip** the build into a folder of its own. (Running it from inside the zip does not work.)
2. **Copy your ROM** into that folder. Any file name is fine.
3. **Start the game** and choose *Extract now*. This makes `zelda3_assets.dat` from your ROM. It
   needs Python 3 once; on Windows, tick *Add python.exe to PATH* when you install it. On a Mac,
   `extract_assets.command` does the same with a double-click.

That is the whole install. The ROM is not needed again after the extraction.

If the game says it cannot find its assets, turn to §15.
Otherwise, turn to §4.

## §3 · Why you bring your own ROM

<!-- page: rom -->

The code in this project is free software: you may read it, change it and share it. The game's
pictures, music, maps and text are not; they belong to Nintendo, and they are in the ROM.

So the project draws the line there. Everything made from your ROM (the ROM itself,
`zelda3_assets.dat`, your own saves, your screenshots) stays on your computer. (The project keeps a
few screenshots of its own on its web page, to show what the game looks like; that was its
maintainers' choice. The game data itself is never in it.) **Never share those files,
and never put them in a copy of the project you send to someone.** Each player brings their own,
including the friends you play with online.

The ROM must be the US version. Its SHA-1 is `6d4f10a8b10e10dbe624cb23cf03b88bb8252973`, if you
want to check yours.

Turn back to §2.

## §4 · The title screen, and your file

<!-- page: starting -->

The title says *Four Swords of the Past*, and the legend after it is retold: a blade that split one
knight into four heroes, in green, red, blue and purple. (A build may leave the retold story out; the
original legend plays then. See §14.)

Pick a file and a name, as in the original game. A new file can start shuffled (§12) or with every
item, for testing (*Gameplay options*).

From here the routes fork. Count yours:

- Alone, at this computer: turn to §5.
- With friends at this computer: turn to §7.
- With friends somewhere else: turn to §9.
- With the items shuffled: turn to §12.

## §5 · Out in the world

<!-- page: outdoors -->

The keyboard: arrow keys to walk, **X** is the A button, **Z** is B (your sword), **S** is X, **A**
is Y (your item), **Enter** is Start, **Right Shift** is Select. A controller works as the SNES pad
did. **Esc** opens the menu (so does the controller's Guide button, or Back with Start).

Everything else is the game you may remember. What is new shows at the edges of the screen: your
hearts in your own corner, a small map, and, when you play with others, arrows pointing to
teammates who are off the screen.

Two keys worth knowing now: **F12** saves a picture of the screen into a `screenshots` folder next
to the game, and **Ctrl+F12** saves the same picture without the mod's labels and arrows.

Going indoors? Turn to §6.
Opening the menu? Turn to §8.
Want a different look (wider, softer, or in 3D)? Turn to §10.

## §6 · In a dungeon

<!-- page: dungeons -->

Dungeons play as they always did: keys, chests, switches, a boss at the bottom. Two differences come
from the co-op.

**Everything you find is shared.** Keys, items and rupees belong to the whole team, so a key one
player picks up opens a door for another.

**Floor switches hold for anyone.** Any Link standing on a switch holds it down, so two players can
keep a door open for a third.

If you fall, your Link becomes a ghost; a teammate standing next to you brings you back. Alone, it
is the original game over.

Playing with others in here? Turn to §7.
Back outside? Turn to §5.

## §7 · Together on one computer

<!-- page: together -->

The keyboard plus the first controller are player 1. **Every other controller joins by pressing
START.** You become player 2, 3 or 4, in red, blue or purple; player 1 is green. (*Multiplayer >
Player colors* has a set chosen so that players with any kind of colour blindness can tell them
apart.)

What changes when you are more than one:

- **Each player has their own hearts**, in their own corner. The items are shared.
- **You can split up.** The camera keeps the group in view; when one player wanders off, the screen
  splits, and joins again as you come back together.
- **A Link with no hearts becomes a ghost.** Stand next to the ghost to bring them back.
- **You can pick a teammate up and throw them**, over a pit if you like, and two of you can lift the
  heavy rocks one Link cannot.
- **Pings**: a button (set it under *Controls*). A tap puts a *look here* ring just in front of you;
  holding it calls *help* where you stand.
- **Watch everyone**: **Ctrl+F11** (or *Multiplayer > Watch everyone*) shows every player's world on
  screen at once, side by side or in four quarters. Press it again for your own screen. It only
  changes the picture, so it works online too.
- **Friendly fire** is off unless you switch it on (*Multiplayer*).

To leave: *Multiplayer > Remove local player*, or unplug the controller. Coming back gives you the
hearts you left with.

Want to add friends who are not in the room? Turn to §9.
Want to know what each menu screen holds? Turn to §8.

## §8 · The menu

<!-- page: menus -->

**Esc** opens it. In co-op the game does not pause while one player looks at it, so be quick, or be
somewhere safe.

The screens, and the route to each one's section:

- **Multiplayer**: your name, the players, hosting and joining, name tags, colours, friendly fire.
  For playing online, turn to §9; for players on this computer, §7.
- **Video and HD options**: widescreen, the HD world map, smooth scaling, no sprite flicker, dimmed
  flashes, the 2D-HD look and the 3D view. Turn to §10.
- **Sound**: three volumes, each from 0 to 100 percent: master, music and effects. Turn to §11.
- **Gameplay options**: the randomizer, the opening story, new files with all items, and the
  original game's optional fixes. For the randomizer, turn to §12.
- **Controls**: each player's keys and buttons. Turn to §11.
- **Controls help**: every key and button on one page. **Save settings**, and **Quit game** (press it
  twice).

Settings are saved when you close the menu, in `zelda3.coop.ini` next to the game.

If a screen in your menu has fewer lines than this section describes, your copy was built without
some mods. Turn to §14.

## §9 · Playing online

<!-- page: online -->

One player hosts; the others join. No server sits in the middle.

**To host**: *Multiplayer > Host a game*. The menu shows a **room code** (something like
`FW00-00DS-EB52`); select it to copy it, and send it to your friends. Players on your home network
can also find your game under *Find LAN games*. The game asks your router to open the way in (UDP
port 47474); if your router will not, the menu says so and tells you which port to forward.

**To join**: *Multiplayer > Join by code or address*, and paste the code (Ctrl+V). Lower case,
missing dashes and the letter O for zero are all fine. You download the host's game and appear next
to the group.

**If the connection drops**, your seat waits two minutes for you, and you come back with your hearts.

**If it says "Different game version or mods"**: your two copies are not the same game. Either one
of you has an older build, or one copy was built with a mod that changes how the game plays and the
other without it. Each of you can run `zelda3 --mods` to see what your copy carries. Turn to §14.

*Online input delay* (in *Multiplayer*) trades a little responsiveness for fewer corrections on a
slow line; it can only be changed while you are not connected.

Back to playing together? Turn to §7.
Something not working? Turn to §15.

## §10 · How it looks

<!-- page: looks -->

The game can look like the original, or not.

- **Widescreen**: 16:10, 16:9 and 18:9, as well as the original 4:3.
- **2D-HD look** (beta): depth of field, bloom, warm light from torches and the lamp, dark rooms lit
  by what is burning in them. Its settings are under *Video and HD options > 2D-HD settings*.
- **3D view** (beta): the world as blocks and standing pictures, with a camera you turn (the right
  stick, or drag with the right mouse button). A left click, or pressing the left stick in (L3), goes to
  **first person**. **F11** switches the 3D view on or off for a while. Each player on a couch picks
  2D or 3D for themselves.

None of these change the game itself: your friends online can each choose their own.

If your menu has no 2D-HD or 3D lines, your copy was built without them. Turn to §14.
Otherwise, back to the world: turn to §5.

## §11 · Sound and controls

<!-- page: sound-and-controls -->

**Sound.** *Sound* has master, music and effects volumes. **Shift with =** and **Shift with -**
change the master volume while you play. On a couch you hear everyone's sound effects and player 1's
music; online, each computer hears its own player's world.

**Controls.** *Controls* sets up each player: two keyboard keys and two controller buttons for every
SNES button, which controller is whose, the stick's dead zone, and a swap for controllers whose face
buttons are the other way round. A *Pressed* line shows what the game sees, so you can check a pad
before you play.

Most keys the game listens for are also in `zelda3.ini`, in plain words (`Screenshot = F12`), if you
prefer to edit a file. (The ping and view buttons are set in *Controls*; Esc always opens the menu.)

Turn back to §8.

## §12 · The items, shuffled

<!-- page: randomizer -->

The randomizer moves the items to new chests, and still makes sure the game can be finished.

*Gameplay options > Randomize new games*: **on, new seed** shuffles every new file differently; a
fixed **seed** (*Enter a seed*) shuffles the same way every time, so friends can race the same
shuffle. A shuffled file starts open: no rainy-night escape, and you can go almost anywhere at once.

*Write spoiler log* saves where everything went, for when you are stuck (or for after). The
**tracker** after the map shows which places still hold something.

Want to shuffle with players of other games? Turn to §13.
Back to choosing a route? Turn to §4.

## §13 · Archipelago

<!-- page: archipelago -->

[Archipelago](https://archipelago.gg) is a multiworld: several games, each player's items hidden in
the others' worlds. Find someone else's sword in a chest here, and it appears in their game.

*Multiplayer > Archipelago* connects to a room: the server's address, your slot name and the
password. *DeathLink* (if the room uses it) ties the games together: when one game ends in a game over, the
others' do too. (In co-op, one Link going down is not a death; the team's game over is.)

To make a room you need this game's YAML file (`Four Swords of the Past.yaml`, which comes with the
Windows build) and the Archipelago software, from its own site.

Online with friends as well? Turn to §9.
Back to the shuffle on its own? Turn to §12.

## §14 · Which mods your copy has

<!-- page: builds -->

**Can we play together tonight?** If you all downloaded the same build from the project's page, yes.
The rest of this section is for when someone built their own copy.

This game is made of the original plus several **mods**: the opening story, the screenshots, the
2D-HD look, the 3D view, the randomizer, Archipelago, online play, and the help key. Whoever builds
a copy chooses which of them it carries. A mod that is left out is not hidden; it is simply not in
the program, and its menu lines are gone.

To see what your copy carries, run it with `--mods`, from a terminal in the game's folder:

```
zelda3 --mods
```

It prints the mods that are in, by name.

Two copies can play online together only if the mods that change how the game plays (the story, the
randomizer, Archipelago, online play) are the same in both. Mods that only change the look or your
own computer (2D-HD, 3D, screenshots, the help key) do not matter.

To build your own copy with a different set, that is the other book, the developer's guide.

Something else wrong? Turn to §15.
Back to the start of a game? Turn to §4.

## §15 · When something is wrong

<!-- page: trouble -->

Four things go wrong more than others. For each, here is what it means and what to do.

**"zelda3_assets.dat is missing"**, and the game offers to extract: the assets have not been made
from your ROM yet. Put the ROM in the game's folder and choose *Extract now*. If that fails, Python
3 is missing (§2).

**"Different game version or mods"**, when joining: the two copies differ (§9, §14). Use the same
build, or builds with the same game-changing mods.

**macOS says it "cannot be opened"**: the build is not notarized by Apple, so the download is held
back. Right-click it, choose *Open*, and confirm.

**No sound, or the wrong player's music**: check *Sound* (§11). On a couch, the music is player 1's.

If none of these is yours, the community is on the project's Discord (linked from the README), and
bug reports go to the project's GitHub page.

To check the beginning again, turn to §1.
To read about this book's help key, turn to §16.

## §16 · The help key, and this book

<!-- page: help -->

In a copy built with the **helpbook** mod, **Ctrl+H** opens this book at the section for what you are
doing: the menu if it is open; §4 at the title screen, the legend, file select or a game over; §17
at the credits; §9 online; §7 with friends; §6 in a dungeon; and §5 outdoors. On Windows it opens as a help window (the `.chm` file in the `help` folder); on other
systems, in your browser.

The help key is off in the copies the project ships; it is a new mod, and a builder switches it on.
If the help window opens with blank pages, Windows is holding back a file that came from the
internet: right-click `FourSwords-Manual.chm`, choose *Properties*, tick *Unblock*, and open it again.
The key can be changed in `zelda3.ini` (`Help = Ctrl+H`).

This book is also in the `help` folder as web pages, as one long page, and as plain text, and its
source is `docs/books/manual.md`.

For the last word, turn to §17.

## §17 · What this book did not count

<!-- page: last -->
<!-- end -->

Six routes in §1, and one shared road under them. Here are the things I did not count, so you know
the edges of this book:

- **The game itself.** How to beat a dungeon or find a heart piece is the original game's business,
  and there are fine guides for it elsewhere.
- **How co-op works inside.** `MULTIPLAYER.md` and `docs/HOW_THE_MOD_WORKS.md` tell that, in plain
  words, at length.
- **Making your own changes.** That is the other book: *the Developer's Guide*.

**Who was talking.** This book was written by an AI (Claude, working as "Mentat" for Corwin Brust, who
makes things with Bryant in the inputloop organisation), with one character sheet at the desk:
Teodoro Ilunga, an author from the game books Corwin's workshop keeps. He is fictional. He is here
for one habit: he counts the ways from where you are to where you want to be, and then checks whether two of those ways are really the same way. I wrote
every word; his sheet decided what to count.

**Whose game this is.** The game is Nintendo's *A Link to the Past* (1991), rewritten in C by snesrev
(zelda3), with LakeSnes's picture and sound chips, and turned into four-player co-op by Bryant
Frankford with his agent Alfred. This book is new; the game is theirs.
