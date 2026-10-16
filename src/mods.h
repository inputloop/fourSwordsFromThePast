// Which mods this build carries (see mods/SPEC.md). The MOD_* values come from
// src/mods_build.h, which tools/mods.py makes from mods/default.cfg and your own
// mods.cfg; make, Visual Studio and the Windows scripts run it before compiling.
// A file compiled on its own, with no mods_build.h made (a test driver, a
// packager's script), gets the shipped set: src/mods_default.h (mods/SPEC.md, M22).
#ifndef ZELDA3_MODS_H_
#define ZELDA3_MODS_H_
#if defined(__has_include)
#if __has_include("mods_build.h")
#include "mods_build.h"
#else
#include "mods_default.h"
#endif
#else
#include "mods_build.h"
#endif
#endif  // ZELDA3_MODS_H_
