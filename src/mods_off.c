// Stand-ins for the mods a build leaves out (mods/SPEC.md, M15).
//
// When a mod is off, its own .c files compile to nothing, and the game code that
// calls into it links against these instead. Each stand-in does what the game
// would do if the mod had never been written: nothing is drawn, nothing is sent,
// an item stays the item the original game placed. Each section was written by
// hand from the list `tools/mods_census.py` prints, which is the whole surface
// that the rest of the program uses.
#include "mods.h"
#include "types.h"
#include <string.h>

#if !MOD_ROGUELITE
#include "run.h"
// Off: no file is a run; a game over is the original's.
void Run_SetNewGameSetting(bool on) { (void)on; }
bool Run_GetNewGameSetting() { return false; }
void Run_OnNewFile(uint8 *sram_slot) { (void)sram_slot; }
bool Run_IsRunFile() { return false; }
int Run_Lives() { return 0; }
int Run_Wipes() { return 0; }
bool Run_IsOver() { return false; }
bool Run_AreaIsRunFile(const uint8 *save_area) { (void)save_area; return false; }
int Run_AreaLives(const uint8 *save_area) { (void)save_area; return 0; }
bool Run_AreaIsOver(const uint8 *save_area) { (void)save_area; return false; }
void Run_OnGameOver() {}
void Run_FilterDeathChoice() {}
bool Run_IsCounterAddr(int addr) { (void)addr; return false; }
#endif

#if !MOD_STORY
#include "story.h"
// Off: the original opening text plays. Story_GetMessage leaves the message alone.
void Story_SetEnabled(bool on) { (void)on; }
bool Story_Enabled() { return false; }
bool Story_GetMessage(int idx, const uint8 **data, size_t *size) { (void)idx; (void)data; (void)size; return false; }
int Story_CheckText(bool verbose) { (void)verbose; return 0; }
int Story_MeasureOriginals(bool verbose) { (void)verbose; return 0; }
#endif

#if !MOD_SCREENSHOT
#include "screenshot.h"
// Off: F12 does nothing; a test that waits for a picture is told none came.
void Screenshot_Request(int kind) { (void)kind; }
void Screenshot_Grab(int stage, const uint8 *pixels, int pitch, int width, int height) {
  (void)stage; (void)pixels; (void)pitch; (void)width; (void)height;
}
bool Screenshot_Wait(int ms, bool *ok) { (void)ms; if (ok) *ok = false; return true; }
const char *Screenshot_LastFile(void) { return ""; }
void Screenshot_SetFolder(const char *folder) { (void)folder; }
#endif

#if !MOD_HD2D
#include "hd2d.h"
#include "hd3d.h"
#include "zelda_rtl.h"
#include "snes/ppu.h"
// Off: the plain picture. The 3D view also borrows two things from 2D-HD: its
// worker threads (here, the same jobs one after another, which gives the same
// picture more slowly) and its light gathering (here, no lights).
Hd2dSettings g_hd2d;
int g_draw_extra_left_right = -1;
// Hd2d_DrawFrame is also where every frame is routed: to the 3D view when it
// draws this pane, else the plain picture with the 3D view's overlays on top.
// That routing stays; only the 2D-HD look goes.
void Hd2d_Draw2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  (void)view;
  ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
}
void Hd2d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  if (g_draw_extra_left_right >= 0)
    g_zenv.ppu->extraLeftRight = g_draw_extra_left_right;
  if (Hd3d_DrawFrame(pixel_buffer, pitch, render_flags, view))
    return;
  Hd2d_Draw2D(pixel_buffer, pitch, render_flags, view);
  Hd3d_After2D(pixel_buffer, pitch, render_flags, view);
}
void Hd2d_ParallelFor(int count, Hd2dJob *job, void *ctx) { if (count > 0) job(0, count, ctx); }
void Hd2d_Post(uint8 *pixels, size_t pitch, int w, int h, int view, const Hd2dPostIn *in) {
  (void)pixels; (void)pitch; (void)w; (void)h; (void)view; (void)in;
}
int Hd2d_GatherLights(Hd2dLight *out, int max, bool dark) { (void)out; (void)max; (void)dark; return 0; }
bool Hd2d_IsDarkRoom(const struct Ppu *ppu, float *lit) { (void)ppu; if (lit) *lit = 0; return false; }
int Hd2d_HiddenPixels(void) { return 0; }
bool Hd2d_LitDarkRoom(void) { return false; }
int Hd2d_NumLights(void) { return 0; }
#endif

