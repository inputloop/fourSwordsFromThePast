#include "mods.h"  // (mods/SPEC.md: this file is the screenshot mod)
#if MOD_SCREENSHOT
// The screenshot keys (see screenshot.h). The frame is copied on the main
// thread when it is finished; the PNG is compressed and written on a worker
// thread, so a big picture (4x the resolution, widescreen) doesn't hitch the
// game. One picture is written at a time: a key pressed meanwhile takes the
// first frame after it is done.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif
#include "screenshot.h"
#include "ui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"

static int g_requested;  // the kinds asked for (main thread)
static char g_folder[256] = "screenshots";
static char g_last_file[300];
static bool g_report;    // say where the picture went once it is written

// The worker owns g_job while g_busy is set.
static SDL_atomic_t g_busy, g_ok;
static struct {
  uint8 *rgb;
  int width, height;
  char file[300];
} g_job;

static int SDLCALL WriteThread(void *arg) {
  (void)arg;
  int ok = stbi_write_png(g_job.file, g_job.width, g_job.height, 3, g_job.rgb, g_job.width * 3);
  free(g_job.rgb);
  g_job.rgb = NULL;
  SDL_AtomicSet(&g_ok, ok != 0);
  SDL_AtomicSet(&g_busy, 0);
  return 0;
}

void Screenshot_Request(int kind) {
  g_requested |= kind;
}

void Screenshot_SetFolder(const char *folder) {
  snprintf(g_folder, sizeof(g_folder), "%s", folder);
}

const char *Screenshot_LastFile(void) {
  return g_last_file;
}

bool Screenshot_Wait(int ms, bool *ok) {
  uint32 start = SDL_GetTicks();
  while (SDL_AtomicGet(&g_busy) && SDL_GetTicks() - start < (uint32)ms)
    SDL_Delay(1);
  if (ok)
    *ok = SDL_AtomicGet(&g_ok) != 0;
  return !SDL_AtomicGet(&g_busy);
}

static bool FileExists(const char *file) {
  FILE *f = fopen(file, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

// screenshots/fsotp-2026-10-14_15-30-02.png, then -2, -3... within a second.
static void ChooseFileName(char *buf, size_t size) {
  time_t t = time(NULL);
  char stamp[32] = "picture";
  struct tm *tm = localtime(&t);
  if (tm)
    strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", tm);
  for (int n = 1; n < 1000; n++) {
    if (n == 1)
      snprintf(buf, size, "%s/fsotp-%s.png", g_folder, stamp);
    else
      snprintf(buf, size, "%s/fsotp-%s-%d.png", g_folder, stamp, n);
    if (!FileExists(buf))
      break;
  }
}

void Screenshot_Grab(int stage, const uint8 *pixels, int pitch, int width, int height) {
  if (g_report && !SDL_AtomicGet(&g_busy)) {
    g_report = false;
    if (SDL_AtomicGet(&g_ok)) {
      Ui_Toast(g_last_file);
    } else {
      Ui_Toast("Couldn't save the screenshot");
    }
  }
  if (!(g_requested & stage) || SDL_AtomicGet(&g_busy))
    return;
  g_requested &= ~stage;
#ifdef _WIN32
  _mkdir(g_folder);
#else
  mkdir(g_folder, 0755);
#endif
  ChooseFileName(g_job.file, sizeof(g_job.file));
  snprintf(g_last_file, sizeof(g_last_file), "%s", g_job.file);
  g_report = true;
  // 0x00RRGGBB to RGB bytes.
  uint8 *rgb = malloc((size_t)width * height * 3);
  if (!rgb) {
    SDL_AtomicSet(&g_ok, 0);
    return;
  }
  for (int y = 0; y < height; y++) {
    const uint32 *src = (const uint32 *)(pixels + (size_t)y * pitch);
    uint8 *dst = rgb + (size_t)y * width * 3;
    for (int x = 0; x < width; x++, dst += 3) {
      uint32 c = src[x];
      dst[0] = c >> 16, dst[1] = c >> 8, dst[2] = c;
    }
  }
  g_job.rgb = rgb, g_job.width = width, g_job.height = height;
  SDL_AtomicSet(&g_busy, 1);
  SDL_Thread *thread = SDL_CreateThread(WriteThread, "screenshot", NULL);
  if (thread)
    SDL_DetachThread(thread);
  else
    WriteThread(NULL);  // (no thread: write it right away)
}
#endif  // MOD_SCREENSHOT
