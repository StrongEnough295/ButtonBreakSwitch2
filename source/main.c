#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define SAVE_FILE_PATH "sdmc:/buttonbreak_s2_highscore.txt"

int main(int argc, char* argv[])
{
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadHandheld);

    PadState pad;
    padInitializeDefault(&pad);

    int totalPresses = 0;
    int highScore = 0;

    FILE* readFile = fopen(SAVE_FILE_PATH, "r");
    if (readFile != NULL) {
        fscanf(readFile, "%d", &highScore);
        fclose(readFile);
    }

    const char* bgColors[] = {
        "\x1b[41m",
        "\x1b[42m",
        "\x1b[44m",
        "\x1b[46m",
        "\x1b[45m"
    };
    int numColors = 5;
    int currentColorIndex = 0;

    HidVibrationDeviceHandle rumbleHandlesStandard[2];
    HidVibrationDeviceHandle rumbleHandlesHandheld[2];

    HidVibrationValue rumbleValueOn;
    HidVibrationValue rumbleValueOff;

    hidInitializeVibrationDevices(rumbleHandlesStandard, 2, HidNpadIdType_No1, HidNpadStyleSet_NpadStandard);
    hidInitializeVibrationDevices(rumbleHandlesHandheld, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);

    rumbleValueOn.freq_low = 160.0f;
    rumbleValueOn.amp_low = 0.35f;
    rumbleValueOn.freq_high = 320.0f;
    rumbleValueOn.amp_high = 0.35f;

    memset(&rumbleValueOff, 0, sizeof(rumbleValueOff));

    printf("ButtonBreakSwitch 2 (v0.6.0-s2)\n");
    printf("Created by StrongEnough\n");
    printf("----------------------------\n");
    printf("High Score: %d\n", highScore);
    printf("----------------------------\n");
    printf("Press the + and - buttons together to exit.\n");

    while (appletMainLoop())
    {
        padUpdate(&pad);

        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        bool buttonPressed = false;

        if ((kHeld & (HidNpadButton_Plus | HidNpadButton_Minus)) == (HidNpadButton_Plus | HidNpadButton_Minus))
            break;

        if (kDown & HidNpadButton_A) { buttonPressed = true; printf("--------------------------------\nYou pressed the A Button!\n"); }
        else if (kDown & HidNpadButton_X) { buttonPressed = true; printf("--------------------------------\nYou pressed the X Button!\n"); }
        else if (kDown & HidNpadButton_Y) { buttonPressed = true; printf("--------------------------------\nYou pressed the Y Button!\n"); }
        else if (kDown & HidNpadButton_B) { buttonPressed = true; printf("--------------------------------\nYou pressed the B Button!\n"); }
        else if (kDown & HidNpadButton_R) { buttonPressed = true; printf("--------------------------------\nYou pressed the R Button!\n"); }
        else if (kDown & HidNpadButton_L) { buttonPressed = true; printf("--------------------------------\nYou pressed the L Button!\n"); }
        else if (kDown & HidNpadButton_ZR) { buttonPressed = true; printf("--------------------------------\nYou pressed the ZR Button!\n"); }
        else if (kDown & HidNpadButton_ZL) { buttonPressed = true; printf("--------------------------------\nYou pressed the ZL Button!\n"); }
        else if (kDown & HidNpadButton_Up) { buttonPressed = true; printf("--------------------------------\nYou pressed the D-Pad Up Button!\n"); }
        else if (kDown & HidNpadButton_Down) { buttonPressed = true; printf("--------------------------------\nYou pressed the D-Pad Down Button!\n"); }
        else if (kDown & HidNpadButton_Left) { buttonPressed = true; printf("--------------------------------\nYou pressed the D-Pad Left Button!\n"); }
        else if (kDown & HidNpadButton_Right) { buttonPressed = true; printf("---------------------------------\nYou pressed the D-Pad Right Button!\n"); }
        else if (kDown & HidNpadButton_StickL) { buttonPressed = true; printf("--------------------------------\nYou pressed the Left Stick Button!\n"); }
        else if (kDown & HidNpadButton_StickR) { buttonPressed = true; printf("--------------------------------\nYou pressed the Right Stick Button!\n"); }
        else if (kDown & HidNpadButton_Minus) { buttonPressed = true; printf("--------------------------------\nYou pressed the - Button!\n"); }
        else if (kDown & HidNpadButton_Plus) { buttonPressed = true; printf("--------------------------------\nYou pressed the + Button!\n"); }
        else if (kDown & HidNpadButton_LeftSL) { buttonPressed = true; printf("--------------------------------\nYou pressed the Left SL Button!\n"); }
        else if (kDown & HidNpadButton_LeftSR) { buttonPressed = true; printf("--------------------------------\nYou pressed the Left SR Button!\n"); }
        else if (kDown & HidNpadButton_RightSL) { buttonPressed = true; printf("--------------------------------\nYou pressed the Right SL Button!\n"); }
        else if (kDown & HidNpadButton_RightSR) { buttonPressed = true; printf("--------------------------------\nYou pressed the Right SR Button!\n"); }
        else if (kDown & HidNpadButton_HandheldLeftB) { buttonPressed = true; printf("--------------------------------\nYou pressed the NES/Famicom B Button!\n"); }

        if (buttonPressed) {
            totalPresses++;
            printf("You mashed %d Buttons!\n", totalPresses);

            if (totalPresses > highScore) {
                highScore = totalPresses;
                FILE* writeFile = fopen(SAVE_FILE_PATH, "w");
                if (writeFile != NULL) {
                    fprintf(writeFile, "%d", highScore);
                    fclose(writeFile);
                }
            }

            if (totalPresses % 100 == 0) {
                printf("%s\x1b[2J\x1b[H", bgColors[currentColorIndex]);
                currentColorIndex = (currentColorIndex + 1) % numColors;

                printf("Press the + and - buttons together to exit.\n");
                printf("--------------------------------\n");
                printf("ALL-TIME HIGH SCORE: %d\n", highScore);
                printf("--------------------------------\n");
                printf("You mashed %d Buttons!\n", totalPresses);
                consoleUpdate(NULL);

                u32 currentStyle = padGetStyleSet(&pad);

                HidVibrationDeviceHandle* activeHandles = (currentStyle & HidNpadStyleTag_NpadHandheld)
                    ? rumbleHandlesHandheld
                    : rumbleHandlesStandard;

                hidSendVibrationValues(activeHandles, &rumbleValueOn, 2);

                svcSleepThread(120000000ULL);

                hidSendVibrationValues(activeHandles, &rumbleValueOff, 2);
            }
        }

        consoleUpdate(NULL);
    }

    hidSendVibrationValues(rumbleHandlesStandard, &rumbleValueOff, 2);
    hidSendVibrationValues(rumbleHandlesHandheld, &rumbleValueOff, 2);

    printf("\x1b[0m\x1b[2J\x1b[H");
    consoleUpdate(NULL);

    consoleExit(NULL);
    return 0;
}