#if !MOD_HD3D
#include "hd3d.h"
// Off: every pane is drawn in 2D, buttons and sticks mean what they always
// meant, and the camera is the game's own.
Hd3dSettings g_hd3d;
bool Hd3d_DrawFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  (void)pixel_buffer; (void)pitch; (void)render_flags; (void)view; return false;
}
void Hd3d_After2D(uint8 *pixel_buffer, size_t pitch, uint32 render_flags, int view) {
  (void)pixel_buffer; (void)pitch; (void)render_flags; (void)view;
}
void Hd3d_BeginPanes(bool deferred) { (void)deferred; }
void Hd3d_FinishPanes(void) {}
void Hd3d_SetPanes(const int8 *pane_local, int n) { (void)pane_local; (void)n; }
int Hd3d_ComputeViews(int *views, int8 *pane_local, int max) {  // (only called with the 3D view on)
  (void)views; (void)pane_local; (void)max; return 0;
}
void Hd3d_DumpScene(const char *ppm_path) { (void)ppm_path; }
void Hd3d_GetCamera(int local, Hd3dCamState *c) { (void)local; if (c) memset(c, 0, sizeof(*c)); }
void Hd3d_SetCamera(int local, const Hd3dCamState *c) { (void)local; (void)c; }
void Hd3d_GetStats(int view, int *quads, int *cards) { (void)view; if (quads) *quads = 0; if (cards) *cards = 0; }
void Hd3d_HandleViewButtons(void) {}
const char *Hd3d_LastWhy2D(int view) { (void)view; return "the 3D view is not in this build"; }
bool Hd3d_LocalEnabled(int local) { (void)local; return false; }
void Hd3d_OnKeyCommand(int command, bool pressed) { (void)command; (void)pressed; }
void Hd3d_OnMouseButton(int button, bool pressed, int clicks, uint32 mods, bool touch) {
  (void)button; (void)pressed; (void)clicks; (void)mods; (void)touch;
}
void Hd3d_OnMouseMotion(int xrel, int yrel) { (void)xrel; (void)yrel; }
void Hd3d_OnMouseWheel(int dy) { (void)dy; }
void Hd3d_OnWindowFocus(bool gained) { (void)gained; }
bool Hd3d_PaneIs3D(int view) { (void)view; return false; }
void Hd3d_PressViewButton(int local, int hold_ms) { (void)local; (void)hold_ms; }
bool Hd3d_Project(int view, int x, int y, int above, int *sx, int *sy, bool *behind) {
  (void)view; (void)x; (void)y; (void)above; (void)sx; (void)sy; (void)behind; return false;
}
void Hd3d_RemapInputs(uint16 *local_inputs) { (void)local_inputs; }
uint16 Hd3d_RemapLocal(int local, int player, uint16 input) { (void)local; (void)player; return input; }
void Hd3d_ResetCamera(int local) { (void)local; }
void Hd3d_ResetForTests(void) {}
float Hd3d_ScaleAt(int view, int x, int y) { (void)view; (void)x; (void)y; return 1.0f; }
void Hd3d_SetEnabled(bool on) { (void)on; }
void Hd3d_SetLocalEnabled(int local, bool on) { (void)local; (void)on; }
void Hd3d_SetLocalEnabledQuiet(int local, bool on) { (void)local; (void)on; }
void Hd3d_SetRightStick(int local, float x, float y) { (void)local; (void)x; (void)y; }
void Hd3d_SetTestCamera(bool on) { (void)on; }
int Hd3d_StandZAt(int view, int x, int y) { (void)view; (void)x; (void)y; return 0; }
void Hd3d_TagBegin(uint8 kind, uint8 index) { (void)kind; (void)index; }
void Hd3d_TagEnd(void) {}
void Hd3d_TagFrameStart(void) {}
void Hd3d_ToggleFirstPerson(int local) { (void)local; }
void Hd3d_Update(float dt) { (void)dt; }
bool Hd3d_WantsRelativeMouse(void) { return false; }
#endif

