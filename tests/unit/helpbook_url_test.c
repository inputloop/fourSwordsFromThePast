// The helpbook mod's file:// URLs (src/helpbook_os.c), without SDL or a ROM. Build:
//   cc -DZELDA3_MODS_H_ -DMOD_HELPBOOK=1 tests/unit/helpbook_url_test.c src/helpbook_os.c $(sdl2-config --cflags) -o helpbook_url_test
// (the two -D's stand in for src/mods_build.h with the mod on; SDL is only included, never called).
#include <stdio.h>
#include <string.h>
void Helpbook_FileUrl(const char *path, char *out, size_t size);
int SDL_OpenURL(const char *url) { (void)url; return 0; }  // (linked, never called here)

static int fails;
static void Expect(const char *path, const char *want) {
  char got[256];
  Helpbook_FileUrl(path, got, sizeof(got));
  if (strcmp(got, want)) { printf("FAIL %s -> %s, wanted %s\n", path, got, want); fails++; }
}

int main(void) {
  Expect("/home/jane/zelda3/help/manual/menus.html", "file:///home/jane/zelda3/help/manual/menus.html");
  Expect("C:\\Users\\Jane Doe\\zelda3\\help\\manual\\menus.html", "file:///C:/Users/Jane%20Doe/zelda3/help/manual/menus.html");
  Expect("/games/50% off #1?/menus.html", "file:///games/50%25%20off%20%231%3F/menus.html");
  char small[16];
  Helpbook_FileUrl("/a/very/long/path/that/does/not/fit.html", small, sizeof(small));
  if (strlen(small) >= sizeof(small)) { printf("FAIL a small buffer overflowed\n"); fails++; }
  printf(fails ? "helpbook_url: %d failed\n" : "helpbook_url: all passed\n", fails);
  return fails != 0;
}
