# The Nintendo Switch build (kept, not built by default)

Upstream removed this build in 6a7984b ("Remove the Nintendo Switch build"). The recipe branch
keeps its files, exactly as they were at 0831a3d, so anyone who wants it can still make it; nothing
builds it unless asked (no CI job, not the top Makefile, not the README). That is what "off by
default" means for a whole platform, where it means "not in the program" for a mod (mods/SPEC.md).

To build it you need devkitPro with libnx and the Switch port of SDL2:

    python3 tools/mods.py write     # from the top of the tree: which mods it carries (mods/SPEC.md)
    cd src/platform/switch
    make

The first line is the one new step: the game's sources now include `src/mods_build.h`, which the top
Makefile, Visual Studio and the Windows scripts make for you and this Makefile does not.

**Not checked.** The recipe branch was not able to build this (no devkitPro where it was made). If
it no longer builds, the likeliest cause is a file added to `src/` since 0831a3d that the Switch
cannot compile; switching that file's mod off (mods.cfg) is the first thing to try.
