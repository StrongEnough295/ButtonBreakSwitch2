/* SPDX-License-Identifier: MIT */
#include <gameshare/host.hpp>
#include <switch.h>
#include <cstdio>

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);
    {
        gameshare::Host host;
        while (appletMainLoop()) {
            padUpdate(&pad);
            const u64 down = padGetButtonsDown(&pad);
            if (down & HidNpadButton_Plus) {
                host.stopAndWait();
                if (host.canUnload()) break;
                std::printf("\x1b[10;1HNative resources retained. Close this software from HOME.\n"
                            "Do not return to HBMenu or unload this NRO.\n");
            }
            if (down & HidNpadButton_X) host.start();
            if (down & HidNpadButton_B) host.requestStop();
            const auto controller = host.controller();
            std::printf("\x1b[1;1HGameShare SDK mirror example\n"
                        "X: start local sharing  B: stop  Plus: exit\n"
                        "State: %d  Guest: %s                   \n"
                        "Buttons: %016llx                       \n"
                        "Left: %d, %d   Right: %d, %d                     \n",
                        static_cast<int>(host.state()), controller.connected ? "connected" : "disconnected",
                        static_cast<unsigned long long>(controller.buttons), controller.leftX, controller.leftY,
                        controller.rightX, controller.rightY);
            const auto error = host.takeError();
            if (!error.empty()) std::printf("\x1b[8;1H%-100s", error.c_str());
            consoleUpdate(nullptr);
        }
        host.stopAndWait(); // Finalize before destroying the captured display.
        while (!host.canUnload()) {
            // Keep the NRO, graphics and provider code mapped until the user
            // closes the whole software process. No forced console restart.
            if (!appletMainLoop()) svcExitProcess();
            consoleUpdate(nullptr);
            svcSleepThread(16000000);
        }
    }
    consoleExit(nullptr);
    return 0;
}
