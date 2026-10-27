#include "src/ap_ws.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  setvbuf(stdout,0,_IONBF,0); int port=atoi(argv[1]); SDL_atomic_t cancel={0}; ApWsFail f; char err[256];
  ApWs*ws=ApWs_Connect("127.0.0.1",port,"/",false,5000,&cancel,&f,err,sizeof err);
  if(!ws){printf("connect failed %d %s\n",f,err);return 2;}
  for(int i=0;i<20;i++){char*m;size_t l;int r=ApWs_Poll(ws,1000,&m,&l);
    if(r==1){printf("msg len=%zu first=%.20s\n",l,m);free(m);} else if(r==0){printf("none\n");break;} else {printf("closed: %s\n",ApWs_Error(ws));break;}}
  ApWs_Close(ws);return 0;}
