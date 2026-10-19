#include "src/ap_client.c"
#include "src/ap_json.c"
#include <sys/resource.h>
bool ApWs_SendText(ApWs*w,const char*t,size_t l){(void)w;(void)t;(void)l;return true;}
bool ApWs_SendPing(ApWs*w){(void)w;return true;}
int ApWs_Poll(ApWs*w,int t,char**m,size_t*l){(void)w;(void)t;(void)m;(void)l;return 0;}
bool ApWs_Compressed(ApWs*w){(void)w;return false;}
uint32_t ApWs_LastReceive(ApWs*w){(void)w;return 0;}
const char*ApWs_Error(ApWs*w){(void)w;return "";}
void ApWs_Close(ApWs*w){(void)w;}
double ApWs_WallClock(void){return 1.0;}
ApWs*ApWs_Connect(const char*h,int p,const char*pa,bool t,int to,SDL_atomic_t*c,ApWsFail*f,char*e,size_t es){(void)h;(void)p;(void)pa;(void)t;(void)to;(void)c;(void)f;(void)e;(void)es;return NULL;}
static long rss(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return r.ru_maxrss/1024;}
int main(void){
  g_ap.mutex = SDL_CreateMutex(); snprintf(g_ap.slot,64,"Link"); snprintf(g_ap.game,128,"A Link to the Past");
  static cJSON_Hooks hooks = { JsonAlloc, free }; cJSON_InitHooks(&hooks);
  Conn c; memset(&c,0,sizeof c); c.phase=kPhase_Connected;
  size_t cap = 16u<<20; char *b = malloc(cap+64);
  /* one ReceivedItems message near 16 MiB made of "{}," */
  size_t n = snprintf(b,64,"[{\"cmd\":\"ReceivedItems\",\"index\":0,\"items\":[");
  while (n + 8 < cap) { memcpy(b+n,"{},",3); n+=3; }
  n--; memcpy(b+n,"]}]",3); n+=3;
  clock_t t0=clock(); HandleMessage(&c,b,n);
  printf("msg bytes=%zu items=%d peakRSS=%ldMB cpu=%.2fs\n", n, c.num_items, rss(), (double)(clock()-t0)/CLOCKS_PER_SEC);
  /* append more, index == num_items, 5 more messages */
  for (int i=0;i<5;i++){ char hdr[96]; int h=snprintf(hdr,96,"[{\"cmd\":\"ReceivedItems\",\"index\":%d,\"items\":[",c.num_items);
    memmove(b+h, b+ (strlen("[{\"cmd\":\"ReceivedItems\",\"index\":0,\"items\":[")), n- strlen("[{\"cmd\":\"ReceivedItems\",\"index\":0,\"items\":["));
    size_t nn = n - strlen("[{\"cmd\":\"ReceivedItems\",\"index\":0,\"items\":[") + h;
    memcpy(b,hdr,h); HandleMessage(&c,b,nn); n = nn; /* shape stays */ 
    printf("after append %d: items=%d g_ap.num_items=%d peakRSS=%ldMB\n", i+1, c.num_items, g_ap.num_items, rss()); }
  return 0; }
