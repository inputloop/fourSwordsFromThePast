#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"
#include "session.h"
#include "upnp.h"
#include "ui.h"
#include "hd2d.h"
#include "hd3d.h"
#include "bindings.h"
#include "instance.h"
#include "archipelago.h"
#include "screenshot.h"

static bool g_run_without_emu = 0;

// Forwards
static bool LoadRom(const char *filename);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int pad, int button, bool pressed);
static void HandleGamepadAxisInput(int pad, int axis, int value);
static void OpenOneGamepad(int i);
static void HandleVolumeAdjustment(int volume_adjustment);
static void LoadAssets();
static void SwitchDirectory();

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
static uint8 g_current_window_scale;
// Per controller state. Controller 0 shares local player 0 with the keyboard,
// every further controller is another local (couch) player.
enum { kMaxPads = 4 };
static SDL_JoystickID g_pad_ids[kMaxPads];
static bool g_pad_used[kMaxPads];
static uint32 g_pad_modifiers[kMaxPads];
static uint16 g_pad_last_cmd[kMaxPads][kGamepadBtn_Count];
static int PadIndexFromId(SDL_JoystickID id);
static void RumbleForEvents();
// The last value of each stick axis (SDL sends one axis at a time), cleared
// when a controller goes, so the next one in that slot starts centered.
static int g_pad_last_lx[kMaxPads], g_pad_last_ly[kMaxPads], g_pad_last_rx[kMaxPads], g_pad_last_ry[kMaxPads];
static void ResetPadSticks(int pad);
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
// Size of the whole picture: bigger than one game screen in split screen.
static int g_disp_width, g_disp_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;

