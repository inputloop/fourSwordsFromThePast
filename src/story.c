#include "story.h"
#include <stdlib.h>
#include <string.h>
#include "zelda_rtl.h"

// The opening legend, retold: Hyrule's wise men hid the Four Sword, a blade
// that splits one hero into four, and four Links once sealed Ganon with it.
// The [commands] are the game's own text commands. [NextPic] moves the legend
// on to its next picture, so the story keeps the original's picture beats.
// Lines are at most 28 characters, like the original text.
static const char *const kStory[] = {
  // 0x112: the legend, told over four pictures
  "[ScrollSpd 00][Color 07][Speed 03][Window 02][NextPic]"
  "Long ago, in the kingdom of[2]Hyrule, seven wise men hid[3]the Golden Power away[...][Wait 09][Scroll]"
  "in a secret land. Beside it[Scroll]they laid a blade that could[Scroll]split one hero into four.[Wait 09]"
  "[NextPic][NextPic][Scroll]"
  "Then the thief Ganon broke[Scroll]into the Golden Land[...][Wait 07][Scroll]"
  "and seized its power, and[Scroll]darkness flowed over Hyrule.[Wait 07][Scroll]"
  "So a young knight drew the[Scroll]Four Sword and became four[Scroll]heroes in green, red, blue[Scroll]and purple.[Wait 09]"
  "[NextPic][NextPic][Scroll]"
  "Together they sealed Ganon[Scroll]away for all time[...][Scroll][Wait 09]"
  "[NextPic][NextPic][Scroll]"
  "[...]  [...]But the four heroes[Scroll]faded into the mists of[Scroll]time and became legend[...]",
  // 0x113: the throne room
  "[Window 02][Color 07][Speed 03][1]Ages later, a wizard named[2]Agahnim came to Hyrule to[3]break the seal. He struck[Wait 05][Scroll]down the good King[...][Wait 05]",
  // 0x114: Zelda's prison
  "[Window 02][Color 07][Speed 03][1]With dark magic, he made the[2]heirs of the wise men and[3]the four knights vanish, one[Wait 05][Scroll]after another.[Wait 05]",
  // 0x115: the maiden is taken away
  "[Window 02][Color 07][Speed 03][1]Princess Zelda is next, but[2]the Four Sword stirs again,[3]calling for four heroes[...][Wait 05]",
};

enum {
  kFirstMessage = 0x112,
  kNumMessages = 4,
  kCommandStart = 0x67,
};

// The US character set (text_compression.py, kTextAlphabet_US).
static const char *const kAlphabet[] = {
  "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
  "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f",
  "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
  "w", "x", "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "!", "?",
  "-", ".", ",", "[...]", ">", "(", ")", "[Ankh]", "[Waves]", "[Snake]", "[LinkL]", "[LinkR]",
  "\"", "[Up]", "[Down]", "[Left]", "[Right]", "'", "[1HeartL]", "[1HeartR]", "[2HeartL]", "[3HeartL]",
  "[3HeartR]", "[4HeartL]", "[4HeartR]", " ", "<", "[A]", "[B]", "[X]", "[Y]",
};

// Text commands from 0x67 on, and whether they take a parameter byte.
static const struct { const char *name; uint8 has_param; } kCommands[] = {
  {"NextPic", 0}, {"Choose", 0}, {"Item", 0}, {"Name", 0}, {"Window", 1}, {"Number", 1},
  {"Position", 1}, {"ScrollSpd", 1}, {"Selchg", 0}, {"Unused_Crash", 0}, {"Choose3", 0},
  {"Choose2", 0}, {"Scroll", 0}, {"1", 0}, {"2", 0}, {"3", 0}, {"Color", 1},
  {"Wait", 1}, {"Sound", 1}, {"Speed", 1}, {"Unused_Mark", 0}, {"Unused_Mark2", 0},
  {"Unused_Clear", 0}, {"Waitkey", 0},
};

static bool g_story_enabled;
static uint8 g_encoded[kNumMessages][512];
static size_t g_encoded_size[kNumMessages];

void Story_SetEnabled(bool on) {
  g_story_enabled = on;
}

bool Story_Enabled() {
  return g_story_enabled;
}

static size_t Encode(const char *s, uint8 *out, size_t max) {
  size_t n = 0;
  while (*s && n + 2 < max) {
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < (int)countof(kAlphabet); i++) {
      size_t l = strlen(kAlphabet[i]);
      if (l > best_len && strncmp(s, kAlphabet[i], l) == 0)
        best = i, best_len = l;
    }
    if (best >= 0) {
      out[n++] = (uint8)best;
      s += best_len;
      continue;
    }
    if (*s == '[') {
      const char *end = strchr(s, ']');
      if (!end)
        break;
      char name[32] = { 0 };
      int param = 0;
      size_t l = end - s - 1;
      memcpy(name, s + 1, l < sizeof(name) - 1 ? l : sizeof(name) - 1);
      char *sp = strchr(name, ' ');
      if (sp) {
        *sp = 0;
        param = atoi(sp + 1);
      }
      for (int i = 0; i < (int)countof(kCommands); i++) {
        if (strcmp(name, kCommands[i].name) == 0) {
          out[n++] = (uint8)(kCommandStart + i);
          if (kCommands[i].has_param)
            out[n++] = (uint8)param;
          break;
        }
      }
      s = end + 1;
      continue;
    }
    s++;  // not in the character set
  }
  return n;
}

bool Story_GetMessage(int idx, const uint8 **data, size_t *size) {
  // Only for the US text encoding (the story is written in English).
  if (!g_story_enabled || (g_zenv.dialogue_flags & 1) || idx < kFirstMessage || idx >= kFirstMessage + kNumMessages)
    return false;
  int i = idx - kFirstMessage;
  if (!g_encoded_size[i])
    g_encoded_size[i] = Encode(kStory[i], g_encoded[i], sizeof(g_encoded[i]));
  *data = g_encoded[i];
  *size = g_encoded_size[i];
  return true;
}
