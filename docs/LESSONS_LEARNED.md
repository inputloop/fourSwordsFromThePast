# Lessons learned: Four Swords From the Past

This file is written to be handed to the next project. It collects what this one learned, from the
first commit (a fresh import of the zelda3 port, 2026-10-10) to the merge of the recipe branch
(2026-10-17): a four-player co-op port with online play, a randomizer, two Archipelago worlds, a
3D view, a mods system and two books. Each lesson says what happened here and what to carry
forward. The project's own guides (`MULTIPLAYER.md`, `docs/HOW_THE_MOD_WORKS.md`,
`docs/MODDING.md`, `docs/books/devguide.md`, `RECIPE.org`) say how the code works; this file
says what the work taught.

Facts the lessons lean on:

| what | number |
|---|---|
| commits on the default branch | 183 |
| working days | 8 |
| C source files, lines | 56 files, about 110,000 lines |
| headless game tests | 187 scripts under `tests/coop/` |
| other test layers | unit (`tests/unit/`), mods tool (`tests/mods/`), recipe (`tests/recipe/`), books (`tests/books/`), Archipelago mock and real-server end-to-end (`archipelago/tests/`) |

## 1. Start from an oracle you can check against

The game was ported from a machine-exact reimplementation, so the original behaviour was always
available as the answer key. Every big feature was pinned to it with an equality test rather than
with a judgement:

- The 3D view is display-only. A test runs the same script with the view on and off and compares
  the game's memory and its fingerprint byte for byte. At its test angle (yaw 0, oblique) the
  renderer must produce exactly the original picture.
- Single-player saves and files must play as in the original. Several tests load a plain file and
  check nothing the mod added leaks into it.
- The item randomizer is checked over 5,000 seeds to be beatable and to hold exactly the original
  item set.

**Carry forward:** before adding anything, find the thing that must not change and write the test
that proves it did not. "It looks right" never caught a regression here; "the bytes differ at
0x..." caught dozens.

## 2. One shared machine, many players: the bugs are always "which one"

Four Links live in one emulated console that was built for one. Almost every co-op bug came from
code that reached for *the* player when it should have asked *which* player:

- A rock lifted by two Links at once drew one Link's head in the other's hands. The lift code
  assumed the object it was about to take was still there; a second lifter had already taken it.
  Fix: a "still here" check at the moment of taking (`Link_LiftableStillHere`), not at the start
  of the animation.
- Walking two players into Zora's domain split them into different worlds and left one standing
  in the wrong area. The special overworld areas use a different game module than the normal map,
  and every "are we in the overworld" test in the mod used the normal module's number. Fix: one
  pair of helpers (`Mp_IsPlayModule`, `Mp_IsOverworldModule`) used everywhere, instead of module
  numbers repeated in six files.
- The flute bird, swimming across area edges, bunny form in the Dark World, text boxes, menus:
  each one was a case where the original stored "the state" in one global and the mod had to give
  every player their own, or decide which player's state the shared world follows.

**Carry forward:** when sharing single-owner code, grep for every global that means "the player"
and classify each: per player, per world, or shared. Write the helper that answers "which mode are
we in" once, and make everything call it. A number compared in two places will be compared in a
third place wrongly.

## 3. Determinism is a property you test, not a property you have

Online play is lockstep with rollback: every machine runs the same inputs through the same
simulation, so any hidden source of difference desyncs the game. Lessons:

- The game must never read anything the other machine cannot see: wall clock, uninitialised
  memory, pointer order, the local player's view settings. The 3D view and the 2D-HD look were
  kept strictly on the display side for this reason, and a test runs host and client with one in
  3D to prove it.
- A checksum of the state every few frames, exchanged between machines, found desyncs the players
  would have blamed on lag. The resync path (host sends a full state, client loads it) was then
  made cheap enough to use, and a test asserts it is *never* needed under a lag simulator.
