// Archipelago (archipelago.gg) network client.
//
// The connection (TCP, TLS, WebSocket and the Archipelago JSON protocol) runs
// on a thread of its own, so the server's pings are answered while the game
// is busy or paused. Every function here is meant for the main thread and
// never blocks.
#ifndef ZELDA3_AP_CLIENT_H_
#define ZELDA3_AP_CLIENT_H_

#include "types.h"
#include <stddef.h>

#define AP_GAME_NAME "Four Swords of the Past"
#define AP_ALTTP_GAME_NAME "A Link to the Past"  // Archipelago's own world for this game

enum {
  kApIdBase = 0x46530000,  // item id = base + item code; location id = base + chest number
  kApSpecialLocationBase = kApIdBase + 0x100,  // + Rando_SpecialItem's `which`
  kApDefaultPort = 38281,
  kApSlotDataVersion = 1,  // slot_data "version" this build understands
};

typedef enum ApStatus {
  kApStatus_Off,         // never started, or stopped
  kApStatus_Connecting,  // connecting or handshaking (first attempt)
  kApStatus_Connected,   // the server accepted the slot
  kApStatus_Retrying,    // the connection broke; tries again by itself
  kApStatus_Failed,      // refused (slot, password, game, version) or unusable
                         // address; needs ApClient_Start again
} ApStatus;

typedef struct ApConfig {
  // "host", "host:port", "ws://host:port", "wss://host:port" or
  // "archipelago://host:port". Without a port: 38281. Without ws:// or
  // wss://, both are tried.
  const char *server;
  const char *slot;      // slot (player) name
  const char *password;  // NULL or "" for none
  const char *game;      // NULL: AP_GAME_NAME (tests connect as other games)
  const char *uuid;      // NULL: made up once per process
} ApConfig;

// Connects, and keeps reconnecting until ApClient_Stop. Forgets the items,
// checks, goal and texts of an earlier connection.
void ApClient_Start(const ApConfig *cfg);
void ApClient_Stop(void);
// text (may be NULL) gets one line for the menu, e.g.
// "Connected to archipelago.gg:38281 (wss) as Link" or the reason it failed.
ApStatus ApClient_GetStatus(char *text, size_t size);

// The slot_data of "A Link to the Past" rooms this game looks at (its
// option values; -1 if missing).
enum {
  kApAlttp_CrystalsGt, kApAlttp_CrystalsGanon, kApAlttp_OpenPyramid, kApAlttp_SmallKeyShuffle,
  kApAlttp_Progressive, kApAlttp_Swordless, kApAlttp_RetroBow, kApAlttp_RetroCaves, kApAlttp_ShopItemSlots,
  kApAlttp_BossShuffle, kApAlttp_PotShuffle, kApAlttp_EnemyShuffle, kApAlttp_KeyDropShuffle,
  kApAlttp_RandomizeShops, kApAlttp_ShuffleShops, kApAlttp_EntranceShuffle, kApAlttp_Goal, kApAlttp_Mode,
  kApAlttp_PiecesRequired, kApAlttp_MmMedallion, kApAlttp_TrMedallion,  // (medallions: 0 Bombos, 1 Ether, 2 Quake)
  kApAlttp_Count,
};

typedef struct ApRoom {
  char seed_name[64];
  char slot_name[64];
  int slot, team;
  int world;              // ApWorld (archipelago.h): the game connected as
  int slot_data_version;  // slot_data "version", -1 if missing
  int slot_data_death_link;  // slot_data "death_link": 0 or 1, -1 if missing
  int slot_data_goal;     // slot_data "goal", -1 if missing
  int alttp[kApAlttp_Count];  // "A Link to the Past": its options
  // FNV-1a (32 bit) of seed_name + "/" + slot number in decimal + "/7"
  // (7 = the items_handling we connect with), and "/alttp" after it for
  // that world. Never 0.
  uint32 bind_hash;
} ApRoom;
// False before the first Connected after ApClient_Start; afterwards true,
// also while reconnecting.
bool ApClient_GetRoom(ApRoom *room);

// The items the server gave our slot, in the server's order (with
// items_handling 7 that includes items found in our own world and the start
// inventory). After a reconnect the server sends the whole list again; the
// list here only ever grows, and stays when the connection breaks.
int ApClient_ItemCount(void);
// player: the slot whose world the item was found in (0 = the server).
bool ApClient_GetItem(int index, int64 *item, int64 *location, int *player);

// Locations checked in the game. New ones are sent at once; after every
// reconnect the whole set is sent again.
void ApClient_CheckLocations(const int64 *ids, int n);
// The goal was reached: sent now and after every reconnect.
void ApClient_SetGoalReached(void);

// DeathLink. On: the "DeathLink" tag is set (now, or with the next Connect).
void ApClient_SetDeathLink(bool on);
void ApClient_SendDeath(const char *cause);
// True once for each death received from someone else; source gets who died.
bool ApClient_PollDeath(char *source, size_t size);

// Lines for the screen (printable ASCII; other characters become '?'),
// oldest first: items we sent and received, hints about our items,
// and server messages.
typedef enum ApTextKind {
  kApText_Normal,
  kApText_Progression,  // an item the receiver needs
  kApText_Useful,
  kApText_Trap,
  kApText_Error,        // connection problems
} ApTextKind;
bool ApClient_PollText(char *buf, size_t size, ApTextKind *kind);

#endif  // ZELDA3_AP_CLIENT_H_
