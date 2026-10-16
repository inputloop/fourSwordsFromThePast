<!-- GB:book title="Four Swords of the Past: the Developer's Guide" author="Mentat (for Corwin), with Delphine Roux-Abara's sheet at the desk" -->

# Four Swords of the Past: the Developer's Guide

*A game book for people who change the game. Start at §1, or go straight to the section for the
moment you are in. Every section tells you where to go next; none of them strands you, and a program
checks that (§14).*

**Who is talking.** The instrument that built the recipe branch (Mentat, an AI, writing for Corwin)
wrote this, with one character sheet at the desk: Delphine Roux-Abara, an author from the lab's own
game books, who is fictional and holds one conviction: the hard part of a big system is not learning
it, it is *finding the table again*. So this book is arranged by the moment you are in, not by the
order the code was written.

**Before you read on, do one thing.** Build the game once, exactly as it is (`make -j`, with your ROM
as `zelda3.sfc`), and run `sh tests/run_coop_tests.sh`. Every route below assumes you have a green
run to come back to.

## §1 · What are you about to change?

<!-- page: first -->

Pick the one that is true, and go.

- You want **a different set of mods in your build**: turn to §2.
- You want to **write a new mod**: turn to §6.
- You want to **change the game itself** (Link, enemies, a room): turn to §11.
- You want to **change these books**: turn to §14.
- You want to **know how this branch was put together, and why**: turn to §16.

## §2 · Choose your mods

<!-- page: choose -->

Make a file called `mods.cfg` at the top of the tree. It is yours: git ignores it. Write only the
lines you want to change; every other mod keeps its default.

```
+helpbook          # switch a mod on
-archipelago       # switch a mod off
hd3d off           # the same thing, in words
```

Then build as usual. `make` runs `python3 tools/mods.py write` first; it rewrites
`src/mods_build.h` only when your choice changed, so a rebuild is quick when nothing did.

To see what you have chosen, and where each choice came from:

```
python3 tools/mods.py show
```

What each mod is: turn to §3.
The tool refused your file: turn to §4.
You would rather choose in the RECIPE, or in C4: turn to §5.

## §3 · The catalog

<!-- page: catalog -->

`mods/catalog.tsv` is the one place a mod is declared: its name; whether it changes the game's
**simulation** (the `sim` column: `yes` if online players must match); the mods it **requires**; the
**source files** that are the mod; the **tests** that need it; a one-line summary. The tool reads it;
nothing about a particular mod is written in the tool.

`mods/default.cfg` says which mods a plain build carries. The shipped set is the game as Bryant ships
it, plus every new mod **off**.

I will not copy the table here, because a copy is a table that goes stale. Print it:

```
python3 tools/mods.py show
```

Two things the table cannot show you, both measured with the census (§7):

- **Co-op is not a mod.** It is the game now: 103 of its functions are used from outside its own
  files. Nothing switches it off.
- **Online play is the network layer only** (`net.c`, `upnp.c`). The session code also runs couch
  co-op, so it stays.

Back to choosing: turn to §2.

## §4 · When the tool says no

<!-- page: refusals -->

`tools/mods.py` refuses a composition, says why in words, and exits 1. These are the refusals, each
with its clause in `mods/SPEC.md`:

| it says | what you did | clause |
|---|---|---|
| `"x" is not a mod` | a name not in the catalog (a typo, often) | M5 |
| `should be on or off` | some other value | M6 |
| `X is on ... but needs Y, which is off` | switched off something another mod needs | M7 |
| `switched both on and off in this file` | named one mod twice, both ways | M8 |
| `does not exist` / `claimed by both` | a catalog row is wrong | M9 |
| `does not match mods/default.cfg` | changed the default without `write-default` | M21 |

Every row sends you to the same place: fix the line it names, and go back to §2.

## §5 · From C4 to the build

<!-- page: c4 -->

The recipe at the top of the tree (`RECIPE.org`) holds a block like this:

```
#+begin_src mods
+helpbook -archipelago
#+end_src
```

`python3 tools/mods.py from-recipe RECIPE.org` checks that block and copies it to your `mods.cfg`.
That is the whole bridge from a document to a build: a person, or an agent handed the RECIPE, edits
the block in words and runs one command.

