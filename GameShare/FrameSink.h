// Copyright (c) 2012- PPSSPP Project.
// Licensed under GPLv2 or later. See LICENSE.TXT.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace Draw { class DrawContext; }
using Switch2FrameCallback = void (*)(void *, const uint8_t *, uint32_t, uint32_t, size_t);
void Switch2SetFrameSink(Switch2FrameCallback callback, void *user, uint32_t width, uint32_t height);
void Switch2CaptureFrame(Draw::DrawContext *draw);
