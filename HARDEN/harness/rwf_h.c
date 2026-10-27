#include <stdio.h>
#include <stdlib.h>
#include "util.h"
void Die(const char *e){ fprintf(stderr,"Die: %s\n",e); exit(1);}
int main(int argc,char**argv){ size_t n=0; uint8 *p=ReadWholeFile(argv[1],&n); printf("p=%p n=%zu\n",(void*)p,n); return 0;}