void NORETURN Die(const char *error) {
#if defined(_WIN32)
  // Windows builds have no console, so show errors in a message box.
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

static SDL_mutex *g_audio_mutex;

// The players playing on this computer (what each local player watches), a
// bit each.
static uint8 LocalPlayersMask() {
  const SessionStatus *st = Session_GetStatus();
  uint8 mask = 0;
  for (int i = 0; i < kSessionMaxLocal; i++) {
    int p = st->local_slot[i] < 0 ? -1 : Ui_WatchedPlayer(i);
    if (p >= 0 && p < kMpMaxPlayers)
      mask |= 1 << p;
  }
  return mask;
}

// The worlds to show: one per local player, in different places (split
// screen), and for each the first local player seeing it (-1: nobody).
static int GetViews(int *views, int8 *pane_local) {
  const SessionStatus *st = Session_GetStatus();
  int n = 0;
  for (int i = 0; i < kSessionMaxLocal; i++) {
    // (A ghost waiting in a paused world watches a standing teammate.)
    int inst = st->local_slot[i] < 0 ? -1 : Inst_ViewOfPlayer(Ui_WatchedPlayer(i));
    bool dup = inst < 0;
    for (int j = 0; j < n; j++)
      dup |= views[j] == inst;
    if (!dup)
      pane_local[n] = (int8)i, views[n++] = inst;
  }
  if (n == 0)
    pane_local[n] = -1, views[n++] = Inst_Current();
  return n;
}

static void DrawSplitScreen(uint8 *pixel_buffer, int pitch, const int *views, int nviews, uint32 render_flags) {
  int home = Inst_Current();
  for (int v = 0; v < nviews; v++) {
    Inst_Switch(views[v]);
    uint8 *dst = pixel_buffer + (v & 1) * g_snes_width * 4 + (v >> 1) * g_snes_height * pitch;
    Hd2d_DrawFrame(dst, pitch, render_flags, v);
  }
  if (nviews == 3) {
    for (int y = 0; y < g_snes_height; y++)
      memset(pixel_buffer + (g_snes_height + y) * pitch + g_snes_width * 4, 0, g_snes_width * 4);
  }
  Inst_Switch(home);
}

static void DrawPpuFrameWithPerf() {
  int views[kInstMax];
  int8 pane_local[kInstMax];
  // With the 3D view, a player in first person has a pane of their own.
  int nviews = g_hd3d.enabled ? Hd3d_ComputeViews(views, pane_local, kInstMax) : GetViews(views, pane_local);
  // "Watch everyone": then a pane for each other player's world, always in
  // 2D (only the players here have 3D cameras).
  int players[kInstMax];
  nviews = Ui_AddWatchedPanes(views, pane_local, players, nviews, kInstMax);
  Hd3d_SetPanes(pane_local, nviews);
  // Each screen shows the HUD item of a player playing on this computer.
  const SessionStatus *st = Session_GetStatus();
  g_mp_local_mask = 0;
  for (int i = 0; i < kSessionMaxLocal; i++)
    if (st->local_slot[i] >= 0)
      g_mp_local_mask |= 1 << st->local_slot[i];
  // Tell the overlay whose eyes each screen looks through.
  UiView uv[kInstMax];
  for (int v = 0; v < nviews; v++) {
    uv[v].x = nviews > 1 ? (v & 1) * g_snes_width : 0;
    uv[v].y = nviews > 1 ? (v >> 1) * g_snes_height : 0;
    uv[v].w = g_snes_width, uv[v].h = g_snes_height;
    uv[v].player = players[v];
    uv[v].local = pane_local[v] >= 0 ? pane_local[v] : -1;
  }
  Ui_SetViews(uv, nviews);
  int cols = nviews >= 2 ? 2 : 1, rows = nviews >= 3 ? 2 : 1;
  g_disp_width = g_snes_width * cols;
  g_disp_height = g_snes_height * rows;
  if (nviews == 1)
    Inst_Switch(views[0]);
  int render_scale = nviews == 1 ? PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags) : 1;
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_disp_width * render_scale,
                             g_disp_height * render_scale,
                             &pixel_buffer, &pitch);
  if (!pixel_buffer)
    return;  // the renderer couldn't give us a buffer this frame
  if (nviews > 1) {
    // Split screen: every world is drawn at normal resolution. The worlds are
    // read under the audio lock; 3D panes are then drawn outside it (that
    // takes longer and needs no game memory).
    SDL_LockMutex(g_audio_mutex);
    Hd3d_BeginPanes(true);
    DrawSplitScreen(pixel_buffer, pitch, views, nviews, g_ppu_render_flags & ~kPpuRenderFlags_4x4Mode7);
    SDL_UnlockMutex(g_audio_mutex);
    Hd3d_FinishPanes();
    Screenshot_Grab(kScreenshot_Clean, pixel_buffer, pitch, g_disp_width, g_disp_height);
    Ui_Draw(pixel_buffer, pitch, g_disp_width, g_disp_height, 1);
    Screenshot_Grab(kScreenshot_AsSeen, pixel_buffer, pitch, g_disp_width, g_disp_height);
    g_renderer_funcs.EndDraw();
    return;
  }
  Hd3d_BeginPanes(false);
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    Hd2d_DrawFrame(pixel_buffer, pitch, g_ppu_render_flags, 0);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    Hd2d_DrawFrame(pixel_buffer, pitch, g_ppu_render_flags, 0);
  }
  // The screenshot keys: the frame without the overlay, and as it is seen.
  Screenshot_Grab(kScreenshot_Clean, pixel_buffer, pitch, g_disp_width * render_scale, g_disp_height * render_scale);
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
  Ui_Draw(pixel_buffer, pitch, g_disp_width * render_scale, g_disp_height * render_scale, render_scale);
  Screenshot_Grab(kScreenshot_AsSeen, pixel_buffer, pitch, g_disp_width * render_scale, g_disp_height * render_scale);
  g_renderer_funcs.EndDraw();
}

static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;
static int g_texture_w, g_texture_h;
static bool g_texture_linear;
static int g_logical_w, g_logical_h;

