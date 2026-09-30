/* Test: decode frame N of an STM with our Cinepak decoder, write PPM. */
#include "../src/cinepak.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned rd32(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb");fseek(f,0,2);long n=ftell(f);fseek(f,0,0);unsigned char*d=malloc(n);fread(d,1,n,f);fclose(f);
  int want=atoi(argv[2]);unsigned ds=rd32(d),cs=rd32(d+4),nc=rd32(d+8);
  Cinepak c;cinepak_init(&c,320,240);
  for(unsigned k=0;k<nc;k++){unsigned char*ch=d+ds+(size_t)k*cs;
    for(unsigned r=rd32(ch+16);r;r=rd32(ch+r)){unsigned char*rec=ch+r;int num=rec[6]|rec[7]<<8;unsigned char*fr=rec+8;
      unsigned len=fr[1]<<16|fr[2]<<8|fr[3];cinepak_decode(&c,fr,len);
      if(num==want){FILE*o=fopen(argv[3],"wb");fprintf(o,"P6 320 240 255\n");
        for(int i=0;i<320*240;i++){unsigned p=c.frame[i];fputc(p>>16&255,o);fputc(p>>8&255,o);fputc(p&255,o);}fclose(o);return 0;}}}
  return 1;}
