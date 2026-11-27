/* SPDX-License-Identifier: ISC */
#pragma once
#include <switch.h>
#include <switch2/services/vi2.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { GS_CPU_IMAGE_WIDTH=1280, GS_CPU_IMAGE_HEIGHT=720 };
Result gsCpuImageStreamerOpen(Vi2Layer* layer);
Result gsCpuImageStreamerPresentRgba8(const void* source,size_t stride,u32 width,u32 height);
Result gsCpuImageStreamerClose(void);
bool gsCpuImageStreamerHasResources(void);
#ifdef __cplusplus
}
#endif
