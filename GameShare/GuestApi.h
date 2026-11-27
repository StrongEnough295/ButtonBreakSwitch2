// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Only plain data and callbacks cross the two isolated PPSSPP copies.
struct PpssppGuestInput {
	uint64_t buttons;
	int32_t leftX, leftY, rightX, rightY;
	bool connected;
};
struct PpssppGuestHost {
	uint32_t version;
	void *user;
	void (*frame)(void *, const uint8_t *, uint32_t, uint32_t, size_t);
	void (*log)(void *, const char *);
};
struct PpssppGuestIdentity {
	uintptr_t config;
	uintptr_t memoryBaseVariable;
	uintptr_t coreState;
};
// State: 0 stopped, 1 starting, 2 running, 3 failed, 4 stopping.
extern "C" {
bool ppsspp_guest_start(const PpssppGuestHost *host);
void ppsspp_guest_request_stop();
void ppsspp_guest_set_active(bool active, bool localAdhoc);
void ppsspp_guest_join();
int ppsspp_guest_state();
void ppsspp_guest_input(const PpssppGuestInput *input);
void ppsspp_guest_mix(float *stereo, int frames, int rate);
PpssppGuestIdentity ppsspp_guest_identity();
}
