# Build-time mods: the specification

*Every clause has a number. The tests in `tests/mods/` name the clause they check, and the
tool `tools/mods.py` is written to these clauses, not the other way round.*

## Files

- **M1.** `mods/catalog.tsv` declares every mod: its name, whether it changes the game's
  simulation (`sim`), the mods it `requires`, its `sources` in `src/`, the `tests` that need it,
  and a one-line `summary`. Lines starting with `#` are comments. Nothing about a particular mod
  is written in the tool.
- **M2.** `mods/default.cfg` (committed) gives the shipped composition: which mods are on.
- **M3.** `mods.cfg` at the top of the tree (never committed; listed in `.gitignore`) is the
  developer's own composition. It changes only the mods it names; the rest keep their default.
- **M4.** A composition line is one of: `NAME on`, `NAME off`, `+NAME`, `-NAME`. Several `+`/`-`
  words may share a line (`+hd3d -archipelago`). Anything after `#` is a comment. Blank lines are
  ignored. Names are case-insensitive.

## Checks (the tool refuses, exit 1, and says why in words)

- **M5.** A name that is not in the catalog.
- **M6.** A value that is not `on` or `off`.
- **M7.** A mod switched on while a mod it `requires` is off. The message names both and says
  which line to change.
- **M8.** A mod named twice in one file with different values.
- **M9.** A source file in the catalog that does not exist in `src/`, or that two mods both claim.

## Output

- **M10.** `write` makes `src/mods_build.h`: one `#define MOD_<NAME> 0|1` per mod, in catalog
  order; `MODS_COMPOSITION`, a string of the mods that are on; and `MODS_SIM_TAG`, which is `0`
  when every `sim` mod has its default value and otherwise a number from 1 to 32767 that depends
  only on which `sim` mods are on.
- **M11.** `write` leaves the file untouched (same bytes, same time stamp) when its content
  would not change, so a build does not recompile everything for nothing.
- **M12.** `show` prints the composition in words: each mod, on or off, where the value came from
  (`default` or `mods.cfg`), and its summary.
- **M13.** `skip-tests` prints, one per line, the test names under `tests/coop/` that need a mod
  which is off.
- **M14.** `from-recipe FILE` reads the first `#+begin_src mods` … `#+end_src` block in an Org
  file and writes it to `mods.cfg` (after checking it as in M5 to M8). This is how a RECIPE, edited
  in C4 or by hand, sets the build.

## The game

- **M15.** A mod that is off is not in the program: its sources compile to nothing but the small
  stand-ins in `src/mods_off.c`, which do nothing and report "not available".
- **M16.** A mod that is off shows no menu entries; its `zelda3.ini` keys are still accepted (one
  `zelda3.ini` works with every build) and do nothing.
- **M17.** Online, two programs whose `MODS_SIM_TAG` differ refuse each other with the message
  "Different game version or mods". Programs with the shipped composition keep the protocol
  version of the upstream game, so they play with builds made without this system.
- **M18.** `zelda3 --mods` prints `MODS_COMPOSITION` and exits.
- **M19.** `guard` checks that each file a mod owns starts with `#include "mods.h"` and
  `#if MOD_<NAME>` and ends with `#endif  // MOD_<NAME>`; `guard --fix` puts that in place on a
  file that has none of it. A file with half a guard is refused, never patched.
- **M20.** `from-assy FILE` reads a C4 CAMPAIGN: each line of the form
  `<…>mods ADD NAME as(on)` or `as(off)` is one choice. As C4-LANG §4 rules, a line that does not
  resolve is a comment, never an error; but a line naming something that is not a mod is *said*
  (on stderr) so a typo is never silent. `--into ORG` writes the choices, as `+NAME`/`-NAME`
  words, into the first `#+begin_src mods` block of an Org file (the RECIPE), and changes
  nothing else in it.

## Changed in this spec

- M16 as first written said a mod that is off "reads no zelda3.ini keys of its own". The code
  keeps reading them (so one zelda3.ini works with every build), and they change nothing. M16
  now says what is true: **a mod that is off shows no menu entries; its zelda3.ini keys are
  still accepted and do nothing.**
- **M21.** `src/mods_default.h` (committed) is the header of the shipped composition, for a build
  where Python is not at hand (`run_with_tcc.bat` copies it to `src/mods_build.h`). `write-default`
  makes it; `check` and `write` refuse when it no longer matches `mods/default.cfg`, so the two
  can never drift apart unseen.
- **M22.** A file compiled on its own, without `src/mods_build.h` (a test driver, a packager's
  script, `cc src/x.c`), gets the shipped composition from `src/mods_default.h`: every mod's own
  file compiles that way in a clean tree. (Added after the Archipelago client tests, which compile
  `src/ap_*.c` directly, failed in CI on the first push: `mods.h` used to stop with an error there.)