// (Re)creates the streaming texture. Called when the needed size or filtering changes,
// so options can be changed in game without recreating the renderer.
static bool SdlRenderer_CreateTexture(int width, int height) {
  if (g_texture)
    SDL_DestroyTexture(g_texture);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, g_config.linear_filtering ? "best" : "nearest");
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    g_texture_w = g_texture_h = 0;
    return false;
  }
  g_texture_w = width, g_texture_h = height;
  g_texture_linear = g_config.linear_filtering;
  return true;
}

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (int i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  g_logical_w = g_disp_width, g_logical_h = g_disp_height;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_disp_width, g_disp_height);

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
  return SdlRenderer_CreateTexture(g_snes_width * tex_mult, g_snes_height * tex_mult);
}

static void SdlRenderer_Destroy() {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  *pixels = NULL;
  // Display options (widescreen, HD mode 7, smooth scaling) may change while playing.
  if (width > g_texture_w || height > g_texture_h || g_texture_linear != g_config.linear_filtering) {
    if (!SdlRenderer_CreateTexture(IntMax(width, g_texture_w), IntMax(height, g_texture_h)))
      return;
  }
  if (!g_config.ignore_aspect_ratio && (g_logical_w != g_disp_width || g_logical_h != g_disp_height)) {
    g_logical_w = g_disp_width, g_logical_h = g_disp_height;
    SDL_RenderSetLogicalSize(g_renderer, g_disp_width, g_disp_height);
  }
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    *pixels = NULL;
  }
}