C4 sits one step further back. In C4 a change is written as a **campaign**: a stream of lines that
each name one act, in a file ending `.assy`. The lab's rule for campaigns is that a line which does
not resolve is a comment, never an error, so you can write prose between the acts:

```
# the laptop at the cabin has an old graphics chip
c4.m.mods ADD hd3d as(off)
c4.m.mods ADD helpbook as(on)
```

`python3 tools/mods.py from-assy CAMPAIGN.assy --into RECIPE.org` turns that into the RECIPE's
block. A line that names something that is not a mod is kept as a comment, as C4 requires, but it is
**said out loud**, so a misspelt mod never vanishes in silence.

The route, end to end: edit in C4, plan it (`sh h/assy.sh plan` in a C4 tree, which never acts),
write it into the RECIPE, hand the RECIPE on, `from-recipe`, `make`. The worked example is in
`RECIPE.org`.

Back to choosing by hand: turn to §2.
To see how the tests hold this route: turn to §9.

## §6 · Write a new mod

<!-- page: new-mod -->

Here is the whole job, in order. Do them in this order; every later step leans on an earlier one.

1. **Write the mod** as one or more new files in `src/`, with a header for whatever the rest of the
   game calls.
2. **Declare it**: a row in `mods/catalog.tsv`, a line in `mods/default.cfg` (new mods ship `off`),
   then `python3 tools/mods.py write-default`.
3. **Guard its files**: `python3 tools/mods.py guard --fix`. Each file now starts with
   `#include "mods.h"` and `#if MOD_<NAME>` and ends with `#endif`.
4. **Find its surface**: what the rest of the program calls into it. Turn to §7 for how.
5. **Write the stand-ins** in `src/mods_off.c`, one for every name in that surface. Turn to §8.
6. **Hide its menu lines** with `#if MOD_<NAME>` in `src/ui.c`.
7. **Add both new files to Visual Studio's lists** (`zelda3.vcxproj` and `zelda3.vcxproj.filters`):
   it does not find files by itself. The Makefile and the MinGW script do.
8. **Test it**, both ways: turn to §9.

Want to see all eight steps done on one real mod? Turn to §12.

## §7 · The census: what does the rest of the game touch?

<!-- page: census -->

Do not guess a mod's surface; measure it. After a normal build:

```
python3 tools/mods_census.py GROUPS.tsv src snes
```

`GROUPS.tsv` names each mod's object files (`hd2d<TAB>hd2d.o`). The census reads the compiled
objects' symbol tables (`nm`) and prints every name a mod defines that something outside it uses,
and who uses it. That list is exactly the set of stand-ins you owe.

The census gives you the surface. It does not give you the **meaning**, and the meaning is where a
mod can catch you. Turn to §15 and read the first erratum before you write a single stand-in.

Then turn to §8.

## §8 · Stand-ins

<!-- page: stand-ins -->

A stand-in is what the rest of the game gets when your mod is not in the program. One rule covers
all of them: **do what the game would do if the mod had never been written.**

- A question gets the answer "no" or "nothing": `Hd3d_PaneIs3D` is false, `Rando_CurrentSeed` is 0.
- A change passes its input through untouched: `Rando_ChestItem(idx, vanilla)` returns `vanilla`.
- An action does nothing, and where the game expects a reason, gives one in words:
  `Hd3d_LastWhy2D` says *the 3D view is not in this build*.
- **Anything the original game needs keeps happening.** If the mod's file was also the road the
  ordinary game ran down, the stand-in keeps the road and drops only the mod. (§15, first erratum.)

Bryant's code makes most of this easy, because it already asks before it acts: every randomizer hook
checks `Rando_CurrentSeed()` first, and every 3D path checks `g_hd3d.enabled`. A stand-in that
answers "not on" sends the game down the original path.

Then test it: turn to §9.

## §9 · Test it, both ways, and prove the tests can fail

<!-- page: testing -->

Three layers. Run them in this order.

