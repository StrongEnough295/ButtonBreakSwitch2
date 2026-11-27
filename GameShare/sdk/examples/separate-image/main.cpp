/* SPDX-License-Identifier: MIT */
#include <gameshare/host.hpp>
#include <gameshare/cpu_image_streamer.hpp>
#include <switch.h>
#include <cstdio>
#include <vector>

static void makeFrame(std::vector<u8>& pixels,u64 frame,u64 buttons) {
    const u32 width=gameshare::CpuImageStreamer::Width,height=gameshare::CpuImageStreamer::Height;
    const u32 marker=static_cast<u32>((frame*9)%(width-120));
    for(u32 y=0;y<height;y++)for(u32 x=0;x<width;x++) {
        const bool checker=((x/80+y/80)&1)!=0;
        u8* p=pixels.data()+(static_cast<size_t>(y)*width+x)*4;
        p[0]=static_cast<u8>((x/5+frame*2)&255);
        p[1]=static_cast<u8>(y*255/(height-1));
        p[2]=checker?190:35;p[3]=255;
        if(y>=80&&y<200&&x>=marker&&x<marker+120)p[0]=p[1]=p[2]=255;
        if((buttons&HidNpadButton_A)&&(y<35||y>=height-35)) {p[0]=255;p[1]=20;p[2]=20;}
        if((buttons&HidNpadButton_B)&&(x<35||x>=width-35)) {p[0]=20;p[1]=70;p[2]=255;}
    }
}
int main() {
    // Required by this optional uploader: borrow an already live CPU-console
    // graphics context. This helper is not suitable for a Vulkan/EGL owner.
    consoleInit(nullptr);
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    PadState pad{};padInitializeDefault(&pad);
    {
        auto image=std::make_shared<gameshare::CpuImageStreamer>();
        gameshare::HostConfig config;
        config.videoMode=gameshare::VideoMode::SeparateDisplay;
        config.videoTarget=image;
        config.mirrorAudio=false; // This image example produces no audio.
        gameshare::Host host(config);
        std::vector<u8> pixels(static_cast<size_t>(image->Width)*image->Height*4);
        u64 frame=0;u32 renderError=0;bool keepMapped=false;
        while(appletMainLoop()) {
            padUpdate(&pad);const u64 down=padGetButtonsDown(&pad);
            if(down&HidNpadButton_Plus) {
                host.stopAndWait();
                if(host.canUnload())break;
                keepMapped=true;
            }
            if(!keepMapped&&(down&HidNpadButton_X))host.start();
            if(down&HidNpadButton_B)host.requestStop();
            const auto controller=host.controller();
            if(host.state()==gameshare::Host::State::Hosting) {
                makeFrame(pixels,frame++,controller.buttons);
                const auto rc=image->presentRgba8(pixels.data(),image->Width*4);
                // A close racing this UI-frame check may simply reject a draw.
                if(rc&&host.state()==gameshare::Host::State::Hosting) {
                    renderError=rc;host.requestStop();
                }
            }
            std::printf("\x1b[1;1HGameShare SDK separate-image example\n"
                        "This text is the host Default display.\n"
                        "Guest sees an independent animated RGBA image.\n"
                        "X: start  B: stop  Plus: exit\n"
                        "Guest A: red top/bottom; Guest B: blue sides\n"
                        "State: %d  Guest: %s  Upload: 0x%X                  \n"
                        "Guest buttons: %016llx                              \n",
                        static_cast<int>(host.state()),controller.connected?"connected":"disconnected",
                        static_cast<unsigned>(renderError),static_cast<unsigned long long>(controller.buttons));
            const auto error=host.takeError();
            if(!error.empty())std::printf("\x1b[9;1H%-100s",error.c_str());
            if(keepMapped)std::printf("\x1b[12;1HNative resources retained. Close this software from HOME.\n"
                                     "Do not return to HBMenu or unload this NRO.\n");
            consoleUpdate(nullptr);svcSleepThread(16000000);
        }
        host.stopAndWait();
        while(!host.canUnload()) {
            std::printf("\x1b[12;1HNative resources retained. Close this software from HOME.\n"
                        "Do not return to HBMenu or unload this NRO.\n");
            if(!appletMainLoop())svcExitProcess();
            consoleUpdate(nullptr);svcSleepThread(16000000);
        }
    }
    consoleExit(nullptr);
    return 0;
}