static void SdlRenderer_EndDraw() {
  if (!g_texture)
    return;

//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

void InstallCrashHandler();

static bool g_running = true;

static void RequestQuit() {
  g_running = false;
}

static void ToggleFullscreen() {
  g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static uint32 CalcRenderFlags() {
  return g_config.new_renderer * kPpuRenderFlags_NewRenderer |
         g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
         g_config.extend_y * kPpuRenderFlags_Height240 |
         g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
}

// Applies display related settings changed in the options menu without a restart.
static void ApplyDisplaySettings() {
  bool size_changed = g_snes_width != g_config.extended_aspect_ratio * 2 + 256 ||
                      g_snes_height != (g_config.extend_y ? 240 : 224);
  g_zenv.ppu->extraLeftRight = g_draw_extra_left_right = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);
  g_disp_width = g_snes_width, g_disp_height = g_snes_height;
  g_ppu_render_flags = CalcRenderFlags();
  // The renderers pick up the new size and filtering on the next frame.
  if (size_changed)
    ChangeWindowScale(0);
}

#undef main
int main(int argc, char** argv) {
  argc--, argv++;
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }
  InstallCrashHandler();
  ParseConfigFile(config_file);
  LoadAssets();
  LoadLinkGraphics();

  ZeldaInitialize();
  g_zenv.ppu->extraLeftRight = g_draw_extra_left_right = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);
  g_disp_width = g_snes_width, g_disp_height = g_snes_height;


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;

  g_ppu_render_flags = CalcRenderFlags();
  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaSetLanguage(g_config.language);
  // The 2D-HD look as configured (the headless tests use it too).
  g_hd2d.enabled = g_config.hd2d;
  g_hd2d.depth_of_field = g_config.hd2d_depth_of_field;
  g_hd2d.bloom = g_config.hd2d_bloom;
  g_hd2d.vignette = g_config.hd2d_vignette;
  g_hd2d.grade = g_config.hd2d_grade;
  g_hd2d.lighting = g_config.hd2d_lighting;
  g_hd2d.see_through = g_config.hd2d_see_through;
  // The 3D view as configured: for player 1, and for couch players 2-4 their
  // own choice where one is saved.
  Hd3d_SetEnabled(g_config.hd3d);
  for (int i = 1; i < kSessionMaxLocal; i++)
    if (g_config.hd3d_couch[i - 1] >= 0)
      Hd3d_SetLocalEnabled(i, g_config.hd3d_couch[i - 1] != 0);
  g_hd3d.camera_returns = g_config.hd3d_camera_returns;
  g_hd3d.turn_controls = g_config.hd3d_turn_controls;
  g_hd3d.mouse_look = g_config.hd3d_mouse_look;
  g_hd3d.fade_scenery = g_config.hd3d_fade_scenery;
  g_hd3d.invert_y = g_config.hd3d_invert_y;
  g_hd3d.stick_speed = g_config.hd3d_stick_speed;
  g_hd3d.mouse_speed = g_config.hd3d_mouse_speed;
  g_hd3d.fov = g_config.hd3d_fov;
  g_hd3d.perspective = g_config.hd3d_perspective;

  if (argc >= 2 && strcmp(argv[0], "--mptest") == 0) {
    int RunMpTest(const char *script, int width, int height, uint32 render_flags);
    g_zenv.ppu->extraLeftRight = g_draw_extra_left_right = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
    return RunMpTest(argv[1], g_snes_width, g_snes_height, g_ppu_render_flags);
  }

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);

  if (!g_renderer_funcs.Initialize(window))
    return 1;

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec want = { 0 }, have;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  if (g_config.enable_audio) {
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = g_config.audio_channels;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = have.channels;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = malloc(g_frames_per_block * have.channels * sizeof(int16));
  }

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  ZeldaReadSram();

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;
  bool relative_mouse = false;
  uint64 last_counter = SDL_GetPerformanceCounter();

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  Session_Init(g_config.mp_player_name);
  Upnp_SetStunServer(g_config.mp_stun_server);
  Session_SetFriendlyFire(g_config.mp_friendly_fire);
  Session_SetInputDelay(g_config.mp_input_delay);
  Session_SetStory(g_config.four_swords_story);
  g_mp_color_scheme = g_config.mp_colorblind;
  Session_SetRandomizer(g_config.randomizer);
  Session_SetFullInventory(g_config.full_inventory);
  snprintf(g_ap_settings.server, sizeof(g_ap_settings.server), "%s", g_config.ap_server ? g_config.ap_server : "");
  snprintf(g_ap_settings.slot, sizeof(g_ap_settings.slot), "%s", g_config.ap_slot ? g_config.ap_slot : "");
  snprintf(g_ap_settings.password, sizeof(g_ap_settings.password), "%s", g_config.ap_password ? g_config.ap_password : "");
  g_ap_settings.death_link = g_config.ap_death_link;
  static const UiHostFuncs kUiHost = { &ApplyDisplaySettings, &ToggleFullscreen, &ChangeWindowScale, &RequestQuit };
  Ui_Init(&kUiHost);

  while(g_running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERDEVICEREMOVED: {
        int pad = PadIndexFromId(event.cdevice.which);
        if (pad >= 0) {
          g_pad_used[pad] = false;
          g_pad_modifiers[pad] = 0;
          Bindings_ResetController(pad);
          ResetPadSticks(pad);
          // The player whose profile used that pad leaves (not pad 0's: player 1 has the keyboard).
          for (int i = 1; i < kSessionMaxLocal; i++)
            if (g_bind[i].controller == pad)
              Session_LocalLeave(i);
        }
        break;
      }
      case SDL_CONTROLLERAXISMOTION: {
        int pad = PadIndexFromId(event.caxis.which);
        if (pad >= 0)
          HandleGamepadAxisInput(pad, event.caxis.axis, event.caxis.value);
        break;
      }
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        int pad = PadIndexFromId(event.cbutton.which);
        if (b >= 0 && pad >= 0)
          HandleGamepadInput(pad, b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        else if (event.wheel.which != SDL_TOUCH_MOUSEID && !g_paused)
          Hd3d_OnMouseWheel(event.wheel.y);  // (the 3D view's zoom)
        break;
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        // The 3D view: left click first person, middle click the classic
        // view, right drag turns the camera. (Paused, only letting go of the
        // right button: a click would change a picture that isn't drawn.)
        if (!g_paused || (event.button.button == SDL_BUTTON_RIGHT && event.button.state != SDL_PRESSED))
          Hd3d_OnMouseButton(event.button.button, event.button.state == SDL_PRESSED, event.button.clicks,
                             SDL_GetModState(), event.button.which == SDL_TOUCH_MOUSEID);
        break;
      case SDL_MOUSEMOTION:
        // (Only while the 3D view has the mouse: moves of the free cursor,
        // during a pause say, would all turn the camera at once later.)
        if (event.motion.which != SDL_TOUCH_MOUSEID && relative_mouse)
          Hd3d_OnMouseMotion(event.motion.xrel, event.motion.yrel);
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
          Hd3d_OnWindowFocus(event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED);
        break;
      case SDL_KEYDOWN:
        if (event.key.repeat)
          break;
        if (!Ui_HandleKey(event.key.keysym.sym, true)) {
          Bindings_OnKey(event.key.keysym.sym, true);
          // Keys used for playing don't also trigger hotkeys.
          if (!Bindings_IsGameKey(event.key.keysym.sym))
            HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        }
        break;
      case SDL_TEXTINPUT:
        Ui_HandleText(event.text.text);
        break;
      case SDL_KEYUP:
        Bindings_OnKey(event.key.keysym.sym, false);
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        g_running = false;
        break;
      }
    }

    // The 3D view captures the mouse for mouse look and right drags (not
    // while paused or in the menu).
    bool want_relative = !g_paused && Hd3d_WantsRelativeMouse();
    if (want_relative != relative_mouse) {
      relative_mouse = want_relative;
      SDL_SetRelativeMouseMode(want_relative ? SDL_TRUE : SDL_FALSE);
    }

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (device)
        SDL_PauseAudioDevice(device, audiopaused);
    }

    // Nobody pauses an online game (it would freeze everybody).
    if (Session_IsNetplay()) {
      g_paused = false;
      g_turbo = false;
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    uint16 local_inputs[kSessionMaxLocal];
    for (int i = 0; i < kSessionMaxLocal; i++)
      local_inputs[i] = Bindings_GetInput(i);
    // Couch drop-in: Start on a controller without a player joins the game.
    // (Not Back+Start: that opens the menu, to set the controller up first.)
    for (int i = 1; i < kSessionMaxLocal; i++) {
      if (!Session_IsLocalJoined(i) && (local_inputs[i] & 0xc) == 8)
        Session_LocalJoin(i);
    }
    // The overlay menu takes the inputs while it is open.
    Ui_Update(local_inputs, Bindings_GetMenuFallbackInput(Ui_MenuOwner(), Ui_MenuController()));
    // The 3D view's cameras move in real time (turbo frames included), and a
    // turned camera turns the d-pad: the one thing it changes in the game,
    // done here so only the turned buttons are sent.
    uint64 counter = SDL_GetPerformanceCounter();
    float dt = (float)((double)(counter - last_counter) / (double)SDL_GetPerformanceFrequency());
    last_counter = counter;
    Hd3d_Update(dt > 0.05f ? 0.05f : dt);
    Hd3d_HandleViewButtons();
    Hd3d_RemapInputs(local_inputs);

    bool is_replay = false;
    // Sound comes from the world of the first local player; the other local
    // players' worlds are heard for their sound effects (the option).
    Inst_SetAudioPlayer(Ui_WatchedPlayer(0));
    Inst_SetSfxPlayers(g_config.mp_coop_sound_effects ? LocalPlayersMask() : 0);
    if (!Ui_PausesGame()) {
      SDL_LockMutex(g_audio_mutex);
      Session_Tick(local_inputs, g_config.features0);
      is_replay = Session_LastFrameWasReplay();
      RumbleForEvents();
      SDL_UnlockMutex(g_audio_mutex);
    }
    // Archipelago: what the game did goes to the server (the host's job).
    Ap_Tick();

    frameCtr++;

    if ((g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0) {
      continue;
    }

    DrawPpuFrameWithPerf();

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }
  // Tell the others we are leaving. (A client must not autosave the host's world.)
  bool was_client = Session_IsNetplay() && !Session_AllowsLocalStateChanges();
  Session_Disconnect();
  if (g_config.autosave && !was_client)
    HandleCommand(kKeys_Save + 0, true);
  Screenshot_Wait(5000, NULL);  // (a picture still being written)

  // clean sdl
  if (g_config.enable_audio) {
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
  }

  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();

  SDL_DestroyWindow(window);
  SDL_Quit();
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last)
    return;  // game buttons are handled by the per-player bindings

  // Online, pausing or fast-forwarding would stop or outrun everybody else.
  if (Session_IsNetplay() && (j == kKeys_Turbo || j == kKeys_ReplayTurbo || j == kKeys_Pause || j == kKeys_PauseDimmed))
    return;
  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }
  if (j >= kKeys_View3d && j <= kKeys_View3dReset) {
    Hd3d_OnKeyCommand(j, pressed);  // (display only: allowed online)
    return;
  }
  if (j == kKeys_Screenshot || j == kKeys_ScreenshotClean) {
    if (pressed)  // (display only too)
      Screenshot_Request(j == kKeys_Screenshot ? kScreenshot_AsSeen : kScreenshot_Clean);
    return;
  }
  if (j == kKeys_WatchEveryone) {
    if (pressed)
      Ui_ToggleWatchEveryone();  // (display only: allowed online)
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  // Anything that changes the game state outside of the frame stream would
  // desync a netplay client. The host may do it, and clients then resync.
  if (!Session_AllowsLocalStateChanges() &&
      (j <= kKeys_ReplayRef_Last || j == kKeys_CheatLife || j == kKeys_CheatEquipment || j == kKeys_CheatKeys ||
       j == kKeys_CheatWalkThroughWalls || j == kKeys_Reset || j == kKeys_ClearKeyLog || j == kKeys_StopReplay))
    return;
  // A load, reset or cheat changes the world outside of the frame stream:
  // the clients get a fresh copy once it is done (not for a save: nothing changed).
  bool resync = Session_IsNetplay() &&
                ((j <= kKeys_ReplayRef_Last && !(j >= kKeys_Save && j <= kKeys_Save_Last)) || j == kKeys_Reset ||
                 (j >= kKeys_CheatLife && j <= kKeys_CheatWalkThroughWalls));
  // (Archipelago: the game overs of a loaded state aren't new ones.)
  if ((j <= kKeys_ReplayRef_Last && !(j >= kKeys_Save && j <= kKeys_Save_Last)) || j == kKeys_Reset)
    Ap_OnStateLoaded();
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      if (g_sram_foreign)
        ZeldaReadSram();  // back to our own files after a game we joined
      ZeldaReset(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    default: assert(0);
    }
  }
  if (resync)
    Session_ResyncAll();
}

