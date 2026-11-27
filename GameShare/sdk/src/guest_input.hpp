/* SPDX-License-Identifier: MIT */
#pragma once
#include <switch.h>

typedef enum {
    GuestInput_NoStyle = 0,
    GuestInput_UnsupportedStyle = 1,
    GuestInput_NoSamples = 2,
    GuestInput_Unstable = 3,
    GuestInput_Ready = 4,
} GuestInputStatus;

/* A decoded guest0 controller sample. Ready is the only status with sample data.
 * Button/attribute bits retain the native HID representation.
 */
typedef struct {
    u32 status, npad_id, style_set, attributes;
    u64 sampling_number, buttons;
    s32 left_x, left_y, right_x, right_y;
} GuestInputSample;

/* One mapper per process; serialize Open/Poll/Close on the native
 * worker. The caller owns the cmd300 shared-memory handle and retains it until
 * Close succeeds. Only a READ mapping is created; the handle is never closed.
 * size/offset are the two u64 values returned by cmd300. This finite reader
 * accepts page-multiple mappings up to16MiB and an8-byte-aligned format offset.
 */
Result guestInputOpen(Handle borrowed_handle, u64 size, u64 offset);

/* Read guest0 / Npad0; if its style is zero, try Npad0x20. Select FullKey(bit0),
 * then JoyDual(bit2), then Handheld(bit1) on Npad0x20. JoyLeft/Right are not
 * decoded. Makes at most3 seqlock attempts and never sleeps or spins until a
 * producer becomes idle. Zero Result with a non-Ready status means no usable
 * sample this poll; malformed bounds/header values return BadInput.
 */
Result guestInputPoll(GuestInputSample *out);

/* Unmap only. On failure the mapping is retained and Close may be retried;
 * the caller must not close its shared-memory handle or unload the NRO yet.
 */
Result guestInputClose(void);
/* Thread-safe lifetime predicate for the main-thread exit guard. */
bool guestInputHasResources(void);
