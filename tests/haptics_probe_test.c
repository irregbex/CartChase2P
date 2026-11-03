#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "dkc1_haptics.h"
static unsigned char ram[0x20000];
static void word(unsigned addr,unsigned value){ram[addr]=(unsigned char)value;ram[addr+1]=(unsigned char)(value>>8);}
static Dkc1StompProbe falling(unsigned slot){
  Dkc1StompProbe p;memset(ram,0,sizeof ram);ram[0x82]=(unsigned char)slot;
  word(0xd45+slot,slot==2?1:2);word(0x1029+slot,1);word(0xef1+slot,0xf8b8);
  Dkc1StompProbeCapture(&p,ram);return p;
}
int main(int argc,char **argv){
  Dkc1StompProbe p=falling(2);
  word(0xef3,0x0900);
#ifdef DKC1_DIXIE_VARIANT
  assert(Dkc1StompProbeAccepted(&p,ram));
  word(0xef3,0x0720);assert(!Dkc1StompProbeAccepted(&p,ram));
#else
  assert(!Dkc1StompProbeAccepted(&p,ram));
  word(0xef3,0x0720);assert(Dkc1StompProbeAccepted(&p,ram));
#endif
  /* Ordinary jump, damage, held upward velocity and switched actors fail. */
  word(0xef3,0x0780);assert(!Dkc1StompProbeAccepted(&p,ram));
  word(0xef3,0x0800);word(0x102b,17);assert(!Dkc1StompProbeAccepted(&p,ram));
  p=falling(4);word(0xef5,0x0880);assert(Dkc1StompProbeAccepted(&p,ram));
  p.vertical_velocity=1;assert(!Dkc1StompProbeAccepted(&p,ram));
  p=falling(4);word(0xef5,0x0880);ram[0x82]=2;assert(!Dkc1StompProbeAccepted(&p,ram));
  /* Optional private 00000-015ff WRAM trace: exercise the real detector
   * against consecutive recorded frames without redistributing game data. */
  if(argc==3){
    FILE *input=fopen(argv[1],"rb");assert(input);
    memset(ram,0,sizeof ram);unsigned frame=0,hits=0,last=0;
    while(fread(ram,1,0x1600,input)==0x1600){
      ++frame;
      if(frame>1 && Dkc1StompProbeAccepted(&p,ram)){++hits;last=frame;}
      Dkc1StompProbeCapture(&p,ram);
    }
    assert(!ferror(input));fclose(input);
    unsigned expected=(unsigned)strtoul(argv[2],NULL,10);
    assert(hits==(expected?1u:0u));assert(last==expected);
    printf("TRACE_STOMP_PASS frames=%u hits=%u frame=%u\n",frame,hits,last);
  }
  return 0;
}