// The 3D view's camera keys act while they are held: a key goes up as the
// command it went down as, even if Shift, Ctrl or Alt changed meanwhile.
static struct { int key; uint32 cmd; } g_held_view_keys[8];

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  for (size_t i = 0; i < countof(g_held_view_keys); i++) {
    if (g_held_view_keys[i].key == keyCode && !pressed) {
      if (g_held_view_keys[i].cmd != (uint32)j)
        HandleCommand(g_held_view_keys[i].cmd, false);
      g_held_view_keys[i].key = 0;
    } else if (!g_held_view_keys[i].key && pressed && j >= kKeys_View3dTurnLeft && j <= kKeys_View3dTiltDown) {
      g_held_view_keys[i].key = keyCode, g_held_view_keys[i].cmd = (uint32)j;
      break;
    }
  }
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller) {
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
      return;
    }
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    if (PadIndexFromId(id) >= 0)
      return;
    for (int p = 0; p < kMaxPads; p++) {
      if (!g_pad_used[p]) {
        g_pad_used[p] = true;
        g_pad_ids[p] = id;
        Bindings_ResetController(p);
        ResetPadSticks(p);
        g_pad_modifiers[p] = 0;
        printf("Controller %d connected: %s\n", p + 1, SDL_GameControllerName(controller));
        return;
      }
    }
  }
}

