// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
// Native CPU-image layout and setup adapted from libounce cpu_window.c (ISC).
// Copyright (c) 2026 libounce contributors.
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#include "Switch/GameShare/CpuRenderer.h"
#include <switch2/kernel/svc.h>
#include <switch2/services/gsv.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>

extern "C" Result __real_gsvGpuCreateSlotEvent(GsvGpu *, uint32_t, _Event *, bool);
extern "C" Result __real_gsvGpuOpenWorker(GsvGpu *, uint32_t, _Event *);
extern "C" void __real_gsvGpuClose(GsvGpu *);

namespace {
std::mutex driverGpuMutex;
GsvGpu *driverGpu = nullptr;
uint64_t driverSlots = 0;
uint64_t guestSlot = 0;
_Event guestSlotEvent{};
bool driverGpuReady = false;
bool guestSlotInUse = false;

constexpr uint32_t TRACKED_SLOTS = 64;
constexpr uint64_t SlotBit(uint32_t slot) {
	return slot < TRACKED_SLOTS ? uint64_t{1} << slot : 0;
}

Result ReserveGuestSlot() {
	uint32_t first = 0, last = 0;
	Result rc = gsvGpuGetSlotRange(driverGpu, &first, &last);
	if (R_FAILED(rc)) {
		return rc;
	}
	last = std::min(last, TRACKED_SLOTS - 1);
	for (uint32_t candidate = first; candidate <= last; ++candidate) {
		if (driverSlots & SlotBit(candidate)) {
			continue;
		}
		rc = __real_gsvGpuCreateSlotEvent(driverGpu, candidate, &guestSlotEvent, false);
		if (R_SUCCEEDED(rc)) {
			guestSlot = SlotBit(candidate);
		}
		return rc;
	}
	return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

Result BorrowDriverGpu(GsvGpu *gpu, uint32_t *slot, _Event **event) {
	std::lock_guard<std::mutex> lock(driverGpuMutex);
	if (!driverGpuReady || guestSlotInUse) {
		return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
	}
	if (!guestSlot) {
		const Result rc = ReserveGuestSlot();
		if (R_FAILED(rc)) {
			return rc;
		}
	} else {
		std::fprintf(stderr, "GameShare reusing GPU slot %d completion event\n", __builtin_ctzll(guestSlot));
	}
	*gpu = *driverGpu;
	*slot = __builtin_ctzll(guestSlot);
	*event = &guestSlotEvent;
	guestSlotInUse = true;
	return 0;
}

void ReleaseGuestSlot() {
	std::lock_guard<std::mutex> lock(driverGpuMutex);
	// Closing the copy of the event handle doesn't unbind the native slot.
	// Reserve both until the driver closes its GPU, and lend them again on reconnect.
	guestSlotInUse = false;
}

constexpr uint32_t WIDTH = 1280, HEIGHT = 720, PITCH = WIDTH * 4;
constexpr size_t IMAGE_SIZE = PITCH * 768;
constexpr uint64_t WAIT_NS = 2000000000ULL;
Result BadInput() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
bool Empty(const Vi2Dependency &dependency) {
	uint32_t token;
	uint64_t value;
	memcpy(&token, dependency.data, 4);
	memcpy(&value, dependency.data + 8, 8);
	return !token && !dependency.data[4] && !value;
}
}

extern "C" {
Result __wrap_gsvGpuCreateSlotEvent(GsvGpu *gpu, uint32_t slot, _Event *event, bool autoclear) {
	std::lock_guard<std::mutex> lock(driverGpuMutex);
	if (driverGpu == gpu && (guestSlot & SlotBit(slot))) {
		return BadInput();
	}
	const Result rc = __real_gsvGpuCreateSlotEvent(gpu, slot, event, autoclear);
	if (R_SUCCEEDED(rc) && (!driverGpu || driverGpu == gpu)) {
		driverGpu = gpu;
		driverSlots |= SlotBit(slot);
	}
	return rc;
}

Result __wrap_gsvGpuOpenWorker(GsvGpu *gpu, uint32_t slot, _Event *event) {
	std::lock_guard<std::mutex> lock(driverGpuMutex);
	if (driverGpu == gpu && (guestSlot & SlotBit(slot))) {
		return BadInput();
	}
	const Result rc = __real_gsvGpuOpenWorker(gpu, slot, event);
	if (R_SUCCEEDED(rc)) {
		if (!driverGpu || driverGpu == gpu) {
			driverGpu = gpu;
			driverSlots |= SlotBit(slot);
			driverGpuReady = true;
		}
	}
	return rc;
}

void __wrap_gsvGpuClose(GsvGpu *gpu) {
	std::lock_guard<std::mutex> lock(driverGpuMutex);
	if (driverGpu == gpu) {
		if (guestSlot) {
			eventClose(&guestSlotEvent);
			guestSlot = 0;
		}
		guestSlotInUse = false;
		driverGpu = nullptr;
		driverGpuReady = false;
		driverSlots = 0;
	}
	__real_gsvGpuClose(gpu);
}
}

struct Switch2GuestCpuRenderer::Impl {
	Vi2Layer *layer = nullptr;
	GsvSync sync{};
	GsvGpu gpu{};
	_Event *event = nullptr;
	bool gsv = false, syncOpen = false;
	bool clientOpen = false;
	bool attribute = false, range = false, rangePending = false;
	bool presented = false, unregistered = false, ready = false;
	uint8_t client = 0;
	uint32_t slot = 0, rangeId = 0, next = 0;
	uint8_t *backing = nullptr;
	Result quarantine = 0;
	struct Image {
		uint32_t memory = 0, image = 0;
		bool memoryOwned = false, imageOwned = false, alias = false;
		Vi2Dependency release{};
	} images[2];
	Result Fail(const char *stage, Result rc) {
		std::fprintf(stderr, "GameShare image %s: %08x\n", stage, rc);
		return rc;
	}
	Result WaitRelease(const Vi2Dependency &release) {
		if (Empty(release)) {
			return 0;
		}
		if (release.data[4] != 1) {
			return BadInput();
		}
		uint32_t token;
		uint64_t threshold;
		memcpy(&token, release.data, 4);
		memcpy(&threshold, release.data + 8, 8);
		const uint64_t start = armGetSystemTick();
		for (;;) {
			uint64_t value = 0;
			uint8_t mode = 0;
			Result rc = gsvSyncReadCounter(&sync, ~token, &value, &mode);
			if (R_FAILED(rc)) {
				return rc;
			}
			if (mode != 1) {
				return BadInput();
			}
			if (value >= threshold) {
				return 0;
			}
			if (armGetSystemTick() - start >= armNsToTicks(WAIT_NS)) {
				return KERNELRESULT(TimedOut);
			}
			svcSleepThread(1000000);
		}
	}
	Result Acquire(uint8_t *slot) {
		const uint64_t start = armGetSystemTick();
		for (;;) {
			uint64_t available = 0;
			Result rc = vi2GetLayerAvailability(layer, &available);
			if (R_FAILED(rc)) {
				return rc;
			}
			if (available) {
				*slot = __builtin_ctzll(available);
				return 0;
			}
			if (armGetSystemTick() - start >= armNsToTicks(WAIT_NS)) {
				return KERNELRESULT(TimedOut);
			}
			rc = vi2WaitLayerAvailability(layer, 100000000);
			if (R_FAILED(rc) && R_VALUE(rc) != KERNELRESULT(TimedOut)) {
				return rc;
			}
		}
	}
};
Switch2GuestCpuRenderer::Switch2GuestCpuRenderer() : impl_(std::make_unique<Impl>()) {}
Switch2GuestCpuRenderer::~Switch2GuestCpuRenderer() {
	if (Retire()) {
		// A failed release cannot authorize freeing a consumer's backing/context.
		(void)impl_.release();
	}
}
uint32_t Switch2GuestCpuRenderer::Open(Vi2Layer *layer) {
	auto &s = *impl_;
	if (!layer || s.layer || s.quarantine) {
		return BadInput();
	}
	s.layer = layer;
#define OPEN_STEP(stage, call, owned) do { Result rc = (call); if (R_FAILED(rc)) { return s.Fail(stage, rc); } owned = true; } while (false)
	OPEN_STEP("root", gsvInitialize(), s.gsv);
	OPEN_STEP("sync", gsvOpenSync(&s.sync), s.syncOpen);
	// OunceVK owns the registered VM, main session and worker. The host
	// retires the guest renderer before either Vulkan context shuts down.
	Result rc = BorrowDriverGpu(&s.gpu, &s.slot, &s.event);
	if (R_FAILED(rc)) { return s.Fail("borrow GPU and worker event", rc); }
	OPEN_STEP("client", gsvGpuRegisterClient(&s.gpu, 0x0050535050534747ULL, &s.client), s.clientOpen);
	GsvImageLayout layout;
	gsvMakeImageLayout(&layout, WIDTH, HEIGHT, 7, PITCH / 64, 1, 1, 0);
	uint64_t size = 0;
	rc = gsvGetImageSize(&layout, &size);
	if (R_FAILED(rc) || size != IMAGE_SIZE) { return s.Fail("image geometry", R_FAILED(rc) ? rc : BadInput()); }
	s.backing = static_cast<uint8_t *>(aligned_alloc(0x10000, IMAGE_SIZE * 2));
	if (!s.backing) { return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); }
	memset(s.backing, 0, IMAGE_SIZE * 2);
	OPEN_STEP("memory attribute", svcUnknown80(s.backing, IMAGE_SIZE * 2, 0x20, 0x20), s.attribute);
	// An uncertain worker registration cannot authorize an untracked free.
	s.range = true;
	rc = gsvGpuRegisterMemory(&s.gpu, reinterpret_cast<uintptr_t>(s.backing), IMAGE_SIZE * 2, s.client, &s.rangeId);
	if (R_FAILED(rc) || !s.rangeId) {
		s.quarantine = R_FAILED(rc) ? rc : BadInput();
		return s.Fail("backing registration", s.quarantine);
	}
	for (uint32_t i = 0; i < 2; ++i) {
		auto &image = s.images[i];
		OPEN_STEP("reserve image memory", gsvReserveImageMemory(3, IMAGE_SIZE, &image.memory), image.memoryOwned);
		rc = gsvRegisterImageMemory(image.memory, reinterpret_cast<uintptr_t>(s.backing) + i * IMAGE_SIZE, IMAGE_SIZE, s.rangeId, i * IMAGE_SIZE);
		if (R_FAILED(rc)) { return s.Fail("image memory", rc); }
		OPEN_STEP("create image", gsvCreateImage(&layout, image.memory, &image.image), image.imageOwned);
		OPEN_STEP("display alias", gsvCreateDisplayAlias(image.image, 1), image.alias);
	}
#undef OPEN_STEP
	s.ready = true;
	std::fprintf(stderr, "GameShare guest image ready: 1280x720, separate VI2 layer, worker slot %u\n", s.slot);
	return 0;
}
uint32_t Switch2GuestCpuRenderer::Present(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride) {
	auto &s = *impl_;
	if (!s.ready || !pixels || width != WIDTH || height != HEIGHT || stride < PITCH) { return BadInput(); }
	if (s.quarantine) { return s.quarantine; }
	auto &image = s.images[s.next];
	Result rc = s.WaitRelease(image.release);
	if (R_FAILED(rc)) { return s.Fail("image release", rc); }
	uint8_t slot = 0;
	rc = s.Acquire(&slot);
	if (R_FAILED(rc)) { return s.Fail("layer availability", rc); }
	auto *out = s.backing + s.next * IMAGE_SIZE;
	// Copy Turing sectors (four RGBA pixels), rather than recomputing a GOB for every pixel.
	for (uint32_t y = 0; y < HEIGHT; ++y) {
		for (uint32_t x = 0; x < WIDTH; x += 4) {
			const uint32_t xb = x * 4, gx = xb % 64, gy = y % 8;
			const uint32_t block = (y / 128) * (PITCH / 64) + xb / 64;
			const uint32_t offset = block * 8192 + ((y % 128) / 8) * 512 +
				(gx / 32) * 256 + (gy / 4) * 128 + ((gx % 32) / 16) * 64 + (gy % 4) * 16;
			memcpy(out + offset, pixels + static_cast<size_t>(y) * stride + xb, 16);
		}
	}
	armDCacheFlush(out, IMAGE_SIZE);
	Vi2PresentOptions options;
	vi2MakeBasicPresentOptions(&options, WIDTH, HEIGHT, armGetSystemTick());
	options.interval_numerator = 1;
	options.interval_denominator = 60;
	Vi2Dependency producer{};
	uint64_t available = 0;
	const bool first = !s.presented;
	s.presented = true;
	rc = vi2PresentLayer(s.layer, slot, image.image, &options, &producer, &available, &image.release);
	if (R_FAILED(rc) || image.release.data[4] != 1) {
		s.quarantine = R_FAILED(rc) ? rc : BadInput();
		return s.Fail("present", s.quarantine);
	}
	if (first) { std::fprintf(stderr, "GameShare first separate guest frame presented\n"); }
	s.next = (s.next + 1) % 2;
	return 0;
}
uint32_t Switch2GuestCpuRenderer::Retire() {
	auto &s = *impl_;
	s.ready = false;
	if (s.quarantine) { return s.quarantine; }
	if (s.presented && !s.unregistered) {
		uint8_t slot = 0;
		Result rc = s.Acquire(&slot);
		if (R_FAILED(rc)) { return s.quarantine = rc; }
		Vi2PresentOptions options;
		vi2MakeBasicPresentOptions(&options, WIDTH, HEIGHT, armGetSystemTick());
		options.flags &= ~uint64_t(1);
		uint64_t available = 0;
		Vi2Dependency release{};
		rc = vi2UnregisterLayerImage(s.layer, slot, &options, &available, &release);
		if (R_FAILED(rc) || !Empty(release)) { return s.quarantine = R_FAILED(rc) ? rc : BadInput(); }
		s.unregistered = true;
	}
	for (auto &image : s.images) {
		Result rc = s.WaitRelease(image.release);
		if (R_FAILED(rc)) { return s.quarantine = rc; }
		image.release = {};
		if (image.alias) {
			rc = gsvDestroyDisplayAlias(image.image);
			if (R_FAILED(rc)) { return s.quarantine = rc; }
			image.alias = false;
		}
		if (image.imageOwned) {
			rc = gsvDestroyImage(image.image);
			if (R_FAILED(rc)) { return s.quarantine = rc; }
			image.imageOwned = false;
		}
		if (image.memoryOwned) {
			rc = gsvReleaseImageMemory(image.memory);
			if (R_FAILED(rc)) { return s.quarantine = rc; }
			image.memoryOwned = false;
		}
	}
	if (s.range) {
		if (!s.rangePending) {
			Result rc = eventClear(s.event);
			if (R_SUCCEEDED(rc)) { rc = gsvGpuUnregisterMemory(&s.gpu, s.rangeId, s.slot); }
			if (R_FAILED(rc)) { return s.quarantine = rc; }
			s.rangePending = true;
		}
		Result rc = eventWait(s.event, WAIT_NS);
		if (R_FAILED(rc)) { return s.quarantine = rc; }
		s.rangePending = false;
		s.range = false;
	}
	if (s.attribute) {
		Result rc = svcUnknown80(s.backing, IMAGE_SIZE * 2, 0x20, 0);
		if (R_FAILED(rc)) { return s.quarantine = rc; }
		s.attribute = false;
	}
	free(s.backing);
	s.backing = nullptr;
	if (s.clientOpen) {
		Result rc = gsvGpuReleaseClient(&s.gpu, s.client);
		if (R_FAILED(rc)) { return s.quarantine = rc; }
		s.clientOpen = false;
	}
	if (s.event) {
		ReleaseGuestSlot();
		s.event = nullptr;
	}
	if (s.syncOpen) { gsvSyncClose(&s.sync); s.syncOpen = false; }
	if (s.gsv) { gsvExit(); s.gsv = false; }
	return 0;
}
