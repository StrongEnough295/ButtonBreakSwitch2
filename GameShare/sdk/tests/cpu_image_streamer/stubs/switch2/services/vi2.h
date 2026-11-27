/* SPDX-License-Identifier: ISC
 * Offline synthetic test fixture. Public libounce declarations are based on
 * ouncebrew/libounce d1718bb7076fb356cb4cb81ab13d035db5367eea.
 * Copyright (c) 2026 libounce contributors (public API declarations).
 * See LICENSE and README.md for provenance and the scope of these mocks.
 */
#pragma once
#include <switch.h>
typedef GsvNativeSync Vi2Dependency;
typedef struct { u64 token;u32 destroy_notification,availability_notification; } Vi2Layer;
typedef struct { uint16_t crop_left,crop_top,crop_right_inclusive,crop_bottom_inclusive;
 u64 flags,system_tick;uint16_t interval_denominator,interval_numerator;u32 unk_x1c;u8 metadata[32];
} Vi2PresentOptions;
void vi2MakeBasicPresentOptions(Vi2PresentOptions*,u32,u32,u64);
Result vi2WaitLayerAvailability(Vi2Layer*,u64);
Result vi2GetLayerAvailability(Vi2Layer*,u64*);
Result vi2PresentLayer(Vi2Layer*,u8,u32,const Vi2PresentOptions*,const Vi2Dependency*,u64*,Vi2Dependency*);
Result vi2UnregisterLayerImage(Vi2Layer*,u8,const Vi2PresentOptions*,u64*,Vi2Dependency*);

