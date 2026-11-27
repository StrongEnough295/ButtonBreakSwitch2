/* SPDX-License-Identifier: MIT
 * Synthetic bytes only. Include the production C++ SDK reader and intercept atomic
 * loads to force deterministic producer changes during its real reader.
 */
#include "switch.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u8 *fixture;static size_t fixture_size;
static unsigned map_calls,unmap_calls,close_calls,seq_reads,payload_reads,style_reads;
static Result map_result,unmap_result;
static Permission map_perm;
static const volatile u64 *tracked_seq;
static const volatile u32 *tracked_style;
static int change_seq,change_style;
static u64 mock_load(const volatile u64 *p,int order) {
    u64 value=__atomic_load_n(p,order);
    if(p==tracked_seq){
        ++seq_reads;
        if(change_seq && (seq_reads%2)==0){
            value+=2;__atomic_store_n((volatile u64*)p,value,__ATOMIC_RELAXED);
            if(change_seq==1)change_seq=0;
        }
    } else if(tracked_seq && p>tracked_seq && p<tracked_seq+7)++payload_reads;
    return value;
}
static u32 mock_load(const volatile u32 *p,int order) {
    u32 value=__atomic_load_n(p,order);
    if(p==tracked_style){++style_reads;if(change_style && !(style_reads%2))return value^1;}
    return value;
}
#define __atomic_load_n(p,o) mock_load((p),(o))
#include "guest_input.cpp"
#undef __atomic_load_n
void shmemLoadRemote(SharedMemory*s,Handle h,size_t n,Permission p){*s=SharedMemory{h,n,p,NULL};}
Result shmemMap(SharedMemory*s){++map_calls;map_perm=s->perm;if(map_result)return map_result;assert(s->size<=fixture_size);s->map_addr=fixture;return 0;}
Result shmemUnmap(SharedMemory*s){++unmap_calls;assert(s->handle==123);assert(s->map_addr==fixture);if(unmap_result)return unmap_result;s->map_addr=NULL;return 0;}
Result shmemClose(SharedMemory*s){(void)s;++close_calls;return 0;}
static void put32(size_t o,u32 x){assert(o+4<=fixture_size);memcpy(fixture+o,&x,4);}
static void put64(size_t o,u64 x){assert(o+8<=fixture_size);memcpy(fixture+o,&x,8);}
static void reset(void){assert(!guestInputHasResources());memset(fixture,0,fixture_size);map_result=unmap_result=0;tracked_seq=NULL;tracked_style=NULL;seq_reads=payload_reads=0;change_seq=change_style=0;}
static size_t ring_for(u32 style){return style==1?0x28:style==4?0xeb8:0x770;}
static size_t sample_at_style(size_t off,unsigned group,u32 style,unsigned tail){return off+group*0x24000+ring_for(style)+0x10+tail*0x38;}
static size_t sample_at(size_t off,unsigned group,unsigned tail){return sample_at_style(off,group,1,tail);}
static void sample_style(size_t off,unsigned group,u32 style,unsigned tail,u64 number){
 size_t ring=off+group*0x24000+ring_for(style),e=sample_at_style(off,group,style,tail);
 put32(off+group*0x24000,style);put64(ring,tail);put64(ring+8,1);put64(e,100);
 put64(e+8+8,number);put64(e+8+16,UINT64_C(0x80000000fedcba98));
 put32(e+8+24,(u32)INT32_MIN);put32(e+8+28,(u32)-32767);put32(e+8+32,32767);put32(e+8+36,INT32_MAX);put32(e+8+40,3);
 tracked_seq=(const volatile u64*)(fixture+e);tracked_style=(const volatile u32*)(fixture+off+group*0x24000);
}
static void sample(size_t off,unsigned group,unsigned tail,u64 number){sample_style(off,group,1,tail,number);}
static void check_fields(const GuestInputSample*out,u64 number){
 assert(out->status==GuestInput_Ready&&out->sampling_number==number&&out->buttons==UINT64_C(0x80000000fedcba98));
 assert(out->left_x==INT32_MIN&&out->left_y==-32767&&out->right_x==32767&&out->right_y==INT32_MAX&&out->attributes==3);
}
int main(void){
 fixture_size=0x4a000;fixture=static_cast<u8*>(aligned_alloc(0x1000,fixture_size));assert(fixture);
 GuestInputSample out;
 assert(guestInputPoll(&out)!=0);assert(out.status==0);assert(guestInputPoll(NULL)!=0);assert(guestInputClose()==0);
 const u64 bad[][2]={{0,0},{0x4a001,0},{0x1001000,0},{UINT64_MAX,0},{0x4a000,1},{0x4a000,UINT64_MAX},{0x4a000,0x4a008},{0x4a000,0xa08}};
 for(size_t i=0;i<sizeof bad/sizeof bad[0];++i){unsigned n=map_calls;assert(guestInputOpen(123,bad[i][0],bad[i][1])!=0);assert(map_calls==n);assert(!guestInputHasResources());}
 assert(guestInputOpen(0,0x4a000,0)!=0);reset();map_result=0x1234;assert(guestInputOpen(123,0x4a000,0)==map_result);assert(!guestInputHasResources());assert(!unmap_calls);reset();
 assert(guestInputOpen(123,0x4a000,0xa00)==0);assert(map_perm==Perm_R);assert(guestInputHasResources());assert(guestInputOpen(123,0x4a000,0)!=0);sample_style(0xa00,2,4,32,99);
 assert(guestInputPoll(&out)==0);assert(out.status==GuestInput_Ready&&out.npad_id==0x20&&out.sampling_number==99&&out.buttons==UINT64_C(0x80000000fedcba98));
 assert(out.left_x==INT32_MIN&&out.left_y==-32767&&out.right_x==32767&&out.right_y==INT32_MAX&&out.attributes==3);assert(guestInputClose()==0);reset();
 assert(guestInputOpen(123,0x4a000,0)==0);assert(guestInputPoll(&out)==0&&out.status==GuestInput_NoStyle);put32(0,2);sample(0,2,0,7);assert(guestInputPoll(&out)==0&&out.status==GuestInput_UnsupportedStyle&&out.npad_id==0);
 put32(0,1);put64(0x28,UINT64_MAX);put64(0x30,0);assert(guestInputPoll(&out)==0&&out.status==GuestInput_NoSamples);
 put64(0x30,34);assert(guestInputPoll(&out)!=0&&out.sampling_number==0);put64(0x30,1);put64(0x28,33);assert(guestInputPoll(&out)!=0);put64(0x28,UINT64_MAX);assert(guestInputPoll(&out)!=0);
 for(unsigned i=0;i<33;++i){sample(0,0,i,500+i);assert(guestInputPoll(&out)==0&&out.status==GuestInput_Ready&&out.sampling_number==500+i);}
 /* Same48-byte field layout and seqlock behavior for all qualified styles. */
 const u32 styles[]={1,4,2};
 for(unsigned kind=0;kind<3;++kind){
  u32 style=styles[kind];unsigned group=style==2?2:0;
  memset(fixture,0,fixture_size);
  for(unsigned tail=0;tail<33;++tail){sample_style(0,group,style,tail,1000+tail);assert(guestInputPoll(&out)==0);check_fields(&out,1000+tail);assert(out.npad_id==(group?0x20u:0u)&&out.style_set==style);}
  size_t entry=sample_at_style(0,group,style,32);put64(entry,101);seq_reads=payload_reads=0;
  assert(guestInputPoll(&out)==0&&out.status==GuestInput_Unstable&&seq_reads==3&&payload_reads==0);
  put64(entry,100);seq_reads=payload_reads=0;change_seq=2;
  assert(guestInputPoll(&out)==0&&out.status==GuestInput_Unstable&&seq_reads==6&&payload_reads==18);change_seq=0;
 }
 /* Combined masks select FullKey before JoyDual; Handheld is qualified on0x20. */
 memset(fixture,0,fixture_size);sample_style(0,0,4,1,200);sample_style(0,0,1,2,100);put32(0,7);
 assert(guestInputPoll(&out)==0);check_fields(&out,100);put32(0,6);assert(guestInputPoll(&out)==0);check_fields(&out,200);
 put32(0,2);assert(guestInputPoll(&out)==0&&out.status==GuestInput_UnsupportedStyle);
 put32(0,8);assert(guestInputPoll(&out)==0&&out.status==GuestInput_UnsupportedStyle);put32(0,16);assert(guestInputPoll(&out)==0&&out.status==GuestInput_UnsupportedStyle);
 sample(0,0,0,123);put64(sample_at(0,0,0),101);seq_reads=payload_reads=0;assert(guestInputPoll(&out)==0&&out.status==GuestInput_Unstable);assert(seq_reads==3&&payload_reads==0&&out.buttons==0);
 put64(sample_at(0,0,0),100);seq_reads=payload_reads=0;change_seq=2;assert(guestInputPoll(&out)==0&&out.status==GuestInput_Unstable);assert(seq_reads==6&&payload_reads==18&&out.sampling_number==0);
 seq_reads=payload_reads=0;change_seq=1;assert(guestInputPoll(&out)==0&&out.status==GuestInput_Ready&&out.sampling_number==123);assert(seq_reads==4&&payload_reads==12);
 seq_reads=payload_reads=0;style_reads=0;change_style=1;assert(guestInputPoll(&out)==0&&out.status==GuestInput_Unstable);assert(seq_reads==6&&payload_reads==18);change_style=0;
 for(unsigned i=0;i<2000;++i){u64 n=(UINT64_C(0x9e3779b97f4a7c15)*(i+1)),count=i%36;put64(0x28,n);put64(0x30,count);Result rc=guestInputPoll(&out);assert(count==0?(rc==0&&out.status==GuestInput_NoSamples):(rc!=0));}
 unmap_result=0x5566;assert(guestInputClose()==unmap_result);assert(guestInputHasResources());assert(guestInputPoll(&out)!=0);unmap_result=0;assert(guestInputClose()==0);assert(!guestInputHasResources());unsigned old=unmap_calls;assert(guestInputClose()==0&&unmap_calls==old);assert(close_calls==0);free(fixture);
 puts("PASS: borrowed READ mapping lifecycle, extreme bounds, all33 tails for FullKey/JoyDual/Handheld, priority/fallback, stable/empty/odd/changing seqlock,3-attempt limit,2000 malformed headers; no handle close");return 0;
}
