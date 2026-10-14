#include "audio.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "features.h"
#include "snes/snes_regs.h"
#include "spc_player.h"
#include "third_party/opus-1.3.1-stripped/opus.h"
#include "config.h"
#include "assets.h"
#include "instance.h"
#include <math.h>

// This needs to hold a lot more things than with just PCM
typedef struct MsuPlayerResumeInfo {
  uint32 tag;
  uint32 offset;
  uint32 samples_until_repeat;
  uint16 range_cur, range_repeat;
  uint64 initial_packet_bytes;  // To verify we seeked right
  uint8 orig_track;        // Using the old zelda track numbers
  uint8 actual_track;      // The MSU track index we're playing (Different if using msu deluxe)
} MsuPlayerResumeInfo;

enum {
  kMsuState_Idle = 0,
  kMsuState_FinishedPlaying = 1,
  kMsuState_Resuming = 2,
  kMsuState_Playing = 3,
};

typedef struct MsuPlayer {
  FILE *f;
  uint32 buffer_size, buffer_pos;
  uint32 preskip, samples_until_repeat;
  uint32 total_samples_in_file, repeat_position;
  uint32 cur_file_offs;
  MsuPlayerResumeInfo resume_info;
  uint8 enabled;
  uint8 state;
  float volume, volume_step, volume_target;
  uint16 range_cur, range_repeat;
  OpusDecoder *opus;
  int16 buffer[960 * 2];
} MsuPlayer;

static MsuPlayer g_msu_player;

static void MsuPlayer_Open(MsuPlayer *mp, int orig_track, bool resume_from_snapshot);

