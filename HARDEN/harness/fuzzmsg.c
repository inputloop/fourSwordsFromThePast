#include "src/ap_client.c"
#include "src/ap_json.c"
/* stubs for ap_ws */
bool ApWs_SendText(ApWs*w,const char*t,size_t l){(void)w;(void)t;(void)l;return true;}
bool ApWs_SendPing(ApWs*w){(void)w;return true;}
int ApWs_Poll(ApWs*w,int t,char**m,size_t*l){(void)w;(void)t;(void)m;(void)l;return 0;}
bool ApWs_Compressed(ApWs*w){(void)w;return false;}
uint32_t ApWs_LastReceive(ApWs*w){(void)w;return 0;}
const char*ApWs_Error(ApWs*w){(void)w;return "";}
void ApWs_Close(ApWs*w){(void)w;}
double ApWs_WallClock(void){return 1.0;}
ApWs*ApWs_Connect(const char*h,int p,const char*pa,bool t,int to,SDL_atomic_t*c,ApWsFail*f,char*e,size_t es){(void)h;(void)p;(void)pa;(void)t;(void)to;(void)c;(void)f;(void)e;(void)es;return NULL;}
static const char *seeds[] = {
 "[{\"cmd\":\"RoomInfo\",\"seed_name\":\"abc\",\"games\":[\"A Link to the Past\",\"Archipelago\",\"X\"],\"datapackage_checksums\":{\"A Link to the Past\":\"c1\",\"Archipelago\":\"c2\",\"X\":\"c3\"}}]",
 "[{\"cmd\":\"DataPackage\",\"data\":{\"games\":{\"A Link to the Past\":{\"checksum\":\"c1\",\"item_name_to_id\":{\"Bow\":59,\"Lamp\":18,\"Big\":1e999,\"Neg\":-5},\"location_name_to_id\":{\"Chest\":1572864,\"Z\":3}},\"Archipelago\":{\"item_name_to_id\":{},\"location_name_to_id\":{}}}}}]",
 "[{\"cmd\":\"Connected\",\"team\":0,\"slot\":2,\"players\":[{\"team\":0,\"slot\":2,\"alias\":\"Link\",\"name\":\"Link\"},{\"team\":0,\"slot\":1,\"alias\":\"Bob\"}],\"slot_info\":{\"1\":{\"name\":\"Bob\",\"game\":\"X\",\"type\":1,\"group_members\":[]},\"2\":{\"name\":\"Link\",\"game\":\"A Link to the Past\",\"group_members\":[1,2,1e300]},\"3\":{\"name\":\"G\",\"game\":\"X\",\"group_members\":[2]}},\"slot_data\":{\"death_link\":1,\"goal\":0,\"crystals_needed_for_gt\":7,\"mm_medalion\":\"Ether\",\"progressive\":2}}]",
 "[{\"cmd\":\"ReceivedItems\",\"index\":0,\"items\":[{\"item\":59,\"location\":1572864,\"player\":2,\"flags\":1},{\"item\":1e30,\"location\":-1,\"player\":99999999999}]}]",
 "[{\"cmd\":\"ReceivedItems\",\"index\":2,\"items\":[{\"item\":\"18\",\"location\":3,\"player\":1}]}]",
 "[{\"cmd\":\"PrintJSON\",\"type\":\"ItemSend\",\"receiving\":2,\"item\":{\"item\":59,\"location\":3,\"player\":1,\"flags\":4}},{\"cmd\":\"PrintJSON\",\"type\":\"Hint\",\"receiving\":2,\"item\":{\"item\":59,\"location\":3,\"player\":1,\"flags\":0},\"data\":[{\"text\":\"[Hint]: \"},{\"type\":\"player_id\",\"text\":\"1\"},{\"type\":\"item_id\",\"text\":\"59\",\"player\":2},{\"type\":\"location_id\",\"text\":\"3\",\"player\":1}]}]",
 "[{\"cmd\":\"PrintJSON\",\"type\":\"Chat\",\"data\":[{\"text\":\"hi \\u001b[2J \\ud800 \\u00e9\"},{\"type\":\"player_id\",\"text\":\"-2147483649\"}]}]",
 "[{\"cmd\":\"Bounced\",\"tags\":[\"DeathLink\"],\"data\":{\"time\":1700000000.5,\"source\":\"Bob\",\"cause\":\"x\"}}]",
 "[{\"cmd\":\"ConnectionRefused\",\"errors\":[\"InvalidSlot\",\"Weird\"]}]",
};
static unsigned long long rs = 1234567ull;
static unsigned rnd(void){ rs ^= rs<<13; rs ^= rs>>7; rs ^= rs<<17; return (unsigned)(rs>>11); }
int main(int argc,char**argv){
  long iters = argc>1 ? atol(argv[1]) : 100000;
  g_ap.mutex = SDL_CreateMutex();
  snprintf(g_ap.slot,sizeof g_ap.slot,"Link"); snprintf(g_ap.game,sizeof g_ap.game,"A Link to the Past"); snprintf(g_ap.uuid,sizeof g_ap.uuid,"u");
  snprintf(g_ap.addr.display,sizeof g_ap.addr.display,"h:1");
  static cJSON_Hooks hooks = { JsonAlloc, free }; cJSON_InitHooks(&hooks);
  int n = sizeof seeds/sizeof*seeds;
  for(long it=0; it<iters; it++){
    Conn c; memset(&c,0,sizeof c); c.phase = rnd()%4; c.slot=2; c.team=0; snprintf(c.seed_name,sizeof c.seed_name,"abc");
    /* run a short sequence of 1-4 mutated seeds on the same Conn */
    int k = 1 + rnd()%4;
    for(int j=0;j<k;j++){
      const char*s = seeds[rnd()%n]; size_t l=strlen(s); char*b=malloc(l+64); memcpy(b,s,l); size_t bl=l;
      int muts = rnd()%4;
      for(int m=0;m<muts;m++){
        unsigned t=rnd()%5; size_t p=rnd()%(bl?bl:1);
        if(t==0) b[p]=(char)rnd();
        else if(t==1 && bl>1){ memmove(b+p,b+p+1,bl-p-1); bl--; }
        else if(t==2 && bl+8<l+64){ memmove(b+p+1,b+p,bl-p); b[p]="0123456789-.e\"[]{},:"[rnd()%20]; bl++; }
        else if(t==3){ size_t q=rnd()%(bl?bl:1); char tmp=b[p]; b[p]=b[q]; b[q]=tmp; }
        else { bl = p; }
      }
      HandleMessage(&c,b,bl); free(b);
      g_ap.num_texts=0;
    }
    /* also leave the checks/items state from previous loops: reset occasionally */
    FreeConn(&c);
    if (it%1000==0){ FreeGames(); free(g_ap.items); g_ap.items=NULL; g_ap.num_items=g_ap.items_cap=0; }
  }
  printf("done %ld\n", iters); return 0; }
