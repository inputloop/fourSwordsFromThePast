#include "src/ap_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char**argv){
  setvbuf(stdout,0,_IONBF,0);
  ApConfig c={0}; c.server=argv[1]; c.slot="Link"; c.password="hunter2-slot-password"; c.game="A Link to the Past";
  ApClient_Start(&c);
  for(int i=0;i<30;i++){ usleep(100000); char t[256]; ApStatus s=ApClient_GetStatus(t,sizeof t); 
    if(s==kApStatus_Connected){ printf("status: %s\n",t); break;} }
  usleep(300000);
  printf("items=%d\n",ApClient_ItemCount());
  for(int i=0;i<ApClient_ItemCount();i++){int64 it,lo;int pl;ApClient_GetItem(i,&it,&lo,&pl);printf(" item %lld from player %d\n",(long long)it,pl);}
  char b[256];ApTextKind k; while(ApClient_PollText(b,sizeof b,&k)) printf("text[%d]: %s\n",k,b);
  ApClient_Stop(); return 0; }
