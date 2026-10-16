# The Nintendo Switch build

Upstream removed this build in 6a7984b ("Remove the Nintendo Switch build"). The recipe branch
relands it as a **build option**: nothing builds it for players (no download, not the top `make`,
not the README), and `make switch` builds it for anyone who asks. CI builds it on every push
(the "on Nintendo Switch" job, in devkitPro's own image), so it keeps building.

    make switch          # from the top of the tree, with devkitPro (libnx and switch-sdl2)

or, with Docker and nothing else installed:

    docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 make switch

It makes `src/platform/switch/zelda3.nro`.

**Its own mods.** The Switch reads `mods/switch.cfg` in place of `mods.cfg`, so it carries its own
set: the shipped mods without **Archipelago** (its secure connections load the system's OpenSSL at
run time, which a Switch does not have) and without **the network** (this port never starts the
Switch's sockets). Change that file to try others; `python3 tools/mods.py --cfg mods/switch.cfg show`
says what the Switch would carry.

**What changed from the files upstream removed.** The Makefile's `SRC_DIR` was `../..`, which is
`src/`; it could not reach `snes/` or `third_party/`, so the build had not worked for some time. It
is now the top of the tree, and the Makefile writes the Switch's composition before it compiles.
The other files (`zelda3.ini`, `icon.jpg`, `.gitignore`) are as they were at 0831a3d.

**Not checked:** running it. It builds and links; it has not been run on a Switch or an emulator.
