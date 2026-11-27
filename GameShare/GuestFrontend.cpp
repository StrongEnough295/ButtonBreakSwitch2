// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#define PPSSPP_SWITCH2_GUEST_BUILD 1
#include "Switch/Switch2Main.cpp"
#include "Switch/GameShare/GuestApi.h"
#include "Switch/GameShare/FrameSink.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Core.h"
#include "Core/HLE/GameShareAdhoc.h"
#include "Core/MemMap.h"
#include "Common/System/OSD.h"
#include <exception>
#include <thread>

namespace {
std::thread guestThread;
std::atomic<int> guestState{0};
std::atomic<bool> guestActive{true};
PpssppGuestHost guestHost{};
std::mutex guestInputMutex;
PpssppGuestInput guestInput{};
uint64_t guestInputTick = 0;
std::mutex audioMutex;
RenderCallback audioCallback;
void *audioUser;

void GuestLog(const char *message) {
	if (guestHost.log) {
		guestHost.log(guestHost.user, message);
	}
}
class GuestAudioBackend : public AudioBackend {
public:
	~GuestAudioBackend() override {
		std::lock_guard<std::mutex> lock(audioMutex);
		audioCallback = nullptr;
		audioUser = nullptr;
	}
	void EnumerateDevices(std::vector<AudioDeviceDesc> *devices, bool capture) override {
		if (!capture) {
			devices->push_back({"GameShare application mix", "default"});
		}
	}
	void SetRenderCallback(RenderCallback callback, void *user) override {
		std::lock_guard<std::mutex> lock(audioMutex);
		audioCallback = callback;
		audioUser = user;
	}
	bool InitOutputDevice(std::string_view, LatencyMode, bool *reverted) override {
		if (reverted) {
			*reverted = false;
		}
		return true;
	}
	int SampleRate() const override { return 48000; }
	int BufferSize() const override { return 1536; }
	int PeriodFrames() const override { return 512; }
};
void GuestPollInput() {
	static uint64_t previous = 0;
	PpssppGuestInput input;
	uint64_t sampleTick;
	{
		std::lock_guard<std::mutex> lock(guestInputMutex);
		input = guestInput;
		sampleTick = guestInputTick;
	}
	if (!input.connected || !sampleTick || armGetSystemTick() - sampleTick > armNsToTicks(500000000)) {
		input = {};
	}
	const uint64_t down = input.buttons & ~previous;
	const uint64_t up = previous & ~input.buttons;
	previous = input.buttons;
	struct Button { uint64_t native; InputKeyCode key; };
	static constexpr Button buttons[] = {
		{HidNpadButton_B, NKCODE_BUTTON_A}, {HidNpadButton_A, NKCODE_BUTTON_B},
		{HidNpadButton_Y, NKCODE_BUTTON_X}, {HidNpadButton_X, NKCODE_BUTTON_Y},
		{HidNpadButton_Up, NKCODE_DPAD_UP}, {HidNpadButton_Down, NKCODE_DPAD_DOWN},
		{HidNpadButton_Left, NKCODE_DPAD_LEFT}, {HidNpadButton_Right, NKCODE_DPAD_RIGHT},
		{HidNpadButton_L, NKCODE_BUTTON_L1}, {HidNpadButton_R, NKCODE_BUTTON_R1},
		{HidNpadButton_Plus, NKCODE_BUTTON_START}, {HidNpadButton_Minus, NKCODE_BUTTON_SELECT},
		{HidNpadButton_StickR, NKCODE_HOME},
	};
	for (const auto &button : buttons) {
		if (down & button.native) {
			NativeKey(KeyInput(DEVICE_ID_XINPUT_0, button.key, KeyInputFlags::DOWN));
		}
		if (up & button.native) {
			NativeKey(KeyInput(DEVICE_ID_XINPUT_0, button.key, KeyInputFlags::UP));
		}
	}
	AxisInput axes[] = {
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_X, std::clamp(input.leftX / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_Y, std::clamp(-input.leftY / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_Z, std::clamp(input.rightX / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_RZ, std::clamp(-input.rightY / 32767.0f, -1.0f, 1.0f)},
		{DEVICE_ID_XINPUT_0, JOYSTICK_AXIS_LTRIGGER, (input.buttons & HidNpadButton_ZL) ? 1.0f : 0.0f},
	};
	NativeAxis(axes, std::size(axes));
}
void GuestMain() {
	SetCurrentThreadName("GameShare PSP");
	try {
		mkdir(DATA_PATH, 0777);
		NativeTimeService timeService;
		cpu_info.num_cores = 3;
		cpu_info.logical_cpu_count = 1;
		g_VFS.Register("", new DirectoryReader(Path("romfs:/")));
		CommandLineOptions options;
		options.gpuBackend = GPUBackend::VULKAN;
		options.log = std::string(DATA_PATH) + "ppsspp.log";
		const char *arguments[] = {"ppsspp-gameshare-guest"};
		NativeInit(1, arguments, options, DATA_PATH, "sdmc:/", DATA_PATH);
		g_Config.bShowTouchControls = false;
		g_Config.bFastMemory = false;
		g_Config.iCpuCore = static_cast<int>(CPUCore::JIT);
		g_Config.iInternalResolution = 1;
		g_Config.iGPUBackend = static_cast<int>(GPUBackend::VULKAN);
		g_Config.iInflightFrames = 1;
		g_Config.bRenderMultiThreading = false;
		if (g_Config.currentDirectory == Path("/") || g_Config.currentDirectory == Path("sdmc:/")) {
			g_Config.currentDirectory = Path("sdmc:/switch/ppsspp-switch2/games");
		}
		displayWidth = 1280;
		displayHeight = 720;
		displayRefreshRate = 60.0f;
		VulkanGraphicsContext graphics;
		graphics.SetOffscreen(displayWidth, displayHeight);
		std::string error;
		if (!graphics.InitAPI(nullptr, &g_Config.sVulkanDevice, &error)) {
			GuestLog(error.c_str());
			NativeShutdown();
			guestState = 3;
			return;
		}
		if (!graphics.InitSurface(WINDOWSYSTEM_VI_NN, nullptr, nullptr, &error)) {
			GuestLog(error.c_str());
			graphics.ShutdownAPI();
			NativeShutdown();
			guestState = 3;
			return;
		}
		Native_UpdateScreenScale(displayWidth, displayHeight, 1.0f);
		NativeResized();
		Switch2SetFrameSink(guestHost.frame, guestHost.user, displayWidth, displayHeight);
		if (!NativeInitGraphics(&graphics)) {
			GuestLog("Guest graphics initialization failed");
			guestState = 3;
		} else {
			guestState = 2;
			GuestLog("Isolated guest UI ready (1280x720, ARM64 JIT)");
			g_OSD.Show(OSDType::MESSAGE_INFO, "GameShare guest: choose a game", 8.0f);
			while (!quitRequested) {
				GuestPollInput();
				if (!guestActive) {
					svcSleepThread(20000000);
					continue;
				}
				NativeFrame(&graphics);
			}
			guestState = 4;
		}
		Core_StateProcessed();
		NativeShutdownGraphics(&graphics);
		Switch2SetFrameSink(nullptr, nullptr, 0, 0);
		graphics.ShutdownSurface();
		graphics.ShutdownAPI();
		NativeShutdown();
		if (guestState != 3) {
			guestState = 0;
		}
	} catch (const std::exception &error) {
		GuestLog(error.what());
		guestState = 5;
	}
}
}
AudioBackend *System_CreateAudioBackend() { return new GuestAudioBackend(); }
bool Switch2GameShareAvailable() { return false; }
void Switch2GameShareToggle() {}
void Switch2GameSharePoll() {}
bool Switch2GameSharePrepareExit() { return true; }

extern "C" bool ppsspp_guest_start(const PpssppGuestHost *host) {
	if (!host || host->version != 1 || guestThread.joinable()) {
		return false;
	}
	guestHost = *host;
	quitRequested = false;
	{
		std::lock_guard<std::mutex> lock(guestInputMutex);
		guestInput = {};
	}
	guestState = 1;
	try {
		guestThread = std::thread(GuestMain);
	} catch (const std::exception &error) {
		GuestLog(error.what());
		guestState = 3;
		return false;
	}
	return true;
}
extern "C" void ppsspp_guest_request_stop() { quitRequested = true; }
extern "C" void ppsspp_guest_set_active(bool active, bool localAdhoc) {
	SetGameShareAdhocEnabled(localAdhoc);
	guestActive = active;
}
extern "C" void ppsspp_guest_join() {
	if (guestThread.joinable()) {
		guestThread.join();
	}
}
extern "C" int ppsspp_guest_state() { return guestState.load(); }
extern "C" void ppsspp_guest_input(const PpssppGuestInput *input) {
	std::lock_guard<std::mutex> lock(guestInputMutex);
	guestInput = input ? *input : PpssppGuestInput{};
	guestInputTick = input ? armGetSystemTick() : 0;
}
extern "C" void ppsspp_guest_mix(float *stereo, int frames, int rate) {
	if (guestState != 2) {
		return;
	}
	std::lock_guard<std::mutex> lock(audioMutex);
	if (audioCallback) {
		audioCallback(stereo, frames, rate, audioUser);
	}
}
extern "C" PpssppGuestIdentity ppsspp_guest_identity() {
	return {reinterpret_cast<uintptr_t>(&g_Config), reinterpret_cast<uintptr_t>(&Memory::base), reinterpret_cast<uintptr_t>(&coreState)};
}