static void Rumble(int i, uint16 low, uint16 high, uint32 ms) {
  int c = g_bind[i].controller;
  SDL_GameController *ctl = c < kMaxPads && g_pad_used[c] ? SDL_GameControllerFromInstanceID(g_pad_ids[c]) : NULL;
  if (ctl)
    SDL_GameControllerRumble(ctl, low, high, ms);
}

// A local player's controller tells them what happens to their own Link: a
// hit (harder for a bigger one), going down, getting up again, being lifted,
// thrown and landing, lifting someone, and a slow heartbeat while low on
// hearts. Only this computer's players, only on their own pads, and nothing
// here touches the game.
static void RumbleForEvents() {
  static struct {
    bool known, downed, carried, thrown, carrying;
    uint8 hp;
    uint32 beat;
  } prev[kSessionMaxLocal];
  const SessionStatus *st = Session_GetStatus();
  uint32 now = SDL_GetTicks();
  for (int i = 0; i < kSessionMaxLocal; i++) {
    int p = st->local_slot[i];
    if (p < 0 || !Inst_IsJoined(p)) {
      prev[i].known = false;
      continue;
    }
    bool thrown, carrying;
    bool carried = Inst_PlayerCarry(p, &thrown, &carrying) >= 0;
    bool downed = Inst_PlayerDowned(p);
    uint8 hp = Inst_PlayerHealth(p);
    if (prev[i].known && g_bind[i].rumble) {
      int lost = prev[i].hp - hp;
      bool fired = true;
      if (downed && !prev[i].downed)
        Rumble(i, 0xffff, 0xc000, 600);              // down
      else if (!downed && prev[i].downed)
        Rumble(i, 0x2000, 0x7000, 250);              // up again
      else if (!downed && lost > 0 && lost <= 0x40)
        Rumble(i, (uint16)IntMin(0x5000 + lost * 0x1800, 0xffff), (uint16)IntMin(0x2800 + lost * 0xc00, 0xffff),
               120 + lost * 20);                     // hurt
      else if (thrown && !prev[i].thrown)
        Rumble(i, 0x5000, 0x3000, 120);              // thrown
      else if (!thrown && prev[i].thrown)
        Rumble(i, 0x7000, 0x1000, 90);               // landed
      else if (carried && !prev[i].carried)
        Rumble(i, 0x1800, 0x2800, 80);               // lifted
      else if (carrying && !prev[i].carrying)
        Rumble(i, 0x3000, 0x1000, 80);               // lifting someone
      else
        fired = false;
      // Low on hearts (as the game beeps for it), while playing: a
      // heartbeat. (Not over an event just felt: a new rumble replaces the
      // one going on.)
      int c = IntMin(link_health_capacity >> 3, 20);
      int limit = c < 8 ? 9 : c < 15 ? 17 : 25;
      uint8 m = Inst_PlayerModule(p);
      if (fired) {
        prev[i].beat = now;
      } else if (!downed && hp > 0 && hp < limit && hp < link_health_capacity && (m == 7 || m == 9) &&
                 now - prev[i].beat > 1400) {
        Rumble(i, 0x2400, 0, 70);
        prev[i].beat = now;
      }
    }
    prev[i].known = true;
    prev[i].downed = downed, prev[i].carried = carried, prev[i].thrown = thrown, prev[i].carrying = carrying;
    prev[i].hp = hp;
  }
}