static const uint8 kMsuTracksWithRepeat[48] = {
  1,0,1,1,1,1,1,1,0,1,0,1,1,1,1,0,
  1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,
  1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

static const uint8 kIsMusicOwOrDungeon[] = {
  0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, // 1 = ow songs : 2 = lw, 5 = forest, 7 = town, 9 = dark world, 13 = mountain
  2, 2, 2, 0, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 0, 0, // 2 = indoor songs : 16, 17, 18, 22, 23, 27
};


static const uint8 kMsuDeluxe_OW_Songs[] = {
  37, 37, 42, 38, 38, 38, 38, 39, 37, 37, 42, 38, 38, 38, 38, 41, // lw
  42, 42, 42, 42, 42, 42, 40, 40, 43, 43, 42, 47, 47, 42, 45, 45,
  43, 43, 43, 47, 47, 42, 45, 45, 112, 112, 48, 42, 42, 42, 42, 45,
  44, 44, 48, 48, 48, 46, 46, 46, 44, 44, 44, 48, 48, 46, 46, 46,
  49, 49, 51, 50, 50, 50, 50, 50, 49, 49, 51, 50, 50, 50, 50, 51, // dw
  51, 51, 51, 51, 51, 51, 51, 51, 52, 52, 51, 56, 56, 51, 54, 54,
  52, 52, 52, 56, 56, 51, 54, 54, 58, 52, 57, 51, 51, 51, 51, 54,
  53, 53, 57, 57, 57, 55, 55, 110, 53, 53, 57, 57, 57, 55, 55, 110,
  37, 41, 41, 42, 42, 42, 42, 42, 42, 41, 41, 42, 42, 42, 42, 42, // special
  42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
};

static const uint8 kMsuDeluxe_Entrance_Songs[] = {
   59,  59,  60,  61,  61,  61,  62,  62,  63,  64,  64,  64, 105,  65,  65,  66,
   66,  62,  67,  62,  62,  68,  62,  62,  68,  68,  62,  62,  62,  62,  62,  62,
   62,  62,  62,  62,  69,  70,  71,  72,  73,  73,  73, 106, 102,  74,  62,  62,
   75,  75,  76,  77,  78,  68,  79,  80,  81,  62,  62,  62,  82,  75, 242,  59,
   59,  76, 242, 242, 242,  96,  83,  99,  59, 242, 242, 242,  84,  95, 104,  62,
   85,  62,  62,  86, 242,  67, 103,  83,  83,  87,  76,  88,  81,  98,  81,  88,
   83,  89,  75,  97,  90,  91,  91, 100,  92,  93,  92, 242,  93, 107,  62,  75,
   62,  67,  62, 242, 242, 242,  73,  73,  73,  73, 102, 114,  81,  76,  62,  67,
   62,  61,  94,  62, 103,
};


// Remap an track number into a potentially different track number (used for msu deluxe)
static uint8 RemapMsuDeluxeTrack(MsuPlayer *mp, uint8 track) {
  if (!(mp->enabled & kMsuEnabled_MsuDeluxe) || track >= sizeof(kIsMusicOwOrDungeon))
    return track;
  switch (kIsMusicOwOrDungeon[track]) {
  case 1:
    return BYTE(overworld_area_index) < sizeof(kMsuDeluxe_OW_Songs) ? kMsuDeluxe_OW_Songs[BYTE(overworld_area_index)] : track;
  case 2:
    if (which_entrance >= sizeof(kMsuDeluxe_Entrance_Songs) || kMsuDeluxe_Entrance_Songs[which_entrance] == 242)
      return track;
    return kMsuDeluxe_Entrance_Songs[which_entrance];
  default:
    return track;
  }
}

bool ZeldaIsPlayingMusicTrack(uint8 track) {
  MsuPlayer *mp = &g_msu_player;
  if (mp->state != kMsuState_Idle && mp->enabled & kMsuEnabled_MsuDeluxe)
    return RemapMsuDeluxeTrack(mp, track) == mp->resume_info.actual_track;
  else
    return track == music_unk1;
}

bool ZeldaIsPlayingMusicTrackWithBug(uint8 track) {
  MsuPlayer *mp = &g_msu_player;
  if (mp->state != kMsuState_Idle && mp->enabled & kMsuEnabled_MsuDeluxe)
    return RemapMsuDeluxeTrack(mp, track) == mp->resume_info.actual_track;
  else
    return track == (enhanced_features0 & kFeatures0_MiscBugFixes ? music_unk1 : last_music_control);
}

uint8 ZeldaGetEntranceMusicTrack(int i) {
  MsuPlayer *mp = &g_msu_player;
  uint8 rv = kEntranceData_musicTrack[i];

  // For some entrances the original performs a fade out, while msu deluxe has new tracks.
  if (mp->state != kMsuState_Idle && mp->enabled & kMsuEnabled_MsuDeluxe) {
    if (rv == 242 && kMsuDeluxe_Entrance_Songs[which_entrance] != 242)
      rv = 16;
  }

  return rv;
}

static const uint8 kVolumeTransitionTarget[4] = { 0, 64, 255, 255};
static const uint8 kVolumeTransitionStep[4] = { 7, 3, 3, 24};
// These are precomputed in the config parse
static float kVolumeTransitionStepFloat[4];
static float kVolumeTransitionTargetFloat[4];

void ZeldaPlayMsuAudioTrack(uint8 music_ctrl) {
  MsuPlayer *mp = &g_msu_player;
  if (!mp->enabled) {
    mp->resume_info.tag = 0;
    zelda_apu_write(APUI00, music_ctrl);
    return;
  }
  ZeldaApuLock();
  if ((music_ctrl & 0xf0) != 0xf0)
    MsuPlayer_Open(mp, music_ctrl, false);
  else if (music_ctrl >= 0xf1 && music_ctrl <= 0xf3) {
    mp->volume_target = kVolumeTransitionTargetFloat[music_ctrl - 0xf1];
    mp->volume_step = kVolumeTransitionStepFloat[music_ctrl - 0xf1];
  }

  if (mp->state == 0) {
    zelda_apu_write(APUI00, music_ctrl);
  } else {
    zelda_apu_write(APUI00, 0xf0);  // pause spc player
  }
  ZeldaApuUnlock();
}

static void MsuPlayer_CloseFile(MsuPlayer *mp) {
  if (mp->f)
    fclose(mp->f);
  opus_decoder_destroy(mp->opus);
  mp->opus = NULL;
  mp->f = NULL;
  if (mp->state != kMsuState_FinishedPlaying)
    mp->state = kMsuState_Idle;
  memset(&mp->resume_info, 0, sizeof(mp->resume_info));
}

static void MsuPlayer_Open(MsuPlayer *mp, int orig_track, bool resume_from_snapshot) {
  MsuPlayerResumeInfo resume;
  int actual_track = RemapMsuDeluxeTrack(mp, orig_track);

  if (!resume_from_snapshot) {
    resume.tag = 0;
    // Attempt to resume MSU playback when exiting back to the overworld.
    if (main_module_index == 9 &&
        actual_track == ((MsuPlayerResumeInfo *)msu_resume_info_alt)->actual_track && g_config.resume_msu) {
      memcpy(&resume, msu_resume_info_alt, sizeof(mp->resume_info));
    }
    if (mp->state >= kMsuState_Resuming)
      memcpy(msu_resume_info_alt, &mp->resume_info, sizeof(mp->resume_info));
  } else {
    memcpy(&resume, msu_resume_info, sizeof(mp->resume_info));
  }

  mp->volume_target = kVolumeTransitionTargetFloat[3];
  mp->volume_step = kVolumeTransitionStepFloat[3];

  mp->state = kMsuState_Idle;
  MsuPlayer_CloseFile(mp);
  if (actual_track == 0)
    return;
  char fname[256], buf[8];
  snprintf(fname, sizeof(fname), "%s%d.%s", g_config.msu_path ? g_config.msu_path : "", actual_track, mp->enabled & kMsuEnabled_Opuz ? "opuz" : "pcm");
  printf("Loading MSU %s\n", fname);
  mp->f = fopen(fname, "rb");
  if (mp->f == NULL)
    goto READ_ERROR;
  setvbuf(mp->f, NULL, _IOFBF, 16384);
  if (fread(buf, 1, 8, mp->f) != 8) READ_ERROR: {
    fprintf(stderr, "Unable to read MSU file %s\n", fname);
    MsuPlayer_CloseFile(mp);
    return;
  }
  uint32 file_tag = *(uint32 *)(buf + 0);
  mp->repeat_position = *(uint32 *)(buf + 4);
  mp->state = (resume.actual_track == actual_track && resume.tag == file_tag) ? kMsuState_Resuming : kMsuState_Playing;
  if (mp->state == kMsuState_Resuming) {
    memcpy(&mp->resume_info, &resume, sizeof(mp->resume_info));
  } else {
    mp->resume_info.orig_track = orig_track;
    mp->resume_info.actual_track = actual_track;
    mp->resume_info.tag = file_tag;
    mp->resume_info.range_cur = 8;
  }
  mp->cur_file_offs = mp->resume_info.offset;
  mp->samples_until_repeat = mp->resume_info.samples_until_repeat;
  mp->range_cur = mp->resume_info.range_cur;
  mp->range_repeat = mp->resume_info.range_repeat;
  mp->buffer_size = mp->buffer_pos = 0;
  mp->preskip = 0;
  if (file_tag == (('Z' << 24) | ('U' << 16) | ('P' << 8) | 'O')) {
    mp->opus = opus_decoder_create(48000, 2, NULL);
    if (!mp->opus)
      goto READ_ERROR;
    if (mp->state == kMsuState_Resuming)
      fseek(mp->f, mp->cur_file_offs, SEEK_SET);
  } else if (file_tag == (('1' << 24) | ('U' << 16) | ('S' << 8) | 'M')) {
    fseek(mp->f, 0, SEEK_END);
    mp->total_samples_in_file = (ftell(mp->f) - 8) / 4;
    mp->samples_until_repeat = mp->total_samples_in_file - mp->cur_file_offs;
    fseek(mp->f, mp->cur_file_offs * 4 + 8, SEEK_SET);
  } else {
    goto READ_ERROR;
  }
}

static void MixToBufferWithVolume(int16 *dst, const int16 *src, size_t n, float volume) {
  if (volume == 1.0f) {
    for (size_t i = 0; i < n; i++) {
      dst[i * 2 + 0] += src[i * 2 + 0];
      dst[i * 2 + 1] += src[i * 2 + 1];
    }
  } else {
    uint32 vol = (int32)(65536 * volume);
    for (size_t i = 0; i < n; i++) {
      dst[i * 2 + 0] += src[i * 2 + 0] * vol >> 16;
      dst[i * 2 + 1] += src[i * 2 + 1] * vol >> 16;
    }
  }
}

static void MixToBufferWithVolumeRamp(int16 *dst, const int16 *src, size_t n, float volume, float volume_step, float ideal_target) {
  int64 vol = volume * 281474976710656.0f;
  int64 step = volume_step * 281474976710656.0f;
  for (size_t i = 0; i < n; i++) {
    uint32 v = (vol >> 32);
    dst[i * 2 + 0] += src[i * 2 + 0] * v >> 16;
    dst[i * 2 + 1] += src[i * 2 + 1] * v >> 16;
    vol += step;
  }
}

static void MixToBuffer(MsuPlayer *mp, int16 *dst, const int16 *src, uint32 n) {
  if (mp->volume != mp->volume_target) {
    float step = mp->volume < mp->volume_target ? mp->volume_step : -mp->volume_step;
    float new_vol = mp->volume + step * n;
    uint32 curn = n;
    if (step >= 0 ? new_vol >= mp->volume_target : new_vol < mp->volume_target) {
      uint32 maxn = (uint32)((mp->volume_target - mp->volume) / step);
      curn = UintMin(maxn, curn);
      new_vol = mp->volume_target;
    }
    float vol = mp->volume;
    mp->volume = new_vol;
    MixToBufferWithVolumeRamp(dst, src, curn, vol, step, new_vol);
    dst += curn * 2, src += curn * 2, n -= curn;
  }
  MixToBufferWithVolume(dst, src, n, mp->volume);
}

void MsuPlayer_Mix(MsuPlayer *mp, int16 *audio_buffer, int audio_samples) {
  int r;

  do {
    if (mp->buffer_size - mp->buffer_pos == 0) {
      if (mp->opus != NULL) {
        if (mp->samples_until_repeat == 0) {
          if (mp->range_cur == 0) FINISHED_PLAYING: {
            mp->state = kMsuState_FinishedPlaying;
            MsuPlayer_CloseFile(mp);
            return;
          }
          opus_decoder_ctl(mp->opus, OPUS_RESET_STATE);
          fseek(mp->f, mp->range_cur, SEEK_SET);
          uint8 *file_data = (uint8 *)mp->buffer;
          if (fread(file_data, 1, 10, mp->f) != 10) READ_ERROR: {
            fprintf(stderr, "MSU read/decode error!\n");
            zelda_apu_write(APUI00, mp->resume_info.orig_track);
            MsuPlayer_CloseFile(mp);
            return;
          }
          uint32 file_offs = *(uint32 *)&file_data[0];
          assert((file_offs & 0xF0000000) == 0);
          uint32 samples_until_repeat = *(uint32 *)&file_data[4];
          uint16 preskip = *(uint32 *)&file_data[8];
          mp->samples_until_repeat = samples_until_repeat;
          mp->preskip = preskip & 0x3fff;
          if (preskip & 0x4000)
            mp->range_repeat = mp->range_cur;
          mp->range_cur = (preskip & 0x8000) ? mp->range_repeat : mp->range_cur + 10;
          mp->cur_file_offs = file_offs;
          mp->resume_info.range_repeat = mp->range_repeat;
          mp->resume_info.range_cur = mp->range_cur;
          fseek(mp->f, file_offs, SEEK_SET);
        }
        assert(mp->samples_until_repeat != 0);
        for (;;) {
          uint8 *file_data = (uint8 *)mp->buffer;
          *(uint64 *)file_data = 0;
          if (fread(file_data, 1, 2, mp->f) != 2)
            goto READ_ERROR;
          int size = *(uint16 *)file_data & 0x7fff;
          if (size > 1275)
            goto READ_ERROR;
          int n = (*(uint16 *)file_data >> 15);
          if (fread(&file_data[2], 1, size, mp->f) != size)
            goto READ_ERROR;
          // Verify if the snapshot matches the file on disk.
          uint64 initial_file_data = *(uint64 *)file_data;
          if (mp->state == kMsuState_Resuming) {
            mp->state = kMsuState_Playing;
            if (mp->resume_info.initial_packet_bytes != initial_file_data)
              goto READ_ERROR;
          }
          mp->resume_info.initial_packet_bytes = initial_file_data;
          mp->resume_info.samples_until_repeat = mp->samples_until_repeat + mp->preskip;
          mp->resume_info.offset = mp->cur_file_offs;
          mp->cur_file_offs += 2 + size;
          file_data[1] = 0xfc;
          r = opus_decode(mp->opus, &file_data[2 - n], size + n, mp->buffer, 960, 0);
          if (r <= 0)
            goto READ_ERROR;
          if (r > mp->preskip)
            break;
          mp->preskip -= r;
        }
      } else {
        if (mp->samples_until_repeat == 0) {
          if (mp->resume_info.actual_track < sizeof(kMsuTracksWithRepeat) && !kMsuTracksWithRepeat[mp->resume_info.actual_track])
            goto FINISHED_PLAYING;
          mp->samples_until_repeat = mp->total_samples_in_file - mp->repeat_position;
          if (mp->samples_until_repeat == 0)
            goto READ_ERROR; // impossible to make progress
          mp->cur_file_offs = mp->repeat_position;
          fseek(mp->f, mp->cur_file_offs * 4 + 8, SEEK_SET);
        }
        r = UintMin(960, mp->samples_until_repeat);
        if (fread(mp->buffer, 4, r, mp->f) != r)
          goto READ_ERROR;
        mp->resume_info.offset = mp->cur_file_offs;
        mp->cur_file_offs += r;
      }
      uint32 n = UintMin(r - mp->preskip, mp->samples_until_repeat);
      mp->samples_until_repeat -= n;
      mp->buffer_pos = mp->preskip;
      mp->buffer_size = mp->buffer_pos + n;
      mp->preskip = 0;
    }
#if 0
    if (mp->samples_to_play > 44100 * 5) {
      mp->buffer_pos = mp->buffer_size;
    }
#endif
    int nr = IntMin(audio_samples, mp->buffer_size - mp->buffer_pos);
    int16 *buf = mp->buffer + mp->buffer_pos * 2;
    mp->buffer_pos += nr;

#if 0
    static int t;
    for (int i = 0; i < nr; i++) {
      buf[i * 2 + 0] = buf[i * 2 + 1] = 5000 * sinf(2 * 3.1415 * t++ / 440);
    }
#endif
    MixToBuffer(mp, audio_buffer, buf, nr);

#if 0
    static FILE *f;
    if (!f)f = fopen("out.pcm", "wb");
    fwrite(audio_buffer, 4, nr, f);
    fflush(f);
#endif
    audio_samples -= nr, audio_buffer += nr * 2;
  } while (audio_samples != 0);
}

// Maintain a queue cause the snes and audio callback are not in sync.
struct ApuWriteEnt {
  uint8 ports[4];
};
// One frame's port values per entry, pushed by the game thread, popped by
// the audio thread. The main driver has one; so has each extra sound effect
// driver (below).
typedef struct ApuQueue {
  struct ApuWriteEnt ents[16];
  uint8 pos, count, total;
} ApuQueue;
static struct ApuWriteEnt g_apu_write;
static ApuQueue g_apu_queue;
void zelda_apu_write(uint32_t adr, uint8_t val) {
  g_apu_write.ports[adr & 0x3] = val;
}

// Each world instance has its own sound port state; only the viewed one is heard.
void ZeldaGetApuPorts(uint8 *out) {
  memcpy(out, g_apu_write.ports, 4);
}

void ZeldaSetApuPorts(const uint8 *in) {
  memcpy(g_apu_write.ports, in, 4);
}

static void ApuQueue_Push(ApuQueue *q, const struct ApuWriteEnt *e) {
  q->ents[q->pos++ & 0xf] = *e;
  if (q->count < 16)
    q->count++;
  q->total++;
}

static void ApuQueue_Pop(ApuQueue *q, SpcPlayer *p) {
  if (q->count != 0)
    memcpy(p->input_ports, &q->ents[(q->pos - q->count--) & 0xf], 4);
}

// The game runs a little ahead of the sound: a frame whose ports are the
// same as the ones playing is dropped now and then, so the queue stays short.
static void ApuQueue_Discard(ApuQueue *q, SpcPlayer *p) {
  if (q->count != 0 && memcmp(p->input_ports, &q->ents[(q->pos - q->count) & 0xf], 4) == 0) {
    if (q->total >= 16) {
      q->total = 14;
      q->count--;
    }
  } else {
    q->total = 0;
  }
}

static void ApuQueue_Reset(ApuQueue *q) {
  q->pos = q->total = q->count = 0;
}

void ZeldaPushApuState() {
  ZeldaApuLock();
  ApuQueue_Push(&g_apu_queue, &g_apu_write);
  ZeldaApuUnlock();
}

// Couch co-op: the worlds of the other local players are heard too, for
// their sound effects. Each such world gets a sound driver of its own (an
// SPC player with its DSP, like the main one) that is fed that world's sound
// effect ports only (ports 2 and 3; the music and the ambient sounds of
// port 0 and 1 play from the main world alone), with the music voices left
// out of its mix as for "music off". Its output is added to the main
// driver's a little quieter. The drivers live outside the game's state:
// nothing here is hashed, saved or rolled back, the game never reads them,
// and the main driver runs exactly as it does without them.
typedef struct SfxSource {
  SpcPlayer *player;
  int world;  // the world instance heard, -1: none
  ApuQueue queue;
  struct ApuWriteEnt write;  // the ports pushed for it (ports 0 and 1 stay 0)
} SfxSource;
enum { kSfxSourceMax = kInstMax - 1 };
enum { kSfxGain = 205 };  // 0.8 in 1/256ths
static SfxSource g_sfx[kSfxSourceMax];
static int16 *g_sfx_buffer;  // one block of one source
static int g_sfx_buffer_size;
// The song bank the main driver holds (NULL: not known, as after a reset).
static const uint8 *g_main_bank;
// For the tests: the energy of the main driver and of the extra ones.
static double g_audio_stats_main, g_audio_stats_sfx;
static uint64 g_audio_stats_samples;

// A fresh driver for a world: the sound program and samples of the intro
// bank (the sound effects live in the part every bank shares), no music,
// and the world's current ports as what it last saw, so a sound that ended
// a while ago isn't played again.
static void SfxSource_Bind(SfxSource *s, int world, const uint8 *ports) {
  SpcPlayer *p = s->player;
  if (!p)
    s->player = p = SpcPlayer_Create();
  SpcPlayer_Initialize(p);  // (also clears its ram)
  SpcPlayer_Upload(p, kSoundBank_intro);
  p->sfx_voices = 0;
  SpcPlayer_SetMusicOff(p, true);
  s->world = world;
  memset(&s->write, 0, sizeof(s->write));
  s->write.ports[2] = ports[2];
  s->write.ports[3] = ports[3];
  memcpy(p->input_ports, s->write.ports, 4);
  memcpy(p->last_value_from_snes, s->write.ports, 4);
  ApuQueue_Reset(&s->queue);
}

// The other worlds heard this frame and their ports, from Inst_RunFrame (the
// main world's frame was pushed just before). Called with the same n == 0
// when the feature is off: every driver is then let go.
void ZeldaPushSfxWorlds(int n, const int *worlds, const uint8 (*ports)[4]) {
  ZeldaApuLock();
  for (int k = 0; k < kSfxSourceMax; k++) {
    SfxSource *s = &g_sfx[k];
    bool keep = false;
    for (int i = 0; i < n; i++)
      keep |= worlds[i] == s->world;
    if (!keep)
      s->world = -1;
  }
  for (int i = 0; i < n; i++) {
    SfxSource *s = NULL, *unused = NULL;
    for (int k = 0; k < kSfxSourceMax; k++) {
      if (g_sfx[k].world == worlds[i])
        s = &g_sfx[k];
      else if (g_sfx[k].world < 0 && !unused)
        unused = &g_sfx[k];
    }
    if (!s) {
      if (!unused)
        continue;
      SfxSource_Bind(s = unused, worlds[i], ports[i]);
    }
    s->write.ports[2] = ports[i][2];
    s->write.ports[3] = ports[i][3];
    ApuQueue_Push(&s->queue, &s->write);
  }
  ZeldaApuUnlock();
}

static void ResetSfxSources_Locked() {
  for (int k = 0; k < kSfxSourceMax; k++)
    g_sfx[k].world = -1;
}

static void AudioStats_Add(double *sum, const int16 *buf, size_t n) {
  double acc = 0;
  for (size_t i = 0; i < n; i++)
    acc += (double)buf[i] * buf[i];
  *sum += acc;
}

// Renders the extra drivers' sound and adds it to the main driver's.
static void MixSfxSources(int16 *audio_buffer, int samples, int channels) {
  int n = samples * channels;
  for (int k = 0; k < kSfxSourceMax; k++) {
    SfxSource *s = &g_sfx[k];
    if (s->world < 0)
      continue;
    if (g_sfx_buffer_size < n) {
      free(g_sfx_buffer);
      g_sfx_buffer = (int16 *)malloc(n * sizeof(int16));
      g_sfx_buffer_size = n;
    }
    ApuQueue_Pop(&s->queue, s->player);
    SpcPlayer_GenerateSamples(s->player);
    dsp_getSamples(s->player->dsp, g_sfx_buffer, samples, channels);
    for (int i = 0; i < n; i++) {
      int v = g_sfx_buffer[i] * kSfxGain >> 8;
      g_sfx_buffer[i] = (int16)v;
      v += audio_buffer[i];
      audio_buffer[i] = (int16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    AudioStats_Add(&g_audio_stats_sfx, g_sfx_buffer, n);
  }
}

void ZeldaDiscardUnusedAudioFrames() {
  ApuQueue_Discard(&g_apu_queue, g_zenv.player);
  for (int k = 0; k < kSfxSourceMax; k++)
    if (g_sfx[k].world >= 0)
      ApuQueue_Discard(&g_sfx[k].queue, g_sfx[k].player);
}

static void ZeldaResetApuQueue() {
  ApuQueue_Reset(&g_apu_queue);
  for (int k = 0; k < kSfxSourceMax; k++)
    ApuQueue_Reset(&g_sfx[k].queue);
}

void ZeldaResetAudioStats() {
  ZeldaApuLock();
  g_audio_stats_main = g_audio_stats_sfx = 0;
  g_audio_stats_samples = 0;
  ZeldaApuUnlock();
}

// The RMS (0..32767) of what the main driver and the extra sound effect
// drivers (after their gain) rendered since the last reset.
void ZeldaGetAudioStats(double *main_rms, double *sfx_rms) {
  ZeldaApuLock();
  double n = g_audio_stats_samples ? (double)g_audio_stats_samples : 1;
  *main_rms = sqrt(g_audio_stats_main / n);
  *sfx_rms = sqrt(g_audio_stats_sfx / n);
  ZeldaApuUnlock();
}

int ZeldaNumSfxSources() {
  int n = 0;
  for (int k = 0; k < kSfxSourceMax; k++)
    n += g_sfx[k].world >= 0;
  return n;
}

uint8_t zelda_read_apui00() {
  // This needs to be here because the ancilla code reads
  // from the apu and we don't want to make the core code
  // dependent on the apu timings, so relocated this value
  // to 0x648.
  return g_ram[kRam_APUI00];
}

uint8_t zelda_apu_read(uint32_t adr) {
  return g_zenv.player->port_to_snes[adr & 0x3];
}

void ZeldaRenderAudio(int16 *audio_buffer, int samples, int channels) {
  ZeldaApuLock();
  ApuQueue_Pop(&g_apu_queue, g_zenv.player);
  SpcPlayer_GenerateSamples(g_zenv.player);
  dsp_getSamples(g_zenv.player->dsp, audio_buffer, samples, channels);
  AudioStats_Add(&g_audio_stats_main, audio_buffer, (size_t)samples * channels);
  g_audio_stats_samples += (size_t)samples * channels;
  MixSfxSources(audio_buffer, samples, channels);
  if (g_msu_player.f && channels == 2 && !g_zenv.player->music_off)
    MsuPlayer_Mix(&g_msu_player, audio_buffer, samples);
  ZeldaApuUnlock();
}

// Music off, sound effects on (videos without the music).
void ZeldaSetMusicOff(bool off) {
  ZeldaApuLock();
  SpcPlayer_SetMusicOff(g_zenv.player, off);
  ZeldaApuUnlock();
}

// Audio rendered for a recording (mptest record), where nothing else renders
// it. The sound queue starts empty, so the sound is in step with the
// pictures; and while it is on, the game keeps seeing the music state it saw
// before (APUI00), so a recorded run plays exactly like one that isn't.
static bool g_audio_capture, g_capture_music_playing;
static bool IsMusicPlaying(void);
void ZeldaSetAudioCapture(bool on) {
  ZeldaApuLock();
  if (on && !g_audio_capture)
    g_capture_music_playing = IsMusicPlaying();
  g_audio_capture = on;
  ZeldaResetApuQueue();
  ZeldaApuUnlock();
}

bool ZeldaIsMusicPlaying() {
  return g_audio_capture ? g_capture_music_playing : IsMusicPlaying();
}

static bool IsMusicPlaying(void) {
  if (g_msu_player.state != kMsuState_Idle) {
    return g_msu_player.state != kMsuState_FinishedPlaying;
  } else {
    return g_zenv.player->port_to_snes[0] != 0;
  }
}

void ZeldaRestoreMusicAfterLoad_Locked(bool is_reset) {
  // Restore spc variables from the ram dump.
  SpcPlayer_CopyVariablesFromRam(g_zenv.player);
  // This is not stored in the snapshot
  g_zenv.player->timer_cycles = 0;

  // Restore input ports state
  SpcPlayer *spc_player = g_zenv.player;
  memcpy(spc_player->input_ports, &spc_player->ram[0x410], 4);
  memcpy(g_apu_write.ports, spc_player->input_ports, 4);

  if (is_reset) {
    SpcPlayer_Initialize(g_zenv.player);
  }
  g_main_bank = is_reset ? NULL : ZeldaSongBankOf(g_ram);

  MsuPlayer *mp = &g_msu_player;
  if (mp->enabled) {
    mp->volume = 0.0;
    MsuPlayer_Open(mp, (music_unk1 == 0xf1) ? ((MsuPlayerResumeInfo*)msu_resume_info)->orig_track : 
                   music_unk1, true);

    // If resuming in the middle of a transition, then override
    // the volume with that of the transition.
    if (last_music_control >= 0xf1 && last_music_control <= 0xf3) {
      uint8 target = kVolumeTransitionTarget[last_music_control - 0xf1];
      if (target != msu_volume) {
        float f = kVolumeTransitionTargetFloat[3] * (1.0f / 255);
        mp->volume = msu_volume * f;
        mp->volume_target = target * f;
        mp->volume_step = kVolumeTransitionStepFloat[last_music_control - 0xf1];
      }
    }

    if (g_msu_player.state)
      zelda_apu_write(APUI00, 0xf0);  // pause spc player
  }
  ZeldaResetApuQueue();
  // (The extra sound effect drivers start afresh with the next frame.)
  ResetSfxSources_Locked();
}

void ZeldaSaveMusicStateToRam_Locked() {
  SpcPlayer_CopyVariablesToRam(g_zenv.player);
  // SpcPlayer.input_ports is not saved to the SpcPlayer ram by SpcPlayer_CopyVariablesToRam,
  // in any case, we want to save the most recently written data, and that might still
  // be in the queue. 0x410 is a free memory location in the SPC ram, so store it there.
  SpcPlayer *spc_player = g_zenv.player;
  memcpy(&spc_player->ram[0x410], g_apu_write.ports, 4);

  msu_volume = g_msu_player.volume * 255;
  memcpy(msu_resume_info, &g_msu_player.resume_info, sizeof(g_msu_player.resume_info));
}

void ZeldaEnableMsu(uint8 enable) {
  g_msu_player.volume = 1.0f;
  g_msu_player.enabled = enable;
  if (enable & kMsuEnabled_Opuz) {
    if (g_config.audio_freq != 48000)
      fprintf(stderr, "Warning: MSU Opuz requires: AudioFreq = 48000\n");
  } else if (enable) {
    if (g_config.audio_freq != 44100)
      fprintf(stderr, "Warning: MSU requires: AudioFreq = 44100\n");
  }

  float volscale = g_config.msuvolume * (1.0f / 255 / 100);
  float stepscale = g_config.msuvolume * (60.0f / 256 / 100) / g_config.audio_freq;
  for (size_t i = 0; i != countof(kVolumeTransitionStepFloat); i++) {
    kVolumeTransitionStepFloat[i] = kVolumeTransitionStep[i] * stepscale;
    kVolumeTransitionTargetFloat[i] = kVolumeTransitionTarget[i] * volscale;
  }
}

// Only the heard world's songs go to the driver: another local player's world
// loading its dungeon songs used to cut the music off. A world's bank is
// uploaded when it becomes the heard one (ZeldaUseSongBank).
static void UploadSongBank_Locked(const uint8 *p) {
  if (p == g_main_bank)
    return;
  SpcPlayer_Upload(g_zenv.player, p);
  g_main_bank = p;
}

void LoadSongBank(const uint8 *p) {  // 808888
  ZeldaApuLock();
  if (Inst_HearsCurrent())
    UploadSongBank_Locked(p);
  ZeldaApuUnlock();
}

// The songs a world's game has loaded: the dungeon ones or the others
// (flag_which_music_type), the credits' once there.
const uint8 *ZeldaSongBankOf(const uint8 *ram) {
  if (ram[0x10] == 0x19 || ram[0x10] == 0x1a)
    return kSoundBank_ending;
  return ram[0x136] ? kSoundBank_indoor : kSoundBank_intro;
}

// The world heard changed to one with these songs: the driver takes them,
// and plays the world's song afresh (its port is read as new).
void ZeldaUseSongBank(const uint8 *p) {
  ZeldaApuLock();
  if (p != g_main_bank) {
    UploadSongBank_Locked(p);
    g_zenv.player->last_value_from_snes[0] = 0;
  }
  ZeldaApuUnlock();
}
