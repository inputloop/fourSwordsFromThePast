// Co-op session management: local drop-in/out, LAN/internet hosting and joining.
//
// Every simulated frame is described by an MpFrameInput (the joypads of all
// four player slots plus join/leave events and session settings). Offline and
// as a host, frames are built locally; as a client, frames come from the host.
// Because the game is deterministic, running the same frames gives the same
// game on every machine.
#ifndef ZELDA3_SESSION_H_
#define ZELDA3_SESSION_H_

#include "types.h"
#include "mp.h"

enum {
  kSessionPort = 47474,
  kSessionMaxLocal = 4,
  kSessionMaxClients = 3,
  kSessionMaxEvents = 4,
};

enum SessionMode {
  kSessionOffline = 0,
  kSessionHost = 1,
  kSessionClient = 2,
};

enum SessionEventType {
  kSessEv_Join = 1,
  kSessEv_Leave = 2,
};

typedef struct MpFrameInput {
  uint16 inputs[kMpMaxPlayers];
  uint8 flags;  // bit0: friendly fire, bit1: music playing (apui00), bit7: hash valid
  uint8 nevents;
  uint32 features;
  uint32 hash;  // hash of the state after this frame, if flags bit7
  uint8 events[kSessionMaxEvents][4];  // type, slot, owner client id, owner local index
} MpFrameInput;

typedef struct SessionLanGame {
  char name[24];
  char addr[32];
  uint8 players;
  uint32 last_seen;
} SessionLanGame;

typedef struct SessionStatus {
  int mode;
  int client_state;  // for clients: 0 idle, 1 connecting, 2 loading, 3 playing
  int my_id;
  int ping_ms;
  int frames_buffered;
  int snapshot_progress;  // percent
  int resyncs;
  char host_addr[48];
  char public_addr[48];
  char message[96];
  int8 local_slot[kSessionMaxLocal];  // which player slot each local player controls (-1 = none)
  uint8 slot_owner[kMpMaxPlayers];    // client id owning each slot (0 = host), 0xff if free
} SessionStatus;

void Session_Init(const char *player_name);
// Runs the session for one display frame. local_inputs holds the joypad of
// each local player (keyboard/pad 0 is local player 0). Returns how many game
// frames were simulated (0 when waiting for the host).
int Session_Tick(const uint16 *local_inputs, uint32 wanted_features);
// Local drop-in/out (couch co-op, also works while hosting or joined).
void Session_LocalJoin(int local_index);
void Session_LocalLeave(int local_index);
bool Session_IsLocalJoined(int local_index);
void Session_SetFriendlyFire(bool on);
bool Session_GetFriendlyFire();

bool Session_Host(uint16 port, bool use_upnp);
bool Session_Join(const char *address);
void Session_Disconnect();
// Forces every connected client to reload the host's state (after the host loads a save state).
void Session_ResyncAll();

void Session_RefreshLan();
int Session_GetLanGames(SessionLanGame *out, int max);

const SessionStatus *Session_GetStatus();
bool Session_IsNetplay();
// Whether local save states / turbo may be used.
bool Session_AllowsLocalStateChanges();
bool Session_LastFrameWasReplay();

#endif  // ZELDA3_SESSION_H_
