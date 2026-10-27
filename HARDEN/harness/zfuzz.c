#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "third_party/stb/stb_image.h"
int main(void){
  FILE*f=fopen("deflate_seeds.bin","rb"); static unsigned char all[1<<20]; size_t n=fread(all,1,sizeof all,f); fclose(f);
  unsigned char*seeds[64]; size_t lens[64]; int ns=0; for(size_t p=0;p<n&&ns<64;){uint32_t l=*(uint32_t*)(all+p);seeds[ns]=all+p+4;lens[ns++]=l;p+=4+l;}
  static const uint8_t tail[9]={0,0,0xff,0xff,1,0,0,0xff,0xff}; unsigned long long rs=99;
  long ok=0,bad=0;
  for(long it=0;it<400000;it++){
    rs^=rs<<13;rs^=rs>>7;rs^=rs<<17; int si=rs%ns; size_t l=lens[si];
    uint8_t*in=malloc(l+9); memcpy(in,seeds[si],l); memcpy(in+l,tail,9); size_t il=l+9;
    int muts=1+(rs>>20)%4; for(int m=0;m<muts;m++){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17; size_t p=rs%il; int t=(rs>>40)%3; if(t==0)in[p]^=1<<((rs>>45)%8); else if(t==1)in[p]=(uint8_t)(rs>>50); else il=p+1;}
    rs^=rs<<13;rs^=rs>>7;rs^=rs<<17; int olen=(int)(rs%(l*6+64))+1; char*out=malloc(olen+1);
    int r=stbi_zlib_decode_noheader_buffer(out,olen,(const char*)in,(int)il); if(r>=0)ok++;else bad++;
    free(out);free(in);}
  printf("ok=%ld bad=%ld\n",ok,bad);return 0;}
