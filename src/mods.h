// Which mods this build carries (see mods/SPEC.md). The MOD_* values come from
// src/mods_build.h, which tools/mods.py makes from mods/default.cfg and your own
// mods.cfg; make (and the Visual Studio project) run it before compiling.
#ifndef ZELDA3_MODS_H_
#define ZELDA3_MODS_H_
#if defined(__has_include)
#if !__has_include("mods_build.h")
#error "src/mods_build.h is missing: run  python3 tools/mods.py write"
#endif
#endif
#include "mods_build.h"
#endif  // ZELDA3_MODS_H_
