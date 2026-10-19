#include <stdio.h>
int main(void){ int health = 0 >> 3; long n=0; unsigned short sink[64]; unsigned short*dst=sink,*dst_org=dst; int row=10;
 do { n++; if (n>1000000) break; dst++; if (--row==0) dst=dst_org+21; } while (--health);
 printf("iterations before cut-off at 1,000,000: %ld (dst advanced %ld uint16 past a 64-entry array)\n", n, (long)(dst-sink)); return 0;}