- The lag simulator (`ZELDA3_NETSIM`: delay, jitter, loss) turned "it works on my LAN" into a
  measured claim: tests run pairs over 60, 150 and 300 ms links and fail on a resync, on a guessed
  button, or on the client's own Link ever being moved by a prediction.
- `tests/cmpdump.py` compares two machines' state dumps frame by frame. When a desync appears, the
  first differing byte names the subsystem; without it a desync is a day of guessing.

**Carry forward:** build the lag simulator and the state comparator before the second netplay
feature, not after the first desync report. Make "no resync happened" a test assertion.

## 4. Headless, scripted tests were the project's spine

`zelda3 --mptest <script>` plays the game with no window from a text file of commands ("load
save 2", "player 2 holds Up", "run 300 frames", "expect player 2 downed", "expect_shot"). The
commands are documented at the top of `src/mptest.c`. This one tool did most of the work:

- A bug report became a script within minutes, and the fix was proven by the same script. The
  heavy-lift and Zora's domain fixes each shipped with a test that reproduces the report first.
- Stress tests with invariants found bugs no scenario would have: four players wander at random
  for 20,000 frames and the test fails if any Link teleports, any ghost moves, any player is in
  two worlds, or any visible Link is not drawn.
- New harness commands were cheap, so each feature added the few it needed (`aproom`,
  `apoption`, `expect_applace`, `arrive`, `expect_thrown`). Two cautions from this project: a
  command added twice in a long session compiled and shadowed the first (a duplicate `poke` had
  to be removed), and a command that resets state (`aproom` wipes pretend room settings) must be
  documented as such, because a test that set options before it silently lost them.
- Screenshot comparisons use baselines made from the user's own copy on first run and are never
  committed (the graphics are not ours to share), so visual checks run locally, not in CI. A
  mismatch writes the new picture and a red-pixel diff. That is a workable split: CI proves logic,
  the developer's machine proves looks.

**Carry forward:** a scripted headless runner is the first thing to build when porting or modding a
game. Write every bug fix as script-then-fix. Add random-walk tests with invariants early; they
find the bugs you did not think to script.

## 5. Prove the tests can fail

The recipe branch added a habit worth keeping: every checking tool has a `--prove-red` mode that
plants faults one at a time and requires the tests to go red. It paid for itself at once. The
recipe's own self-check printed "every check passed" after a check had failed when run with plain
`sh`, and only the planted-fault run exposed it.

**Carry forward:** for any script that says "passed", plant a fault and watch it say "failed"
before trusting the green.

## 6. Build-time options need a matrix, not a promise

The mods system lets a build leave features out. The only way to know the catalog's claims ("this
test needs mod X") were true was to build every composition (shipped, each mod off alone, all off)
and run every test in each (`tools/mods_matrix.sh`, about an hour). The matrix disagreed with the
hand-written catalog in several rows, and the catalog was corrected from it, and the rule written
down: believe the matrix over the catalog.

The Nintendo Switch port shows the same lesson from the other side. It was ripped out on request
(it was a second code path nobody could test here), then relanded a day later as a build option
that nothing builds by default. A platform is cheapest as an option with a guard, most expensive
as a parallel copy of the code.

**Carry forward:** anything optional at build time gets a matrix run, and the matrix is data in the
repository. Platforms are options behind one guard, never forks.

## 7. Speak the standard protocol first, keep your own as a toggle

Archipelago support came twice. First the project wrote its own world (its own location ids, its
own item codes, a generator for the multiworld). Then the user asked for Archipelago's stock
"A Link to the Past" world as the default, with the home-made one behind a menu toggle. What the
second pass taught:

- Generate the id tables from the upstream source, check the generated file in, and keep the
  generator (`archipelago/tools/gen_alttp_table.py`). Hand-copied tables of 247 locations would
  have drifted; the generator was rerun whenever upstream's definitions were read more carefully.
- Read upstream's defaults, not just its options. `key_drop_shuffle` is on by default upstream, so
  "refuse key drops" would have refused almost every real game. Supporting them changed the save
  layout (the checked-bits area grew from 32 to 40 bytes), which was fine only because nothing
  had shipped yet. Decide layouts after reading the defaults.
- Refuse loudly what you cannot play. Entrance shuffle, inverted mode, swordless and shop options
  are refused with the option named, at connect time, rather than played wrongly.
- Do not change an identifier existing files depend on. The room binding hash for the first world
  stayed as it was; the new world carries its difference in a spare flag bit (frame flag bit 6)
  and gets its own hash suffix. Old files keep working.
- Keep the translation in one place. Every upstream item code is mapped in one function
  (`AlttpReceive`), and every "this place has been checked" call goes through one
  (`Ap_PlaceItem`), so the game's own item-giving code stayed untouched and a mistake had one
  home.
- Test against the real thing. A mock server caught protocol errors cheaply; the end-to-end test
  that generates a multiworld with upstream's own generator and connects to a real `MultiServer`
  caught the ones the mock agreed with us about.

**Carry forward:** when a standard exists, implement it before your own variant, generate the
tables from the standard's source, read its defaults, refuse what you cannot do, and run one test
against the real server.

## 8. Reserve space in the save, and move things while you still can

Save-file bytes were allocated in a block (`0x414` onward) with a documented end marker
(`kApSave_End`). When the Archipelago checked-bits area grew, two fields moved to make room. That
was cheap on day seven and would have been a migration on day seventy.

**Carry forward:** document every save byte in one header with an end marker; leave a gap after
each bitmap; treat the day of first release as the day the layout freezes.

## 9. Root-cause, do not patch the symptom

Two audits (one of the whole mod, one of the flute feature) and the council read of the recipe
branch all found the same shape of bug: a fix had been placed where the symptom appeared. The
weapon-switching, enemy-respawn and camera bugs were re-done from the cause once that was
understood. The council's fixes list is a good model of a review that names what was wrong, what
changed, and how it is proven (`git show 39d79cc`).

The review council itself was: a doubt-first critic, a reward-first critic, an editor's seat, and
two independent fact-checks of every claim in the player's book. Nine claims in the book were
wrong. The two opposed critics converging on the same finding was the signal that it was real.

**Carry forward:** before a release, read the diff with two opposed biases and fact-check every
sentence of user documentation against the running program. A bot or reviewer finding is a claim
to trace to a real caller before fixing; a fix that cannot be traced is widening the code.

## 10. Documentation written in the same commit, in one voice, with no numbers you cannot regenerate

Every feature landed with its README section, its line in the plain-language guide, and its test
list in the same commit, and the docs stayed truthful because of it. Two things went wrong anyway:

- Counts in prose drift. "186 tests" appears in a guide; the suite now has 187. Either generate
  the number or write "the suite".
- Voice drifts across authors. The recipe branch measures voice (`tools/books.py voice`) across
  every text it wrote so that drift is a number, not an argument.

The repository's house rule, no em-dashes anywhere, is enforced by the recipe's check. A style rule
with a grep behind it is kept; a style rule in a README is not.

**Carry forward:** docs in the same commit as the code; no hand-typed counts; one voice, measured;
every style rule gets a check.

## 11. Working with an AI agent: what made it go well

Most of the code was written by an AI agent working from the user's requests and bug reports,
with the user playtesting. What worked:

- Standing rules stated once and kept in every session: never commit ROM-derived data (the ROM,
  the assets file, dialogue, screenshot baselines, dungeon data), push only to the assigned
  branch, no pull request unless asked, commit trailers in a fixed form, CRLF files edited
  byte-preservingly.
- Bug reports as a sentence plus a screenshot were enough, because the headless harness turned
  them into reproductions. "This happened when two players entered Zora's domain" became a
  script that fails, then a fix, within one session.
- Long sessions compact their context. The hand-off summary at each compaction (what was asked,
  what is done, what is pending, which commands and byte offsets matter) is what kept work
  continuous; a task list kept in the session showed what remained.
- The agent ran the full suite before every push and reported the output, including the one
  flaky timing test and its rerun, rather than a bare "tests pass".
- A merge from another contributor's branch arrived mid-task. Pulling, reading its commit
  messages, and adapting (the Switch build it relanded, the stand-ins it added for the new
  Archipelago functions) was cheaper than defending the earlier state.

What to watch for:

- An agent will add a harness command without checking whether one exists already (the duplicate
  `poke`). Review new commands against the list at the top of the harness file.
- An agent will fix the module number it sees, not the six others. Ask for the helper, not the fix.
- Upstream code was read from a clone, not from memory; the one place memory was trusted (the
  default of an upstream option) was wrong. Clone and read.

**Carry forward:** give the agent the rules, the oracle, the harness and the real upstream source;
ask it for reproductions before fixes; read its hand-off summaries; and review the places where it
would most plausibly repeat itself.

## 12. The recipe: a build that explains itself

The last branch made its claim explicit: a literate recipe (`RECIPE.org`), with the commands, what
to expect from each, and self-checks that two independent readers (awk and Python) must extract
identically, explains a branch to a stranger better than its history does. Beside it,
`CONTEXT-MODELS/` keeps the models the work was planned from: measured data, each idea speaking for
itself about what it gives and costs, each reader saying what they came for, and relations with
evidence. Whether the claim holds is itself measured.

**Carry forward:** a project that will be handed on should carry its own recipe: ordered steps,
expected output at each, a self-check that can be shown to fail, and the reasons beside the
commands. The commit log is not that document.

## A checklist for the next project

1. Find the oracle and write the equality test first.
2. Build the headless script runner before the second feature.
3. Classify every global as per player, per world, or shared; write one "which mode" helper.
4. Add a random-walk test with invariants.
5. For netplay: lag simulator, state checksum, frame-by-frame comparator, "no resync" assertion.
6. Every checker gets a `--prove-red`.
7. Every build-time option gets a matrix run, kept as data.
8. Standard protocol first, generated tables, read the defaults, refuse the unsupported, one test
   against the real server.
9. One header for save bytes, with gaps and an end marker.
10. Docs in the same commit, one voice, no hand-typed counts, a check behind each style rule.
11. Two opposed reviewers plus a fact-check before release.
12. A recipe, not just a log, for whoever comes next.

## Where to look in this repository

| lesson | files |
|---|---|
| display-only proof, yaw-0 identity | `tests/coop/hd3d_*.txt`, `src/hd3d*.c` |
| which-player helpers | `src/mp.h` (`Mp_IsPlayModule`, `Mp_IsOverworldModule`), `src/player.c` (`Link_LiftableStillHere`) |
| netplay sync tests | `tests/coop/net_*.txt`, `tests/cmpdump.py`, `ZELDA3_NETSIM` in `src/net*.c` |
| headless harness | `src/mptest.c` (command list at the top), `tests/run_coop_tests.sh` |
| prove-red | `tests/recipe/run.sh --prove-red`, `tests/mods/run.sh --prove-red` |
| build matrix | `tools/mods_matrix.sh`, `tools/mods_needs.py`, `CONTEXT-MODELS/data/` |
| Archipelago stock world | `src/archipelago.c`, `src/ap_client.c`, `src/ap_alttp_data.h`, `archipelago/tools/gen_alttp_table.py`, `archipelago/tests/e2e_alttp.sh` |
| save layout | `src/archipelago.h` (`kApSave_*`) |
| the council's fixes | `git show 39d79cc` |
| voice and style checks | `tools/books.py voice`, the house-style block in `RECIPE.org` |
| the recipe and models | `RECIPE.org`, `CONTEXT-MODELS/README.md` |
