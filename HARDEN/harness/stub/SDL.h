#include <stdint.h>
#include <stddef.h>
typedef int SDL_SpinLock;
typedef struct { volatile int value; } SDL_atomic_t;
uint32_t SDL_GetTicks(void);
uint64_t SDL_GetPerformanceCounter(void);
void SDL_AtomicLock(SDL_SpinLock*); void SDL_AtomicUnlock(SDL_SpinLock*);
#define SDLCALL
