#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/cJSON/cJSON.h"
static void*run(void*a){ (void)a; for(int d=990; d<=1002; d+=4){ char*b=malloc(2*d+1); memset(b,'[',d); memset(b+d,']',d); b[2*d]=0; cJSON*r=cJSON_ParseWithLength(b,2*d); printf("depth %d -> %s\n",d,r?"parsed":"rejected"); cJSON_Delete(r); free(b);} return 0;}
int main(void){ pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 256*1024); pthread_t t; pthread_create(&t,&at,run,0); pthread_join(t,0); return 0;}
