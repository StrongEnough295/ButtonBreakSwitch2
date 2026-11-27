/* SPDX-License-Identifier: MIT */
#include <gameshare/guest_display.hpp>
#include <algorithm>
#include <cassert>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using gameshare::NativeGuestDisplay;
using gameshare::RendererHooks;
static std::vector<std::string> calls;
static std::string failure;
static constexpr Result Failed=0x5151;
static const void* retainedLayers[16]{};
static unsigned retainedCount=0;
static Result step(const char* name) {calls.emplace_back(name);return failure==name?Failed:0;}
Result vi2Initialize(){return step("init");}
void vi2Exit(){(void)step("exit");}
Result vi2OpenDisplay(Vi2Display* out,const char* name){assert(std::string(name)=="player1");auto r=step("display");if(!r)*out={1,2};return r;}
Result vi2CloseDisplay(Vi2Display* display){assert(display->name==1);return step("close-display");}
void vi2CreateEndpoint(Vi2Endpoint* out,Vi2Display* display,const char* name){assert(std::string(name)=="default");*out={display,3};(void)step("endpoint");}
void vi2CloseEndpoint(Vi2Endpoint* endpoint){assert(endpoint->display->name==1);*endpoint={};(void)step("close-endpoint");}
Result vi2CreateLayer(Vi2Layer* out){auto r=step("layer");if(!r)*out={7,8,9};return r;}
Result vi2AttachLayer(Vi2Layer* layer,Vi2Display* display){assert(layer->token==7&&display->name==1);return step("attach");}
Result vi2SelectLayer(Vi2Layer* layer,Vi2Endpoint* endpoint){assert(layer->token==7&&endpoint->display->name==1);return step("select");}
Result vi2DetachLayer(Vi2Layer* layer){assert(layer->token==7);return step("detach");}
Result vi2ClearLayerSelection(Vi2Endpoint* endpoint){assert(endpoint->display->name==1);return step("clear");}
Result vi2CloseLayer(Vi2Layer* layer){assert(layer->token==7);return step("close-layer");}
static Result rendererOpen(void*,Vi2Layer* layer){assert(layer->token==7);retainedLayers[retainedCount]=layer;if(failure=="throw-open")throw 1;return step("renderer-open");}
static Result rendererClose(void*){if(failure=="throw-close")throw 1;return step("renderer-close");}
static Result draw(void*,Vi2Layer* layer){assert(layer->token==7);if(failure=="throw-draw")throw 1;return step("draw");}
static RendererHooks hooks(){return {nullptr,rendererOpen,rendererClose,{}};}
static void reset(){calls.clear();failure.clear();}
static void success(){
 reset();NativeGuestDisplay display(hooks());
 assert(!display.hasResources()&&!display.isOpen());assert(display.withLayer(draw,nullptr));
 assert(display.open()==0&&display.isOpen()&&display.hasResources());
 const auto opened=calls;assert(display.open()==0&&calls==opened);
 assert(display.withLayer(draw,nullptr)==0);assert(display.retire()==0);
 assert(!display.isOpen()&&display.hasResources());
 assert(calls.back()=="renderer-close");const auto retired=calls;
 assert(display.retire()==0&&calls==retired);assert(display.open()!=0&&calls==retired);
 assert(display.withLayer(draw,nullptr)!=0);assert(display.close()==0);
 assert(!display.isOpen()&&!display.hasResources());
 const std::vector<std::string> expected={"init","display","endpoint","layer","attach","select","renderer-open","draw","renderer-close","detach","clear","close-layer","close-endpoint","close-display","exit"};
 assert(calls==expected);const auto closed=calls;assert(display.close()==0&&calls==closed);
 retainedLayers[retainedCount]=nullptr;
}
static void partialOpen(){
 for(const char* stage:{"init","display","layer","attach","select","renderer-open"}){
  reset();failure=stage;NativeGuestDisplay display(hooks());assert(display.open()==Failed);
  const auto first=calls;assert(display.open()==Failed&&calls==first);
  failure.clear();assert(display.close()==0&&!display.hasResources());
  if(std::string(stage)=="renderer-open"){
   auto a=std::find(calls.begin(),calls.end(),"renderer-close"),b=std::find(calls.begin(),calls.end(),"detach");assert(a<b);
  }
  retainedLayers[retainedCount]=nullptr;
 }
 reset();RendererHooks invalid{};invalid.open=rendererOpen;NativeGuestDisplay bad(invalid);
 assert(bad.open()!=0&&!bad.hasResources()&&calls.empty());assert(bad.close()==0);
}
static void quarantines(){
 for(const char* stage:{"renderer-close","detach","clear","close-layer","close-display"}){
  reset();auto display=std::make_unique<NativeGuestDisplay>(hooks());assert(display->open()==0);
  failure=stage;assert(display->close()==Failed&&display->hasResources()&&!display->isOpen());
  const auto failed=calls;assert(display->withLayer(draw,nullptr)==Failed);assert(display->close()==Failed&&calls==failed);
  // Keep an interior pointer reachable to the deliberately retained private
  // allocation. Destroying the wrapper must neither detach nor force-free it.
  ++retainedCount;display.reset();assert(calls==failed);
 }
}
static void ownership(){
 reset();auto owner=std::make_shared<int>(1);std::weak_ptr<int> weak=owner;
 auto h=hooks();h.owner=owner;auto display=std::make_unique<NativeGuestDisplay>(h);owner.reset();h.owner.reset();
 assert(!weak.expired());assert(display->open()==0&&display->close()==0);display.reset();assert(weak.expired());
 retainedLayers[retainedCount]=nullptr;
}
static void exceptions(){
 reset();{NativeGuestDisplay display(hooks());failure="throw-open";assert(display.open()!=0);failure.clear();assert(display.close()==0);}
 retainedLayers[retainedCount]=nullptr;
 reset();{NativeGuestDisplay display(hooks());assert(display.open()==0);failure="throw-draw";assert(display.withLayer(draw,nullptr)!=0);failure.clear();assert(display.close()==0);}
 retainedLayers[retainedCount]=nullptr;
 reset();auto owner=std::make_shared<int>(3);std::weak_ptr<int> weak=owner;auto h=hooks();h.owner=owner;
 auto display=std::make_unique<NativeGuestDisplay>(h);owner.reset();h.owner.reset();assert(display->open()==0);
 failure="throw-close";assert(display->close()!=0&&display->hasResources());const auto failed=calls;
 ++retainedCount;display.reset();assert(calls==failed&&!weak.expired());
}
struct Gate{std::mutex mutex;std::condition_variable cv;bool entered=false,release=false;};
static Result blockingDraw(void* p,Vi2Layer*){
 auto& gate=*static_cast<Gate*>(p);std::unique_lock<std::mutex> lock(gate.mutex);gate.entered=true;gate.cv.notify_all();gate.cv.wait(lock,[&]{return gate.release;});return 0;
}
static void serialization(){
 reset();NativeGuestDisplay display(hooks());assert(display.open()==0);Gate gate;std::atomic<bool> closed=false;
 std::thread rendering([&]{assert(display.withLayer(blockingDraw,&gate)==0);});
 {std::unique_lock<std::mutex> lock(gate.mutex);gate.cv.wait(lock,[&]{return gate.entered;});}
 std::thread closing([&]{assert(display.close()==0);closed=true;});
 assert(!closed);{std::lock_guard<std::mutex> lock(gate.mutex);gate.release=true;}gate.cv.notify_all();
 rendering.join();closing.join();assert(closed&&!display.hasResources());retainedLayers[retainedCount]=nullptr;
}
int main(){success();partialOpen();ownership();serialization();exceptions();quarantines();}
