// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/GameShare/Integration.h"
#include "Switch/GameShare/GuestApi.h"
#include "Switch/GameShare/AdhocServer.h"
#include "Switch/GameShare/CpuRenderer.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/HLE/GameShareAdhoc.h"
#include "Core/MemMap.h"
#include <gameshare/host.hpp>
#include <gameshare/guest_display.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {
struct Frame {
	uint32_t width, height;
	size_t stride;
	uint64_t serial;
	std::vector<uint8_t> pixels;
};
std::atomic<bool> toggleRequested{false};
struct Session {
	GameShareAdhocServer adhoc;
	bool localAdhoc = false;
	std::shared_ptr<Switch2GuestCpuRenderer> renderer;
	std::shared_ptr<gameshare::NativeGuestDisplay> display;
	std::unique_ptr<gameshare::Host> host;
	std::thread presenter;
	std::atomic<bool> presenterStop{true}, presenterDone{true};
	std::mutex frameMutex;
	std::condition_variable frameCondition;
	std::shared_ptr<const Frame> frame;
	uint64_t serial = 0;
	bool guestStarted = false, exiting = false, quarantined = false, quarantineWarning = false;
	gameshare::Host::State previous = gameshare::Host::State::Idle;

	void Message(const char *text, bool error = false) {
		std::fprintf(stderr, "GameShare: %s\n", text);
		g_OSD.Show(error ? OSDType::MESSAGE_ERROR : OSDType::MESSAGE_INFO, text, 6.0f, "gameshare");
	}
	static void Log(void *, gameshare::LogLevel, const char *text) {
		std::fprintf(stderr, "GameShare SDK: %s\n", text);
	}
	static void GuestLog(void *, const char *text) {
		std::fprintf(stderr, "GameShare guest: %s\n", text);
	}
	static void GuestAudio(void *, int16_t *pcm, uint32_t frames) {
		std::array<float, gameshare::GuestAudioMaxFrames * 2> samples{};
		ppsspp_guest_mix(samples.data(), frames, 48000);
		for (uint32_t i = 0; i < frames * 2; ++i) {
			const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
			pcm[i] = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
		}
	}
	static void FrameReady(void *user, const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride) {
		auto &s = *static_cast<Session *>(user);
		if (width != 1280 || height != 720 || stride != 1280 * 4) {
			return;
		}
		auto next = std::make_shared<Frame>();
		next->width = width;
		next->height = height;
		next->stride = stride;
		next->pixels.assign(pixels, pixels + height * stride);
		{
			std::lock_guard<std::mutex> lock(s.frameMutex);
			next->serial = ++s.serial;
			s.frame = std::move(next);
		}
		s.frameCondition.notify_one();
	}
	void Start() {
		if (quarantined || exiting) {
			Message("Close the hosting title from HOME before restarting GameShare.", true);
			return;
		}
		if (presenter.joinable()) {
			if (!presenterDone) {
				Message("GameShare is still stopping.");
				return;
			}
			presenter.join();
		}
		if (host) {
			host->stopAndWait();
			if (!host->canUnload()) {
				quarantined = true;
				Message("GameShare cleanup requires closing the title from HOME.", true);
				return;
			}
			host.reset();
			display.reset();
			renderer.reset();
		}
		if (!guestStarted) {
			const auto guest = ppsspp_guest_identity();
			if (guest.config == reinterpret_cast<uintptr_t>(&g_Config) ||
				guest.memoryBaseVariable == reinterpret_cast<uintptr_t>(&Memory::base) ||
				guest.coreState == reinterpret_cast<uintptr_t>(&coreState)) {
				Message("Guest emulator isolation check failed.", true);
				return;
			}
			std::fprintf(stderr, "GameShare isolation: host config=%p guest=%p; memory globals=%p/%p; core state=%p/%p\n",
				static_cast<void *>(&g_Config), reinterpret_cast<void *>(guest.config),
				static_cast<void *>(&Memory::base), reinterpret_cast<void *>(guest.memoryBaseVariable),
				static_cast<void *>(const_cast<CoreState *>(&coreState)), reinterpret_cast<void *>(guest.coreState));
			const PpssppGuestHost callbacks{1, this, FrameReady, GuestLog};
			if (!ppsspp_guest_start(&callbacks)) {
				Message("Could not create the second PPSSPP instance.", true);
				return;
			}
			guestStarted = true;
		} else if (ppsspp_guest_state() != 1 && ppsspp_guest_state() != 2) {
			Message("The guest exited. Restart PPSSPP before creating another guest.", true);
			return;
		}
		ppsspp_guest_set_active(true, false);
		renderer = std::make_shared<Switch2GuestCpuRenderer>();
		gameshare::RendererHooks hooks;
		hooks.user = renderer.get();
		hooks.owner = renderer;
		hooks.open = [](void *user, Vi2Layer *layer) { return static_cast<Switch2GuestCpuRenderer *>(user)->Open(layer); };
		hooks.close = [](void *user) { return static_cast<Switch2GuestCpuRenderer *>(user)->Retire(); };
		display = std::make_shared<gameshare::NativeGuestDisplay>(hooks);
		gameshare::HostConfig config;
		config.videoMode = gameshare::VideoMode::SeparateDisplay;
		config.videoTarget = display;
		config.mirrorAudio = false;
		config.guestAudio = GuestAudio;
		config.log = Log;
		host = std::make_unique<gameshare::Host>(config);
		presenterStop = false;
		presenterDone = true;
		previous = gameshare::Host::State::Idle;
		host->start();
		Message("Starting GameShare. The guest has its own PPSSPP menu.");
	}
	void Stop() {
		presenterStop = true;
		frameCondition.notify_all();
		ppsspp_guest_input(nullptr);
		ppsspp_guest_set_active(false, false);
		StopAdhoc();
		if (host) {
			host->requestStop();
		}
	}
	void StopAdhoc() {
		if (localAdhoc) {
			SetGameShareAdhocEnabled(false);
			localAdhoc = false;
			adhoc.Stop();
		}
	}
	void StartPresenter() {
		if (presenter.joinable()) {
			return;
		}
		presenterDone = false;
		presenter = std::thread([this] {
			SetCurrentThreadName("GameShare image");
			uint64_t seen = 0;
			while (!presenterStop) {
				std::shared_ptr<const Frame> image;
				{
					std::unique_lock<std::mutex> lock(frameMutex);
					frameCondition.wait_for(lock, std::chrono::milliseconds(50), [&] { return presenterStop || (frame && frame->serial != seen); });
					if (presenterStop) { break; }
					if (!frame || frame->serial == seen) { continue; }
					image = frame;
					seen = image->serial;
				}
				struct Draw { Switch2GuestCpuRenderer *renderer; const Frame *frame; } draw{renderer.get(), image.get()};
				const uint32_t rc = display->withLayer([](void *user, Vi2Layer *) {
					auto &draw = *static_cast<Draw *>(user);
					return draw.renderer->Present(draw.frame->pixels.data(), draw.frame->width, draw.frame->height, draw.frame->stride);
				}, &draw);
				if (rc) {
					std::fprintf(stderr, "GameShare image submission stopped: %08x\n", rc);
					host->requestStop();
					break;
				}
			}
			presenterDone = true;
		});
	}
	void Poll() {
		if (toggleRequested.exchange(false) && !exiting) {
			if (host && (host->state() == gameshare::Host::State::Starting || host->state() == gameshare::Host::State::Hosting)) {
				Stop();
				Message("Stopping GameShare.");
			} else {
				Start();
			}
		}
		if (!host) { return; }
		const auto state = host->state();
		const std::string error = host->takeError();
		if (!error.empty()) { Message(error.c_str(), true); }
		if (state != previous) {
			std::fprintf(stderr, "GameShare state: %d -> %d\n", static_cast<int>(previous), static_cast<int>(state));
			previous = state;
			if (state == gameshare::Host::State::Hosting) {
				StartPresenter();
				localAdhoc = adhoc.Start();
				if (localAdhoc) {
					const bool restart = GameShareAdhocNeedsRestart();
					SetGameShareAdhocEnabled(true);
					Message(restart ? "Restart the host game to use GameShare Ad Hoc." : "GameShare connected. Local Ad Hoc is available.");
				} else {
					Message("GameShare connected, but local Ad Hoc could not start.", true);
				}
			}
		}
		if (localAdhoc && !adhoc.Running()) {
			StopAdhoc();
			Message("GameShare Ad Hoc stopped.", true);
		}
		const int guest = ppsspp_guest_state();
		if (guestStarted && guest != 1 && guest != 2) { Stop(); }
		if (state == gameshare::Host::State::Hosting && !exiting && (guest == 1 || guest == 2)) {
			if (guest == 2) {
				const auto input = host->controller();
				const PpssppGuestInput converted{input.buttons, input.leftX, input.leftY, input.rightX, input.rightY, input.connected};
				ppsspp_guest_input(&converted);
			}
			ppsspp_guest_set_active(true, localAdhoc);
		} else {
			ppsspp_guest_input(nullptr);
			if (state != gameshare::Host::State::Starting) {
				StopAdhoc();
				ppsspp_guest_set_active(false, false);
				presenterStop = true;
				frameCondition.notify_all();
			}
		}
		if (presenter.joinable() && presenterDone) { presenter.join(); }
		if (state == gameshare::Host::State::Idle || state == gameshare::Host::State::Failed) {
			// Idle/Failed is published after SDK cleanup, but its thread still needs joining.
			host->stopAndWait();
			if (!host->canUnload()) {
				quarantined = true;
			}
		}
	}
	bool PrepareExit() {
		if (!exiting) {
			exiting = true;
			Stop();
			ppsspp_guest_request_stop();
		}
		if (quarantined || ppsspp_guest_state() == 5) {
			if (!quarantineWarning) {
				Message("Resources retained. Close the entire hosting title from HOME.", true);
				quarantineWarning = true;
			}
			return false;
		}
		if (host) {
			const auto state = host->state();
			if (state != gameshare::Host::State::Idle && state != gameshare::Host::State::Failed) { return false; }
			host->stopAndWait();
			if (!host->canUnload()) { quarantined = true; return false; }
		}
		const int guest = ppsspp_guest_state();
		if (guest == 1 || guest == 2 || guest == 4 || !presenterDone) { return false; }
		ppsspp_guest_join();
		if (presenter.joinable()) { presenter.join(); }
		return true;
	}
};
// Kept alive until process/NRO exit, including an SDK quarantine.
Session session;
}
bool Switch2GameShareAvailable() { return true; }
void Switch2GameShareToggle() { toggleRequested = true; }
void Switch2GameSharePoll() { session.Poll(); }
bool Switch2GameSharePrepareExit() { return session.PrepareExit(); }
