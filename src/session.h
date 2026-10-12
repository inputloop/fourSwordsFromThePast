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
  kSessionKeepSeatSeconds = 120,  // how long a dropped player's seat waits for them
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
  uint8 flags;  // bit0: friendly fire, bit1: music playing (apui00),
                // bit2: Four Swords story, bit3: randomizer setting follows, bit7: hash valid
  uint8 nevents;
  uint32 features;
  uint32 hash;  // hash of the state after this frame, if flags bit7
  uint32 rando;  // randomizer setting for new games, if flags bit3 (see randomizer.h)
  uint8 events[kSessionMaxEvents][4];  // type, slot, owner client id, owner local index
} MpFrameInput;

typedef struct SessionLanGame {
  char name[24];
  char addr[32];
  uint8 players;
  uint32 last_seen;
} SessionLanGame;

// One player seat, as the host sees it (clients get the host's list).
enum {
  kSeat_Open = 0,
  kSeat_Playing = 1,
  kSeat_Reconnecting = 2,  // lost the connection; the seat is kept for a while
};
typedef struct SessionPlayer {
  uint8 state;      // kSeat_*
  uint8 owner;      // computer: 0 the host, else a client id
  uint8 local;      // which of that computer's players
  uint8 seconds_left;  // reconnecting: until the seat is given up
  uint16 ping_ms;   // remote players: round trip to the host
  char name[16];
} SessionPlayer;

typedef struct SessionStatus {
  int mode;
  int client_state;  // for clients: 0 idle, 1 connecting, 2 loading, 3 playing, 4 reconnecting
  int reconnect_seconds_left;  // clients, while reconnecting
  int my_id;
  int ping_ms;
  int frames_buffered;
  int snapshot_progress;  // percent
  int resyncs;
  int predicted;  // clients: frames shown ahead of what the host confirmed
  int rollbacks;  // how often a guess about other players was wrong
  int own_corrections;  // clients: frames where the host used other buttons for our own Link
  // statistics for the tests (netstats)
  int mispredicted;     // frames whose guessed remote buttons were wrong
  int b_flips;          // of those, frames where a remote B button was guessed wrong
  int replayed_frames;  // frames simulated again by rollbacks
  int max_rollback;     // deepest rollback (frames)
  int margin, lead_debt;  // clients: the host's latest report of how early our inputs arrive
  char host_addr[48];
  char public_addr[48];
  bool upnp_failed;  // hosting: the router didn't open the port (forward it by hand)
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
// Online play: local button presses take effect this many frames later.
void Session_SetInputDelay(int frames);
// The Four Swords opening story (the host decides).
void Session_SetStory(bool on);
bool Session_GetStory();
// Randomizer for games created from now on: kRando_Off, kRando_RandomSeed or a seed (the host decides).
void Session_SetRandomizer(uint32 setting);
uint32 Session_GetRandomizer();

bool Session_Host(uint16 port, bool use_upnp);
// address: a room code, "host", "host:port" or "a.b.c.d:port".
bool Session_Join(const char *address);
void Session_SetName(const char *player_name);
// The four seats: who is in them, how far away and whether they are back.
void Session_GetPlayers(SessionPlayer out[kMpMaxPlayers]);
// Host: removes the remote player in this seat (and that computer's other
// players) from the game. They can't come back on their own.
bool Session_Kick(int slot);

// Room codes: a host's address and port as 12 letters and digits with a
// check, "ABCD-EFGH-JKMN" (no server involved: the code is the address).
void Session_EncodeRoomCode(uint32 ip, uint16 port, char *out, size_t size);
bool Session_DecodeRoomCode(const char *code, uint32 *ip, uint16 *port);
// The code to give to other players while hosting: for the internet when
// the public address is known, else for the LAN. False when not hosting.
bool Session_GetRoomCode(char *out, size_t size, bool *internet);

// Playing in someone else's game (as a client, or after leaving it): save a
// copy of that game's progress into file 1-3 of our own save file at path.
bool Session_CanKeepCopy();
bool Session_KeepCopy(int file, const char *path, char *msg, size_t msg_size);
void Session_Disconnect();
// Forces every connected client to reload the host's state (after the host loads a save state).
void Session_ResyncAll();

void Session_RefreshLan();
int Session_GetLanGames(SessionLanGame *out, int max);

const SessionStatus *Session_GetStatus();
void Session_ResetCounters();
bool Session_IsNetplay();
// Whether local save states / turbo may be used.
bool Session_AllowsLocalStateChanges();
bool Session_LastFrameWasReplay();
// Tests: called inside the simulation before frame f
// is run (so it is replayed by rollbacks, and runs at the same frame everywhere).
extern void (*g_session_frame_hook)(uint32 f);
// Test instrumentation: print the confirmed state every n frames (0: off).
extern int g_session_dump_every;

#endif  // ZELDA3_SESSION_H_
