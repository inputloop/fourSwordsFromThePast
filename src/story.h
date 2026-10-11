// "Four Swords of the Past": an alternative story for the opening legend
// that plays after the title screen (the attract mode).
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

#endif  // ZELDA3_STORY_H_
