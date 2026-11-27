/* SPDX-License-Identifier: MIT -- actual C++ wrapper, synthetic CPU and VI2. */
#include <gameshare/cpu_image_streamer.hpp>
#include <gameshare/guest_display.hpp>
#include "../../src/cpu_image_streamer.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
static std::vector<std::string> calls;
static bool alive=false;
static Result closeFailure=0;
static bool contradictoryClose=false;
static unsigned opens=0,presents=0,closes=0;
static const void* retainedLayer;
static const void* wantedSource;
Result vi2Initialize(){calls.emplace_back("init");return 0;}
void vi2Exit(){calls.emplace_back("exit");}
Result vi2OpenDisplay(Vi2Display* out,const char*){*out={1,2};return 0;}
Result vi2CloseDisplay(Vi2Display*){calls.emplace_back("display-close");return 0;}
void vi2CreateEndpoint(Vi2Endpoint* out,Vi2Display* display,const char*){*out={display,3};}
void vi2CloseEndpoint(Vi2Endpoint*){}
Result vi2CreateLayer(Vi2Layer* out){*out={7,8,9};return 0;}
Result vi2AttachLayer(Vi2Layer*,Vi2Display*){return 0;}
Result vi2SelectLayer(Vi2Layer*,Vi2Endpoint*){return 0;}
Result vi2DetachLayer(Vi2Layer*){calls.emplace_back("detach");return 0;}
Result vi2ClearLayerSelection(Vi2Endpoint*){return 0;}
Result vi2CloseLayer(Vi2Layer*){calls.emplace_back("layer-close");return 0;}
Result gsCpuImageStreamerOpen(Vi2Layer* layer){assert(!alive);retainedLayer=layer;alive=true;++opens;return 0;}
Result gsCpuImageStreamerPresentRgba8(const void* source,size_t stride,u32 width,u32 height){
 assert(alive&&source==wantedSource&&stride==5200&&width==1280&&height==720);++presents;return 0;
}
Result gsCpuImageStreamerClose(){calls.emplace_back("cpu-retire");++closes;if(closeFailure)return closeFailure;if(!contradictoryClose)alive=false;return 0;}
bool gsCpuImageStreamerHasResources(){return alive;}
int main(int argc,char**){
 using gameshare::CpuImageStreamer;
 int pixels=0;wantedSource=&pixels;
 {CpuImageStreamer first,second;
  assert(first.presentRgba8(&pixels,5200)!=0&&presents==0);
  assert(!first.open()&&!first.open()&&opens==1);
  assert(second.open()!=0&&opens==1);assert(!second.close()&&alive&&closes==0);
  assert(!first.presentRgba8(&pixels,5200)&&presents==1);
  const auto before=calls.size();assert(!first.retire()&&!first.isOpen()&&first.hasResources());
  assert(calls.size()==before+1&&calls.back()=="cpu-retire"&&!alive);
  assert(first.presentRgba8(&pixels,5200)!=0&&presents==1);
  assert(!first.retire()&&closes==1);assert(!first.close()&&!first.hasResources());
  assert(std::find(calls.begin(),calls.end(),"cpu-retire")<std::find(calls.begin()+static_cast<std::ptrdiff_t>(before),calls.end(),"detach"));
  assert(!second.open()&&opens==2);assert(!second.retire()&&!second.close());
 }
 {auto retained=std::make_unique<CpuImageStreamer>();assert(!retained->open());
  contradictoryClose=argc>1;closeFailure=contradictoryClose?0:0x6161;
  const Result expected=contradictoryClose?MAKERESULT(Module_Libnx,LibnxError_BadInput):closeFailure;
  assert(retained->retire()==expected&&retained->hasResources());
  const auto before=calls;const auto retired=closes;
  assert(retained->close()==expected&&closes==retired);retained.reset();assert(calls==before&&alive&&retainedLayer);
 }
}