1. **The tool's own tests**: `sh tests/mods/run.sh`. One test or more for each clause of
   `mods/SPEC.md`, written in shell, a different language from the Python tool, so a misreading of the
   spec in one is not copied into the other. Then `sh tests/mods/run.sh --prove-red`: it plants eight
   faults in a copy of the tool, one at a time, and each must turn the tests red. A test suite that
   has never failed has not been tested.
2. **The game's tests**: `sh tests/run_coop_tests.sh`. Tests that need a mod your build leaves out
   say `SKIP` and why (the catalog's `tests` column).
3. **The matrix**: `sh tools/mods_matrix.sh OUT`. It builds the shipped set, each mod off alone,
   and all of them off, and runs *every* test in each, skipping nothing, so you learn which tests
   really need which mod. It writes `OUT/builds.tsv` (sizes, times) and `OUT/tests.tsv` (every
   result). It takes about an hour. Compare its findings with the catalog's `tests` column, and
   believe the matrix.

Changing what goes online? Turn to §10.
All green? Turn to §11 if you are changing the game itself, or back to §1.

## §10 · Online: which builds may play together

<!-- page: online-safety -->

Online, every computer runs the same game, frame by frame. Two programs that would make different
choices from the same buttons cannot play together; they drift apart within seconds.

So a mod marked `sim yes` in the catalog changes the **wire version** the game sends when it joins.
A build whose `sim` mods match the shipped set sends the plain version, exactly as before this branch,
and plays with builds made without it. A build with a different set sends a tag made from which `sim`
mods it has, and two builds with different tags refuse each other with *Different game version or
mods*.

When you write a mod, mark it `sim yes` if it can change anything the game remembers or decides.
Looks, sound and things on your own computer are `sim no`. When in doubt, `yes`: a refusal is
annoying; a desync is worse.

Back to testing: turn to §9.

## §11 · Changing the game itself

<!-- page: game -->

For a change to Link, an enemy, a room or a rule, the existing modding guide is the place:
`docs/MODDING.md`. It has the map of the code, the **golden rules** that keep co-op and online play
in step, and recipes (Link's walking speed, a new option, an overlay, a hotkey and more). Read its
golden rules before you change anything that runs every frame.

If your change is big enough to want an off switch, it is a mod. Turn to §6.

## §12 · Worked example: the helpbook mod

<!-- page: helpbook -->

The helpbook mod is new on this branch and small enough to read whole. **Ctrl+H** opens the player's
manual at the page for the moment: the menu, the title screen, online, together, a dungeon, outdoors.

The eight steps of §6, as they were done:

1. `src/helpbook.c` decides the page (`Helpbook_Topic`) and `src/helpbook_os.c` opens it (the
   Windows help viewer, or the browser). Two files, for a reason printed in §15.
2. `mods/catalog.tsv`: `helpbook`, `sim no`, no requirements, both files, tests `helpbook*`.
   `mods/default.cfg`: `helpbook off`.
3. `guard --fix` put the guard around both files.
4. The surface: `Helpbook_Topic`, `Helpbook_Open`.
5. The stand-ins: a topic of `""` and an `Open` that does nothing.
6. No menu line (a key is enough), so nothing to hide. The key's name, `Help`, is read from
   `zelda3.ini` in every build, so one `zelda3.ini` serves all of them; it does nothing in a build
   without the mod.
7. Both files are in the Visual Studio project.
8. `tests/coop/helpbook.txt` checks the key and four of the pages; the test runner skips it in builds
   without the mod. A wrong page planted in it turns it red.

Test your own the same way: turn to §9.

## §13 · Building on Windows

<!-- page: windows -->

Three ways, and each one chooses the mods first.

- **Visual Studio** (`Zelda3.sln`): a pre-build step runs `python tools\mods.py write`.
- **MinGW from Linux** (`tools/build_windows_mingw.sh`): runs the same, and copies the books
  (`help/`) into the ready-to-play folder if you built them.
- **`run_with_tcc.bat`**, on the player's own PC: if there is no Python, it uses
  `src/mods_default.h`, the shipped set, committed. `mods.py check` refuses if that file ever stops
  matching `mods/default.cfg`, so it cannot go stale unseen.

**Other platforms.** The Nintendo Switch build, removed upstream, is kept in
`src/platform/switch/` and built by nothing unless you ask (its README says how). A whole platform is
"off by default" by not being built; a mod is "off" by not being in the program.

Back to choosing mods: turn to §2.

## §14 · These books

<!-- page: books -->

Each book is one Markdown file, `docs/books/manual.md` and `docs/books/devguide.md`. A section starts
`## §N · Title`; any `§N` in the text is a route; `<!-- page: NAME -->` names the section's page;
`<!-- end -->` marks the one section allowed to have no way on.

`python3 tools/books.py check` refuses a book in which a route goes nowhere, a section cannot be
reached from §1, a section strands its reader, or a page the help key can ask for is missing.
`python3 tools/books.py build` (or `make books`) makes the `help/` folder: a Windows help file
(`.chm`) for each book, the pages as HTML, each book on one page, and plain text.

The markers at the top are the lab's game-book markers, so the lab's own linter (`c4/h/gb1md`) can
read these books too: a second reader.

Then turn to §15, which is where this book keeps its own mistakes.

## §15 · Errata, printed beside the error

<!-- page: errata -->

A book about finding the right page owns the pages it got wrong. These are the mistakes made
building this branch, each caught, each with what caught it.

1. **The black screen that did not ship.** The 2D-HD mod's file also holds `Hd2d_DrawFrame`, which is
   how *every* frame is drawn. A stand-in that "did nothing" would have drawn nothing. Caught by
   reading the census list before writing the stand-ins; the stand-in keeps the drawing and drops the
   look. *Lesson: the census measures a surface, not a meaning (§7).*
2. **The rebuild that did not rebuild.** The first Makefile rule made the header from a phony target
   with no recipe of its own, so `make` never saw the header change, and an all-off build came out
   byte for byte the same size as an all-on one. Caught by the size.
3. **The wrong file guarded.** The first catalog put `session.c` under online play; `guard --fix`
   wrapped it. Caught by reading the census, which shows couch co-op running through it.
4. **The half guard.** `guard --fix` would have wrapped a file that already had half a guard. Caught
   by the tool's own test for clause M19, on its first run.
5. **The key that was not free.** The first helpbook test said Ctrl+F1 does nothing. It replays a
   saved state. Caught by the test.
6. **The Windows header.** `windows.h` and the game's names for its memory cannot share a file; the
   Linux build was fine and the Windows build failed. Caught by building for Windows; it is why the
   helpbook mod is two files.
7. **The sealed dungeons that did not ship.** Upstream's change taught Archipelago eleven new answers
   the game asks for. One of them, `Ap_MedallionOpens`, also holds the original game's rule (Ether
   opens Misery Mire, Quake opens Turtle Rock). A stand-in answering "no" would have sealed both
   dungeons in every build without Archipelago. Caught by reading each new name's caller before
   writing its stand-in, the habit the first erratum taught. The census found the eleven; reading
   found the one.

What none of these caught is the next one. Turn to §16.

## §16 · The recipe, and the models behind it

<!-- page: recipe -->

`RECIPE.org` at the top of the tree is the literate recipe for this branch: what each part is, in
order, with the commands that make it and what you should see. Read it top to bottom once.

`CONTEXT-MODELS/` holds the models the work was planned from: each mod speaking for itself, in the
first person, with its numbers; the readers these books were written for; the contributors (whose own
words go in their own files, written by them and nobody else); and the vocabulary the project uses.

For the flags, by moment, turn to §17.

## §17 · The flags

<!-- page: flags -->
<!-- end -->

By moment, not by chapter. Put your finger on the one you are in.

| the moment | the section |
|---|---|
| "I want a smaller build" | §2 |
| "what is this mod for?" | §3 |
| "the tool said no" | §4 |
| "I have a RECIPE / a C4 campaign" | §5 |
| "I'm writing a mod" | §6, then §7, §8 |
| "what does it call?" | §7 |
| "what should the off version do?" | §8 |
| "is it tested?" | §9 |
| "my friend can't join" | §10 |
| "I'm changing Link / a room" | §11 |
| "show me one done" | §12 |
| "Windows" | §13 |
| "the books" | §14 |
| "what went wrong before?" | §15 |
| "why is it like this?" | §16 |
