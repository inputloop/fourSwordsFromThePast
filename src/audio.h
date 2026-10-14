#ifndef ZELDA3_AUDIO_H_
#define ZELDA3_AUDIO_H_

#include "types.h"

// Things for msu
bool ZeldaIsPlayingMusicTrack(uint8 track);
bool ZeldaIsPlayingMusicTrackWithBug(uint8 track);
void ZeldaPlayMsuAudioTrack(uint8 track);
bool ZeldaIsMusicPlaying();

void ZeldaEnableMsu(uint8 enable);

void ZeldaRenderAudio(int16 *audio_buffer, int samples, int channels);
void ZeldaSetMusicOff(bool off);
void ZeldaSetAudioCapture(bool on);
void ZeldaDiscardUnusedAudioFrames();
void ZeldaRestoreMusicAfterLoad_Locked(bool is_reset);
void ZeldaSaveMusicStateToRam_Locked();
void ZeldaPushApuState();
void ZeldaGetApuPorts(uint8 *out);
void ZeldaSetApuPorts(const uint8 *in);
// Couch co-op: the other local players' worlds, heard for their sound
// effects by a driver each (music off). n == 0 lets every such driver go.
void ZeldaPushSfxWorlds(int n, const int *worlds, const uint8 (*ports)[4]);
int ZeldaNumSfxSources();
// The songs a world's game has loaded, and the heard world's songs for the
// main driver when another world becomes the heard one.
const uint8 *ZeldaSongBankOf(const uint8 *ram);
void ZeldaUseSongBank(const uint8 *p);
// For the tests: the loudness (RMS) of the main driver's output and of the
// extra sound effect drivers' since the last reset.
void ZeldaResetAudioStats();
void ZeldaGetAudioStats(double *main_rms, double *sfx_rms);

#endif  // ZELDA3_AUDIO_H_
