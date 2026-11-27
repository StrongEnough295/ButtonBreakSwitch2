/* SPDX-License-Identifier: ISC
 * Offline synthetic test fixture. Public libounce declarations are based on
 * ouncebrew/libounce d1718bb7076fb356cb4cb81ab13d035db5367eea.
 * Copyright (c) 2026 libounce contributors (public API declarations).
 * See LICENSE and README.md for provenance and the scope of these mocks.
 */
/* Includes the provider directly to inspect retained ownership after faults.
 * This fixture never links a Switch SDK or contacts a console. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/cpu_image_streamer.c"

static u8 source_pixels[720][1280*4];
static Result upload(void){return gsCpuImageStreamerPresentRgba8(source_pixels,sizeof(source_pixels[0]),1280,720);}
static CpuWindow owner;
static Vi2Layer host_layer={1,1,2},guest_layer={2,3,4};
static u64 clock_ns;
static bool console_exists=true,release_reached=true;
static Result present_result,register_result,attribute_result;
static bool malformed_release;
static unsigned getdefault_calls,registered,attrs_set,attrs_clear,presented,unregistered_layer;
static unsigned destroyed_alias,destroyed_image,released_memory,retired_range,event_clears,event_waits;
static unsigned event_timeouts,flushed;
static u32 next_id=10;
void mutexLock(Mutex*x){assert(!*x);*x=1;}
void mutexUnlock(Mutex*x){assert(*x==1);*x=0;}
bool eventActive(Event*x){return x->revent!=0;}
Result eventClear(Event*x){assert(x==&owner.slot_event);event_clears++;return 0;}
Result eventWait(Event*x,u64 ns){assert(x==&owner.slot_event&&ns==WAIT_NS);event_waits++;
 if(event_timeouts){event_timeouts--;clock_ns+=ns;return KERNELRESULT(TimedOut);}return 0;}
u64 armGetSystemTick(void){return clock_ns;}
u64 armNsToTicks(u64 ns){return ns;}
u64 armTicksToNs(u64 ns){return ns;}
void armDCacheFlush(void*p,size_t n){assert(p&&n==EXPECTED_IMAGE_SIZE);flushed++;}
Result svcSleepThread(u64 ns){clock_ns+=ns;return 0;}
bool cpuwinDefaultHasImages(void){return console_exists;}
CpuWindow*cpuwinGetDefault(void){getdefault_calls++;return &owner;}
void gsvMakeImageLayout(GsvImageLayout*l,u32 w,u32 h,u32 bh,u32 pg,u32 pf,u8 bf,uint16_t lum){
 assert(w==1280&&h==720&&bh==7&&pg==80&&pf==1&&bf==1&&!lum);memset(l,0,sizeof(*l));}
Result gsvGetImageSize(const GsvImageLayout*l,u64*n){(void)l;*n=EXPECTED_IMAGE_SIZE;return 0;}
Result svcUnknown80(void*p,u64 n,u32 mask,u32 val){assert(p&&n==EXPECTED_IMAGE_SIZE*2&&mask==32);
 if(val){attrs_set++;return attribute_result;}attrs_clear++;return 0;}
Result gsvGpuRegisterMemory(GsvGpu*g,u64 a,u64 n,u8 c,u32*out){
 assert(g==&owner.gpu&&a&&n==EXPECTED_IMAGE_SIZE*2&&c==owner.client);registered++;*out=30;return register_result;}
Result gsvReserveImageMemory(u8 type,u64 n,u32*out){assert(type==3&&n==EXPECTED_IMAGE_SIZE);*out=next_id++;return 0;}
Result gsvRegisterImageMemory(u32 id,u64 address,u64 size,u32 range,u64 off){
 assert(id&&address==(uintptr_t)renderer.backing+off&&size==EXPECTED_IMAGE_SIZE&&range==30);return 0;}
Result gsvCreateImage(const GsvImageLayout*l,u32 m,u32*out){assert(l&&m);*out=next_id++;return 0;}
Result gsvCreateDisplayAlias(u32 i,u32 n){assert(i&&n==1);return 0;}
Result gsvDestroyDisplayAlias(u32 i){assert(i);destroyed_alias++;return 0;}
Result gsvDestroyImage(u32 i){assert(i);destroyed_image++;return 0;}
Result gsvReleaseImageMemory(u32 i){assert(i);released_memory++;return 0;}
Result gsvGpuUnregisterMemory(GsvGpu*g,u32 r,u32 slot){assert(g==&owner.gpu&&r==30&&slot==0);retired_range++;return 0;}
Result gsvSyncReadCounter(GsvSync*s,u32 id,u64*v,u8*m){assert(s==&owner.sync&&id==5);*v=release_reached?1:0;*m=1;return 0;}
void vi2MakeBasicPresentOptions(Vi2PresentOptions*o,u32 w,u32 h,u64 tick){
 assert(w==1280&&h==720);memset(o,0,sizeof(*o));o->flags=3;o->system_tick=tick;o->interval_denominator=60;o->interval_numerator=1;}
Result vi2WaitLayerAvailability(Vi2Layer*l,u64 ns){assert(l==&guest_layer);clock_ns+=ns;return KERNELRESULT(TimedOut);}
Result vi2GetLayerAvailability(Vi2Layer*l,u64*out){assert(l==&guest_layer);*out=1;return 0;}
Result vi2PresentLayer(Vi2Layer*l,u8 slot,u32 image,const Vi2PresentOptions*o,const Vi2Dependency*p,u64*a,Vi2Dependency*r){
 assert(l==&guest_layer&&!slot&&image&&o->flags==3&&releaseEmpty(p));presented++;*a=1;memset(r,0,sizeof(*r));
 u32 token=~5u;u64 threshold=1;memcpy(r->data,&token,4);memcpy(r->data+8,&threshold,8);r->data[4]=malformed_release?0:1;return present_result;}
Result vi2UnregisterLayerImage(Vi2Layer*l,u8 s,const Vi2PresentOptions*o,u64*a,Vi2Dependency*r){
 assert(l==&guest_layer&&!s&&o->flags==2);unregistered_layer++;*a=1;memset(r,0,sizeof(*r));return 0;}

static void resetProcess(void){
 /* Models process death after a quarantined test; the mock server is gone. */
 free(renderer.backing);memset(&renderer,0,sizeof(renderer));resources_alive=renderer_mutex=0;
 owner=(CpuWindow){.is_open=true,.sync={.s={3},.generation=7},.gpu={.main={4},.worker={5}},
  .slot_event={6,0,false},.client=1,.layer=&host_layer,.num_images=2};
 clock_ns=0;console_exists=release_reached=true;present_result=register_result=attribute_result=0;malformed_release=false;
 getdefault_calls=registered=attrs_set=attrs_clear=presented=unregistered_layer=0;
 destroyed_alias=destroyed_image=released_memory=retired_range=event_clears=event_waits=event_timeouts=flushed=0;
}
static void checkTiling(void){
 assert(pixelOffset(0,0)==0&&pixelOffset(4,0)==64&&pixelOffset(8,0)==256&&pixelOffset(0,1)==16);
 assert(pixelOffset(0,4)==128&&pixelOffset(0,8)==512&&pixelOffset(16,0)==8192&&pixelOffset(0,128)==655360);
 u8*seen=calloc(1,(size_t)EXPECTED_IMAGE_SIZE/4);assert(seen);
 for(u32 y=0;y<720;y++)for(u32 x=0;x<1280;x++){u32 n=pixelOffset(x,y);assert(n%4==0&&n+4<=EXPECTED_IMAGE_SIZE&&!seen[n/4]);seen[n/4]=1;}
 free(seen);
}
static void checkUpload(void){
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer));
 const size_t stride=1280*4+28,span=719*stride+1280*4;
 u8 *pixels=malloc(span);assert(pixels);memset(pixels,0xcc,span);
 for(u32 y=0;y<720;y++)for(u32 x=0;x<1280;x++){
  u8 color[4]={(u8)x,(u8)y,(u8)(x^y),255};memcpy(pixels+y*stride+x*4,color,4);
 }
 assert(gsCpuImageStreamerPresentRgba8(NULL,stride,1280,720)!=0);
 assert(gsCpuImageStreamerPresentRgba8(pixels,stride,640,720)!=0);
 assert(gsCpuImageStreamerPresentRgba8(pixels,1,1280,720)!=0);
 assert(gsCpuImageStreamerPresentRgba8(pixels,SIZE_MAX,1280,720)!=0);
 assert(gsCpuImageStreamerPresentRgba8((void*)(UINTPTR_MAX-1),5120,1280,720)!=0);
 assert(!presented&&!flushed);
 assert(!gsCpuImageStreamerPresentRgba8(pixels,stride,1280,720));
 const struct{u32 x,y,offset;} known[]={{0,0,0},{4,0,64},{8,0,256},{0,1,16},{0,4,128},{0,8,512},{16,0,8192},{0,128,655360}};
 for(size_t i=0;i<sizeof(known)/sizeof(known[0]);i++){
  const u8 expected[4]={(u8)known[i].x,(u8)known[i].y,(u8)(known[i].x^known[i].y),255};
  assert(!memcmp((u8*)renderer.backing+known[i].offset,expected,4));
 }
 assert(!gsCpuImageStreamerClose());free(pixels);
}
int main(void){
 checkTiling();checkUpload();resetProcess();console_exists=false;
 assert(gsCpuImageStreamerOpen(&guest_layer)!=0&&!getdefault_calls&&!gsCpuImageStreamerHasResources());
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer)&&gsCpuImageStreamerHasResources());
 assert(gsCpuImageStreamerOpen(&guest_layer)!=0);assert(!upload()&&!upload());
 assert(presented==2&&flushed==2&&renderer.frame==2);assert(!gsCpuImageStreamerClose()&&!gsCpuImageStreamerHasResources());
 assert(destroyed_alias==2&&destroyed_image==2&&released_memory==2&&retired_range==1&&attrs_clear==1);
 assert(event_clears==1&&event_waits==1);assert(!gsCpuImageStreamerClose());
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer));assert(!upload());present_result=0x1234;
 assert(upload()==present_result&&gsCpuImageStreamerHasResources());assert(gsCpuImageStreamerClose()==present_result);
 assert(!destroyed_alias&&!retired_range&&!attrs_clear);assert(upload()==present_result&&presented==2);
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer));malformed_release=true;
 assert(upload()!=0&&gsCpuImageStreamerClose()!=0&&!retired_range&&gsCpuImageStreamerHasResources());
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer)&&!upload());event_timeouts=1;
 assert(gsCpuImageStreamerClose()==KERNELRESULT(TimedOut)&&renderer.range_pending&&gsCpuImageStreamerHasResources());
 assert(retired_range==1&&unregistered_layer==1&&destroyed_alias==2&&event_clears==1);
 assert(!gsCpuImageStreamerClose()&&!gsCpuImageStreamerHasResources()&&retired_range==1&&event_clears==1&&event_waits==2);
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer)&&!upload());release_reached=false;
 assert(gsCpuImageStreamerClose()==KERNELRESULT(TimedOut)&&clock_ns>=WAIT_NS&&!destroyed_alias&&!retired_range);
 assert(gsCpuImageStreamerHasResources()&&unregistered_layer==1);release_reached=true;
 assert(!gsCpuImageStreamerClose()&&unregistered_layer==1&&!gsCpuImageStreamerHasResources());
 resetProcess();register_result=0x4321;assert(gsCpuImageStreamerOpen(&guest_layer)==register_result);
 assert(gsCpuImageStreamerClose()==register_result&&gsCpuImageStreamerHasResources()&&!attrs_clear&&!retired_range);
 resetProcess();attribute_result=0x123;assert(gsCpuImageStreamerOpen(&guest_layer)==attribute_result&&!gsCpuImageStreamerHasResources()&&!registered);
 resetProcess();assert(!gsCpuImageStreamerOpen(&guest_layer));owner.gpu.worker.session++;
 assert(upload()!=0&&gsCpuImageStreamerClose()!=0&&gsCpuImageStreamerHasResources()&&!presented&&!retired_range);
 resetProcess();puts("PASS: RGBA stride and bounds, independent tiling vectors, normal lifetime, quarantine, retry without replay, owner identity");return 0;
}

