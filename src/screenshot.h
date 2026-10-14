// The screenshot keys: F12 saves the frame as it is on screen, Ctrl+F12 the
// same frame without the mod's overlay (the game's own HUD stays). Display
// only: nothing here touches the game, so it works online too.
#ifndef ZELDA3_SCREENSHOT_H_
#define ZELDA3_SCREENSHOT_H_

#include "types.h"

enum {
  kScreenshot_Clean = 1,   // grabbed before Ui_Draw
  kScreenshot_AsSeen = 2,  // grabbed after it
};

// A key asks for a picture of the next frame drawn.
void Screenshot_Request(int kind);
// Called with the finished frame (0x00RRGGBB pixels) twice per frame: with
// kScreenshot_Clean before Ui_Draw and kScreenshot_AsSeen after it. Copies
// the pixels if that kind was asked for; a worker thread writes the PNG to
// the folder (screenshots/ by default) and the overlay then says where.
void Screenshot_Grab(int stage, const uint8 *pixels, int pitch, int width, int height);
// Waits up to ms milliseconds for the picture being written; false if one
// still is. *ok tells whether the last one was written.
bool Screenshot_Wait(int ms, bool *ok);
// The file of the last picture taken ("" if none yet).
const char *Screenshot_LastFile(void);
// Where the pictures go (tests).
void Screenshot_SetFolder(const char *folder);

#endif  // ZELDA3_SCREENSHOT_H_
