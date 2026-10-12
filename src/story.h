// "Four Swords of the Past": an alternative story for the opening legend
// that plays after the title screen (the attract mode), and the people of
// Hyrule speaking to four heroes instead of one.
#ifndef ZELDA3_STORY_H_
#define ZELDA3_STORY_H_

#include "types.h"

// Whether the opening tells the Four Swords story. Part of the simulation
// (the text length decides the timing of the opening), so in netplay it comes
// with every frame from the host.
void Story_SetEnabled(bool on);
bool Story_Enabled();

// The replacement for dialogue message idx, in the game's US text encoding.
// Returns false to use the original message.
bool Story_GetMessage(int idx, const uint8 **data, size_t *size);

// (Tests) Checks every rewritten message against the text box, with the
// real font: returns how many lines are too wide or pages too long.
int Story_CheckText(bool verbose);
// (Tests) Measures the original messages the same way; returns the widest line.
int Story_MeasureOriginals(bool verbose);

#endif  // ZELDA3_STORY_H_
