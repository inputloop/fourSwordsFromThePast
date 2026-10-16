#include "mods.h"  // (mods/SPEC.md: this file is the archipelago mod)
#if MOD_ARCHIPELAGO
// cJSON (third_party/cJSON, MIT license) for the Archipelago client. It is
// compiled from here because the build scripts only pick up src/*.c.
#include "third_party/cJSON/cJSON.c"
#endif  // MOD_ARCHIPELAGO
