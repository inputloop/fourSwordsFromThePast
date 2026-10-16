#include "mods.h"  // (mods/SPEC.md: this file is the helpbook mod)
#if MOD_HELPBOOK
// The helpbook mod's half that talks to the operating system: open one page of
// the player's manual. No game headers here (see helpbook.c).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

bool Helpbook_OpenPage(const char *topic, char *why, size_t why_size);
void Helpbook_FileUrl(const char *path, char *out, size_t size);

// A file:// URL for an absolute path: forward slashes, and a space, '#', '%' or '?' written as %XX,
// so the browser reads the whole path. (tests/unit/helpbook_url_test.c)
void Helpbook_FileUrl(const char *path, char *out, size_t size) {
  size_t n = (size_t)snprintf(out, size, "file://%s", path[0] == '/' ? "" : "/");
  for (const char *c = path; *c && n + 4 < size; c++) {
    if (*c == '\\')
      out[n++] = '/';
    else if (*c == ' ' || *c == '#' || *c == '%' || *c == '?')
      n += (size_t)snprintf(out + n, size - n, "%%%02X", (unsigned char)*c);
    else
      out[n++] = *c;
  }
  out[n < size ? n : size - 1] = 0;
}

static bool FileExists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

// An absolute path for a file next to the game (the game runs from its own folder).
static bool FullPath(const char *rel, char *out, size_t size) {
#ifdef _WIN32
  return _fullpath(out, rel, size) != NULL;
#else
  char *p = realpath(rel, NULL);
  if (!p)
    return false;
  snprintf(out, size, "%s", p);
  free(p);
  return true;
#endif
}

bool Helpbook_OpenPage(const char *topic, char *why, size_t why_size) {
  char path[512], full[768], arg[1024];
#ifdef _WIN32
  // Windows help: hh.exe opens a page inside the .chm as "file.chm::/page.html".
  if (FullPath("help\\FourSwords-Manual.chm", full, sizeof(full)) && FileExists(full)) {
    // Quoted: a game folder may have a space in its name (C:\Users\Jane Doe\...).
    snprintf(arg, sizeof(arg), "\"%s::/%s.html\"", full, topic);
    if ((INT_PTR)ShellExecuteA(NULL, "open", "hh.exe", arg, NULL, SW_SHOWNORMAL) > 32)
      return true;
  }
#endif
  snprintf(path, sizeof(path), "help/manual/%s.html", topic);
  if (!FullPath(path, full, sizeof(full)) || !FileExists(full)) {
    snprintf(why, why_size, "The manual isn't next to the game (make books puts it in help/)");
    return false;
  }
  Helpbook_FileUrl(full, arg, sizeof(arg));
  if (SDL_OpenURL(arg) != 0) {
    snprintf(why, why_size, "Couldn't open the manual in a browser");
    return false;
  }
  return true;
}
#endif  // MOD_HELPBOOK
