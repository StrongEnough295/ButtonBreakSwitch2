// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#include "Switch/GameShare/FrameSink.h"
#include "Common/GPU/thin3d.h"
#include <vector>
#include <cstdio>

namespace {
Switch2FrameCallback sink;
void *sinkUser;
uint32_t width, height;
std::vector<uint8_t> pixels;
bool captureLogged = false, failureLogged = false;
}
void Switch2SetFrameSink(Switch2FrameCallback callback, void *user, uint32_t w, uint32_t h) {
	sink = callback;
	sinkUser = user;
	width = w;
	height = h;
	pixels.clear();
	captureLogged = failureLogged = false;
}
void Switch2CaptureFrame(Draw::DrawContext *draw) {
	if (!sink || !width || !height) {
		return;
	}
	pixels.resize(static_cast<size_t>(width) * height * 4);
	if (draw->CopyFramebufferToMemory(nullptr, Draw::Aspect::COLOR_BIT, 0, 0, width, height,
		Draw::DataFormat::R8G8B8A8_UNORM, pixels.data(), width, Draw::ReadbackMode::BLOCK, "GameShare guest")) {
		if (!captureLogged) {
			std::fprintf(stderr, "GameShare guest frame readback ready\n");
			captureLogged = true;
		}
		sink(sinkUser, pixels.data(), width, height, static_cast<size_t>(width) * 4);
	} else if (!failureLogged) {
		std::fprintf(stderr, "GameShare guest backbuffer readback failed\n");
		failureLogged = true;
	}
}
