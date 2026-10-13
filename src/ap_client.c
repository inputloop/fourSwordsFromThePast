// Archipelago network client: stand-in that never connects, until the real
// client (TCP/TLS, WebSocket, the Archipelago protocol) replaces this file.
#include "ap_client.h"
#include <stdio.h>

void ApClient_Start(const ApConfig *cfg) {}
void ApClient_Stop(void) {}

ApStatus ApClient_GetStatus(char *text, size_t size) {
  if (text)
    snprintf(text, size, "Not available in this build");
  return kApStatus_Failed;
}

bool ApClient_GetRoom(ApRoom *room) { return false; }
int ApClient_ItemCount(void) { return 0; }
bool ApClient_GetItem(int index, int64 *item, int64 *location, int *player) { return false; }
void ApClient_CheckLocations(const int64 *ids, int n) {}
void ApClient_SetGoalReached(void) {}
void ApClient_SetDeathLink(bool on) {}
void ApClient_SendDeath(const char *cause) {}
bool ApClient_PollDeath(char *source, size_t size) { return false; }
bool ApClient_PollText(char *buf, size_t size, ApTextKind *kind) { return false; }
