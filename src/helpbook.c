#include "mods.h"  // (mods/SPEC.md: this file is the helpbook mod)
#if MOD_HELPBOOK
#include "helpbook.h"
#include <stdio.h>
#include "variables.h"
#include "instance.h"
#include "session.h"
#include "ui.h"

// The order is the order of the questions a player is most likely asking:
// "what is this menu?" before "where am I?".
const char *Helpbook_Topic(void) {
  if (Ui_IsOpen())
    return "menus";
  // The game's own modules (src/misc.c, kMainRouting), by what the player is looking at.
  switch (main_module_index) {
  case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:  // the title, file select, naming
  case 0x14:  // the legend after the title
  case 0x12: case 0x17: case 0x1B:  // game over, save and quit, choosing where to start
    return "starting";
  case 0x19: case 0x1A:  // the Triforce room, the credits
    return "last";
  }
  if (Session_IsNetplay())
    return "online";
  if (Inst_NumJoined() > 1)
    return "together";
  switch (main_module_index) {
  case 0x06: case 0x07: case 0x11: case 0x13: case 0x16:  // a dungeon, falling into one, its boss beaten
    return "dungeons";
  }
  return "outdoors";
}

// (helpbook_os.c: the opening itself, kept apart because windows.h and the game's
// RAM names in variables.h cannot share a file.)
bool Helpbook_OpenPage(const char *topic, char *why, size_t why_size);

void Helpbook_Open(void) {
  char why[128];
  if (!Helpbook_OpenPage(Helpbook_Topic(), why, sizeof(why)))
    Ui_Toast(why);
}
#endif  // MOD_HELPBOOK
