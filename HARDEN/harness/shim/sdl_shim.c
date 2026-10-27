#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include "SDL.h"
struct SDL_mutex{pthread_mutex_t m;}; struct SDL_Thread{pthread_t t;int (*fn)(void*);void*d;int detached;};
SDL_mutex *SDL_CreateMutex(void){SDL_mutex*m=malloc(sizeof*m);pthread_mutex_init(&m->m,0);return m;}
int SDL_LockMutex(SDL_mutex*m){return pthread_mutex_lock(&m->m);} int SDL_UnlockMutex(SDL_mutex*m){return pthread_mutex_unlock(&m->m);}
void SDL_DestroyMutex(SDL_mutex*m){pthread_mutex_destroy(&m->m);free(m);}
static void*tramp(void*a){SDL_Thread*t=a;t->fn(t->d);return 0;}
SDL_Thread*SDL_CreateThread(int(*fn)(void*),const char*n,void*d){(void)n;SDL_Thread*t=calloc(1,sizeof*t);t->fn=fn;t->d=d;pthread_create(&t->t,0,tramp,t);return t;}
void SDL_DetachThread(SDL_Thread*t){pthread_detach(t->t);}
void SDL_WaitThread(SDL_Thread*t,int*s){(void)s;pthread_join(t->t,0);free(t);}
void SDL_Delay(uint32_t ms){usleep(ms*1000);}
uint32_t SDL_GetTicks(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint32_t)(ts.tv_sec*1000+ts.tv_nsec/1000000);}
uint64_t SDL_GetPerformanceCounter(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec*1000000000ull+ts.tv_nsec;}
void*SDL_LoadObject(const char*p){return dlopen(p,RTLD_NOW|RTLD_GLOBAL);} void*SDL_LoadFunction(void*h,const char*n){return dlsym(h,n);} void SDL_UnloadObject(void*h){dlclose(h);} void SDL_ClearError(void){}
