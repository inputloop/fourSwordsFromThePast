#pragma once
#include "types.h"
#include <SDL_keycode.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,
  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Replay,
  kKeys_Replay_Last = kKeys_Replay + 19,
  kKeys_LoadRef,
  kKeys_LoadRef_Last = kKeys_LoadRef + 19,
  kKeys_ReplayRef,
  kKeys_ReplayRef_Last = kKeys_ReplayRef + 19,
  kKeys_CheatLife,
  kKeys_CheatKeys,
  kKeys_CheatEquipment,
  kKeys_CheatWalkThroughWalls,
  kKeys_ClearKeyLog,
  kKeys_StopReplay,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_ReplayTurbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  // The 3D view (display only, so they work online too): on/off, turn,
  // tilt, back to the classic view. Handled on press and release.
  kKeys_View3d,
  kKeys_View3dTurnLeft,
  kKeys_View3dTurnRight,
  kKeys_View3dTiltUp,
  kKeys_View3dTiltDown,
  kKeys_View3dReset,
  // Screenshots (display only too): as on screen, and without the overlay.
  kKeys_Screenshot,
  kKeys_ScreenshotClean,
  // "Watch everyone": every joined player's world on screen (display only).
  kKeys_WatchEveryone,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
  kOutputMethod_OpenGL_ES,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool enhanced_mode7;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  uint8 extended_aspect_ratio;
  bool extend_y;
  bool no_sprite_limits;
  bool hd2d;                   // the 2D-HD look (hd2d.c)
  uint8 hd2d_depth_of_field, hd2d_bloom, hd2d_vignette, hd2d_grade, hd2d_lighting;
  bool hd2d_see_through;
  bool hd3d;                   // the 3D view (hd3d.c), for player 1
  int8 hd3d_couch[3];          // ... for couch players 2-4: 0, 1, or -1 (as hd3d)
  bool hd3d_camera_returns, hd3d_turn_controls, hd3d_mouse_look, hd3d_fade_scenery, hd3d_invert_y;
  uint8 hd3d_stick_speed, hd3d_mouse_speed, hd3d_fov, hd3d_perspective;
  bool display_perf_title;
  uint8 enable_msu;
  bool resume_msu;
  bool disable_frame_delay;
  uint8 msuvolume;
  uint32 features0;

  // [Multiplayer]
  bool mp_friendly_fire;
  uint32 randomizer;  // for new games: 0 off, 1 random seed, else this seed
  bool four_swords_story;  // the opening legend tells the Four Swords story
  bool four_swords_title;  // "Four Swords of the Past" on the title screen
  bool tracker_after_map;  // randomized files: the check tracker shows after closing a map
  bool full_inventory;  // new games start with every item (for testing)
  bool mp_use_upnp;
  int mp_input_delay;  // frames (online only)
  bool mp_name_tags;   // P1-P4 over each Link
  bool mp_watch_everyone;  // every joined player's world on screen (display only)
  bool mp_coop_sound_effects;  // couch co-op: every local player's sound effects are heard
  bool mp_colorblind;  // color-blind friendly player colors
  uint16 mp_port;
  const char *mp_player_name;
  const char *mp_last_join;
  const char *mp_stun_server;  // asked for our public address when UPnP fails ("" = never)

  // [Archipelago]
  const char *ap_server;
  const char *ap_slot;
  const char *ap_password;
  int ap_death_link;  // 0: as the room says, 1: on, 2: off

  const char *link_graphics;
  char *memory_buffer;
  const char *shader;
  const char *msu_path;
  const char *language;
} Config;

enum {
  kMsuEnabled_Msu = 1,
  kMsuEnabled_MsuDeluxe = 2,
  kMsuEnabled_Opuz = 4,
};
enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