static int PadIndexFromId(SDL_JoystickID id) {
  for (int p = 0; p < kMaxPads; p++)
    if (g_pad_used[p] && g_pad_ids[p] == id)
      return p;
  return -1;
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int pad, int button, bool pressed) {
  if (!!(g_pad_modifiers[pad] & (1 << button)) == pressed)
    return;
  if (pressed && Ui_HandlePadButton(pad, button)) {
    g_pad_modifiers[pad] |= 1 << button;
    return;  // used by the menu (e.g. assigning a button)
  }
  Bindings_OnPadButton(pad, button, pressed);
  if (button == kGamepadBtn_Guide && pressed)
    Ui_ToggleFromController(pad);
  g_pad_modifiers[pad] ^= 1 << button;
  if (pressed)
    g_pad_last_cmd[pad][button] = FindCmdForGamepadButton(button, g_pad_modifiers[pad]);
  uint32 j = g_pad_last_cmd[pad][button];
  if (j == 0)
    return;
  if (j > kKeys_Controls_Last)
    HandleCommand(j, pressed);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

static void ResetPadSticks(int pad) {
  g_pad_last_lx[pad] = g_pad_last_ly[pad] = g_pad_last_rx[pad] = g_pad_last_ry[pad] = 0;
}

static void HandleGamepadAxisInput(int pad, int axis, int value) {
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &g_pad_last_lx[pad] : &g_pad_last_ly[pad]) = value;
    Bindings_OnPadAxis(pad, g_pad_last_lx[pad], g_pad_last_ly[pad]);
  } else if (axis == SDL_CONTROLLER_AXIS_RIGHTX || axis == SDL_CONTROLLER_AXIS_RIGHTY) {
    // The 3D view's camera.
    *(axis == SDL_CONTROLLER_AXIS_RIGHTX ? &g_pad_last_rx[pad] : &g_pad_last_ry[pad]) = value;
    Bindings_OnPadRightAxis(pad, g_pad_last_rx[pad], g_pad_last_ry[pad]);
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(pad, axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

static bool LoadRom(const char *filename) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if(!file) Die("Failed to read file");
  bool result = EmuInitialize(file, length);
  free(file);
  return result;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124)
    memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

// Offers to run extract_assets.bat (Windows) or extract_assets.command
// (macOS, Linux) when the assets are missing: it finds the ROM, gets the
// two Python libraries and builds zelda3_assets.dat.
#if defined(_WIN32)
#define kExtractScript "extract_assets.bat"
#define kExtractCommand "cmd /c extract_assets.bat"
#define kExtractNeeds "This needs Python 3 (python.org)."
#else
#define kExtractScript "extract_assets.command"
#define kExtractCommand "/bin/sh ./extract_assets.command"
#define kExtractNeeds "This needs Python 3 (macOS offers to install it; see the terminal)."
#endif
static bool OfferAssetExtraction() {
  FILE *f = fopen(kExtractScript, "rb");
  if (!f)
    return false;
  fclose(f);
  static const SDL_MessageBoxButtonData kButtons[] = {
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Extract now" },
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
  };
  const SDL_MessageBoxData box = {
    SDL_MESSAGEBOX_INFORMATION, NULL, kWindowTitle,
    "zelda3_assets.dat is missing.\n\n"
    "The game builds it once from your own ROM of A Link to the Past (USA).\n"
    "Put the ROM (.sfc or .smc) in this folder, then choose Extract now.\n"
    kExtractNeeds,
    2, kButtons, NULL
  };
  int button = 0;
  if (SDL_ShowMessageBox(&box, &button) != 0 || button != 1)
    return false;
  int err = system(kExtractCommand);
  (void)err;
  return true;
}

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data && OfferAssetExtraction())
    data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Missing zelda3_assets.dat.\n\n"
          "Put your ROM of A Link to the Past (USA) (.sfc or .smc) next to the game and run "
          "extract_assets.bat (Windows) or extract_assets.command (macOS, Linux). "
          "This needs Python 3. See README.md.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  if (g_config.features0 & kFeatures0_DimFlashes) { // patch dungeon floor palettes
    kPalette_DungBgMain[0x484] = 0x70;
    kPalette_DungBgMain[0x485] = 0x95;
    kPalette_DungBgMain[0x486] = 0x57;
  }
}

// Go some steps up and find zelda3.ini
static void SwitchDirectory() {
  char buf[4096];
  // Prefer the folder the executable is in when it has zelda3.ini.
  char *base = SDL_GetBasePath();
  if (base) {
    snprintf(buf, sizeof(buf), "%szelda3.ini", base);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      int err = chdir(base);
      (void)err;
      SDL_free(base);
      return;
    }
    SDL_free(base);
  }
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