#if !MOD_RANDOMIZER
#include "randomizer.h"
// Off: every chest and every gift holds what the original game put there. The
// game's own code checks Rando_CurrentSeed() first, and 0 means "not randomized".
uint32 Rando_CurrentSeed() { return 0; }
uint8 Rando_ChestItem(int idx, uint8 vanilla_item) { (void)idx; return vanilla_item; }
uint8 Rando_SpecialItem(int which, uint8 vanilla_item) { (void)which; return vanilla_item; }
uint8 Rando_AdjustReceivedItem(uint8 item) { return item; }
bool Rando_SpecialDone(int which) { (void)which; return false; }
bool Rando_IsApLocation(int idx) { (void)idx; return false; }
uint32 Rando_GetNewGameSetting() { return kRando_Off; }
void Rando_SetNewGameSetting(uint32 setting) { (void)setting; }
void Rando_GiveStartItems() {}
void Rando_UncleGivesItems() {}  // (only called on a randomized file)
void Rando_OnNewFile(uint8 *sram_slot) { (void)sram_slot; }
void Rando_OnFileLoaded() {}
void Rando_OnSmithsTakeSword() {}
void Rando_OnSmithsReturnSword() {}
bool Rando_Tracker(const uint8 *save, RandoTracker *t) { (void)save; (void)t; return false; }
bool Rando_WriteSpoiler(uint32 seed, const char *path) { (void)seed; (void)path; return false; }
bool Rando_Check(uint32 seed, bool verbose) { (void)seed; (void)verbose; return false; }
int Rando_TestRoomAccess(const uint8 counts[256], int room) { (void)counts; (void)room; return 0; }
int Rando_SwordLevel() { return 0; }  // (asked only by Archipelago, which needs the randomizer)
bool Rando_TestWin(const uint8 counts[256]) { (void)counts; return false; }
#endif

#if !MOD_ARCHIPELAGO
#include "archipelago.h"
// Off: there is no room to connect to, and no file is an Archipelago file.
ApSettings g_ap_settings;
void Ap_Connect() {}
void Ap_Disconnect() {}
void Ap_Tick() {}
void Ap_BeforeFrame() {}
bool Ap_IsConnecting() { return false; }
bool Ap_IsApFile() { return false; }
uint32 Ap_FileBind() { return 0; }
uint32 Ap_HostBind() { return 0; }
uint32 Ap_GetNewGameBind() { return 0; }
void Ap_SetNewGameBind(uint32 bind) { (void)bind; }
void Ap_ApplyEvent(const uint8 *ev) { (void)ev; }
bool Ap_NextEvent(uint8 ev[4]) { (void)ev; return false; }
bool Ap_PollNotice(char *buf, size_t size, int *kind) { (void)buf; (void)size; (void)kind; return false; }
void Ap_StatusLine(char *buf, size_t size) { if (size) buf[0] = 0; }
void Ap_OnChestOpened(int chest) { (void)chest; }
void Ap_OnSpecialPlace(int which) { (void)which; }
void Ap_OnNewFile(uint8 *sram_slot) { (void)sram_slot; }
void Ap_OnFileLoaded() {}
void Ap_OnStateLoaded() {}
void Ap_OnGameOver() {}
void Ap_OnGoal() {}
void Ap_TestRoom(uint32 bind, bool death_link, ApWorld world) { (void)bind; (void)death_link; (void)world; }
void Ap_TestGiveItem(int64 item) { (void)item; }
void Ap_TestDeathLink() {}
const ApTestSent *Ap_TestSent() { static ApTestSent none; return &none; }
// Archipelago's own A Link to the Past world (upstream 8464ead). Every place
// below answers "not a place of the multiworld" (-1), and every gate the
// original game's own answer, which the caller hands in as `vanilla`.
int Ap_BossPlace(int dungeon) { (void)dungeon; return -1; }
int Ap_FairyPlace(bool dark_world) { (void)dark_world; return -1; }
int Ap_HeartPiecePlace(bool indoors, int where, int mask) { (void)indoors; (void)where; (void)mask; return -1; }
int Ap_RoomFlagPlace(int room, int mask) { (void)room; (void)mask; return -1; }
uint8 Ap_PlaceItem(int loc, uint8 vanilla_item) { (void)loc; return vanilla_item; }
bool Ap_PrizeGate(int gate, bool vanilla) { (void)gate; return vanilla; }
// The original game's rule lives here too (Ether opens Misery Mire, Quake opens
// Turtle Rock), so the stand-in keeps it: returning false would seal both.
bool Ap_MedallionOpens(int medallion, int screen) { return medallion == (screen == 0x70 ? 1 : 2); }
bool Ap_IsAlttpFile() { return false; }
ApWorld Ap_HostWorld() { return kApWorld_Fsotp; }
void Ap_SetNewGameWorld(ApWorld w) { (void)w; }
void Ap_TestSetOption(int which, int value) { (void)which; (void)value; }
#endif

