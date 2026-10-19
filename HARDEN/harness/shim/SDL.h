#ifndef SHIM_SDL_H
#define SHIM_SDL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#define SDLCALL
typedef struct { volatile int value; } SDL_atomic_t;
static inline int SDL_AtomicGet(SDL_atomic_t *a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
static inline int SDL_AtomicSet(SDL_atomic_t *a,int v){__atomic_store_n(&a->value,v,__ATOMIC_SEQ_CST);return 0;}
typedef struct SDL_mutex SDL_mutex; typedef struct SDL_Thread SDL_Thread;
SDL_mutex *SDL_CreateMutex(void); int SDL_LockMutex(SDL_mutex*); int SDL_UnlockMutex(SDL_mutex*); void SDL_DestroyMutex(SDL_mutex*);
SDL_Thread *SDL_CreateThread(int (*fn)(void*),const char*name,void*data); void SDL_DetachThread(SDL_Thread*); void SDL_WaitThread(SDL_Thread*,int*);
void SDL_Delay(uint32_t); uint32_t SDL_GetTicks(void); uint64_t SDL_GetPerformanceCounter(void);
#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
#define SDL_strtokr strtok_r
void *SDL_LoadObject(const char*); void *SDL_LoadFunction(void*,const char*); void SDL_UnloadObject(void*); void SDL_ClearError(void);
#endif
