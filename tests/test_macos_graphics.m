#import "macos_graphics.h"
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void WritePPM(const char *path,const uint32_t *pixels,int w,int h) {
  FILE *f=fopen(path,"wb"); if (!f) abort(); fprintf(f,"P6\n%d %d\n255\n",w,h);
  for (int i=0;i<w*h;i++) { unsigned char rgb[]={pixels[i]>>16,pixels[i]>>8,pixels[i]};fwrite(rgb,1,3,f); } fclose(f);
}
int main(int argc,char **argv) {
  @autoreleasepool {
    if (argc<2) return 2;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr,"Metal device unavailable\n");return 77; }
    NSError *error=nil;
    Dkc1MetalGraphics *g=[[Dkc1MetalGraphics alloc] initWithDevice:device shaderPath:@(argv[1]) error:&error];
    if (!g) { fprintf(stderr,"%s\n",error.description.UTF8String);return 1; }
    int w=64,h=64;
    uint32_t *pixels=malloc((size_t)w*h*4);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
      int r=(x*4)&255,green=(y*4)&255,b=((x/4+y/4)&1)*224;
      if (x<24 && y<24) r=green=b=((x+y)&1)*255;
      if (x>28 && x<y*2) r=green=b=232;
      pixels[y*w+x]=0xff000000u|(r<<16)|(green<<8)|b;
    }
    if (argc>3) {
      FILE *f=fopen(argv[3],"rb");int max;char magic[3];
      if (!f || fscanf(f,"%2s%d%d%d",magic,&w,&h,&max)!=4 || strcmp(magic,"P6") || max!=255) return 2;
      fgetc(f);free(pixels);pixels=malloc((size_t)w*h*4);
      for (int i=0;i<w*h;i++) { unsigned char rgb[3];if (fread(rgb,1,3,f)!=3)return 2;pixels[i]=0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2]; }fclose(f);
    }
    id<MTLCommandQueue> queue=[device newCommandQueue];
    int failures=0;uint64_t hashes[12]={0};
    // Native transfer, nearest, bilinear, five Reconstruct modes, three tubes,
    // and Sharp Bilinear. All use the same immutable input pixels.
    for (int test=0;test<12;test++) {
      @autoreleasepool {
        int scale=getenv("DKC1_TEST_SCALE") ? atoi(getenv("DKC1_TEST_SCALE")) : 4;
        if (scale<1 || scale>16) return 2;
        int ow=test ? w*scale : w,oh=test ? h*scale : h;
        if (test && getenv("DKC1_TEST_PIXEL_ASPECT")) ow=w*scale*7/6;
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:ow height:oh mipmapped:NO];
        d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget;
        id<MTLTexture> out=[device newTextureWithDescriptor:d];
        Dkc1GraphicsSettings s;Dkc1GraphicsDefault(&s);
        if (test==2) s.upscaler=kDkc1UpscalerBilinear;
        if (test>=3 && test<=7) {s.upscaler=kDkc1UpscalerReconstruct;s.reconstruct_mode=test-3;}
        if (test>=8 && test<=10) {s.display=kDkc1DisplayCrt;Dkc1CrtSettingsApplyPreset(&s.crt,test-8);}
        if (test==11) s.upscaler=kDkc1UpscalerSharpBilinear;
        id<MTLCommandBuffer> command=[queue commandBuffer];
        if (![g encodePixels:pixels width:w height:h target:out viewport:(MTLViewport){0,0,ow,oh,0,1} settings:s commandBuffer:command]) return 1;
        [command commit];[command waitUntilCompleted];
        if (command.status==MTLCommandBufferStatusError) {fprintf(stderr,"GPU error: %s\n",command.error.description.UTF8String);return 1;}
        uint32_t *data=malloc((size_t)ow*oh*4);[out getBytes:data bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
        uint64_t hash=1469598103934665603ull;
        for (int i=0;i<ow*oh;i++) {
          hash=(hash^(data[i]&0xffffffu))*1099511628211ull;
          if (test==0 && ((data[i]^pixels[i])&0xffffffu)) failures++;
        }
        hashes[test]=hash;printf("case=%d hash=%016llx gpu_ms=%.4f\n",test,(unsigned long long)hash,(command.GPUEndTime-command.GPUStartTime)*1000.0);
        if (argc>2 && argv[2][0]) {char path[4096];snprintf(path,sizeof path,"%s/case-%02d.ppm",argv[2],test);WritePPM(path,data,ow,oh);}
        if (test>=3 && test<=10) {
          // Repeated scanout must be identical, including CRT mask/dither phase.
          id<MTLCommandBuffer> repeat=[queue commandBuffer];
          if (![g encodePixels:pixels width:w height:h target:out viewport:(MTLViewport){0,0,ow,oh,0,1} settings:s commandBuffer:repeat]) return 1;
          [repeat commit];[repeat waitUntilCompleted];
          uint32_t *again=malloc((size_t)ow*oh*4);
          [out getBytes:again bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
          if (memcmp(data,again,(size_t)ow*oh*4)) failures++;
          printf("  repeat_gpu_ms=%.4f identical=%d\n",(repeat.GPUEndTime-repeat.GPUStartTime)*1000.0,!memcmp(data,again,(size_t)ow*oh*4));
          // Change pixels, settings, then viewport without discarding history;
          // compare each with a brand-new renderer (no reusable prior frame).
          uint32_t original=pixels[w*h/2];
          for (int change=0;change<3;change++) {
            if (change==0) pixels[w*h/2]^=0x00ffffff;
            if (change==1) { s.softness=17; s.crt.glow=23; }
            MTLViewport viewport=change==2 ? (MTLViewport){3,5,ow-8,oh-12,0,1} : (MTLViewport){0,0,ow,oh,0,1};
            Dkc1MetalGraphics *fresh=[[Dkc1MetalGraphics alloc] initWithDevice:device shaderPath:@(argv[1]) error:&error];
            if (!fresh) return 1;
            id<MTLTexture> reference=[device newTextureWithDescriptor:d];
            id<MTLCommandBuffer> changed=[queue commandBuffer];
            if (![g encodePixels:pixels width:w height:h target:out viewport:viewport settings:s commandBuffer:changed] ||
                ![fresh encodePixels:pixels width:w height:h target:reference viewport:viewport settings:s commandBuffer:changed]) return 1;
            [changed commit];[changed waitUntilCompleted];
            if (changed.status==MTLCommandBufferStatusError) return 1;
            [out getBytes:data bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
            [reference getBytes:again bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
            if (memcmp(data,again,(size_t)ow*oh*4)) {fprintf(stderr,"cache invalidation failed case=%d change=%d\n",test,change);failures++;}
            [reference release];[fresh release];
          }
          pixels[w*h/2]=original;free(again);
        }
        free(data);[out release];
      }
    }
    if (hashes[1]==hashes[2] || hashes[3]==hashes[4] || hashes[1]==hashes[8] || hashes[8]==hashes[9] || hashes[9]==hashes[10]) failures++;
    free(pixels);[g release];[queue release];[device release];
    printf("Metal graphics: %s (%d mismatches)\n",failures ? "FAILED" : "passed",failures);return failures ? 1 : 0;
  }
}