#if !MOD_NETPLAY
#include "net.h"
#include "upnp.h"
#include <stdio.h>
// Off: no network sockets. Hosting and joining fail with the game's own message
// ("Unable to open the network port"), and play on one computer is unchanged.
bool Net_Init() { return true; }
void Net_SetSim(int delay_ms, int jitter_ms, int loss_percent) { (void)delay_ms; (void)jitter_ms; (void)loss_percent; }
void Net_SetBlackout(bool on) { (void)on; }
NetSocket *Net_Open(uint16_t port, bool broadcast) { (void)port; (void)broadcast; return NULL; }
void Net_Close(NetSocket *s) { (void)s; }
uint16_t Net_LocalPort(NetSocket *s) { (void)s; return 0; }
bool Net_Send(NetSocket *s, const NetAddr *to, const void *data, int len) { (void)s; (void)to; (void)data; (void)len; return false; }
int Net_Recv(NetSocket *s, NetAddr *from, void *data, int maxlen) { (void)s; (void)from; (void)data; (void)maxlen; return -1; }
bool Net_ParseAddr(const char *str, uint16_t default_port, NetAddr *out) { (void)str; (void)default_port; (void)out; return false; }
void Net_AddrToString(const NetAddr *a, char *buf, size_t bufsize) {
  if (bufsize) snprintf(buf, bufsize, "%u.%u.%u.%u:%u", a->ip >> 24, (a->ip >> 16) & 255, (a->ip >> 8) & 255, a->ip & 255, a->port);
}
bool Net_AddrEqual(const NetAddr *a, const NetAddr *b) { return a->ip == b->ip && a->port == b->port; }
uint32_t Net_GetLocalIp() { return 0; }
int Net_TcpRequest(const NetAddr *to, const char *request, int reqlen, char *response, int maxlen, int timeout_ms) {
  (void)to; (void)request; (void)reqlen; (void)response; (void)maxlen; (void)timeout_ms; return -1;
}
void Upnp_MapPortAsync(uint16 port, UpnpCallback *cb) { if (cb) cb(false, "", port); }
void Upnp_UnmapPort() {}
bool Upnp_MapPort(uint16 port, char *external_ip, size_t ip_size) { (void)port; if (ip_size) external_ip[0] = 0; return false; }
void Upnp_SetStunServer(const char *server) { (void)server; }
bool Upnp_StunPublicIp(const char *server, char *ip_out, size_t ip_size) { (void)server; if (ip_size) ip_out[0] = 0; return false; }
#endif

#if !MOD_HELPBOOK
#include "helpbook.h"
// Off: the help key does nothing (its name is still accepted in zelda3.ini).
const char *Helpbook_Topic(void) { return ""; }
void Helpbook_Open(void) {}
#endif
