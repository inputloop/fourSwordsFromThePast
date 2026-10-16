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
  uint8 m = main_module_index;
  if (m <= 5)  // the title, the legend, file select, naming a file
    return "starting";
  if (Session_IsNetplay())
    return "online";
  if (Inst_NumJoined() > 1)
    return "together";
  if (m == 7)
    return "dungeons";
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
