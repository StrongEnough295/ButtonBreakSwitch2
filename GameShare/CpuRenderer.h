// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once
#include "Common/CommonTypes.h"
#include <switch2/services/vi2.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// A native guest-layer image owner, independent of the default-window ABI.
class Switch2GuestCpuRenderer {
public:
	Switch2GuestCpuRenderer();
	~Switch2GuestCpuRenderer();
	uint32_t Open(Vi2Layer *layer);
	uint32_t Present(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride);
	uint32_t Retire();
private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
