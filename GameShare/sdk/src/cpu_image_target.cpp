/* SPDX-License-Identifier: MIT */
#include <gameshare/cpu_image_streamer.hpp>
#include <gameshare/guest_display.hpp>
#include "cpu_image_streamer.h"
#include <mutex>

namespace gameshare {
namespace {
struct CpuOwner {};
std::mutex providerMutex;
CpuOwner* providerOwner=nullptr;
std::uint32_t rendererOpen(void* user,Vi2Layer* layer) {
    std::lock_guard<std::mutex> lock(providerMutex);
    if(providerOwner&&providerOwner!=user)
        return MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized);
    providerOwner=static_cast<CpuOwner*>(user);
    return gsCpuImageStreamerOpen(layer);
}
std::uint32_t rendererClose(void* user) {
    std::lock_guard<std::mutex> lock(providerMutex);
    if(providerOwner!=user)return 0; // This target never claimed the provider.
    const auto rc=gsCpuImageStreamerClose();
    if(rc)return rc;
    if(gsCpuImageStreamerHasResources())return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    providerOwner=nullptr;
    return 0;
}
struct Frame {void* owner;const void* source;std::size_t stride;std::uint32_t width,height;};
std::uint32_t upload(void* user,Vi2Layer*) {
    const auto& frame=*static_cast<Frame*>(user);
    std::lock_guard<std::mutex> lock(providerMutex);
    if(providerOwner!=frame.owner)return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
    return gsCpuImageStreamerPresentRgba8(frame.source,frame.stride,frame.width,frame.height);
}
}
struct CpuImageStreamer::Impl {
    std::shared_ptr<CpuOwner> owner=std::make_shared<CpuOwner>();
    NativeGuestDisplay display{RendererHooks{owner.get(),rendererOpen,rendererClose,owner}};
};
CpuImageStreamer::CpuImageStreamer():impl(std::make_unique<Impl>()){}
CpuImageStreamer::~CpuImageStreamer(){
    // NativeGuestDisplay preserves its complete native state and the strong
    // CpuOwner on failure. This outer wrapper owns no native storage itself.
    try {(void)close();}
    catch(...){}
}
std::uint32_t CpuImageStreamer::open(){return impl->display.open();}
std::uint32_t CpuImageStreamer::retire(){return impl->display.retire();}
std::uint32_t CpuImageStreamer::close(){return impl->display.close();}
std::uint32_t CpuImageStreamer::presentRgba8(const void* source,std::size_t stride,
    std::uint32_t width,std::uint32_t height){
    Frame frame{impl->owner.get(),source,stride,width,height};
    return impl->display.withLayer(upload,&frame);
}
bool CpuImageStreamer::isOpen() const{return impl->display.isOpen();}
bool CpuImageStreamer::hasResources() const{return impl->display.hasResources();}
} // namespace gameshare
