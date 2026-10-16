// The helpbook mod: Ctrl+H opens the player's manual at the page for what you
// are doing (mods/catalog.tsv). Off in the shipped build; switch it on with
// "+helpbook" in mods.cfg.
#ifndef ZELDA3_HELPBOOK_H_
#define ZELDA3_HELPBOOK_H_
#include "types.h"

// The manual page (a file name without .html) for this moment: "menus",
// "starting", "online", "together", "dungeons" or "outdoors". Every name here
// must be a page of docs/books/manual (tools/books.py check says so).
const char *Helpbook_Topic(void);
// Opens that page: the compiled help (help/FourSwords-Manual.chm) on Windows,
// else help/manual/<topic>.html in the browser. Says so on screen if the
// manual is not next to the game.
void Helpbook_Open(void);

#endif  // ZELDA3_HELPBOOK_H_
